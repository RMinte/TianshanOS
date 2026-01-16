/**
 * @file ts_cmd_wifi.c
 * @brief WiFi Console Commands
 * 
 * 实现 wifi 命令族：
 * - wifi --status           显示 WiFi 状态
 * - wifi --scan             扫描附近 AP
 * - wifi --ap               管理 AP 模式
 * - wifi --connect          连接到 AP (STA 模式)
 * - wifi --disconnect       断开连接
 * - wifi --start/--stop     启动/停止接口
 * - wifi --save             保存配置
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-17
 */

#include "ts_console.h"
#include "ts_net_manager.h"
#include "ts_wifi.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>

#define TAG "cmd_wifi"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *status;
    struct arg_lit *scan;
    struct arg_lit *ap;
    struct arg_lit *connect;
    struct arg_lit *disconnect;
    struct arg_lit *start;
    struct arg_lit *stop;
    struct arg_lit *save;
    struct arg_str *ssid;
    struct arg_str *pass;
    struct arg_str *iface;   /* ap 或 sta */
    struct arg_int *channel;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_wifi_args;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static const char *auth_mode_str(int auth_mode)
{
    switch (auth_mode) {
        case 0: return "OPEN";
        case 1: return "WEP";
        case 2: return "WPA_PSK";
        case 3: return "WPA2_PSK";
        case 4: return "WPA_WPA2_PSK";
        case 5: return "WPA2_ENTERPRISE";
        case 6: return "WPA3_PSK";
        case 7: return "WPA2_WPA3_PSK";
        default: return "UNKNOWN";
    }
}

/*===========================================================================*/
/*                          Command: wifi --status                            */
/*===========================================================================*/

static int do_wifi_status(bool json_out)
{
    ts_net_manager_status_t status;
    esp_err_t ret = ts_net_manager_get_status(&status);
    
    if (ret != ESP_OK) {
        ts_console_error("Failed to get WiFi status\n");
        return 1;
    }
    
    if (json_out) {
        ts_console_printf("{\n");
        
        /* AP 状态 */
        ts_console_printf("  \"ap\": {\n");
        ts_console_printf("    \"state\": \"%s\",\n", ts_net_state_to_str(status.wifi_ap.state));
        ts_console_printf("    \"has_ip\": %s", status.wifi_ap.has_ip ? "true" : "false");
        if (status.wifi_ap.has_ip) {
            ts_console_printf(",\n    \"ip\": \"%s\"", status.wifi_ap.ip_info.ip);
        }
        ts_console_printf("\n  },\n");
        
        /* STA 状态 */
        ts_console_printf("  \"sta\": {\n");
        ts_console_printf("    \"state\": \"%s\",\n", ts_net_state_to_str(status.wifi_sta.state));
        ts_console_printf("    \"has_ip\": %s", status.wifi_sta.has_ip ? "true" : "false");
        if (status.wifi_sta.has_ip) {
            ts_console_printf(",\n    \"ip\": \"%s\",\n", status.wifi_sta.ip_info.ip);
            ts_console_printf("    \"gateway\": \"%s\"", status.wifi_sta.ip_info.gateway);
        }
        ts_console_printf("\n  }\n");
        
        ts_console_printf("}\n");
    } else {
        ts_console_printf("\n");
        ts_console_printf("╔══════════════════════════════════════════════════════════════╗\n");
        ts_console_printf("║                       WiFi Status                            ║\n");
        ts_console_printf("╠══════════════════════════════════════════════════════════════╣\n");
        
        /* AP 状态 */
        ts_console_printf("║ \033[1mAccess Point (AP)\033[0m                                           ║\n");
        const char *ap_color = status.wifi_ap.state == TS_NET_STATE_CONNECTED ? "\033[32m" : "\033[90m";
        ts_console_printf("║   State:    %s%-12s\033[0m                                   ║\n",
            ap_color, ts_net_state_to_str(status.wifi_ap.state));
        if (status.wifi_ap.has_ip) {
            ts_console_printf("║   IP:       %-15s                              ║\n", status.wifi_ap.ip_info.ip);
            
            /* 获取连接的客户端数量 */
            uint8_t sta_count = ts_wifi_ap_get_sta_count();
            ts_console_printf("║   Clients:  %-3d                                            ║\n", sta_count);
        }
        
        ts_console_printf("╠══════════════════════════════════════════════════════════════╣\n");
        
        /* STA 状态 */
        ts_console_printf("║ \033[1mStation (STA)\033[0m                                               ║\n");
        const char *sta_color = status.wifi_sta.state == TS_NET_STATE_GOT_IP ? "\033[32m" : "\033[90m";
        ts_console_printf("║   State:    %s%-12s\033[0m                                   ║\n",
            sta_color, ts_net_state_to_str(status.wifi_sta.state));
        
        if (status.wifi_sta.has_ip) {
            ts_console_printf("║   IP:       %-15s                              ║\n", status.wifi_sta.ip_info.ip);
            ts_console_printf("║   Gateway:  %-15s                              ║\n", status.wifi_sta.ip_info.gateway);
            
            int8_t rssi = ts_wifi_sta_get_rssi();
            ts_console_printf("║   RSSI:     %d dBm                                          ║\n", rssi);
        }
        
        ts_console_printf("╚══════════════════════════════════════════════════════════════╝\n");
        ts_console_printf("\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: wifi --scan                              */
/*===========================================================================*/

static int do_wifi_scan(bool json_out)
{
    ts_console_printf("Scanning for WiFi networks...\n");
    
    /* 确保 WiFi 已初始化并设置为 STA 模式（用于扫描） */
    ts_wifi_mode_t mode = ts_wifi_get_mode();
    if (mode == TS_WIFI_MODE_OFF) {
        ts_wifi_set_mode(TS_WIFI_MODE_STA);
    }
    
    /* 启动扫描（阻塞） */
    esp_err_t ret = ts_wifi_scan_start(true);
    if (ret != ESP_OK) {
        ts_console_error("Scan failed: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    /* 获取扫描结果 */
    ts_wifi_scan_result_t results[20];
    uint16_t count = 20;
    
    ret = ts_wifi_scan_get_results(results, &count);
    if (ret != ESP_OK) {
        ts_console_error("Failed to get scan results: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    if (json_out) {
        ts_console_printf("[\n");
        for (int i = 0; i < count; i++) {
            ts_console_printf("  {\n");
            ts_console_printf("    \"ssid\": \"%s\",\n", results[i].ssid);
            ts_console_printf("    \"bssid\": \"%02x:%02x:%02x:%02x:%02x:%02x\",\n",
                results[i].bssid[0], results[i].bssid[1], results[i].bssid[2],
                results[i].bssid[3], results[i].bssid[4], results[i].bssid[5]);
            ts_console_printf("    \"rssi\": %d,\n", results[i].rssi);
            ts_console_printf("    \"channel\": %d,\n", results[i].channel);
            ts_console_printf("    \"auth\": \"%s\"\n", auth_mode_str(results[i].auth_mode));
            ts_console_printf("  }%s\n", (i < count - 1) ? "," : "");
        }
        ts_console_printf("]\n");
    } else {
        ts_console_printf("\nFound %d networks:\n\n", count);
        ts_console_printf("  %-32s  %6s  %4s  %-15s\n", "SSID", "RSSI", "CH", "Security");
        ts_console_printf("  %-32s  %6s  %4s  %-15s\n", "--------------------------------", "------", "----", "---------------");
        
        for (int i = 0; i < count; i++) {
            const char *rssi_color = results[i].rssi > -50 ? "\033[32m" :
                                     results[i].rssi > -70 ? "\033[33m" : "\033[31m";
            ts_console_printf("  %-32s  %s%4d dB\033[0m  %4d  %-15s\n",
                results[i].ssid,
                rssi_color, results[i].rssi,
                results[i].channel,
                auth_mode_str(results[i].auth_mode));
        }
        ts_console_printf("\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: wifi --ap                                */
/*===========================================================================*/

static int do_wifi_ap(const char *ssid, const char *pass, int channel)
{
    /* 获取当前配置 */
    ts_net_if_config_t config;
    ts_net_manager_get_config(TS_NET_IF_WIFI_AP, &config);
    
    bool changed = false;
    
    if (ssid && ssid[0] != '\0') {
        strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
        changed = true;
    }
    
    if (pass) {
        strncpy(config.password, pass, sizeof(config.password) - 1);
        changed = true;
    }
    
    if (channel > 0 && channel <= 13) {
        config.channel = (uint8_t)channel;
        changed = true;
    }
    
    if (!changed) {
        /* 显示当前配置 */
        ts_console_printf("WiFi AP Configuration:\n");
        ts_console_printf("  SSID:     %s\n", config.ssid);
        ts_console_printf("  Password: %s\n", config.password[0] ? "****" : "(none)");
        ts_console_printf("  Channel:  %d\n", config.channel);
        ts_console_printf("  Enabled:  %s\n", config.enabled ? "yes" : "no");
        return 0;
    }
    
    config.enabled = true;
    
    esp_err_t ret = ts_net_manager_set_config(TS_NET_IF_WIFI_AP, &config);
    if (ret != ESP_OK) {
        ts_console_error("Failed to set AP config: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("AP configuration updated:\n");
    ts_console_printf("  SSID: %s\n", config.ssid);
    ts_console_printf("  Password: %s\n", config.password[0] ? "****" : "(none)");
    ts_console_printf("\nUse 'wifi --start --iface ap' to start the AP\n");
    ts_console_printf("Use 'wifi --save' to persist the configuration\n");
    
    return 0;
}

/*===========================================================================*/
/*                          Command: wifi --connect                           */
/*===========================================================================*/

static int do_wifi_connect(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') {
        ts_console_error("SSID is required. Use: wifi --connect --ssid <name> --pass <password>\n");
        return 1;
    }
    
    /* 设置 STA 配置 */
    ts_net_if_config_t config = {0};
    config.enabled = true;
    config.auto_start = true;
    strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
    if (pass) {
        strncpy(config.password, pass, sizeof(config.password) - 1);
    }
    
    esp_err_t ret = ts_net_manager_set_config(TS_NET_IF_WIFI_STA, &config);
    if (ret != ESP_OK) {
        ts_console_error("Failed to set STA config: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("Connecting to '%s'...\n", ssid);
    
    ret = ts_net_manager_start(TS_NET_IF_WIFI_STA);
    if (ret != ESP_OK) {
        ts_console_error("Failed to connect: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("Connection initiated. Use 'wifi --status' to check.\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: wifi --start/--stop                      */
/*===========================================================================*/

static int do_wifi_start(const char *iface_str)
{
    ts_net_if_t iface = TS_NET_IF_WIFI_AP;  /* 默认 AP */
    
    if (iface_str) {
        if (strcmp(iface_str, "sta") == 0) {
            iface = TS_NET_IF_WIFI_STA;
        } else if (strcmp(iface_str, "ap") == 0) {
            iface = TS_NET_IF_WIFI_AP;
        }
    }
    
    const char *name = (iface == TS_NET_IF_WIFI_AP) ? "AP" : "STA";
    ts_console_printf("Starting WiFi %s...\n", name);
    
    esp_err_t ret = ts_net_manager_start(iface);
    if (ret == ESP_OK) {
        ts_console_printf("WiFi %s started\n", name);
        return 0;
    } else {
        ts_console_error("Failed to start WiFi %s: %s\n", name, esp_err_to_name(ret));
        return 1;
    }
}

static int do_wifi_stop(const char *iface_str)
{
    ts_net_if_t iface = TS_NET_IF_WIFI_AP;  /* 默认 AP */
    
    if (iface_str) {
        if (strcmp(iface_str, "sta") == 0) {
            iface = TS_NET_IF_WIFI_STA;
        } else if (strcmp(iface_str, "ap") == 0) {
            iface = TS_NET_IF_WIFI_AP;
        }
    }
    
    const char *name = (iface == TS_NET_IF_WIFI_AP) ? "AP" : "STA";
    ts_console_printf("Stopping WiFi %s...\n", name);
    
    esp_err_t ret = ts_net_manager_stop(iface);
    if (ret == ESP_OK) {
        ts_console_printf("WiFi %s stopped\n", name);
        return 0;
    } else {
        ts_console_error("Failed to stop WiFi %s: %s\n", name, esp_err_to_name(ret));
        return 1;
    }
}

/*===========================================================================*/
/*                          Command: wifi --save                              */
/*===========================================================================*/

static int do_wifi_save(void)
{
    ts_console_printf("Saving WiFi configuration...\n");
    esp_err_t ret = ts_net_manager_save_config();
    
    if (ret == ESP_OK) {
        ts_console_printf("WiFi configuration saved to NVS\n");
        return 0;
    } else {
        ts_console_error("Failed to save configuration: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_wifi(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_wifi_args);
    
    /* 显示帮助 */
    if (s_wifi_args.help->count > 0) {
        ts_console_printf("Usage: wifi [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  --status            Show WiFi status\n");
        ts_console_printf("  --scan              Scan for WiFi networks\n");
        ts_console_printf("  --ap                Configure/show AP mode\n");
        ts_console_printf("  --connect           Connect to a WiFi network (STA)\n");
        ts_console_printf("  --disconnect        Disconnect from WiFi (STA)\n");
        ts_console_printf("  --start             Start WiFi interface\n");
        ts_console_printf("  --stop              Stop WiFi interface\n");
        ts_console_printf("  --save              Save configuration to NVS\n");
        ts_console_printf("\n");
        ts_console_printf("Parameters:\n");
        ts_console_printf("  --ssid <name>       WiFi network name\n");
        ts_console_printf("  --pass <password>   WiFi password\n");
        ts_console_printf("  --iface <if>        Interface: ap or sta (default: ap)\n");
        ts_console_printf("  --channel <1-13>    WiFi channel (AP mode)\n");
        ts_console_printf("  --json              Output in JSON format\n");
        ts_console_printf("\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  wifi --status                     Show WiFi status\n");
        ts_console_printf("  wifi --scan                       Scan for networks\n");
        ts_console_printf("  wifi --ap --ssid MyAP --pass 12345678\n");
        ts_console_printf("  wifi --start --iface ap           Start AP\n");
        ts_console_printf("  wifi --connect --ssid Home --pass secret\n");
        ts_console_printf("  wifi --save                       Save config\n");
        return 0;
    }
    
    if (nerrors > 0 && argc > 1) {
        arg_print_errors(stderr, s_wifi_args.end, "wifi");
        return 1;
    }
    
    bool json_out = s_wifi_args.json->count > 0;
    const char *ssid = s_wifi_args.ssid->count > 0 ? s_wifi_args.ssid->sval[0] : NULL;
    const char *pass = s_wifi_args.pass->count > 0 ? s_wifi_args.pass->sval[0] : NULL;
    const char *iface_str = s_wifi_args.iface->count > 0 ? s_wifi_args.iface->sval[0] : NULL;
    int channel = s_wifi_args.channel->count > 0 ? s_wifi_args.channel->ival[0] : 0;
    
    /* 处理各个操作 */
    if (s_wifi_args.status->count > 0 || argc == 1) {
        return do_wifi_status(json_out);
    }
    
    if (s_wifi_args.scan->count > 0) {
        return do_wifi_scan(json_out);
    }
    
    if (s_wifi_args.ap->count > 0) {
        return do_wifi_ap(ssid, pass, channel);
    }
    
    if (s_wifi_args.connect->count > 0) {
        return do_wifi_connect(ssid, pass);
    }
    
    if (s_wifi_args.disconnect->count > 0) {
        return do_wifi_stop("sta");
    }
    
    if (s_wifi_args.start->count > 0) {
        return do_wifi_start(iface_str);
    }
    
    if (s_wifi_args.stop->count > 0) {
        return do_wifi_stop(iface_str);
    }
    
    if (s_wifi_args.save->count > 0) {
        return do_wifi_save();
    }
    
    /* 默认显示状态 */
    return do_wifi_status(json_out);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_wifi_register(void)
{
    /* 初始化参数表 */
    s_wifi_args.status     = arg_lit0("s", "status", "Show WiFi status");
    s_wifi_args.scan       = arg_lit0(NULL, "scan", "Scan for WiFi networks");
    s_wifi_args.ap         = arg_lit0(NULL, "ap", "Configure AP mode");
    s_wifi_args.connect    = arg_lit0(NULL, "connect", "Connect to WiFi (STA)");
    s_wifi_args.disconnect = arg_lit0(NULL, "disconnect", "Disconnect from WiFi");
    s_wifi_args.start      = arg_lit0(NULL, "start", "Start WiFi interface");
    s_wifi_args.stop       = arg_lit0(NULL, "stop", "Stop WiFi interface");
    s_wifi_args.save       = arg_lit0(NULL, "save", "Save configuration to NVS");
    s_wifi_args.ssid       = arg_str0(NULL, "ssid", "<name>", "WiFi SSID");
    s_wifi_args.pass       = arg_str0(NULL, "pass", "<password>", "WiFi password");
    s_wifi_args.iface      = arg_str0(NULL, "iface", "<if>", "Interface: ap, sta");
    s_wifi_args.channel    = arg_int0(NULL, "channel", "<1-13>", "WiFi channel");
    s_wifi_args.json       = arg_lit0("j", "json", "Output in JSON format");
    s_wifi_args.help       = arg_lit0("h", "help", "Show help");
    s_wifi_args.end        = arg_end(5);
    
    const ts_console_cmd_t cmd = {
        .command = "wifi",
        .help = "WiFi management (AP/STA mode, scan, connect)",
        .hint = NULL,
        .category = TS_CMD_CAT_NETWORK,
        .func = cmd_wifi,
        .argtable = &s_wifi_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "WiFi commands registered");
    }
    
    return ret;
}
