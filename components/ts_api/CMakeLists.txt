# TianShanOS API Component

set(TS_API_SRCS
    "src/ts_api.c"
    "src/ts_api_system.c"
    "src/ts_api_config.c"
    "src/ts_api_device.c"
)

idf_component_register(
    SRCS ${TS_API_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ts_core ts_drivers json
    PRIV_REQUIRES ts_hal esp_timer esp_app_format spi_flash bootloader_support
)
