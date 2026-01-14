idf_component_register(
    SRCS
        "src/ts_webui.c"
        # "src/ts_webui_api.c"   # TODO: Fix ts_api_call usage
        # "src/ts_webui_ws.c"    # TODO: Fix dependencies
    INCLUDE_DIRS
        "include"
    REQUIRES
        ts_core
        ts_net
        ts_api
        ts_security
        ts_storage
        esp_http_server
    PRIV_REQUIRES
        json
)

# Embed web files
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/web")
if(EXISTS "${WEB_SRC_DIR}")
    spiffs_create_partition_image(www ${WEB_SRC_DIR} FLASH_IN_PROJECT)
endif()
