# TianShanOS LED Component
# TODO: Fix device struct visibility issues

set(TS_LED_SRCS
    # "src/ts_led.c"          # TODO: Fix device struct
    # "src/ts_led_driver.c"   # TODO: Fix led_strip API
    # "src/ts_led_layer.c"    # TODO: Fix device struct visibility
    # "src/ts_led_effects.c"
    "src/ts_led_color.c"
    # "src/ts_led_image.c"
    # "src/ts_led_preset.c"
)

idf_component_register(
    SRCS ${TS_LED_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ts_core ts_hal driver led_strip
    PRIV_REQUIRES ts_storage ts_event
)
