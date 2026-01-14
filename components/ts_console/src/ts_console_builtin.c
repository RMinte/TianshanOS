/**
 * @file ts_console_builtin.c
 * @brief Built-in Console Commands
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_console_args.h"
#include "ts_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG "console_builtin"

/*===========================================================================*/
/*                          Command: help                                     */
/*===========================================================================*/

static int cmd_help(int argc, char **argv)
{
    ts_console_printf("\nTianShanOS Console Commands\n");
    ts_console_printf("============================\n\n");
    
    /* Print commands by category */
    for (int cat = 0; cat < TS_CMD_CAT_MAX; cat++) {
        const char *cmds[32];
        size_t count = ts_console_get_cmds_by_category(cat, cmds, 32);
        
        if (count > 0) {
            ts_console_printf("[%s]\n", ts_console_category_name(cat));
            for (size_t i = 0; i < count && i < 32; i++) {
                ts_console_printf("  %s\n", cmds[i]);
            }
            ts_console_printf("\n");
        }
    }
    
    ts_console_printf("Use '<command> --help' for command details\n\n");
    
    return 0;
}

/*===========================================================================*/
/*                          Command: version                                  */
/*===========================================================================*/

static int cmd_version(int argc, char **argv)
{
    ts_console_printf("\nTianShanOS\n");
    ts_console_printf("Version: %d.%d.%d\n", 
                      TS_CONSOLE_VERSION_MAJOR,
                      TS_CONSOLE_VERSION_MINOR, 
                      TS_CONSOLE_VERSION_PATCH);
    ts_console_printf("Build: %s %s\n", __DATE__, __TIME__);
    ts_console_printf("IDF: %s\n\n", esp_get_idf_version());
    
    return 0;
}

/*===========================================================================*/
/*                          Command: sysinfo                                  */
/*===========================================================================*/

static int cmd_sysinfo(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    ts_console_printf("\nSystem Information\n");
    ts_console_printf("==================\n\n");
    
    /* Chip info */
    ts_console_printf("Chip Model:    ");
    switch (chip_info.model) {
        case CHIP_ESP32:   ts_console_printf("ESP32\n"); break;
        case CHIP_ESP32S2: ts_console_printf("ESP32-S2\n"); break;
        case CHIP_ESP32S3: ts_console_printf("ESP32-S3\n"); break;
        case CHIP_ESP32C3: ts_console_printf("ESP32-C3\n"); break;
        case CHIP_ESP32C2: ts_console_printf("ESP32-C2\n"); break;
        case CHIP_ESP32C6: ts_console_printf("ESP32-C6\n"); break;
        case CHIP_ESP32H2: ts_console_printf("ESP32-H2\n"); break;
        default:          ts_console_printf("Unknown\n"); break;
    }
    
    ts_console_printf("Cores:         %d\n", chip_info.cores);
    ts_console_printf("Revision:      %d.%d\n", 
                      chip_info.revision / 100, 
                      chip_info.revision % 100);
    
    /* Features */
    ts_console_printf("Features:     ");
    if (chip_info.features & CHIP_FEATURE_WIFI_BGN) ts_console_printf(" WiFi");
    if (chip_info.features & CHIP_FEATURE_BT) ts_console_printf(" BT");
    if (chip_info.features & CHIP_FEATURE_BLE) ts_console_printf(" BLE");
    if (chip_info.features & CHIP_FEATURE_EMB_FLASH) ts_console_printf(" Flash");
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM) ts_console_printf(" PSRAM");
    ts_console_printf("\n");
    
    /* Flash */
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ts_console_printf("Flash Size:    %lu MB\n", flash_size / (1024 * 1024));
    }
    
    /* Memory */
    ts_console_printf("\nMemory Usage\n");
    ts_console_printf("------------\n");
    ts_console_printf("Free heap:     %lu bytes\n", esp_get_free_heap_size());
    ts_console_printf("Min free heap: %lu bytes\n", esp_get_minimum_free_heap_size());
    
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);
    ts_console_printf("Internal free: %u bytes\n", heap_info.total_free_bytes);
    
    heap_caps_get_info(&heap_info, MALLOC_CAP_SPIRAM);
    if (heap_info.total_free_bytes > 0) {
        ts_console_printf("PSRAM free:    %u bytes\n", heap_info.total_free_bytes);
    }
    
    /* Uptime */
    int64_t uptime = esp_timer_get_time() / 1000000;
    int hours = uptime / 3600;
    int mins = (uptime % 3600) / 60;
    int secs = uptime % 60;
    ts_console_printf("\nUptime:        %02d:%02d:%02d\n\n", hours, mins, secs);
    
    return 0;
}

/*===========================================================================*/
/*                          Command: tasks                                    */
/*===========================================================================*/

static int cmd_tasks(int argc, char **argv)
{
    ts_console_printf("\nTask List\n");
    ts_console_printf("=========\n\n");
    
#if configUSE_TRACE_FACILITY
    TaskStatus_t *task_array;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    
    task_array = malloc(task_count * sizeof(TaskStatus_t));
    if (task_array == NULL) {
        ts_console_error("Failed to allocate memory\n");
        return 1;
    }
    
    uint32_t total_runtime;
    task_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
    
    ts_console_printf("%-16s %5s %5s %5s %10s\n", 
                      "Name", "Prio", "State", "Stack", "Core");
    ts_console_printf("------------------------------------------------\n");
    
    for (int i = 0; i < task_count; i++) {
        char state;
        switch (task_array[i].eCurrentState) {
            case eRunning:   state = 'X'; break;
            case eReady:     state = 'R'; break;
            case eBlocked:   state = 'B'; break;
            case eSuspended: state = 'S'; break;
            case eDeleted:   state = 'D'; break;
            default:         state = '?'; break;
        }
        
        ts_console_printf("%-16s %5d %5c %5lu\n",
                          task_array[i].pcTaskName,
                          task_array[i].uxCurrentPriority,
                          state,
                          task_array[i].usStackHighWaterMark);
    }
    
    free(task_array);
    ts_console_printf("\nTotal tasks: %d\n\n", task_count);
#else
    ts_console_printf("Task stats not available (enable configUSE_TRACE_FACILITY)\n\n");
#endif
    
    return 0;
}

/*===========================================================================*/
/*                          Command: free                                     */
/*===========================================================================*/

static int cmd_free(int argc, char **argv)
{
    ts_console_printf("\nMemory Information\n");
    ts_console_printf("==================\n\n");
    
    multi_heap_info_t info;
    
    /* Internal memory */
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
    ts_console_printf("Internal Memory:\n");
    ts_console_printf("  Total:          %u bytes\n", 
                      info.total_free_bytes + info.total_allocated_bytes);
    ts_console_printf("  Free:           %u bytes\n", info.total_free_bytes);
    ts_console_printf("  Allocated:      %u bytes\n", info.total_allocated_bytes);
    ts_console_printf("  Largest block:  %u bytes\n", info.largest_free_block);
    
    /* PSRAM */
    heap_caps_get_info(&info, MALLOC_CAP_SPIRAM);
    if (info.total_free_bytes > 0 || info.total_allocated_bytes > 0) {
        ts_console_printf("\nPSRAM:\n");
        ts_console_printf("  Total:          %u bytes\n", 
                          info.total_free_bytes + info.total_allocated_bytes);
        ts_console_printf("  Free:           %u bytes\n", info.total_free_bytes);
        ts_console_printf("  Allocated:      %u bytes\n", info.total_allocated_bytes);
        ts_console_printf("  Largest block:  %u bytes\n", info.largest_free_block);
    }
    
    ts_console_printf("\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: reboot                                   */
/*===========================================================================*/

static int cmd_reboot(int argc, char **argv)
{
    ts_console_printf("Rebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

/*===========================================================================*/
/*                          Command: clear                                    */
/*===========================================================================*/

static int cmd_clear(int argc, char **argv)
{
    /* ANSI escape code to clear screen */
    ts_console_printf("\033[2J\033[H");
    return 0;
}

/*===========================================================================*/
/*                          Command: echo                                     */
/*===========================================================================*/

static int cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        ts_console_printf("%s%s", argv[i], (i < argc - 1) ? " " : "");
    }
    ts_console_printf("\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: log                                      */
/*===========================================================================*/

static struct {
    struct arg_str *level;
    struct arg_str *tag;
    struct arg_end *end;
} s_log_args;

static int cmd_log(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_log_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_log_args.end, argv[0]);
        return 1;
    }
    
    if (s_log_args.level->count > 0) {
        const char *level_str = s_log_args.level->sval[0];
        ts_log_level_t level;
        
        if (strcmp(level_str, "none") == 0) level = TS_LOG_NONE;
        else if (strcmp(level_str, "error") == 0) level = TS_LOG_ERROR;
        else if (strcmp(level_str, "warn") == 0) level = TS_LOG_WARN;
        else if (strcmp(level_str, "info") == 0) level = TS_LOG_INFO;
        else if (strcmp(level_str, "debug") == 0) level = TS_LOG_DEBUG;
        else if (strcmp(level_str, "verbose") == 0) level = TS_LOG_VERBOSE;
        else {
            ts_console_error("Invalid log level: %s\n", level_str);
            return 1;
        }
        
        if (s_log_args.tag->count > 0) {
            ts_log_set_tag_level(s_log_args.tag->sval[0], level);
            ts_console_success("Set log level for '%s' to %s\n", 
                               s_log_args.tag->sval[0], level_str);
        } else {
            ts_log_set_level(level);
            ts_console_success("Set global log level to %s\n", level_str);
        }
    } else {
        ts_console_printf("Current log level: %d\n", ts_log_get_level());
    }
    
    return 0;
}

/*===========================================================================*/
/*                      Register Built-in Commands                            */
/*===========================================================================*/

esp_err_t ts_console_register_builtin_cmds(void)
{
    /* Initialize log command arguments */
    s_log_args.level = arg_str0("l", "level", "<level>", "Log level (none/error/warn/info/debug/verbose)");
    s_log_args.tag = arg_str0("t", "tag", "<tag>", "Tag to set level for");
    s_log_args.end = arg_end(2);
    
    static const ts_console_cmd_t builtin_cmds[] = {
        {
            .command = "help",
            .help = "Show available commands",
            .hint = NULL,
            .category = TS_CMD_CAT_SYSTEM,
            .func = cmd_help,
            .argtable = NULL
        },
        {
            .command = "version",
            .help = "Show version information",
            .hint = NULL,
            .category = TS_CMD_CAT_SYSTEM,
            .func = cmd_version,
            .argtable = NULL
        },
        {
            .command = "sysinfo",
            .help = "Show system information",
            .hint = NULL,
            .category = TS_CMD_CAT_SYSTEM,
            .func = cmd_sysinfo,
            .argtable = NULL
        },
        {
            .command = "tasks",
            .help = "List running tasks",
            .hint = NULL,
            .category = TS_CMD_CAT_DEBUG,
            .func = cmd_tasks,
            .argtable = NULL
        },
        {
            .command = "free",
            .help = "Show memory usage",
            .hint = NULL,
            .category = TS_CMD_CAT_DEBUG,
            .func = cmd_free,
            .argtable = NULL
        },
        {
            .command = "reboot",
            .help = "Reboot the system",
            .hint = NULL,
            .category = TS_CMD_CAT_SYSTEM,
            .func = cmd_reboot,
            .argtable = NULL
        },
        {
            .command = "clear",
            .help = "Clear the screen",
            .hint = NULL,
            .category = TS_CMD_CAT_SYSTEM,
            .func = cmd_clear,
            .argtable = NULL
        },
        {
            .command = "echo",
            .help = "Echo text",
            .hint = "<text>",
            .category = TS_CMD_CAT_SYSTEM,
            .func = cmd_echo,
            .argtable = NULL
        },
        {
            .command = "log",
            .help = "Get/set log level",
            .hint = NULL,
            .category = TS_CMD_CAT_DEBUG,
            .func = cmd_log,
            .argtable = &s_log_args
        }
    };
    
    return ts_console_register_cmds(builtin_cmds, 
                                     sizeof(builtin_cmds) / sizeof(builtin_cmds[0]));
}
