idf_component_register(
    SRCS "oyobyok_sshd.c" "oyobyok_sftpd.c" "tweetnacl/tweetnacl.c"
    INCLUDE_DIRS "." "tweetnacl"
    REQUIRES lwip mbedtls esp_common
    PRIV_REQUIRES esp_timer
)
# TweetNaCl is written for clarity, not for the compiler's warnings.
set_source_files_properties(tweetnacl/tweetnacl.c PROPERTIES COMPILE_FLAGS "-Wno-unused-function -Wno-sign-compare -Wno-unused-parameter -Wno-implicit-fallthrough -Wno-unused-variable -O2")
