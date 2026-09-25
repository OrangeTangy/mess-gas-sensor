idf_component_register(SRCS "main.c" "mesh_transport.c" INCLUDE_DIRS "."
    REQUIRES sgp30 esp_driver_i2c esp_wifi esp_event esp_netif nvs_flash esp_http_client esp_timer)
