# TianShanOS Console Component
# 使用本地修改版 linenoise 以支持 UTF-8 中文输入

set(TS_CONSOLE_SRCS
    "src/ts_console.c"
    "src/ts_console_args.c"
    "src/ts_console_builtin.c"
    "src/ts_i18n.c"
    "src/ts_script.c"
    "linenoise/linenoise.c"
    "commands/ts_cmd_agx.c"
    "commands/ts_cmd_config.c"
    "commands/ts_cmd_device.c"
    "commands/ts_cmd_dhcp.c"
    "commands/ts_cmd_fan.c"
    "commands/ts_cmd_fs.c"
    "commands/ts_cmd_gpio.c"
    "commands/ts_cmd_hosts.c"
    "commands/ts_cmd_key.c"
    "commands/ts_cmd_led.c"
    "commands/ts_cmd_nat.c"
    "commands/ts_cmd_net.c"
    "commands/ts_cmd_ota.c"
    "commands/ts_cmd_power.c"
    "commands/ts_cmd_register.c"
    "commands/ts_cmd_service.c"
    "commands/ts_cmd_sftp.c"
    "commands/ts_cmd_ssh.c"
    "commands/ts_cmd_storage.c"
    "commands/ts_cmd_system.c"
    "commands/ts_cmd_temp.c"
    "commands/ts_cmd_voltprot.c"
    "commands/ts_cmd_wifi.c"
)

idf_component_register(
    SRCS ${TS_CONSOLE_SRCS}
    INCLUDE_DIRS "include" "linenoise"
    REQUIRES ts_core ts_led ts_storage ts_drivers ts_net ts_security ts_api ts_ota console vfs spi_flash esp_wifi esp_netif esp_app_format
    PRIV_REQUIRES driver esp_vfs_console
)
