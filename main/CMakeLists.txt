# TianShanOS - Main Application
# 主应用程序

idf_component_register(
    SRCS
        "main.c"
        "ts_core_init.c"
        "ts_services.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        ts_core
        ts_config
        ts_log
        ts_event
        ts_service
        ts_storage
        ts_console
        ts_hal
        ts_led
        ts_drivers
        ts_net
        ts_api
        ts_security
        ts_cert
        ts_pki_client
        ts_https
        ts_webui
        ts_automation
        json  # cJSON for PSRAM hooks
)
