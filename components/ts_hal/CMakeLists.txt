# TianShanOS HAL Component
# Hardware Abstraction Layer

set(TS_HAL_SRCS
    "src/ts_hal.c"
    "src/ts_pin_manager.c"
    "src/ts_gpio.c"
    "src/ts_pwm.c"
    "src/ts_i2c.c"
    "src/ts_spi.c"
    "src/ts_uart.c"
    "src/ts_adc.c"
)

# Platform-specific sources
if(CONFIG_IDF_TARGET_ESP32S3)
    list(APPEND TS_HAL_SRCS "platform/esp32s3/ts_platform_s3.c")
elseif(CONFIG_IDF_TARGET_ESP32P4)
    list(APPEND TS_HAL_SRCS "platform/esp32p4/ts_platform_p4.c")
endif()

idf_component_register(
    SRCS ${TS_HAL_SRCS}
    INCLUDE_DIRS "include" "platform/esp32s3" "platform/esp32p4"
    REQUIRES ts_core driver esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_spi esp_driver_uart esp_adc spi_flash esp_psram log
    PRIV_REQUIRES nvs_flash
)
