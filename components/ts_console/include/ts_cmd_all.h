/**
 * @file ts_cmd_all.h
 * @brief All Console Command Registrations
 * 
 * 所有控制台命令的注册函数声明
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_CMD_ALL_H
#define TS_CMD_ALL_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 service 命令
 * service --list/--status/--start/--stop/--restart
 */
esp_err_t ts_cmd_service_register(void);

/**
 * @brief 注册 system 命令
 * system --info/--version/--uptime/--memory/--tasks/--reboot
 */
esp_err_t ts_cmd_system_register(void);

/**
 * @brief 注册 config 命令
 * config --get/--set/--list/--reset
 */
esp_err_t ts_cmd_config_register(void);

/**
 * @brief 注册 fan 命令
 * fan --status/--set/--mode/--enable/--disable
 */
esp_err_t ts_cmd_fan_register(void);

/**
 * @brief 注册 storage 命令
 * storage --status/--mount/--unmount/--list/--read/--space
 */
esp_err_t ts_cmd_storage_register(void);

/**
 * @brief 注册 net 命令
 * net --status/--ip/--set/--dhcp
 */
esp_err_t ts_cmd_net_register(void);

/**
 * @brief 注册 device 命令
 * device --agx/--lpmu/--usb-mux
 */
esp_err_t ts_cmd_device_register(void);

/**
 * @brief 注册 LED 命令
 * led --list/--brightness/--clear/--fill/--effect
 */
esp_err_t ts_cmd_led_register(void);

/**
 * @brief 注册 DHCP 命令
 * dhcp --status/--clients/--start/--stop/--pool/--bind
 */
esp_err_t ts_cmd_dhcp_register(void);

/**
 * @brief 注册 WiFi 命令
 * wifi --status/--scan/--ap/--connect/--start/--stop/--save
 */
esp_err_t ts_cmd_wifi_register(void);

/**
 * @brief 注册 NAT 网关命令
 * nat --status/--enable/--disable/--save
 */
void ts_cmd_nat_register(void);

/**
 * @brief 注册 SSH 命令
 * ssh --host/--user/--password/--exec/--test
 */
esp_err_t ts_cmd_ssh_register(void);

/**
 * @brief 注册 SFTP/SCP 命令
 * sftp --ls/--get/--put/--rm/--mkdir
 */
esp_err_t ts_cmd_sftp_register(void);

/**
 * @brief 注册文件系统命令
 * ls, cat, cd, pwd, mkdir, rm, cp, mv, hexdump
 */
esp_err_t ts_cmd_fs_register(void);

/**
 * @brief 注册所有扩展命令
 * 一次性注册所有非内置命令
 */
esp_err_t ts_cmd_register_all(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_CMD_ALL_H */
