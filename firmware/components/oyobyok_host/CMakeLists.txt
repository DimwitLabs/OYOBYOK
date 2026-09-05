idf_component_register(
    SRCS "oyobyok_host.c" "oyobyok_keymap.c"
    INCLUDE_DIRS "." "../../shared"
    REQUIRES oyobyok_lcd oyobyok_git oyobyok_power fatfs
    PRIV_REQUIRES esp_system esp_timer
)
