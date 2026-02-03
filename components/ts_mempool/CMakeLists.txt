idf_component_register(
    SRCS "src/ts_mempool.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_common
    PRIV_REQUIRES heap
)
