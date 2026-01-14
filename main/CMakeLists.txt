# TianShanOS - Main Application
# 主应用程序

idf_component_register(
    SRCS
        "main.c"
        "ts_core_init.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        ts_core
        ts_config
        ts_log
        ts_event
        ts_service
)
