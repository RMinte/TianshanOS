idf_component_register(
    SRCS
        "src/ts_drivers.c"
        # "src/ts_fan.c"        # TODO: Fix HAL API mismatch
        # "src/ts_power.c"      # TODO: Fix HAL API mismatch
        # "src/ts_device_ctrl.c" # TODO: Fix GPIO API
        # "src/ts_usb_mux.c"    # TODO: Fix GPIO API
    INCLUDE_DIRS
        "include"
    REQUIRES
        ts_core
        ts_hal
    PRIV_REQUIRES
        driver
        esp_adc
        esp_timer
)
