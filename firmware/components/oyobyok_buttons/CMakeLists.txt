idf_component_register(
    SRCS "oyobyok_buttons.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "../../shared"
    REQUIRES driver esp_common oyobyok_power
)
