# TianShanOS - Logging Component
# 日志组件

idf_component_register(
    SRCS
        "src/ts_log.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_system
    PRIV_REQUIRES
        fatfs
        spiffs
)

# 添加版本信息
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    TS_LOG_VERSION_MAJOR=0
    TS_LOG_VERSION_MINOR=1
    TS_LOG_VERSION_PATCH=0
)
