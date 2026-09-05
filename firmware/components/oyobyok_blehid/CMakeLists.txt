# esp_hid_gap.{c,h} are the scan helpers from ESP-IDF's esp_hid_host example, vendored unchanged.
idf_component_register(
    SRCS "oyobyok_blehid.c" "esp_hid_gap.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hid bt oyobyok_host
    PRIV_REQUIRES nvs_flash esp_coex
)
