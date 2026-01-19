/**
 * @file ts_cmd_storage.c
 * @brief Storage Console Commands
 * 
 * 实现 storage 命令族：
 * - storage --status      显示存储状态
 * - storage --mount       挂载 SD 卡
 * - storage --unmount     卸载 SD 卡
 * - storage --list        列出文件
 * - storage --read        读取文件
 * - storage --space       显示磁盘空间
 * 
 * @author TianShanOS Team
 * @version 2.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_storage.h"
#include "ts_api.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#define TAG "cmd_storage"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *status;
    struct arg_lit *mount;
    struct arg_lit *unmount;
    struct arg_lit *list;
    struct arg_lit *read;
    struct arg_lit *space;
    struct arg_lit *format;
    struct arg_str *path;
    struct arg_str *file;
    struct arg_lit *recursive;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_storage_args;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static void format_size(size_t bytes, char *buf, size_t len)
{
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(buf, len, "%.1f GB", (float)bytes / (1024 * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buf, len, "%.1f MB", (float)bytes / (1024 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buf, len, "%.1f KB", (float)bytes / 1024);
    } else {
        snprintf(buf, len, "%zu B", bytes);
    }
}

/*===========================================================================*/
/*                          Command: storage --status                         */
/*===========================================================================*/

static int do_storage_status(bool json)
{
    /* JSON 模式使用 API */
    if (json) {
        ts_api_result_t result;
        esp_err_t ret = ts_api_call("storage.status", NULL, &result);
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
    bool spiffs_mounted = ts_storage_spiffs_mounted();
    bool sd_mounted = ts_storage_sd_mounted();
    
    ts_console_printf("Storage Status:\n\n");
    ts_console_printf("  SPIFFS:  %s%s\033[0m\n", 
        spiffs_mounted ? "\033[32m" : "\033[33m",
        spiffs_mounted ? "Mounted (/spiffs)" : "Not mounted");
    ts_console_printf("  SD Card: %s%s\033[0m\n",
        sd_mounted ? "\033[32m" : "\033[33m",
        sd_mounted ? "Mounted (/sdcard)" : "Not mounted");
    ts_console_printf("\n");
    
    return 0;
}

/*===========================================================================*/
/*                          Command: storage --mount                          */
/*===========================================================================*/

static int do_storage_mount(void)
{
    if (ts_storage_sd_mounted()) {
        ts_console_warn("SD card is already mounted\n");
        return 0;
    }
    
    ts_console_printf("Mounting SD card...\n");
    ts_console_printf("(This may take a few seconds if no card is inserted)\n");
    
    esp_err_t ret = ts_storage_mount_sd(NULL);
    if (ret != ESP_OK) {
        ts_console_error("Failed to mount SD card: %s\n", esp_err_to_name(ret));
        if (ret == ESP_ERR_TIMEOUT) {
            ts_console_printf("Tip: Make sure SD card is properly inserted\n");
        }
        return 1;
    }
    
    ts_console_success("SD card mounted at /sdcard\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: storage --unmount                        */
/*===========================================================================*/

static int do_storage_unmount(void)
{
    if (!ts_storage_sd_mounted()) {
        ts_console_warn("SD card is not mounted\n");
        return 0;
    }
    
    ts_console_printf("Unmounting SD card...\n");
    
    esp_err_t ret = ts_storage_unmount_sd();
    if (ret != ESP_OK) {
        ts_console_error("Failed to unmount SD card: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("SD card unmounted\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: storage --list                           */
/*===========================================================================*/

static int list_directory(const char *path, bool recursive, int depth, bool json, bool *first)
{
    DIR *dir = opendir(path);
    if (!dir) {
        if (!json) {
            ts_console_error("Cannot open directory: %s\n", path);
        }
        return 1;
    }
    
    struct dirent *entry;
    struct stat st;
    char fullpath[512];
    char size_str[16];
    
    while ((entry = readdir(dir)) != NULL) {
        // 跳过 . 和 ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(fullpath, sizeof(fullpath), "%.255s/%.255s", path, entry->d_name);
        
        if (stat(fullpath, &st) != 0) {
            continue;
        }
        
        bool is_dir = S_ISDIR(st.st_mode);
        
        if (json) {
            if (!*first) ts_console_printf(",");
            ts_console_printf("{\"name\":\"%s\",\"type\":\"%s\"",
                entry->d_name, is_dir ? "dir" : "file");
            if (!is_dir) {
                ts_console_printf(",\"size\":%ld", (long)st.st_size);
            }
            ts_console_printf("}");
            *first = false;
        } else {
            // 缩进
            for (int i = 0; i < depth; i++) {
                ts_console_printf("  ");
            }
            
            if (is_dir) {
                ts_console_printf("\033[34m%s/\033[0m\n", entry->d_name);
            } else {
                format_size(st.st_size, size_str, sizeof(size_str));
                ts_console_printf("%-30s  %10s\n", entry->d_name, size_str);
            }
        }
        
        // 递归列出子目录
        if (recursive && is_dir) {
            list_directory(fullpath, recursive, depth + 1, json, first);
        }
    }
    
    closedir(dir);
    return 0;
}

static int do_storage_list(const char *path, bool recursive, bool json)
{
    const char *dir_path = path ? path : "/sdcard";
    
    /* JSON 模式使用 API */
    if (json) {
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "path", dir_path);
        cJSON_AddBoolToObject(params, "recursive", recursive);
        
        ts_api_result_t result;
        esp_err_t ret = ts_api_call("storage.list", params, &result);
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
    ts_console_printf("Contents of %s:\n\n", dir_path);
    ts_console_printf("%-30s  %10s\n", "NAME", "SIZE");
    ts_console_printf("────────────────────────────────────────────\n");
    bool first = true;
    list_directory(dir_path, recursive, 0, false, &first);
    ts_console_printf("\n");
    
    return 0;
}

/*===========================================================================*/
/*                          Command: storage --read                           */
/*===========================================================================*/

static int do_storage_read(const char *filepath)
{
    FILE *f = fopen(filepath, "r");
    if (!f) {
        ts_console_error("Cannot open file: %s\n", filepath);
        return 1;
    }
    
    char line[256];
    int line_num = 1;
    
    ts_console_printf("--- %s ---\n", filepath);
    while (fgets(line, sizeof(line), f) != NULL) {
        ts_console_printf("%4d: %s", line_num++, line);
        if (line_num > 100) {
            ts_console_printf("... (truncated, file too large)\n");
            break;
        }
    }
    ts_console_printf("--- end ---\n");
    
    fclose(f);
    return 0;
}

/*===========================================================================*/
/*                          Command: storage --space                          */
/*===========================================================================*/

static int do_storage_space(bool json)
{
    ts_storage_stats_t spiffs_stats = {0};
    ts_storage_stats_t sd_stats = {0};
    
    ts_storage_spiffs_stats(&spiffs_stats);
    ts_storage_sd_stats(&sd_stats);
    
    if (json) {
        ts_console_printf(
            "{\"spiffs\":{\"total\":%zu,\"used\":%zu,\"free\":%zu},"
            "\"sd\":{\"total\":%zu,\"used\":%zu,\"free\":%zu}}\n",
            spiffs_stats.total_bytes, spiffs_stats.used_bytes, 
            spiffs_stats.total_bytes - spiffs_stats.used_bytes,
            sd_stats.total_bytes, sd_stats.used_bytes,
            sd_stats.total_bytes - sd_stats.used_bytes);
    } else {
        char total_str[16], used_str[16], free_str[16];
        
        ts_console_printf("Disk Space:\n\n");
        ts_console_printf("%-10s  %12s  %12s  %12s  %8s\n",
            "MOUNT", "TOTAL", "USED", "FREE", "USED%");
        ts_console_printf("──────────────────────────────────────────────────────────\n");
        
        if (ts_storage_spiffs_mounted()) {
            format_size(spiffs_stats.total_bytes, total_str, sizeof(total_str));
            format_size(spiffs_stats.used_bytes, used_str, sizeof(used_str));
            format_size(spiffs_stats.total_bytes - spiffs_stats.used_bytes, free_str, sizeof(free_str));
            int pct = spiffs_stats.total_bytes > 0 ? 
                      (spiffs_stats.used_bytes * 100 / spiffs_stats.total_bytes) : 0;
            ts_console_printf("%-10s  %12s  %12s  %12s  %7d%%\n",
                "/spiffs", total_str, used_str, free_str, pct);
        }
        
        if (ts_storage_sd_mounted()) {
            format_size(sd_stats.total_bytes, total_str, sizeof(total_str));
            format_size(sd_stats.used_bytes, used_str, sizeof(used_str));
            format_size(sd_stats.total_bytes - sd_stats.used_bytes, free_str, sizeof(free_str));
            int pct = sd_stats.total_bytes > 0 ? 
                      (sd_stats.used_bytes * 100 / sd_stats.total_bytes) : 0;
            ts_console_printf("%-10s  %12s  %12s  %12s  %7d%%\n",
                "/sdcard", total_str, used_str, free_str, pct);
        }
        
        ts_console_printf("\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_storage(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_storage_args);
    
    if (s_storage_args.help->count > 0) {
        ts_console_printf("Usage: storage [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  -s, --status        Show storage status\n");
        ts_console_printf("      --mount         Mount SD card\n");
        ts_console_printf("      --unmount       Unmount SD card\n");
        ts_console_printf("  -l, --list          List directory contents\n");
        ts_console_printf("  -r, --read          Read file contents\n");
        ts_console_printf("      --space         Show disk space\n");
        ts_console_printf("  -p, --path <path>   Directory path\n");
        ts_console_printf("  -f, --file <file>   File path\n");
        ts_console_printf("      --recursive     Recursive listing\n");
        ts_console_printf("  -j, --json          JSON output\n");
        ts_console_printf("  -h, --help          Show this help\n\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  storage --status\n");
        ts_console_printf("  storage --mount\n");
        ts_console_printf("  storage --list --path /sdcard\n");
        ts_console_printf("  storage --read --file /sdcard/config.json\n");
        ts_console_printf("  storage --space\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, s_storage_args.end, argv[0]);
        return 1;
    }
    
    bool json = s_storage_args.json->count > 0;
    bool recursive = s_storage_args.recursive->count > 0;
    const char *path = s_storage_args.path->count > 0 ? 
                       s_storage_args.path->sval[0] : NULL;
    const char *file = s_storage_args.file->count > 0 ? 
                       s_storage_args.file->sval[0] : NULL;
    
    if (s_storage_args.mount->count > 0) {
        return do_storage_mount();
    }
    
    if (s_storage_args.unmount->count > 0) {
        return do_storage_unmount();
    }
    
    if (s_storage_args.list->count > 0) {
        return do_storage_list(path, recursive, json);
    }
    
    if (s_storage_args.read->count > 0) {
        if (!file) {
            ts_console_error("--file required for --read\n");
            return 1;
        }
        return do_storage_read(file);
    }
    
    if (s_storage_args.space->count > 0) {
        return do_storage_space(json);
    }
    
    // 默认显示状态
    return do_storage_status(json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_storage_register(void)
{
    s_storage_args.status    = arg_lit0("s", "status", "Show status");
    s_storage_args.mount     = arg_lit0(NULL, "mount", "Mount SD");
    s_storage_args.unmount   = arg_lit0(NULL, "unmount", "Unmount SD");
    s_storage_args.list      = arg_lit0("l", "list", "List files");
    s_storage_args.read      = arg_lit0("r", "read", "Read file");
    s_storage_args.space     = arg_lit0(NULL, "space", "Show space");
    s_storage_args.format    = arg_lit0(NULL, "format", "Format SD");
    s_storage_args.path      = arg_str0("p", "path", "<path>", "Directory");
    s_storage_args.file      = arg_str0("f", "file", "<file>", "File path");
    s_storage_args.recursive = arg_lit0(NULL, "recursive", "Recursive");
    s_storage_args.json      = arg_lit0("j", "json", "JSON output");
    s_storage_args.help      = arg_lit0("h", "help", "Show help");
    s_storage_args.end       = arg_end(12);
    
    const ts_console_cmd_t cmd = {
        .command = "storage",
        .help = "Storage management (SD card, SPIFFS)",
        .hint = NULL,
        .category = TS_CMD_CAT_SYSTEM,
        .func = cmd_storage,
        .argtable = &s_storage_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Storage commands registered");
    }
    
    return ret;
}
