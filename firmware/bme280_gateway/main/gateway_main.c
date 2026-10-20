#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

/* The sensor node joins this hotspot and POSTs to http://192.168.4.1/readings.
 * Each accepted body is printed as one USB serial line for the laptop bridge;
 * the bridge, not this gateway, validates the reading. */
static const char *TAG = "GATEWAY";
#define MAX_BODY 512

static esp_err_t readings_post(httpd_req_t *req)
{
    char body[MAX_BODY + 1];
    if (req->content_len == 0 || req->content_len > MAX_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body must be 1..512 bytes");
        return ESP_FAIL;
    }
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) return ESP_FAIL;
        received += n;
    }
    body[received] = '\0';
    /* Printable ASCII only, so one reading can never break the line framing. */
    for (size_t i = 0; i < received; i++) {
        if (body[i] < 0x20 || body[i] > 0x7e) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Printable ASCII only");
            return ESP_FAIL;
        }
    }
    printf("MESS_BME_JSON %s\n", body);
    fflush(stdout);
    httpd_resp_set_status(req, "201 Created");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"forwarded\":true}");
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "Node " MACSTR " joined", MAC2STR(e->mac));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGW(TAG, "Node " MACSTR " left (reason %d)", MAC2STR(e->mac), e->reason);
    }
}

void app_main(void)
{
    const char *ssid = CONFIG_MESS_GW_SSID, *password = CONFIG_MESS_GW_PASSWORD;
    if (strlen(ssid) < 1 || strlen(ssid) > 32 || strlen(password) < 8 || strlen(password) > 63) {
        ESP_LOGE(TAG, "Set a hotspot name (1..32) and password (8..63) under MESS BME280 gateway; not starting an open network");
        return;
    }
    esp_err_t err = nvs_flash_init(); /* Wi-Fi driver needs NVS for calibration data. */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    wifi_config_t conf = {
        .ap = {
            .ssid_len = strlen(ssid),
            .channel = CONFIG_MESS_GW_CHANNEL,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.required = false},
        },
    };
    memcpy(conf.ap.ssid, ssid, strlen(ssid));
    memcpy(conf.ap.password, password, strlen(password));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &conf));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t readings = {.uri = "/readings", .method = HTTP_POST, .handler = readings_post};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &readings));
    ESP_LOGI(TAG, "Hotspot \"%s\" on channel %d; nodes POST to http://192.168.4.1/readings", ssid, CONFIG_MESS_GW_CHANNEL);
}
