idf_component_register(
    SRCS "oyobyok_main.c"
    INCLUDE_DIRS "."
    REQUIRES oyobyok_lcd oyobyok_host oyobyok_git oyobyok_power oyobyok_buttons oyobyok_blehid oyobyok_led
             oyobyok_wifi oyobyok_disk oyobyok_ssh nvs_flash fatfs sdmmc esp_common
)
