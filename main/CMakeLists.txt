# TianShanOS - Main Application
# 主应用程序

idf_build_get_property(ROOT_PASSWORD_RESET_BUILD ROOT_PASSWORD_RESET)

if(ROOT_PASSWORD_RESET_BUILD)
    idf_component_register(
        SRCS
            "root_password_reset_main.c"
        INCLUDE_DIRS
            "."
        REQUIRES
            nvs_flash
            mbedtls
            esp_partition
            app_update
            esp_system
            esp_hw_support
            ts_auth_layout
    )
else()
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
            ts_config_pack
            ts_pki_client
            ts_https
            ts_webui
            ts_automation
            ts_mempool
            json  # cJSON for PSRAM hooks
    )
endif()
