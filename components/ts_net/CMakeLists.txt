idf_component_register(
    SRCS
        "src/ts_net.c"
        "src/ts_net_manager.c"
        "src/ts_eth.c"
        "src/ts_wifi.c"
        "src/ts_dhcp_server.c"
        "src/ts_nat.c"
        "src/ts_http_server.c"
        "src/ts_http_origin.c"
        "src/ts_https_server.c"
        "src/ts_time_sync.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        ts_core
        ts_hal
        ts_storage
        ts_security
        esp_netif
        esp_eth
        esp_wifi
        esp_http_server
        esp_https_server
        lwip
        mbedtls
        mdns
    PRIV_INCLUDE_DIRS
        "private_include"
    PRIV_REQUIRES
        driver
        esp_event
        esp_timer
        nvs_flash
)

# Exact public origins/authorities for intentionally hosted dashboards or proxies.
# Empty means same-origin device UI only; never derive this from incoming headers.
set(TS_HTTP_TRUSTED_ORIGINS "" CACHE STRING "Comma-separated trusted http(s) origins, no wildcards")
if(TS_HTTP_TRUSTED_ORIGINS MATCHES "[;\"\\\r\n]")
    message(FATAL_ERROR "TS_HTTP_TRUSTED_ORIGINS contains an unsafe configuration delimiter")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE TS_HTTP_TRUSTED_ORIGINS="${TS_HTTP_TRUSTED_ORIGINS}")
