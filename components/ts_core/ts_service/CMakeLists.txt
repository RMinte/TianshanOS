# TianShanOS - Service Management Component
# 服务管理组件

idf_component_register(
    SRCS
        "src/ts_service.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        ts_event
        ts_log
    PRIV_REQUIRES
        esp_system
)

# 添加版本信息
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    TS_SERVICE_VERSION_MAJOR=0
    TS_SERVICE_VERSION_MINOR=1
    TS_SERVICE_VERSION_PATCH=0
)
