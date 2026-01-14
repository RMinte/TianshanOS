idf_component_register(
    SRCS
        "src/ts_net.c"
        # "src/ts_eth.c"     # TODO: Fix W5500 and event integration
        # "src/ts_wifi.c"    # TODO: Fix event integration
        "src/ts_dhcp_server.c"
        # "src/ts_http_server.c"  # TODO: Fix ts_storage include
    INCLUDE_DIRS
        "include"
    REQUIRES
        ts_core
        ts_hal
        esp_netif
        esp_eth
        esp_wifi
        esp_http_server
        lwip
    PRIV_REQUIRES
        driver
        esp_event
        nvs_flash
)
