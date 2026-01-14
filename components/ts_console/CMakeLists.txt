# TianShanOS Console Component

set(TS_CONSOLE_SRCS
    "src/ts_console.c"
    "src/ts_console_args.c"
    "src/ts_console_builtin.c"
    "commands/ts_cmd_led.c"
)

idf_component_register(
    SRCS ${TS_CONSOLE_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ts_core ts_led console vfs spi_flash
    PRIV_REQUIRES driver esp_vfs_console
)
