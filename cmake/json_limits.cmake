# Bound recursion inside the parser, before callers can inspect a JSON tree.
# Config packs already accept at most 32 nesting levels. Apply that same bound
# to the actual shared SDK cJSON target, including WS/HTTP/PKI consumers.
# The serial-only recovery build has no application JSON ingress.
idf_component_get_property(ts_json_target json COMPONENT_LIB)
target_compile_definitions(${ts_json_target} PUBLIC CJSON_NESTING_LIMIT=32)
