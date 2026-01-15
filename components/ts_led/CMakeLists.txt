# TianShanOS LED Component

set(TS_LED_SRCS
    "src/ts_led.c"
    "src/ts_led_driver.c"
    "src/ts_led_layer.c"
    "src/ts_led_animation.c"
    "src/ts_led_effect.c"
    "src/ts_led_color.c"
    "src/ts_led_image.c"
    "src/ts_led_preset.c"
    "src/ts_led_qrcode.c"
    "src/qrcodegen/qrcodegen.c"
)

idf_component_register(
    SRCS ${TS_LED_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES ts_core ts_hal driver led_strip
    PRIV_REQUIRES ts_storage ts_event espressif__zlib espressif__esp_jpeg espressif__libpng
)
