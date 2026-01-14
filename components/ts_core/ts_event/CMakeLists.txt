# TianShanOS - Event Bus Component
# 事件总线组件

idf_component_register(
    SRCS
        "src/ts_event.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_event
        esp_timer
    PRIV_REQUIRES
        esp_system
)

# 添加版本信息
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    TS_EVENT_VERSION_MAJOR=0
    TS_EVENT_VERSION_MINOR=1
    TS_EVENT_VERSION_PATCH=0
)
