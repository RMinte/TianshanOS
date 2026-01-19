/**
 * @file ts_cmd_temp.c
 * @brief Temperature Source Console Commands
 * 
 * 实现 temp 命令族：
 * - temp --status           显示当前温度和温度源
 * - temp --set --value V    手动设置温度（调试用）
 * - temp --mode --value M   设置温度源模式（auto/manual）
 * - temp --providers        列出所有温度提供者
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_console.h"
#include "ts_temp_source.h"
#include "ts_log.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "cmd_temp"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *status;
    struct arg_lit *set;
    struct arg_lit *mode;
    struct arg_lit *providers;
    struct arg_dbl *value;       // 温度值 (°C)
    struct arg_str *mode_val;    // auto/manual
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_temp_args;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static const char *source_to_str(ts_temp_source_type_t source)
{
    switch (source) {
        case TS_TEMP_SOURCE_DEFAULT:     return "default";
        case TS_TEMP_SOURCE_SENSOR_LOCAL: return "sensor_local";
        case TS_TEMP_SOURCE_AGX_AUTO:    return "agx_auto";
        case TS_TEMP_SOURCE_MANUAL:      return "manual";
        default:                         return "unknown";
    }
}

/*===========================================================================*/
/*                          Command: temp --status                            */
/*===========================================================================*/

static int do_temp_status(bool json)
{
    ts_temp_data_t data;
    int16_t temp = ts_temp_get_effective(&data);
    
    /* ts_temp_get_effective 返回温度值，检查是否初始化 */
    if (!ts_temp_source_is_initialized()) {
        ts_console_error("Temperature source not initialized\n");
        return 1;
    }
    
    bool manual_mode = ts_temp_is_manual_mode();
    
    if (json) {
        ts_console_printf(
            "{\"temp\":%.1f,\"source\":\"%s\",\"manual_mode\":%s,"
            "\"valid\":%s,\"age_ms\":%lu}\n",
            data.value / 10.0f,
            source_to_str(data.source),
            manual_mode ? "true" : "false",
            data.valid ? "true" : "false",
            data.timestamp_ms);
    } else {
        ts_console_printf("Temperature Status:\n");
        ts_console_printf("  Current:     %.1f°C\n", data.value / 10.0f);
        ts_console_printf("  Source:      %s\n", source_to_str(data.source));
        ts_console_printf("  Mode:        %s\n", manual_mode ? "manual" : "auto");
        ts_console_printf("  Valid:       %s\n", data.valid ? "Yes" : "No");
        
        if (data.valid && data.timestamp_ms > 0) {
            ts_console_printf("  Updated:     %lu ms ago\n", data.timestamp_ms);
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: temp --providers                         */
/*===========================================================================*/

static int do_temp_providers(bool json)
{
    ts_temp_source_type_t sources[] = {
        TS_TEMP_SOURCE_DEFAULT,
        TS_TEMP_SOURCE_SENSOR_LOCAL,
        TS_TEMP_SOURCE_AGX_AUTO,
        TS_TEMP_SOURCE_MANUAL
    };
    
    int count = sizeof(sources) / sizeof(sources[0]);
    
    if (json) {
        ts_console_printf("[");
        bool first = true;
        
        for (int i = 0; i < count; i++) {
            ts_temp_data_t data;
            if (ts_temp_get_by_source(sources[i], &data) == ESP_OK) {
                if (!first) ts_console_printf(",");
                ts_console_printf(
                    "{\"source\":\"%s\",\"priority\":%d,\"temp\":%.1f,\"valid\":%s}",
                    source_to_str(sources[i]),
                    sources[i],
                    data.value / 10.0f,
                    data.valid ? "true" : "false");
                first = false;
            }
        }
        ts_console_printf("]\n");
    } else {
        ts_console_printf("Temperature Providers:\n");
        ts_console_printf("  %-15s %-10s %-12s %s\n", "Source", "Priority", "Temperature", "Valid");
        ts_console_printf("  %-15s %-10s %-12s %s\n", "------", "--------", "-----------", "-----");
        
        for (int i = 0; i < count; i++) {
            ts_temp_data_t data;
            if (ts_temp_get_by_source(sources[i], &data) == ESP_OK) {
                ts_console_printf("  %-15s %-10d %-12.1f %s\n",
                    source_to_str(sources[i]),
                    sources[i],
                    data.value / 10.0f,
                    data.valid ? "Yes" : "No");
            }
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: temp --set                               */
/*===========================================================================*/

static int do_temp_set(double temp_c)
{
    int16_t temp_value = (int16_t)(temp_c * 10);
    
    esp_err_t ret = ts_temp_set_manual(temp_value);
    if (ret != ESP_OK) {
        ts_console_error("Failed to set temperature: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("Temperature set to %.1f°C (manual mode enabled)\n", temp_c);
    return 0;
}

/*===========================================================================*/
/*                          Command: temp --mode                              */
/*===========================================================================*/

static int do_temp_mode(const char *mode_str)
{
    bool manual = false;
    
    if (strcmp(mode_str, "manual") == 0) {
        manual = true;
    } else if (strcmp(mode_str, "auto") == 0) {
        manual = false;
    } else {
        ts_console_error("Invalid mode: %s (use 'auto' or 'manual')\n", mode_str);
        return 1;
    }
    
    esp_err_t ret = ts_temp_set_manual_mode(manual);
    if (ret != ESP_OK) {
        ts_console_error("Failed to set mode: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_printf("Temperature mode set to %s\n", mode_str);
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int temp_cmd_handler(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_temp_args);
    bool json = s_temp_args.json->count > 0;
    
    if (s_temp_args.help->count > 0) {
        ts_console_printf("Usage: temp [OPTIONS]\n");
        ts_console_printf("\nOptions:\n");
        ts_console_printf("  --status        Show current temperature and source\n");
        ts_console_printf("  --set           Set manual temperature\n");
        ts_console_printf("  --mode          Set temperature mode (auto/manual)\n");
        ts_console_printf("  --providers     List all temperature providers\n");
        ts_console_printf("  --value,-V <n>  Temperature value in °C\n");
        ts_console_printf("  --json,-j       Output in JSON format\n");
        ts_console_printf("  --help,-h       Show this help\n");
        ts_console_printf("\nExamples:\n");
        ts_console_printf("  temp --status             # Show current temperature\n");
        ts_console_printf("  temp --set -V 45.5        # Set to 45.5°C (manual mode)\n");
        ts_console_printf("  temp --mode --value auto  # Switch to auto mode\n");
        ts_console_printf("  temp --providers --json   # List providers in JSON\n");
        return 0;
    }
    
    if (nerrors > 0) {
        arg_print_errors(stderr, s_temp_args.end, "temp");
        return 1;
    }
    
    /* temp --providers */
    if (s_temp_args.providers->count > 0) {
        return do_temp_providers(json);
    }
    
    /* temp --set */
    if (s_temp_args.set->count > 0) {
        if (s_temp_args.value->count == 0) {
            ts_console_error("--set requires --value <temperature>\n");
            return 1;
        }
        return do_temp_set(s_temp_args.value->dval[0]);
    }
    
    /* temp --mode */
    if (s_temp_args.mode->count > 0) {
        if (s_temp_args.mode_val->count == 0) {
            ts_console_error("--mode requires --value <auto|manual>\n");
            return 1;
        }
        return do_temp_mode(s_temp_args.mode_val->sval[0]);
    }
    
    /* Default: temp --status */
    return do_temp_status(json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_temp_register(void)
{
    s_temp_args.status    = arg_lit0(NULL, "status", "Show temperature status");
    s_temp_args.set       = arg_lit0(NULL, "set", "Set manual temperature");
    s_temp_args.mode      = arg_lit0(NULL, "mode", "Set temperature mode");
    s_temp_args.providers = arg_lit0(NULL, "providers", "List all providers");
    s_temp_args.value     = arg_dbl0("V", "value", "<°C>", "Temperature value");
    s_temp_args.mode_val  = arg_str0(NULL, "value", "<mode>", "auto or manual");
    s_temp_args.json      = arg_lit0("j", "json", "Output JSON format");
    s_temp_args.help      = arg_lit0("h", "help", "Show help");
    s_temp_args.end       = arg_end(5);
    
    const esp_console_cmd_t cmd = {
        .command = "temp",
        .help = "Temperature source management",
        .hint = NULL,
        .func = temp_cmd_handler,
        .argtable = &s_temp_args,
    };
    
    return esp_console_cmd_register(&cmd);
}
