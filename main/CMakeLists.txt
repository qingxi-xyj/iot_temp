

idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif wakeword driver   esp_driver_i2s esp-skainet
)