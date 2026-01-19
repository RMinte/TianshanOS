/**
 * @file ts_cmd_register.c
 * @brief Register All Console Commands
 * 
 * 统一注册所有扩展命令
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_cmd_all.h"
#include "ts_log.h"

#define TAG "cmd_register"

esp_err_t ts_cmd_register_all(void)
{
    esp_err_t ret;
    int success_count = 0;
    int fail_count = 0;
    
    TS_LOGI(TAG, "Registering all console commands...");
    
    // 系统命令
    ret = ts_cmd_system_register();
    if (ret == ESP_OK) success_count++; else fail_count++;
    
    // 服务命令
    ret = ts_cmd_service_register();
    if (ret == ESP_OK) success_count++; else fail_count++;
    
    // 配置命令
    ret = ts_cmd_config_register();
    if (ret == ESP_OK) success_count++; else fail_count++;
    
    // 风扇命令
    ret = ts_cmd_fan_register();
    if (ret == ESP_OK) success_count++; else fail_count++;
    
    // 存储命令
    ret = ts_cmd_storage_register();
    if (ret == ESP_OK) success_count++; else fail_count++;
    
    // 网络命令
    ret = ts_cmd_net_register();
    if (ret == ESP_OK) success_count++; else fail_count++;
    
    // 设备命令
    ret = ts_cmd_device_register();
    if (ret == ESP_OK) success_count++; else fail_count++;
    
    // LED 命令
    ret = ts_cmd_led_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // DHCP 命令
    ret = ts_cmd_dhcp_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // WiFi 命令
    ret = ts_cmd_wifi_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // NAT 网关命令
    ts_cmd_nat_register();
    success_count++;

    // Key 安全存储命令
    ret = ts_cmd_key_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // SSH 命令
    ret = ts_cmd_ssh_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // SFTP/SCP 文件传输命令
    ret = ts_cmd_sftp_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // Hosts 已知主机管理命令
    ret = ts_cmd_hosts_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // 文件系统命令 (ls, cat, cd, pwd, mkdir, rm, cp, mv, hexdump)
    ret = ts_cmd_fs_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // 电源监控命令
    ret = ts_cmd_power_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // 电压保护策略命令
    ret = ts_cmd_voltprot_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // GPIO 直接控制命令
    ret = ts_cmd_gpio_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // 温度源管理命令
    ret = ts_cmd_temp_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    // AGX 监控命令
    ret = ts_cmd_agx_register();
    if (ret == ESP_OK) success_count++; else fail_count++;

    TS_LOGI(TAG, "Command registration complete: %d succeeded, %d failed",
        success_count, fail_count);
    
    return (fail_count == 0) ? ESP_OK : ESP_FAIL;
}
