# TianShanOS JSONPath Component
#
# 轻量级 JSONPath 解析器，支持：
# - 对象路径: a.b.c
# - 数组索引: a[0], a[-1]
# - 通配符: a[*]
# - 混合路径: a.b[0].c[*].d

idf_component_register(
    SRCS
        "src/ts_jsonpath.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        json
    PRIV_REQUIRES
        esp_common
)
