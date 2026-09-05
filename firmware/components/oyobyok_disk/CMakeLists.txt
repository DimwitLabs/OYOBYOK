idf_component_register(
    SRCS "oyobyok_disk.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_sdmmc fatfs espressif__esp_tinyusb esp_common
)
