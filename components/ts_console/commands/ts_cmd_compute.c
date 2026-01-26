/**
 * @file ts_cmd_compute.c
 * @brief Compute Monitor Console Commands
 * 
 * 实现 compute 命令族（算力设备监控）：
 * - compute --status           显示算力设备监控状态和连接信息
 * - compute --data             显示最新算力设备数据
 * - compute --start            启动算力设备监控
 * - compute --stop             停止算力设备监控
 * - compute --config           显示/设置配置
 * 
 * @author TianShanOS Team
 * @version 2.0.0
 */

#include "ts_console.h"
#include "ts_compute_monitor.h"
#include "ts_api.h"
#include "ts_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "cmd_compute"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *status;
    struct arg_lit *data;
    struct arg_lit *start;
    struct arg_lit *stop;
    struct arg_lit *config;
    struct arg_str *server;      // 服务器 IP
    struct arg_int *port;        // 服务器端口
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_compute_args;

/*===========================================================================*/
/*                          Command: compute --status                             */
/*===========================================================================*/

static int do_compute_status(bool json)
{
    if (!ts_compute_monitor_is_initialized()) {
        if (json) {
            ts_console_printf("{\"error\":\"Compute monitor not initialized\"}\n");
        } else {
            ts_console_error("Compute monitor not initialized\n");
        }
        return 1;
    }
    
    /* JSON 模式使用 API */
    if (json) {
        ts_api_result_t result;
        ts_api_result_init(&result);
        
        esp_err_t ret = ts_api_call("compute.status", NULL, &result);
        if (ret == ESP_OK && result.code == TS_API_OK && result.data) {
            char *json_str = cJSON_PrintUnformatted(result.data);
            if (json_str) {
                ts_console_printf("%s\n", json_str);
                free(json_str);
            }
        } else {
            ts_console_printf("{\"error\":\"%s\"}\n", result.message ? result.message : "Unknown error");
        }
        ts_api_result_free(&result);
        return (ret == ESP_OK) ? 0 : 1;
    }
    
    /* 格式化输出 */
    ts_compute_status_info_t status;
    esp_err_t ret = ts_compute_monitor_get_status(&status);
    if (ret != ESP_OK) {
        ts_console_error("Failed to get status: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("Compute Monitor Status:\n");
    ts_console_printf("  Initialized:    %s\n", status.initialized ? "Yes" : "No");
    ts_console_printf("  Running:        %s\n", status.running ? "Yes" : "No");
    ts_console_printf("  Connection:     %s\n", ts_compute_status_to_str(status.connection_status));
    ts_console_printf("  Reconnects:     %lu\n", status.total_reconnects);
    ts_console_printf("  Messages:       %lu\n", status.messages_received);
    ts_console_printf("  Parse Errors:   %lu\n", status.parse_errors);
    ts_console_printf("  Reliability:    %.1f%%\n", status.connection_reliability);
    
    if (status.connection_status == TS_COMPUTE_STATUS_CONNECTED) {
        ts_console_printf("  Connected:      %llu ms\n", status.connected_time_ms);
    }
    
    if (status.last_error[0] != '\0') {
        ts_console_printf("  Last Error:     %s\n", status.last_error);
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: compute --data                               */
/*===========================================================================*/

static int do_compute_data(bool json)
{
    if (!ts_compute_monitor_is_data_valid()) {
        if (json) {
            ts_console_printf("{\"error\":\"No valid Compute data\"}\n");
        } else {
            ts_console_error("No valid Compute data available\n");
        }
        return 1;
    }
    
    /* JSON 模式使用 API */
    if (json) {
        ts_api_result_t result;
        ts_api_result_init(&result);
        
        esp_err_t ret = ts_api_call("compute.data", NULL, &result);
        if (ret == ESP_OK && result.code == TS_API_OK && result.data) {
            char *json_str = cJSON_PrintUnformatted(result.data);
            if (json_str) {
                ts_console_printf("%s\n", json_str);
                free(json_str);
            }
        } else {
            ts_console_printf("{\"error\":\"%s\"}\n", result.message ? result.message : "Unknown error");
        }
        ts_api_result_free(&result);
        return (ret == ESP_OK) ? 0 : 1;
    }
    
    /* 格式化输出 */
    ts_compute_data_t data;
    esp_err_t ret = ts_compute_monitor_get_data(&data);
    if (ret != ESP_OK) {
        ts_console_error("Failed to get data: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("Compute Data:\n");
    ts_console_printf("  Timestamp:     %s\n", data.timestamp);
    ts_console_printf("\n  CPU (%d cores):\n", data.cpu.core_count);
    for (int i = 0; i < data.cpu.core_count; i++) {
        ts_console_printf("    Core %d: %3d%% @ %4d MHz\n",
            data.cpu.cores[i].id,
            data.cpu.cores[i].usage,
            data.cpu.cores[i].freq_mhz);
    }
    
    ts_console_printf("\n  Memory:\n");
    ts_console_printf("    RAM:  %lu / %lu MB\n", 
        data.memory.ram.used_mb, data.memory.ram.total_mb);
    ts_console_printf("    SWAP: %lu / %lu MB\n",
        data.memory.swap.used_mb, data.memory.swap.total_mb);
    
    ts_console_printf("\n  Temperature:\n");
    ts_console_printf("    CPU:   %.1f°C\n", data.temperature.cpu);
    ts_console_printf("    SoC0:  %.1f°C\n", data.temperature.soc0);
    ts_console_printf("    SoC1:  %.1f°C\n", data.temperature.soc1);
    ts_console_printf("    SoC2:  %.1f°C\n", data.temperature.soc2);
    ts_console_printf("    Tj:    %.1f°C\n", data.temperature.tj);
    
    ts_console_printf("\n  Power:\n");
    ts_console_printf("    GPU+SoC: %lu mW (avg: %lu mW)\n",
        data.power.gpu_soc.current_mw, data.power.gpu_soc.average_mw);
    ts_console_printf("    CPU:     %lu mW (avg: %lu mW)\n",
        data.power.cpu_cv.current_mw, data.power.cpu_cv.average_mw);
    ts_console_printf("    SYS 5V:  %lu mW (avg: %lu mW)\n",
        data.power.sys_5v.current_mw, data.power.sys_5v.average_mw);
    
    ts_console_printf("\n  GPU:\n");
    ts_console_printf("    GR3D Freq: %d%%\n", data.gpu.gr3d_freq_pct);
    
    return 0;
}

/*===========================================================================*/
/*                          Command: compute --start/stop                         */
/*===========================================================================*/

static int do_compute_start(void)
{
    if (!ts_compute_monitor_is_initialized()) {
        /* 初始化默认配置 */
        esp_err_t ret = ts_compute_monitor_init(NULL);
        if (ret != ESP_OK) {
            ts_console_error("Failed to initialize: %s\n", esp_err_to_name(ret));
            return 1;
        }
    }
    
    if (ts_compute_monitor_is_running()) {
        ts_console_printf("Compute monitor already running\n");
        return 0;
    }
    
    esp_err_t ret = ts_compute_monitor_start();
    if (ret != ESP_OK) {
        ts_console_error("Failed to start: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("Compute monitor started\n");
    return 0;
}

static int do_compute_stop(void)
{
    if (!ts_compute_monitor_is_running()) {
        ts_console_printf("Compute monitor not running\n");
        return 0;
    }
    
    esp_err_t ret = ts_compute_monitor_stop();
    if (ret != ESP_OK) {
        ts_console_error("Failed to stop: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("Compute monitor stopped\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: compute --config                             */
/*===========================================================================*/

static int do_compute_config(const char *server, int port, bool json)
{
    ts_compute_config_t config;
    ts_compute_monitor_get_default_config(&config);
    
    bool changed = false;
    
    if (server != NULL) {
        strncpy(config.server_ip, server, sizeof(config.server_ip) - 1);
        changed = true;
    }
    
    if (port > 0) {
        config.server_port = (uint16_t)port;
        changed = true;
    }
    
    if (changed) {
        /* 需要重新初始化才能生效 */
        if (ts_compute_monitor_is_running()) {
            ts_compute_monitor_stop();
        }
        if (ts_compute_monitor_is_initialized()) {
            ts_compute_monitor_deinit();
        }
        
        esp_err_t ret = ts_compute_monitor_init(&config);
        if (ret != ESP_OK) {
            ts_console_error("Failed to reinit with new config: %s\n", esp_err_to_name(ret));
            return 1;
        }
        
        ts_console_printf("Configuration updated: %s:%d\n", config.server_ip, config.server_port);
    } else {
        /* 显示当前配置 */
        if (json) {
            ts_api_result_t result;
            ts_api_result_init(&result);
            
            esp_err_t ret = ts_api_call("compute.config", NULL, &result);
            if (ret == ESP_OK && result.code == TS_API_OK && result.data) {
                char *json_str = cJSON_PrintUnformatted(result.data);
                if (json_str) {
                    ts_console_printf("%s\n", json_str);
                    free(json_str);
                }
            }
            ts_api_result_free(&result);
            return (ret == ESP_OK) ? 0 : 1;
        } else {
            ts_console_printf("Compute Monitor Configuration:\n");
            ts_console_printf("  Server:           %s\n", config.server_ip);
            ts_console_printf("  Port:             %d\n", config.server_port);
            ts_console_printf("  Reconnect:        %lu ms\n", config.reconnect_interval_ms);
            ts_console_printf("  Startup Delay:    %lu ms\n", config.startup_delay_ms);
            ts_console_printf("  Heartbeat Timeout: %lu ms\n", config.heartbeat_timeout_ms);
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int compute_cmd_handler(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_compute_args);
    bool json = s_compute_args.json->count > 0;
    
    if (s_compute_args.help->count > 0) {
        ts_console_printf("Usage: compute [OPTIONS]\n");
        ts_console_printf("\nOptions:\n");
        ts_console_printf("  --status        Show compute monitor status\n");
        ts_console_printf("  --data          Show latest compute data\n");
        ts_console_printf("  --start         Start compute monitoring\n");
        ts_console_printf("  --stop          Stop compute monitoring\n");
        ts_console_printf("  --config        Show/set configuration\n");
        ts_console_printf("  --server <ip>   Set server IP\n");
        ts_console_printf("  --port <n>      Set server port\n");
        ts_console_printf("  --json,-j       Output in JSON format\n");
        ts_console_printf("  --help,-h       Show this help\n");
        ts_console_printf("\nExamples:\n");
        ts_console_printf("  compute --status              # Show connection status\n");
        ts_console_printf("  compute --data --json         # Get Compute data in JSON\n");
        ts_console_printf("  compute --start               # Start monitoring\n");
        ts_console_printf("  compute --config --server 10.10.99.98 --port 58090\n");
        return 0;
    }
    
    if (nerrors > 0) {
        arg_print_errors(stderr, s_compute_args.end, "compute");
        return 1;
    }
    
    /* compute --start */
    if (s_compute_args.start->count > 0) {
        return do_compute_start();
    }
    
    /* compute --stop */
    if (s_compute_args.stop->count > 0) {
        return do_compute_stop();
    }
    
    /* compute --data */
    if (s_compute_args.data->count > 0) {
        return do_compute_data(json);
    }
    
    /* compute --config */
    if (s_compute_args.config->count > 0 || 
        s_compute_args.server->count > 0 || 
        s_compute_args.port->count > 0) {
        const char *server = s_compute_args.server->count > 0 ? s_compute_args.server->sval[0] : NULL;
        int port = s_compute_args.port->count > 0 ? s_compute_args.port->ival[0] : 0;
        return do_compute_config(server, port, json);
    }
    
    /* Default: compute --status */
    return do_compute_status(json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_compute_register(void)
{
    s_compute_args.status = arg_lit0(NULL, "status", "Show monitor status");
    s_compute_args.data   = arg_lit0(NULL, "data", "Show latest compute data");
    s_compute_args.start  = arg_lit0(NULL, "start", "Start compute monitoring");
    s_compute_args.stop   = arg_lit0(NULL, "stop", "Stop compute monitoring");
    s_compute_args.config = arg_lit0(NULL, "config", "Show/set configuration");
    s_compute_args.server = arg_str0(NULL, "server", "<ip>", "Server IP address");
    s_compute_args.port   = arg_int0(NULL, "port", "<n>", "Server port");
    s_compute_args.json   = arg_lit0("j", "json", "Output JSON format");
    s_compute_args.help   = arg_lit0("h", "help", "Show help");
    s_compute_args.end    = arg_end(5);
    
    const esp_console_cmd_t cmd = {
        .command = "compute",
        .help = "Compute device monitoring (Jetson AGX, servers, etc.)",
        .hint = NULL,
        .func = compute_cmd_handler,
        .argtable = &s_compute_args,
    };
    
    return esp_console_cmd_register(&cmd);
}
