/* ESP-WIFI-MESH lifecycle follows Espressif's CC0 internal_communication example.
 * Application telemetry queues/HTTP forwarding are MESS-specific. */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "mesh_transport.h"
#ifdef CONFIG_MESS_MESH
#define CAPACITY 768
#define CONNECTED BIT0
#define GOT_IP BIT1
static const char *TAG="mess_mesh";
static esp_netif_t *station;
static EventGroupHandle_t events;
static QueueHandle_t upstream;
typedef struct { char json[CAPACITY]; } packet_t;
static void event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg; (void)data;
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) xEventGroupSetBits(events,GOT_IP);
    if(base!=MESH_EVENT) return;
    if(id==MESH_EVENT_PARENT_CONNECTED) {
        xEventGroupSetBits(events,CONNECTED);
        ESP_LOGI(TAG,"Parent connected: layer=%d root=%d",esp_mesh_get_layer(),esp_mesh_is_root());
        if(esp_mesh_is_root()) {
            esp_netif_dhcpc_stop(station); esp_netif_dhcpc_start(station);
        }
    }
    if(id==MESH_EVENT_PARENT_DISCONNECTED || id==MESH_EVENT_STOPPED) {
        xEventGroupClearBits(events,CONNECTED|GOT_IP);
        ESP_LOGW(TAG,"Parent lost; mesh will search/reconnect");
    }
}
static bool queue_packet(const char *json) {
    packet_t p;
    if(strlen(json)>=sizeof(p.json)) return false;
    strcpy(p.json,json);
    if(xQueueSend(upstream,&p,0)!=pdTRUE) {
        ESP_LOGW(TAG,"Gateway queue full: dropping packet"); return false;
    }
    return true;
}
bool mess_mesh_submit(const char *json) {
    if(!(xEventGroupGetBits(events)&CONNECTED)) return false;
    if(esp_mesh_is_root()) return queue_packet(json);
    mesh_data_t data={.data=(uint8_t *)json,.size=strlen(json),.proto=MESH_PROTO_JSON,.tos=MESH_TOS_P2P};
    /* Dedicated sender task: P2P may block, never call from the sensor task. */
    esp_err_t err=esp_mesh_send(NULL,&data,MESH_DATA_P2P,NULL,0);
    if(err) ESP_LOGW(TAG,"Mesh send failed: %s",esp_err_to_name(err));
    return err==ESP_OK;
}
static void receive_task(void *arg) {
    (void)arg;
    packet_t p;
    for(;;) {
        if(!esp_mesh_is_root()) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
        mesh_addr_t from;
        mesh_data_t data={.data=(uint8_t *)p.json,.size=sizeof(p.json)-1};
        int flags=0;
        esp_err_t err=esp_mesh_recv(&from,&data,1000,&flags,NULL,0);
        if(err==ESP_OK && data.size>0 && data.size<sizeof(p.json) && data.proto==MESH_PROTO_JSON) {
            p.json[data.size]='\0';
            queue_packet(p.json);
        }
    }
}
static void http_task(void *arg) {
    (void)arg;
    for(;;) {
        if(!esp_mesh_is_root() || !(xEventGroupGetBits(events)&GOT_IP)) {
            vTaskDelay(pdMS_TO_TICKS(500)); continue;
        }
        packet_t p;
        if(xQueueReceive(upstream,&p,pdMS_TO_TICKS(1000))!=pdTRUE) continue;
        bool sent=false;
        for(int attempt=0;attempt<3 && !sent && esp_mesh_is_root();++attempt) {
            esp_http_client_config_t cfg={.url=CONFIG_MESS_SERVER_URL,.timeout_ms=2000,.disable_auto_redirect=true};
            esp_http_client_handle_t client=esp_http_client_init(&cfg);
            if(!client) break;
            esp_http_client_set_method(client,HTTP_METHOD_POST);
            esp_http_client_set_header(client,"Content-Type","application/json");
            esp_http_client_set_post_field(client,p.json,strlen(p.json));
            esp_err_t err=esp_http_client_perform(client);
            int status=esp_http_client_get_status_code(client);
            sent=err==ESP_OK && status>=200 && status<300;
            esp_http_client_cleanup(client);
            if(!sent) vTaskDelay(pdMS_TO_TICKS(250*(attempt+1)));
        }
        if(!sent) ESP_LOGW(TAG,"HTTP forwarding failed after retries: packet dropped");
    }
}
void mess_mesh_start(void) {
    events=xEventGroupCreate(); upstream=xQueueCreate(24,sizeof(packet_t));
    configASSERT(events && upstream);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&station,NULL));
    wifi_init_config_t wifi=WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT,ESP_EVENT_ANY_ID,event,NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_mesh_set_topology(MESH_TOPO_TREE));
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(CONFIG_MESS_MESH_LAYERS));
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(10));
    mesh_cfg_t cfg=MESH_INIT_CONFIG_DEFAULT();
    const uint8_t id[6]={0x4d,0x45,0x53,0x53,0x47,CONFIG_MESS_MESH_GROUP};
    memcpy(cfg.mesh_id.addr,id,6);
    cfg.channel=CONFIG_MESS_MESH_CHANNEL;
    size_t ssid_len=strlen(CONFIG_MESS_WIFI_SSID),pass_len=strlen(CONFIG_MESS_WIFI_PASSWORD);
    size_t mesh_pass_len=strlen(CONFIG_MESS_MESH_PASSWORD);
    configASSERT(ssid_len>0 && ssid_len<=32 && pass_len<=63 && mesh_pass_len>=8 && mesh_pass_len<=63);
    cfg.router.ssid_len=ssid_len;
    memcpy(cfg.router.ssid,CONFIG_MESS_WIFI_SSID,ssid_len);
    memcpy(cfg.router.password,CONFIG_MESS_WIFI_PASSWORD,pass_len);
    cfg.mesh_ap.max_connection=4; cfg.mesh_ap.nonmesh_max_connection=0;
    memcpy(cfg.mesh_ap.password,CONFIG_MESS_MESH_PASSWORD,mesh_pass_len);
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(WIFI_AUTH_WPA2_PSK));
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    ESP_ERROR_CHECK(esp_mesh_start());
    if(strlen(CONFIG_MESS_NTP_SERVER)) {
        esp_sntp_config_t ntp=ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_MESS_NTP_SERVER);
        ESP_ERROR_CHECK(esp_netif_sntp_init(&ntp));
    }
    configASSERT(xTaskCreate(receive_task,"mesh_rx",4096,NULL,4,NULL)==pdPASS);
    configASSERT(xTaskCreate(http_task,"mesh_http",6144,NULL,3,NULL)==pdPASS);
}
#else
void mess_mesh_start(void) {}
bool mess_mesh_submit(const char *json) { (void)json; return false; }
#endif
