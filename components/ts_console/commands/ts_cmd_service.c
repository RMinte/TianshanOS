/**
 * @file ts_cmd_service.c
 * @brief Service Management Console Commands
 * 
 * 实现 service 命令族：
 * - service --list          列出所有服务
 * - service --status -n X   显示服务状态
 * - service --start -n X    启动服务
 * - service --stop -n X     停止服务
 * - service --restart -n X  重启服务
 * 
 * @author TianShanOS Team
 * @version 2.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_service.h"
#include "ts_api.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>

#define TAG "cmd_service"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *list;
    struct arg_lit *status;
    struct arg_lit *start;
    struct arg_lit *stop;
    struct arg_lit *restart;
    struct arg_lit *deps;
    struct arg_str *name;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_service_args;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 获取服务状态名称
 */
static const char *state_to_str(ts_service_state_t state)
{
    switch (state) {
        case TS_SERVICE_STATE_UNREGISTERED: return "UNREGISTERED";
        case TS_SERVICE_STATE_REGISTERED:   return "REGISTERED";
        case TS_SERVICE_STATE_STARTING:     return "STARTING";
        case TS_SERVICE_STATE_RUNNING:      return "RUNNING";
        case TS_SERVICE_STATE_STOPPING:     return "STOPPING";
        case TS_SERVICE_STATE_STOPPED:      return "STOPPED";
        case TS_SERVICE_STATE_ERROR:        return "ERROR";
        default:                            return "UNKNOWN";
    }
}

/**
 * @brief 获取阶段名称
 */
static const char *phase_to_str(ts_service_phase_t phase)
{
    switch (phase) {
        case TS_SERVICE_PHASE_PLATFORM: return "PLATFORM";
        case TS_SERVICE_PHASE_CORE:     return "CORE";
        case TS_SERVICE_PHASE_HAL:      return "HAL";
        case TS_SERVICE_PHASE_DRIVER:   return "DRIVER";
        case TS_SERVICE_PHASE_NETWORK:  return "NETWORK";
        case TS_SERVICE_PHASE_SECURITY: return "SECURITY";
        case TS_SERVICE_PHASE_SERVICE:  return "SERVICE";
        case TS_SERVICE_PHASE_UI:       return "UI";
        default:                        return "UNKNOWN";
    }
}

/**
 * @brief 获取状态颜色代码
 */
static const char *state_color(ts_service_state_t state)
{
    switch (state) {
        case TS_SERVICE_STATE_RUNNING:  return "\033[32m";  // 绿色
        case TS_SERVICE_STATE_STOPPED:  return "\033[33m";  // 黄色
        case TS_SERVICE_STATE_ERROR:    return "\033[31m";  // 红色
        case TS_SERVICE_STATE_STARTING:
        case TS_SERVICE_STATE_STOPPING: return "\033[36m";  // 青色
        default:                        return "\033[0m";   // 默认
    }
}

/*===========================================================================*/
/*                          Command: service --list                           */
/*===========================================================================*/

// 枚举回调上下文
typedef struct {
    bool json;
    bool first;
} service_list_ctx_t;

// 枚举回调函数
static bool service_list_callback(ts_service_handle_t handle, 
                                   const ts_service_info_t *info, 
                                   void *user_data)
{
    service_list_ctx_t *ctx = (service_list_ctx_t *)user_data;
    
    if (ctx->json) {
        if (!ctx->first) ts_console_printf(",");
        ts_console_printf("{\"name\":\"%s\",\"state\":\"%s\",\"phase\":\"%s\",\"healthy\":%s}",
            info->name,
            state_to_str(info->state),
            phase_to_str(info->phase),
            info->healthy ? "true" : "false");
        ctx->first = false;
    } else {
        ts_console_printf("%s%-20s  %-10s\033[0m  %-10s  %s\n",
            state_color(info->state),
            info->name,
            state_to_str(info->state),
            phase_to_str(info->phase),
            info->healthy ? "✓" : "✗");
    }
    
    return true;  // 继续枚举
}

static int do_service_list(bool json_output)
{
    /* JSON 模式使用 API */
    if (json_output) {
        ts_api_result_t result;
        esp_err_t ret = ts_api_call("service.list", NULL, &result);
        if (ret == ESP_OK && result.code == TS_API_OK && result.data) {
            char *json_str = cJSON_PrintUnformatted(result.data);
            if (json_str) {
                ts_console_printf("%s\n", json_str);
                free(json_str);
            }
        } else {
            ts_console_error("API call failed: %s\n", 
                result.message ? result.message : esp_err_to_name(ret));
        }
        ts_api_result_free(&result);
        return (ret == ESP_OK) ? 0 : 1;
    }
    
    /* 格式化输出模式 */
    ts_service_stats_t stats;
    esp_err_t ret = ts_service_get_stats(&stats);
    if (ret != ESP_OK) {
        ts_console_error("Failed to get service stats\n");
        return 1;
    }
    
    service_list_ctx_t ctx = {
        .json = false,
        .first = true
    };
    
    ts_console_printf("Services (%lu total, %lu running):\n\n",
        (unsigned long)stats.total_services, 
        (unsigned long)stats.running_services);
    ts_console_printf("%-20s  %-10s  %-10s  %s\n",
        "NAME", "STATE", "PHASE", "HEALTHY");
    ts_console_printf("────────────────────────────────────────────────────────\n");
    
    // 使用服务枚举 API
    ts_service_enumerate(service_list_callback, &ctx);
    
    ts_console_printf("\n");
    
    return 0;
}

/*===========================================================================*/
/*                          Command: service --status                         */
/*===========================================================================*/

static int do_service_status(const char *name, bool json_output)
{
    /* JSON 模式使用 API */
    if (json_output) {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "name", name);
        
        ts_api_result_t result;
        esp_err_t ret = ts_api_call("service.status", params, &result);
        cJSON_Delete(params);
        
        if (ret == ESP_OK && result.code == TS_API_OK && result.data) {
            char *json_str = cJSON_PrintUnformatted(result.data);
            if (json_str) {
                ts_console_printf("%s\n", json_str);
                free(json_str);
            }
        } else {
            ts_console_error("API call failed: %s\n", 
                result.message ? result.message : esp_err_to_name(ret));
        }
        ts_api_result_free(&result);
        return (ret == ESP_OK) ? 0 : 1;
    }
    
    /* 格式化输出模式 */
    ts_service_handle_t handle = ts_service_find(name);
    if (!handle) {
        ts_console_error("Service '%s' not found\n", name);
        return 1;
    }
    
    ts_service_info_t info;
    esp_err_t ret = ts_service_get_info(handle, &info);
    if (ret != ESP_OK) {
        ts_console_error("Failed to get service info\n");
        return 1;
    }
    
    ts_console_printf("Service: %s\n", info.name);
    ts_console_printf("  State:    %s%s\033[0m\n", 
        state_color(info.state), state_to_str(info.state));
    ts_console_printf("  Phase:    %s\n", phase_to_str(info.phase));
    ts_console_printf("  Healthy:  %s\n", info.healthy ? "Yes" : "No");
    if (info.start_duration_ms > 0) {
        ts_console_printf("  Started:  %lu ms ago\n", 
            (unsigned long)info.start_time_ms);
        ts_console_printf("  Duration: %lu ms\n", 
            (unsigned long)info.start_duration_ms);
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: service --start                          */
/*===========================================================================*/

static int do_service_start(const char *name)
{
    ts_service_handle_t handle = ts_service_find(name);
    if (!handle) {
        ts_console_error("Service '%s' not found\n", name);
        return 1;
    }
    
    ts_service_state_t state = ts_service_get_state(handle);
    if (state == TS_SERVICE_STATE_RUNNING) {
        ts_console_warn("Service '%s' is already running\n", name);
        return 0;
    }
    
    ts_console_printf("Starting service '%s'...\n", name);
    esp_err_t ret = ts_service_start(handle);
    if (ret != ESP_OK) {
        ts_console_error("Failed to start service: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("Service '%s' started\n", name);
    return 0;
}

/*===========================================================================*/
/*                          Command: service --stop                           */
/*===========================================================================*/

static int do_service_stop(const char *name)
{
    ts_service_handle_t handle = ts_service_find(name);
    if (!handle) {
        ts_console_error("Service '%s' not found\n", name);
        return 1;
    }
    
    ts_service_state_t state = ts_service_get_state(handle);
    if (state == TS_SERVICE_STATE_STOPPED) {
        ts_console_warn("Service '%s' is already stopped\n", name);
        return 0;
    }
    
    ts_console_printf("Stopping service '%s'...\n", name);
    esp_err_t ret = ts_service_stop(handle);
    if (ret != ESP_OK) {
        ts_console_error("Failed to stop service: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("Service '%s' stopped\n", name);
    return 0;
}

/*===========================================================================*/
/*                          Command: service --restart                        */
/*===========================================================================*/

static int do_service_restart(const char *name)
{
    ts_service_handle_t handle = ts_service_find(name);
    if (!handle) {
        ts_console_error("Service '%s' not found\n", name);
        return 1;
    }
    
    ts_console_printf("Restarting service '%s'...\n", name);
    esp_err_t ret = ts_service_restart(handle);
    if (ret != ESP_OK) {
        ts_console_error("Failed to restart service: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("Service '%s' restarted\n", name);
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_service(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_service_args);
    
    // 显示帮助
    if (s_service_args.help->count > 0) {
        ts_console_printf("Usage: service [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  -l, --list          List all services\n");
        ts_console_printf("  -s, --status        Show service status\n");
        ts_console_printf("      --start         Start a service\n");
        ts_console_printf("      --stop          Stop a service\n");
        ts_console_printf("      --restart       Restart a service\n");
        ts_console_printf("  -n, --name <name>   Service name\n");
        ts_console_printf("  -j, --json          Output in JSON format\n");
        ts_console_printf("  -h, --help          Show this help\n\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  service --list\n");
        ts_console_printf("  service --status --name storage\n");
        ts_console_printf("  service --start --name console\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, s_service_args.end, argv[0]);
        return 1;
    }
    
    bool json = s_service_args.json->count > 0;
    const char *name = s_service_args.name->count > 0 ? 
                       s_service_args.name->sval[0] : NULL;
    
    // 默认为 --list
    if (s_service_args.list->count > 0 || 
        (s_service_args.status->count == 0 && 
         s_service_args.start->count == 0 &&
         s_service_args.stop->count == 0 &&
         s_service_args.restart->count == 0)) {
        return do_service_list(json);
    }
    
    // 需要服务名称的操作
    if (!name) {
        ts_console_error("Service name required. Use --name <name>\n");
        return 1;
    }
    
    if (s_service_args.status->count > 0) {
        return do_service_status(name, json);
    }
    
    if (s_service_args.start->count > 0) {
        return do_service_start(name);
    }
    
    if (s_service_args.stop->count > 0) {
        return do_service_stop(name);
    }
    
    if (s_service_args.restart->count > 0) {
        return do_service_restart(name);
    }
    
    return do_service_list(json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_service_register(void)
{
    // 初始化参数表
    s_service_args.list    = arg_lit0("l", "list", "List all services");
    s_service_args.status  = arg_lit0("s", "status", "Show service status");
    s_service_args.start   = arg_lit0(NULL, "start", "Start a service");
    s_service_args.stop    = arg_lit0(NULL, "stop", "Stop a service");
    s_service_args.restart = arg_lit0(NULL, "restart", "Restart a service");
    s_service_args.deps    = arg_lit0(NULL, "deps", "Show dependencies");
    s_service_args.name    = arg_str0("n", "name", "<name>", "Service name");
    s_service_args.json    = arg_lit0("j", "json", "JSON output");
    s_service_args.help    = arg_lit0("h", "help", "Show help");
    s_service_args.end     = arg_end(10);
    
    const ts_console_cmd_t cmd = {
        .command = "service",
        .help = "Service management (list, start, stop, restart)",
        .hint = NULL,
        .category = TS_CMD_CAT_SYSTEM,
        .func = cmd_service,
        .argtable = &s_service_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Service commands registered");
    }
    
    return ret;
}
