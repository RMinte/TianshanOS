# TianShanOS API Component

set(TS_API_SRCS
    "src/ts_api.c"
    "src/ts_api_system.c"
    "src/ts_api_config.c"
    "src/ts_api_device.c"
    "src/ts_api_led.c"
    "src/ts_api_network.c"
    "src/ts_api_fan.c"
    "src/ts_api_power.c"
    "src/ts_api_temp.c"
    "src/ts_api_service.c"
    "src/ts_api_storage.c"
    "src/ts_api_gpio.c"
    "src/ts_api_monitor.c"
    "src/ts_api_wifi.c"
    "src/ts_api_dhcp.c"
    "src/ts_api_nat.c"
    "src/ts_api_hosts.c"
    "src/ts_api_key.c"
    "src/ts_api_ssh.c"
    "src/ts_api_sftp.c"
    "src/ts_api_time.c"
    "src/ts_api_ota.c"
    "src/ts_api_log.c"
    "src/ts_api_automation.c"
    "src/ts_api_cert.c"
    "src/ts_api_auth.c"
    "src/ts_api_ui.c"
)

idf_component_register(
    SRCS ${TS_API_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ts_core ts_drivers ts_led ts_net json ts_storage ts_security ts_ota ts_automation ts_cert ts_webui
    PRIV_REQUIRES ts_hal esp_timer esp_app_format spi_flash bootloader_support esp_wifi app_update esp_https_ota esp_http_client espressif__esp_websocket_client
)
