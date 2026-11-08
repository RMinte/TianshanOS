idf_component_register(
    SRCS "src/ts_auth_credential.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES mbedtls esp_hw_support
)
