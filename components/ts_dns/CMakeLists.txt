idf_component_register(
    SRCS "src/ts_dns.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common lwip
    PRIV_REQUIRES esp_timer freertos
)
