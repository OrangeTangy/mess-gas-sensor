#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/i2c_master.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sgp30.h"
#include "esp_mesh.h"
#include "mesh_transport.h"

static const char *TAG="mess";
static QueueHandle_t readings;
static EventGroupHandle_t network;
#ifdef CONFIG_MESS_SENSOR_ENABLED
static i2c_master_dev_handle_t device;
#endif
static uint32_t boot_id;
static char node_id[13];
#define WIFI_READY BIT0
#define SECOND_US INT64_C(1000000)
#define HOUR_US (3600*SECOND_US)
typedef struct { char json[640]; } message_t;
typedef struct {
    uint64_t serial;
    int64_t saved_at;
    uint32_t version;
    uint16_t eco2, tvoc;
} baseline_t;

/* Integration hook: override in another .c file when BME280 is integrated.
 * Return a fresh, measured absolute humidity in g/m^3. Never block here.
 * Main gas task owns all SGP30 transactions, including humidity writes. */
__attribute__((weak)) bool mess_get_absolute_humidity(float *g_m3) {
    (void)g_m3; return false;
}
#ifdef CONFIG_MESS_SENSOR_ENABLED
static void sleep_ms(unsigned ms) {
    /* One extra tick ensures delay never rounds below the sensor minimum. */
    vTaskDelay(pdMS_TO_TICKS(ms)+1);
}
static int bus_write(void *ctx,const uint8_t *data,size_t len) {
    return i2c_master_transmit((i2c_master_dev_handle_t)ctx,data,len,100)==ESP_OK ? 0 : -1;
}
static int bus_read(void *ctx,uint8_t *data,size_t len) {
    return i2c_master_receive((i2c_master_dev_handle_t)ctx,data,len,100)==ESP_OK ? 0 : -1;
}
static bool clock_valid(void) { return time(NULL)>INT64_C(1704067200); }
static bool restore_baseline(sgp30_t *s,uint64_t serial) {
    if(!clock_valid()) return false;
    nvs_handle_t nvs; baseline_t b={0}; size_t size=sizeof(b);
    if(nvs_open("mess_gas",NVS_READONLY,&nvs)!=ESP_OK) return false;
    esp_err_t e=nvs_get_blob(nvs,"baseline",&b,&size); nvs_close(nvs);
    int64_t age=(int64_t)time(NULL)-b.saved_at;
    if(e!=ESP_OK || size!=sizeof(b) || b.version!=1 || b.serial!=serial ||
       age<0 || age>7*24*3600 || !(b.eco2|b.tvoc)) return false;
    bool ok=sgp30_set_baseline(s,b.eco2,b.tvoc)==SGP_OK;
    if(ok) ESP_LOGI(TAG,"Restored baseline matched to sensor, age=%"PRId64" s",age);
    return ok;
}
static void save_baseline(sgp30_t *s,uint64_t serial) {
    if(!clock_valid()) return;
    baseline_t b={.serial=serial,.saved_at=time(NULL),.version=1};
    if(sgp30_get_baseline(s,&b.eco2,&b.tvoc)!=SGP_OK || !(b.eco2|b.tvoc)) return;
    nvs_handle_t nvs;
    esp_err_t e=nvs_open("mess_gas",NVS_READWRITE,&nvs);
    if(e==ESP_OK) {
        e=nvs_set_blob(nvs,"baseline",&b,sizeof(b));
        if(e==ESP_OK) e=nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG,"Baseline save: %s",esp_err_to_name(e));
}
#endif
#ifndef CONFIG_MESS_MESH
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg; (void)data;
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED)
        xEventGroupClearBits(network,WIFI_READY);
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(network,WIFI_READY);
        ESP_LOGI(TAG,"Wi-Fi has an IP address");
    }
}
static void start_wifi(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    wifi_config_t conf={0};
    const char *ssid=CONFIG_MESS_WIFI_SSID,*password=CONFIG_MESS_WIFI_PASSWORD;
    if(strlen(ssid)>32 || strlen(password)>63) {
        ESP_LOGE(TAG,"Wi-Fi credentials exceed supported lengths"); return;
    }
    memcpy(conf.sta.ssid,ssid,strlen(ssid));
    memcpy(conf.sta.password,password,strlen(password));
    conf.sta.threshold.authmode=strlen(password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&conf));
    ESP_ERROR_CHECK(esp_wifi_start());
    if(strlen(CONFIG_MESS_NTP_SERVER)) {
        esp_sntp_config_t ntp=ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_MESS_NTP_SERVER);
        ESP_ERROR_CHECK(esp_netif_sntp_init(&ntp));
    }
}
#endif
static void sender_task(void *arg) {
    (void)arg;
#ifdef CONFIG_MESS_MESH
    mess_mesh_start();
    for(;;) {
        message_t msg;
        if(xQueueReceive(readings,&msg,pdMS_TO_TICKS(1000))==pdTRUE && !mess_mesh_submit(msg.json))
            ESP_LOGW(TAG,"Mesh not ready; sample dropped");
    }
#else
    start_wifi();
    for(;;) {
        if(!(xEventGroupGetBits(network)&WIFI_READY)) {
            esp_wifi_connect();
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        message_t msg;
        if(xQueueReceive(readings,&msg,pdMS_TO_TICKS(1000))!=pdTRUE) continue;
        esp_http_client_config_t cfg={.url=CONFIG_MESS_SERVER_URL,.timeout_ms=2000,
                                     .disable_auto_redirect=true};
        esp_http_client_handle_t client=esp_http_client_init(&cfg);
        if(!client) { ESP_LOGE(TAG,"HTTP allocation failed"); continue; }
        esp_http_client_set_method(client,HTTP_METHOD_POST);
        esp_http_client_set_header(client,"Content-Type","application/json");
        esp_http_client_set_post_field(client,msg.json,(int)strlen(msg.json));
        esp_err_t err=esp_http_client_perform(client);
        int status=esp_http_client_get_status_code(client);
        if(err!=ESP_OK || status<200 || status>=300)
            ESP_LOGW(TAG,"Send failed: %s, HTTP %d; sample dropped",esp_err_to_name(err),status);
        esp_http_client_cleanup(client);
        /* No backlog/retries: the one-element mailbox keeps only the newest sample. */
    }
#endif
}
#ifdef CONFIG_MESS_SENSOR_ENABLED
static void sensor_task(void *arg) {
    (void)arg;
    sgp30_t s={.context=device,.write=bus_write,.read=bus_read,.sleep_ms=sleep_ms};
    uint64_t serial=0, seq=0;
    uint16_t features=0;
    bool initialized=false, restored=false, restore_checked=false;
    unsigned consecutive_errors=0;
    int64_t init_us=0,last_measure_us=0,next_save_us=0;
    TickType_t deadline=xTaskGetTickCount();
    for(;;) {
        int err=SGP_OK;
        int64_t now=esp_timer_get_time();
        if(initialized && last_measure_us && now-last_measure_us>1500000) {
            ESP_LOGW(TAG,"Measurement cadence lost; reinitializing IAQ");
            initialized=false;
        }
        if(!initialized) {
            err=sgp30_identify(&s,&serial,&features);
            if(!err && ((features&0xf000)!=0 || (features&0xff)<0x20)) err=SGP_ARGUMENT;
            if(!err) err=sgp30_selftest(&s);
            if(!err) err=sgp30_init(&s);
            if(!err) {
                initialized=true; consecutive_errors=0;
                init_us=esp_timer_get_time();
                restored=restore_baseline(&s,serial);
                restore_checked=clock_valid();
                next_save_us=init_us+(restored ? HOUR_US : 12*HOUR_US);
                ESP_LOGI(TAG,"SGP30 serial=%012"PRIx64" feature=0x%04x",serial,features);
            }
            last_measure_us=0;
            deadline=xTaskGetTickCount();
        }
        /* Clock sync may arrive later; restore only during startup, never mid-run. */
        if(initialized && !restore_checked && clock_valid() && esp_timer_get_time()-init_us<15*SECOND_US) {
            restored=restore_baseline(&s,serial); restore_checked=true;
            if(restored) next_save_us=init_us+HOUR_US;
        }
        bool humidity=false;
        uint16_t eco2=0,tvoc=0;
        if(initialized) {
            float absolute=0;
            humidity=mess_get_absolute_humidity(&absolute) && isfinite(absolute) &&
                     absolute>0 && absolute<=65535.0f/256.0f;
            int humidity_err=sgp30_set_humidity(&s,humidity ? absolute : 0);
            if(humidity_err) { humidity=false; err=humidity_err; }
            last_measure_us=esp_timer_get_time();
            int measure_err=sgp30_measure(&s,&eco2,&tvoc);
            if(measure_err) err=measure_err;
        }
        now=esp_timer_get_time();
        bool warmup=initialized && now-init_us<16*SECOND_US;
        bool valid=initialized && !err && !warmup;
        const char *status=err ? "sensor_error" : warmup ? "warming_up" : "ok";
        if(err) {
            if(++consecutive_errors>=3) initialized=false;
        } else consecutive_errors=0;
        char eco2_text[12]="null",tvoc_text[12]="null";
        if(valid) {
            snprintf(eco2_text,sizeof(eco2_text),"%u",eco2);
            snprintf(tvoc_text,sizeof(tvoc_text),"%u",tvoc);
        }
        wifi_ap_record_t ap={0};
        int rssi=esp_wifi_sta_get_ap_info(&ap)==ESP_OK ? ap.rssi : -127;
        const char *transport=strlen(CONFIG_MESS_WIFI_SSID)?"wifi":"serial";
        int layer=0;
#ifdef CONFIG_MESS_MESH
        transport="mesh"; layer=esp_mesh_get_layer();
#endif
        message_t msg;
        int n=snprintf(msg.json,sizeof(msg.json),
            "{\"schema_version\":1,\"node_id\":\"%s\",\"boot_id\":\"%08"PRIx32"\","
            "\"sensor\":\"sgp30\",\"sensor_serial\":\"%012"PRIx64"\",\"sequence\":%"PRIu64","
            "\"uptime_ms\":%"PRId64",\"eco2_ppm\":%s,\"tvoc_ppb\":%s,"
            "\"valid\":%s,\"status\":\"%s\",\"error_code\":%d,"
            "\"humidity_compensated\":%s,\"baseline_restored\":%s,\"transport\":\"%s\",\"mesh_layer\":%d,\"rssi_dbm\":%d,\"node_role\":\"sensor\"}",
            node_id,boot_id,serial,seq++,now/1000,eco2_text,tvoc_text,
            valid?"true":"false",status,err,humidity?"true":"false",restored?"true":"false",transport,layer,rssi);
        if(n>0 && n<(int)sizeof(msg.json)) {
            puts(msg.json);
            xQueueOverwrite(readings,&msg);
        }
        if(valid && now>=next_save_us) {
            save_baseline(&s,serial);
            next_save_us=now+HOUR_US;
        }
        /* Never burst missed measurements to catch up. */
        if(xTaskGetTickCount()-deadline>=pdMS_TO_TICKS(1000)) deadline=xTaskGetTickCount();
        xTaskDelayUntil(&deadline,pdMS_TO_TICKS(1000));
    }
}
#endif
#ifndef CONFIG_MESS_SENSOR_ENABLED
static void relay_task(void *arg) {
    (void)arg; uint64_t seq=0;
    for(;;) {
        wifi_ap_record_t ap={0};
        int rssi=esp_wifi_sta_get_ap_info(&ap)==ESP_OK ? ap.rssi : -127;
        const char *transport="wifi"; int layer=0;
#ifdef CONFIG_MESS_MESH
        transport="mesh"; layer=esp_mesh_get_layer();
#endif
        message_t msg;
        snprintf(msg.json,sizeof(msg.json),
          "{\"schema_version\":1,\"node_id\":\"%s\",\"boot_id\":\"%08"PRIx32"\","
          "\"sensor\":\"none\",\"sensor_serial\":\"000000000000\",\"sequence\":%"PRIu64","
          "\"uptime_ms\":%"PRId64",\"eco2_ppm\":null,\"tvoc_ppb\":null,\"valid\":false,"
          "\"status\":\"relay\",\"error_code\":0,\"humidity_compensated\":false,\"baseline_restored\":false,"
          "\"transport\":\"%s\",\"mesh_layer\":%d,\"rssi_dbm\":%d,\"node_role\":\"relay\"}",
          node_id,boot_id,seq++,esp_timer_get_time()/1000,transport,layer,rssi);
        puts(msg.json); xQueueOverwrite(readings,&msg);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init()); /* Do not erase another component's NVS on failure. */
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_STA));
    snprintf(node_id,sizeof(node_id),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    boot_id=esp_random();
    readings=xQueueCreate(1,sizeof(message_t)); network=xEventGroupCreate();
    configASSERT(readings && network);
#ifdef CONFIG_MESS_SENSOR_ENABLED
    i2c_master_bus_config_t bus_config={.i2c_port=I2C_NUM_0,.sda_io_num=CONFIG_MESS_SDA,
        .scl_io_num=CONFIG_MESS_SCL,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,
        .flags.enable_internal_pullup=false};
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config,&bus));
    i2c_device_config_t config={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x58,.scl_speed_hz=100000};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus,&config,&device));
    ESP_LOGI(TAG,"I2C SDA=%d SCL=%d, expecting address 0x58",CONFIG_MESS_SDA,CONFIG_MESS_SCL);
    configASSERT(xTaskCreate(sensor_task,"gas",6144,NULL,5,NULL)==pdPASS);
#else
    configASSERT(xTaskCreate(relay_task,"relay",4096,NULL,5,NULL)==pdPASS);
#endif
    if(strlen(CONFIG_MESS_WIFI_SSID))
        configASSERT(xTaskCreate(sender_task,"sender",6144,NULL,3,NULL)==pdPASS);
    else ESP_LOGI(TAG,"Serial-only mode: configure Wi-Fi in menuconfig for HTTP transmission");
}
