idf_component_register(
    SRCS "oyobyok_power.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_common esp_adc oyobyok_lcd
)
