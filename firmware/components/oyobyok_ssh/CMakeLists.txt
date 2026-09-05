idf_component_register(
    SRCS "oyobyok_ssh.c"
    INCLUDE_DIRS "."
    REQUIRES skuodi__libssh2_esp lwip fatfs esp_common
)
