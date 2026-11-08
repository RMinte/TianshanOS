idf_component_register(
    SRCS "src/ts_http_url.c" "src/ts_http_target.c" "src/ts_http_fetch.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_http_client json http_parser
    PRIV_REQUIRES ts_dns esp_timer freertos mbedtls
)
