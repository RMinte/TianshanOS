/**
 * @file ts_ssh_commands_config.c
 * @brief SSH Command Configuration Storage Implementation
 *
 * 实现 SSH 快捷指令配置的持久化存储（NVS）。
 * 启动时自动为有 var_name 的指令预创建变量。
 */

#include "ts_ssh_commands_config.h"
#include "ts_variable.h"
#include "ts_automation_types.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_random.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

static const char *TAG = "ts_ssh_cmd_cfg";

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

#define NVS_NAMESPACE       "ts_ssh_cmd"
#define NVS_KEY_PREFIX      "c_"

/*===========================================================================*/
/*                              Internal State                                */
/*===========================================================================*/

/** NVS 存储格式 */
typedef struct __attribute__((packed)) {
    char id[TS_SSH_CMD_ID_MAX];
    char host_id[TS_SSH_CMD_HOST_ID_MAX];
    char name[TS_SSH_CMD_NAME_MAX];
    char command[TS_SSH_CMD_COMMAND_MAX];
    char desc[TS_SSH_CMD_DESC_MAX];
    char icon[TS_SSH_CMD_ICON_MAX];
    char expect_pattern[TS_SSH_CMD_PATTERN_MAX];
    char fail_pattern[TS_SSH_CMD_PATTERN_MAX];
    char extract_pattern[TS_SSH_CMD_PATTERN_MAX];
    char var_name[TS_SSH_CMD_VARNAME_MAX];
    uint16_t timeout_sec;
    uint8_t stop_on_match;
    uint8_t enabled;
    uint32_t created_time;
    uint32_t last_exec_time;
} nvs_cmd_entry_t;

static struct {
    bool initialized;
    nvs_handle_t nvs;
    SemaphoreHandle_t mutex;
} s_state = {0};

/*===========================================================================*/
/*                          Internal Functions                                */
/*===========================================================================*/

static void make_nvs_key(int index, char *key, size_t key_size)
{
    snprintf(key, key_size, "%s%d", NVS_KEY_PREFIX, index);
}

static uint32_t get_current_time(void)
{
    time_t now;
    time(&now);
    return (uint32_t)now;
}

static void generate_id(char *id, size_t size)
{
    /* 生成简单的唯一 ID: cmd_XXXXXXXX */
    uint32_t random = esp_random();
    snprintf(id, size, "cmd_%08lx", (unsigned long)random);
}

/**
 * @brief 为指令预创建变量（7 个标准变量）
 */
static void precreate_command_variables(const char *var_name)
{
    if (!var_name || !var_name[0]) {
        return;
    }
    
    /* 检查变量系统是否已初始化 */
    /* 注意：此时可能变量系统还未初始化，延迟到服务启动后处理 */
    
    const struct {
        const char *suffix;
        ts_auto_value_type_t type;
    } var_defs[] = {
        { "status", TS_AUTO_VAL_STRING },
        { "exit_code", TS_AUTO_VAL_INT },
        { "extracted", TS_AUTO_VAL_STRING },
        { "expect_matched", TS_AUTO_VAL_BOOL },
        { "fail_matched", TS_AUTO_VAL_BOOL },
        { "host", TS_AUTO_VAL_STRING },
        { "timestamp", TS_AUTO_VAL_INT },
    };
    
    char full_name[96];
    ts_auto_variable_t var = {0};
    strncpy(var.source_id, var_name, sizeof(var.source_id) - 1);
    var.flags = 0;
    
    for (size_t i = 0; i < sizeof(var_defs) / sizeof(var_defs[0]); i++) {
        snprintf(full_name, sizeof(full_name), "%s.%s", var_name, var_defs[i].suffix);
        strncpy(var.name, full_name, sizeof(var.name) - 1);
        var.value.type = var_defs[i].type;
        
        /* 设置默认值 */
        switch (var_defs[i].type) {
            case TS_AUTO_VAL_STRING:
                var.value.str_val[0] = '\0';
                break;
            case TS_AUTO_VAL_INT:
                var.value.int_val = 0;
                break;
            case TS_AUTO_VAL_BOOL:
                var.value.bool_val = false;
                break;
            default:
                break;
        }
        var.default_value = var.value;
        
        /* 注册变量（如果已存在则忽略） */
        esp_err_t ret = ts_variable_register(&var);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Pre-created variable: %s", full_name);
        } else if (ret == ESP_ERR_INVALID_STATE) {
            /* 变量系统未初始化，稍后重试 */
            ESP_LOGD(TAG, "Variable system not ready, will retry: %s", full_name);
        }
        /* ESP_ERR_INVALID_SIZE 表示变量已存在，忽略 */
    }
}

/**
 * @brief 初始化时为所有指令预创建变量
 */
static void precreate_all_variables(void)
{
    char key[16];
    nvs_cmd_entry_t entry;
    size_t len;
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (entry.var_name[0]) {
                precreate_command_variables(entry.var_name);
            }
        }
    }
}

/*===========================================================================*/
/*                          Initialization                                    */
/*===========================================================================*/

esp_err_t ts_ssh_commands_config_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }
    
    /* 创建互斥锁 */
    s_state.mutex = xSemaphoreCreateMutex();
    if (!s_state.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    /* 打开 NVS */
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_state.nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_state.mutex);
        return ret;
    }
    
    s_state.initialized = true;
    
    /* 统计已有指令数量 */
    size_t count = ts_ssh_commands_config_count();
    ESP_LOGI(TAG, "SSH commands config initialized, %d commands loaded", (int)count);
    
    /* 预创建所有变量（延迟到变量系统初始化后） */
    /* 这里不调用，由服务启动时调用 ts_ssh_commands_precreate_variables() */
    
    return ESP_OK;
}

void ts_ssh_commands_config_deinit(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    nvs_close(s_state.nvs);
    vSemaphoreDelete(s_state.mutex);
    s_state.initialized = false;
}

bool ts_ssh_commands_config_is_initialized(void)
{
    return s_state.initialized;
}

/*===========================================================================*/
/*                          CRUD Operations                                   */
/*===========================================================================*/

esp_err_t ts_ssh_commands_config_add(const ts_ssh_command_config_t *config,
                                      char *out_id, size_t out_id_size)
{
    if (!s_state.initialized || !config || !config->host_id[0] || !config->name[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    /* 检查是否已存在（更新）或找空位（新增） */
    int existing_index = -1;
    int free_index = -1;
    char key[16];
    nvs_cmd_entry_t entry;
    size_t len;
    char new_id[TS_SSH_CMD_ID_MAX] = {0};
    
    /* 如果没有提供 ID，生成一个 */
    if (config->id[0]) {
        strncpy(new_id, config->id, sizeof(new_id) - 1);
    } else {
        generate_id(new_id, sizeof(new_id));
    }
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        esp_err_t ret = nvs_get_blob(s_state.nvs, key, &entry, &len);
        
        if (ret == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.id, new_id) == 0) {
                existing_index = i;
                break;
            }
        } else if (free_index < 0) {
            free_index = i;
        }
    }
    
    int target_index = (existing_index >= 0) ? existing_index : free_index;
    if (target_index < 0) {
        xSemaphoreGive(s_state.mutex);
        ESP_LOGW(TAG, "Max commands reached");
        return ESP_ERR_NO_MEM;
    }
    
    /* 构建 NVS 条目 */
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.id, new_id, sizeof(entry.id) - 1);
    strncpy(entry.host_id, config->host_id, sizeof(entry.host_id) - 1);
    strncpy(entry.name, config->name, sizeof(entry.name) - 1);
    strncpy(entry.command, config->command, sizeof(entry.command) - 1);
    strncpy(entry.desc, config->desc, sizeof(entry.desc) - 1);
    strncpy(entry.icon, config->icon, sizeof(entry.icon) - 1);
    strncpy(entry.expect_pattern, config->expect_pattern, sizeof(entry.expect_pattern) - 1);
    strncpy(entry.fail_pattern, config->fail_pattern, sizeof(entry.fail_pattern) - 1);
    strncpy(entry.extract_pattern, config->extract_pattern, sizeof(entry.extract_pattern) - 1);
    strncpy(entry.var_name, config->var_name, sizeof(entry.var_name) - 1);
    entry.timeout_sec = config->timeout_sec > 0 ? config->timeout_sec : 30;
    entry.stop_on_match = config->stop_on_match ? 1 : 0;
    entry.enabled = config->enabled ? 1 : 0;
    entry.created_time = (existing_index >= 0) ? entry.created_time : get_current_time();
    entry.last_exec_time = config->last_exec_time;
    
    /* 保存到 NVS */
    make_nvs_key(target_index, key, sizeof(key));
    esp_err_t ret = nvs_set_blob(s_state.nvs, key, &entry, sizeof(entry));
    if (ret == ESP_OK) {
        ret = nvs_commit(s_state.nvs);
    }
    
    xSemaphoreGive(s_state.mutex);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s SSH command: %s (%s)", 
                 existing_index >= 0 ? "Updated" : "Added",
                 entry.name, entry.id);
        
        /* 输出生成的 ID */
        if (out_id && out_id_size > 0) {
            strncpy(out_id, new_id, out_id_size - 1);
            out_id[out_id_size - 1] = '\0';
        }
        
        /* 为新指令预创建变量 */
        if (entry.var_name[0]) {
            precreate_command_variables(entry.var_name);
        }
    }
    
    return ret;
}

esp_err_t ts_ssh_commands_config_remove(const char *id)
{
    if (!s_state.initialized || !id || !id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_cmd_entry_t entry;
    size_t len;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.id, id) == 0) {
                ret = nvs_erase_key(s_state.nvs, key);
                if (ret == ESP_OK) {
                    nvs_commit(s_state.nvs);
                    ESP_LOGI(TAG, "Removed SSH command: %s (%s)", entry.name, id);
                }
                break;
            }
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t ts_ssh_commands_config_get(const char *id, ts_ssh_command_config_t *config)
{
    if (!s_state.initialized || !id || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_cmd_entry_t entry;
    size_t len;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.id, id) == 0) {
                memset(config, 0, sizeof(*config));
                strncpy(config->id, entry.id, sizeof(config->id) - 1);
                strncpy(config->host_id, entry.host_id, sizeof(config->host_id) - 1);
                strncpy(config->name, entry.name, sizeof(config->name) - 1);
                strncpy(config->command, entry.command, sizeof(config->command) - 1);
                strncpy(config->desc, entry.desc, sizeof(config->desc) - 1);
                strncpy(config->icon, entry.icon, sizeof(config->icon) - 1);
                strncpy(config->expect_pattern, entry.expect_pattern, sizeof(config->expect_pattern) - 1);
                strncpy(config->fail_pattern, entry.fail_pattern, sizeof(config->fail_pattern) - 1);
                strncpy(config->extract_pattern, entry.extract_pattern, sizeof(config->extract_pattern) - 1);
                strncpy(config->var_name, entry.var_name, sizeof(config->var_name) - 1);
                config->timeout_sec = entry.timeout_sec;
                config->stop_on_match = entry.stop_on_match != 0;
                config->enabled = entry.enabled != 0;
                config->created_time = entry.created_time;
                config->last_exec_time = entry.last_exec_time;
                ret = ESP_OK;
                break;
            }
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t ts_ssh_commands_config_list(ts_ssh_command_config_t *configs,
                                       size_t max_count, size_t *count)
{
    if (!s_state.initialized || !configs || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_cmd_entry_t entry;
    size_t len;
    size_t n = 0;
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX && n < max_count; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            ts_ssh_command_config_t *cfg = &configs[n];
            memset(cfg, 0, sizeof(*cfg));
            strncpy(cfg->id, entry.id, sizeof(cfg->id) - 1);
            strncpy(cfg->host_id, entry.host_id, sizeof(cfg->host_id) - 1);
            strncpy(cfg->name, entry.name, sizeof(cfg->name) - 1);
            strncpy(cfg->command, entry.command, sizeof(cfg->command) - 1);
            strncpy(cfg->desc, entry.desc, sizeof(cfg->desc) - 1);
            strncpy(cfg->icon, entry.icon, sizeof(cfg->icon) - 1);
            strncpy(cfg->expect_pattern, entry.expect_pattern, sizeof(cfg->expect_pattern) - 1);
            strncpy(cfg->fail_pattern, entry.fail_pattern, sizeof(cfg->fail_pattern) - 1);
            strncpy(cfg->extract_pattern, entry.extract_pattern, sizeof(cfg->extract_pattern) - 1);
            strncpy(cfg->var_name, entry.var_name, sizeof(cfg->var_name) - 1);
            cfg->timeout_sec = entry.timeout_sec;
            cfg->stop_on_match = entry.stop_on_match != 0;
            cfg->enabled = entry.enabled != 0;
            cfg->created_time = entry.created_time;
            cfg->last_exec_time = entry.last_exec_time;
            n++;
        }
    }
    
    *count = n;
    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

esp_err_t ts_ssh_commands_config_list_by_host(const char *host_id,
                                               ts_ssh_command_config_t *configs,
                                               size_t max_count, size_t *count)
{
    if (!s_state.initialized || !host_id || !configs || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_cmd_entry_t entry;
    size_t len;
    size_t n = 0;
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX && n < max_count; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.host_id, host_id) == 0) {
                ts_ssh_command_config_t *cfg = &configs[n];
                memset(cfg, 0, sizeof(*cfg));
                strncpy(cfg->id, entry.id, sizeof(cfg->id) - 1);
                strncpy(cfg->host_id, entry.host_id, sizeof(cfg->host_id) - 1);
                strncpy(cfg->name, entry.name, sizeof(cfg->name) - 1);
                strncpy(cfg->command, entry.command, sizeof(cfg->command) - 1);
                strncpy(cfg->desc, entry.desc, sizeof(cfg->desc) - 1);
                strncpy(cfg->icon, entry.icon, sizeof(cfg->icon) - 1);
                strncpy(cfg->expect_pattern, entry.expect_pattern, sizeof(cfg->expect_pattern) - 1);
                strncpy(cfg->fail_pattern, entry.fail_pattern, sizeof(cfg->fail_pattern) - 1);
                strncpy(cfg->extract_pattern, entry.extract_pattern, sizeof(cfg->extract_pattern) - 1);
                strncpy(cfg->var_name, entry.var_name, sizeof(cfg->var_name) - 1);
                cfg->timeout_sec = entry.timeout_sec;
                cfg->stop_on_match = entry.stop_on_match != 0;
                cfg->enabled = entry.enabled != 0;
                cfg->created_time = entry.created_time;
                cfg->last_exec_time = entry.last_exec_time;
                n++;
            }
        }
    }
    
    *count = n;
    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

size_t ts_ssh_commands_config_count(void)
{
    if (!s_state.initialized) {
        return 0;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_cmd_entry_t entry;
    size_t len;
    size_t count = 0;
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            count++;
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return count;
}

esp_err_t ts_ssh_commands_config_clear(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        nvs_erase_key(s_state.nvs, key);
    }
    nvs_commit(s_state.nvs);
    
    xSemaphoreGive(s_state.mutex);
    ESP_LOGI(TAG, "Cleared all SSH commands");
    return ESP_OK;
}

esp_err_t ts_ssh_commands_config_update_exec_time(const char *id)
{
    if (!s_state.initialized || !id || !id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_cmd_entry_t entry;
    size_t len;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    for (int i = 0; i < TS_SSH_COMMANDS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.id, id) == 0) {
                entry.last_exec_time = get_current_time();
                ret = nvs_set_blob(s_state.nvs, key, &entry, sizeof(entry));
                if (ret == ESP_OK) {
                    nvs_commit(s_state.nvs);
                }
                break;
            }
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return ret;
}

/*===========================================================================*/
/*                          Variable Pre-creation                             */
/*===========================================================================*/

esp_err_t ts_ssh_commands_precreate_variables(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Pre-creating variables for all SSH commands");
    precreate_all_variables();
    return ESP_OK;
}
