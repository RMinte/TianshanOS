# TianShanOS - Core Component Collection
# 核心组件集合

# 这个组件作为所有 ts_core 子组件的集合
# 实际的实现在各子目录中

idf_component_register(
    INCLUDE_DIRS
        "include"
    REQUIRES
        ts_config
        ts_log
        ts_event
        ts_service
        ts_mempool
)

# 将子组件目录添加到构建
# ESP-IDF 会自动处理子组件
