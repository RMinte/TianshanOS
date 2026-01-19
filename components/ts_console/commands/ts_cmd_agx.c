/**
 * @file ts_cmd_agx.c
 * @brief AGX Monitor Console Commands
 * 
 * 实现 agx 命令族：
 * - agx --status           显示 AGX 监控状态和连接信息
 * - agx --data             显示最新 AGX 数据
 * - agx --start            启动 AGX 监控
 * - agx --stop             停止 AGX 监控
 * - agx --config           显示/设置配置
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_console.h"
#include "ts_agx_monitor.h"
#include "ts_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "cmd_agx"

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
} s_agx_args;

/*===========================================================================*/
/*                          Command: agx --status                             */
/*===========================================================================*/

static int do_agx_status(bool json)
{
    if (!ts_agx_monitor_is_initialized()) {
        if (json) {
            ts_console_printf("{\"error\":\"AGX monitor not initialized\"}\n");
        } else {
            ts_console_error("AGX monitor not initialized\n");
        }
        return 1;
    }
    
    ts_agx_status_info_t status;
    esp_err_t ret = ts_agx_monitor_get_status(&status);
    if (ret != ESP_OK) {
        ts_console_error("Failed to get status: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    if (json) {
        ts_console_printf(
            "{\"initialized\":%s,\"running\":%s,\"connection\":\"%s\","
            "\"reconnects\":%lu,\"messages\":%lu,\"errors\":%lu,"
            "\"reliability\":%.1f,\"connected_time_ms\":%llu",
            status.initialized ? "true" : "false",
            status.running ? "true" : "false",
            ts_agx_status_to_str(status.connection_status),
            status.total_reconnects,
            status.messages_received,
            status.parse_errors,
            status.connection_reliability,
            status.connected_time_ms);
        
        if (status.last_error[0] != '\0') {
            ts_console_printf(",\"last_error\":\"%s\"", status.last_error);
        }
        ts_console_printf("}\n");
    } else {
        ts_console_printf("AGX Monitor Status:\n");
        ts_console_printf("  Initialized:    %s\n", status.initialized ? "Yes" : "No");
        ts_console_printf("  Running:        %s\n", status.running ? "Yes" : "No");
        ts_console_printf("  Connection:     %s\n", ts_agx_status_to_str(status.connection_status));
        ts_console_printf("  Reconnects:     %lu\n", status.total_reconnects);
        ts_console_printf("  Messages:       %lu\n", status.messages_received);
        ts_console_printf("  Parse Errors:   %lu\n", status.parse_errors);
        ts_console_printf("  Reliability:    %.1f%%\n", status.connection_reliability);
        
        if (status.connection_status == TS_AGX_STATUS_CONNECTED) {
            ts_console_printf("  Connected:      %llu ms\n", status.connected_time_ms);
        }
        
        if (status.last_error[0] != '\0') {
            ts_console_printf("  Last Error:     %s\n", status.last_error);
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: agx --data                               */
/*===========================================================================*/

static int do_agx_data(bool json)
{
    if (!ts_agx_monitor_is_data_valid()) {
        if (json) {
            ts_console_printf("{\"error\":\"No valid AGX data\"}\n");
        } else {
            ts_console_error("No valid AGX data available\n");
        }
        return 1;
    }
    
    ts_agx_data_t data;
    esp_err_t ret = ts_agx_monitor_get_data(&data);
    if (ret != ESP_OK) {
        ts_console_error("Failed to get data: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    if (json) {
        ts_console_printf("{\"timestamp\":\"%s\",", data.timestamp);
        
        /* CPU */
        ts_console_printf("\"cpu\":{\"cores\":%d,\"data\":[", data.cpu.core_count);
        for (int i = 0; i < data.cpu.core_count; i++) {
            if (i > 0) ts_console_printf(",");
            ts_console_printf("{\"id\":%d,\"usage\":%d,\"freq\":%d}",
                data.cpu.cores[i].id,
                data.cpu.cores[i].usage,
                data.cpu.cores[i].freq_mhz);
        }
        ts_console_printf("]},");
        
        /* Memory */
        ts_console_printf("\"memory\":{\"ram\":{\"used\":%lu,\"total\":%lu},"
                         "\"swap\":{\"used\":%lu,\"total\":%lu}},",
            data.memory.ram.used_mb, data.memory.ram.total_mb,
            data.memory.swap.used_mb, data.memory.swap.total_mb);
        
        /* Temperature */
        ts_console_printf("\"temperature\":{\"cpu\":%.1f,\"soc0\":%.1f,"
                         "\"soc1\":%.1f,\"soc2\":%.1f,\"tj\":%.1f},",
            data.temperature.cpu, data.temperature.soc0,
            data.temperature.soc1, data.temperature.soc2,
            data.temperature.tj);
        
        /* Power */
        ts_console_printf("\"power\":{\"gpu_soc\":{\"current\":%lu,\"average\":%lu},"
                         "\"cpu_cv\":{\"current\":%lu,\"average\":%lu},"
                         "\"sys_5v\":{\"current\":%lu,\"average\":%lu}},",
            data.power.gpu_soc.current_mw, data.power.gpu_soc.average_mw,
            data.power.cpu_cv.current_mw, data.power.cpu_cv.average_mw,
            data.power.sys_5v.current_mw, data.power.sys_5v.average_mw);
        
        /* GPU */
        ts_console_printf("\"gpu\":{\"gr3d_freq_pct\":%d}}\n", data.gpu.gr3d_freq_pct);
        
    } else {
        ts_console_printf("AGX Data:\n");
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
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: agx --start/stop                         */
/*===========================================================================*/

static int do_agx_start(void)
{
    if (!ts_agx_monitor_is_initialized()) {
        /* 初始化默认配置 */
        esp_err_t ret = ts_agx_monitor_init(NULL);
        if (ret != ESP_OK) {
            ts_console_error("Failed to initialize: %s\n", esp_err_to_name(ret));
            return 1;
        }
    }
    
    if (ts_agx_monitor_is_running()) {
        ts_console_printf("AGX monitor already running\n");
        return 0;
    }
    
    esp_err_t ret = ts_agx_monitor_start();
    if (ret != ESP_OK) {
        ts_console_error("Failed to start: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("AGX monitor started\n");
    return 0;
}

static int do_agx_stop(void)
{
    if (!ts_agx_monitor_is_running()) {
        ts_console_printf("AGX monitor not running\n");
        return 0;
    }
    
    esp_err_t ret = ts_agx_monitor_stop();
    if (ret != ESP_OK) {
        ts_console_error("Failed to stop: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("AGX monitor stopped\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: agx --config                             */
/*===========================================================================*/

static int do_agx_config(const char *server, int port, bool json)
{
    ts_agx_config_t config;
    ts_agx_monitor_get_default_config(&config);
    
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
        if (ts_agx_monitor_is_running()) {
            ts_agx_monitor_stop();
        }
        if (ts_agx_monitor_is_initialized()) {
            ts_agx_monitor_deinit();
        }
        
        esp_err_t ret = ts_agx_monitor_init(&config);
        if (ret != ESP_OK) {
            ts_console_error("Failed to reinit with new config: %s\n", esp_err_to_name(ret));
            return 1;
        }
        
        ts_console_printf("Configuration updated: %s:%d\n", config.server_ip, config.server_port);
    } else {
        /* 显示当前配置 */
        if (json) {
            ts_console_printf(
                "{\"server\":\"%s\",\"port\":%d,\"reconnect_ms\":%lu,"
                "\"startup_delay_ms\":%lu,\"heartbeat_timeout_ms\":%lu}\n",
                config.server_ip, config.server_port,
                config.reconnect_interval_ms,
                config.startup_delay_ms,
                config.heartbeat_timeout_ms);
        } else {
            ts_console_printf("AGX Monitor Configuration:\n");
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

static int agx_cmd_handler(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_agx_args);
    bool json = s_agx_args.json->count > 0;
    
    if (s_agx_args.help->count > 0) {
        ts_console_printf("Usage: agx [OPTIONS]\n");
        ts_console_printf("\nOptions:\n");
        ts_console_printf("  --status        Show AGX monitor status\n");
        ts_console_printf("  --data          Show latest AGX data\n");
        ts_console_printf("  --start         Start AGX monitoring\n");
        ts_console_printf("  --stop          Stop AGX monitoring\n");
        ts_console_printf("  --config        Show/set configuration\n");
        ts_console_printf("  --server <ip>   Set server IP\n");
        ts_console_printf("  --port <n>      Set server port\n");
        ts_console_printf("  --json,-j       Output in JSON format\n");
        ts_console_printf("  --help,-h       Show this help\n");
        ts_console_printf("\nExamples:\n");
        ts_console_printf("  agx --status              # Show connection status\n");
        ts_console_printf("  agx --data --json         # Get AGX data in JSON\n");
        ts_console_printf("  agx --start               # Start monitoring\n");
        ts_console_printf("  agx --config --server 10.10.99.98 --port 58090\n");
        return 0;
    }
    
    if (nerrors > 0) {
        arg_print_errors(stderr, s_agx_args.end, "agx");
        return 1;
    }
    
    /* agx --start */
    if (s_agx_args.start->count > 0) {
        return do_agx_start();
    }
    
    /* agx --stop */
    if (s_agx_args.stop->count > 0) {
        return do_agx_stop();
    }
    
    /* agx --data */
    if (s_agx_args.data->count > 0) {
        return do_agx_data(json);
    }
    
    /* agx --config */
    if (s_agx_args.config->count > 0 || 
        s_agx_args.server->count > 0 || 
        s_agx_args.port->count > 0) {
        const char *server = s_agx_args.server->count > 0 ? s_agx_args.server->sval[0] : NULL;
        int port = s_agx_args.port->count > 0 ? s_agx_args.port->ival[0] : 0;
        return do_agx_config(server, port, json);
    }
    
    /* Default: agx --status */
    return do_agx_status(json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_agx_register(void)
{
    s_agx_args.status = arg_lit0(NULL, "status", "Show monitor status");
    s_agx_args.data   = arg_lit0(NULL, "data", "Show latest AGX data");
    s_agx_args.start  = arg_lit0(NULL, "start", "Start AGX monitoring");
    s_agx_args.stop   = arg_lit0(NULL, "stop", "Stop AGX monitoring");
    s_agx_args.config = arg_lit0(NULL, "config", "Show/set configuration");
    s_agx_args.server = arg_str0(NULL, "server", "<ip>", "Server IP address");
    s_agx_args.port   = arg_int0(NULL, "port", "<n>", "Server port");
    s_agx_args.json   = arg_lit0("j", "json", "Output JSON format");
    s_agx_args.help   = arg_lit0("h", "help", "Show help");
    s_agx_args.end    = arg_end(5);
    
    const esp_console_cmd_t cmd = {
        .command = "agx",
        .help = "AGX device monitoring",
        .hint = NULL,
        .func = agx_cmd_handler,
        .argtable = &s_agx_args,
    };
    
    return esp_console_cmd_register(&cmd);
}
