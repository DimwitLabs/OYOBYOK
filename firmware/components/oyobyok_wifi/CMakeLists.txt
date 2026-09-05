idf_component_register(
    SRCS "oyobyok_wifi.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "../../shared"
    REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer lwip
)
