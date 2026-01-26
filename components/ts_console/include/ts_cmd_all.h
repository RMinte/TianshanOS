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
 * @brief 注册 config 命令（统一配置管理）
 * config --get/--set/--list/--reset/--show/--allsave/--sync/--export/--meta
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
 * @brief 注册 https 命令
 * https --status/--start/--stop/--restart
 */
esp_err_t ts_cmd_https_register(void);

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
 * @brief 注册 Key 命令（安全密钥存储管理）
 * key --list/--info/--import/--generate/--delete/--export
 */
esp_err_t ts_cmd_key_register(void);

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
 * @brief 注册 hosts 命令（SSH 已知主机管理）
 * hosts --list/--info/--remove/--clear
 */
esp_err_t ts_cmd_hosts_register(void);

/**
 * @brief 注册文件系统命令
 * ls, cat, cd, pwd, mkdir, rm, cp, mv, hexdump
 */
esp_err_t ts_cmd_fs_register(void);

/**
 * @brief 注册 power 命令（电源监控）
 * power status/voltage/chip/start/stop/threshold/interval/stats/reset/debug/test/help
 */
esp_err_t ts_cmd_power_register(void);

/**
 * @brief 注册 voltprot 命令（电压保护策略）
 * voltprot --status/--test/--reset/--config/--debug
 */
esp_err_t ts_cmd_voltprot_register(void);

/**
 * @brief 注册 gpio 命令（GPIO 直接控制）
 * gpio <pin> high|low|input|reset
 * gpio --list/--info
 */
esp_err_t ts_cmd_gpio_register(void);

/**
 * @brief 注册 temp 命令（温度源管理）
 * temp --status/--set/--mode/--providers
 */
esp_err_t ts_cmd_temp_register(void);

/**
 * @brief 注册 monitor 命令（算力设备监控）
 * monitor --status/--data/--start/--stop/--config
 */
esp_err_t ts_cmd_monitor_register(void);

/**
 * @brief 注册 ota 命令（固件升级）
 * ota --status/--progress/--version/--partitions/--url/--file/--validate/--rollback/--abort
 */
esp_err_t ts_cmd_ota_register(void);

/**
 * @brief 注册 pki 命令（PKI 证书管理）
 * pki --status/--generate/--csr/--install/--install-ca/--export-csr/--info/--reset
 */
esp_err_t ts_cmd_pki_register(void);

/**
 * @brief 注册所有扩展命令
 * 一次性注册所有非内置命令
 */
esp_err_t ts_cmd_register_all(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_CMD_ALL_H */
