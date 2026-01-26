/**
 * @file ts_ssh_hosts_config.c
 * @brief SSH Host Configuration Storage Implementation
 *
 * 实现 SSH 主机凭证配置的持久化存储（NVS）。
 */

#include "ts_ssh_hosts_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <time.h>

static const char *TAG = "ts_ssh_hosts_cfg";

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

#define NVS_NAMESPACE       "ts_ssh_cfg"
#define NVS_KEY_COUNT       "count"
#define NVS_KEY_PREFIX      "h_"

/*===========================================================================*/
/*                              Internal State                                */
/*===========================================================================*/

/** NVS 存储格式（不含密码） */
typedef struct __attribute__((packed)) {
    char id[TS_SSH_HOST_ID_MAX];
    char host[TS_SSH_HOST_ADDR_MAX];
    uint16_t port;
    char username[TS_SSH_USERNAME_MAX];
    uint8_t auth_type;
    char keyid[TS_SSH_KEYID_MAX];
    uint32_t created_time;
    uint32_t last_used_time;
    uint8_t enabled;
} nvs_host_entry_t;

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

/*===========================================================================*/
/*                          Initialization                                    */
/*===========================================================================*/

esp_err_t ts_ssh_hosts_config_init(void)
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
    ESP_LOGI(TAG, "SSH hosts config initialized");
    return ESP_OK;
}

void ts_ssh_hosts_config_deinit(void)
{
    if (!s_state.initialized) {
        return;
    }
    
    nvs_close(s_state.nvs);
    vSemaphoreDelete(s_state.mutex);
    s_state.initialized = false;
}

bool ts_ssh_hosts_config_is_initialized(void)
{
    return s_state.initialized;
}

/*===========================================================================*/
/*                          CRUD Operations                                   */
/*===========================================================================*/

esp_err_t ts_ssh_hosts_config_add(const ts_ssh_host_config_t *config)
{
    if (!s_state.initialized || !config || !config->id[0] || !config->host[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    /* 检查是否已存在（更新）或找空位（新增） */
    int existing_index = -1;
    int free_index = -1;
    char key[16];
    nvs_host_entry_t entry;
    size_t len;
    
    for (int i = 0; i < TS_SSH_HOSTS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        esp_err_t ret = nvs_get_blob(s_state.nvs, key, &entry, &len);
        
        if (ret == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.id, config->id) == 0) {
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
        ESP_LOGW(TAG, "Max hosts reached");
        return ESP_ERR_NO_MEM;
    }
    
    /* 构建 NVS 条目 */
    memset(&entry, 0, sizeof(entry));
    strncpy(entry.id, config->id, sizeof(entry.id) - 1);
    strncpy(entry.host, config->host, sizeof(entry.host) - 1);
    entry.port = config->port > 0 ? config->port : 22;
    strncpy(entry.username, config->username, sizeof(entry.username) - 1);
    entry.auth_type = (uint8_t)config->auth_type;
    strncpy(entry.keyid, config->keyid, sizeof(entry.keyid) - 1);
    entry.created_time = (existing_index >= 0) ? entry.created_time : get_current_time();
    entry.last_used_time = config->last_used_time;
    entry.enabled = config->enabled ? 1 : 0;
    
    /* 保存到 NVS */
    make_nvs_key(target_index, key, sizeof(key));
    esp_err_t ret = nvs_set_blob(s_state.nvs, key, &entry, sizeof(entry));
    if (ret == ESP_OK) {
        ret = nvs_commit(s_state.nvs);
    }
    
    xSemaphoreGive(s_state.mutex);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "%s SSH host: %s (%s@%s:%d)", 
                 existing_index >= 0 ? "Updated" : "Added",
                 config->id, config->username, config->host, entry.port);
    }
    
    return ret;
}

esp_err_t ts_ssh_hosts_config_remove(const char *id)
{
    if (!s_state.initialized || !id || !id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_host_entry_t entry;
    size_t len;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    for (int i = 0; i < TS_SSH_HOSTS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.id, id) == 0) {
                ret = nvs_erase_key(s_state.nvs, key);
                if (ret == ESP_OK) {
                    nvs_commit(s_state.nvs);
                    ESP_LOGI(TAG, "Removed SSH host: %s", id);
                }
                break;
            }
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t ts_ssh_hosts_config_get(const char *id, ts_ssh_host_config_t *config)
{
    if (!s_state.initialized || !id || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_host_entry_t entry;
    size_t len;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    for (int i = 0; i < TS_SSH_HOSTS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.id, id) == 0) {
                memset(config, 0, sizeof(*config));
                strncpy(config->id, entry.id, sizeof(config->id) - 1);
                strncpy(config->host, entry.host, sizeof(config->host) - 1);
                config->port = entry.port;
                strncpy(config->username, entry.username, sizeof(config->username) - 1);
                config->auth_type = (ts_ssh_host_auth_type_t)entry.auth_type;
                strncpy(config->keyid, entry.keyid, sizeof(config->keyid) - 1);
                config->created_time = entry.created_time;
                config->last_used_time = entry.last_used_time;
                config->enabled = entry.enabled != 0;
                ret = ESP_OK;
                break;
            }
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t ts_ssh_hosts_config_find(const char *host, uint16_t port, 
                                    const char *username,
                                    ts_ssh_host_config_t *config)
{
    if (!s_state.initialized || !host || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (port == 0) port = 22;
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_host_entry_t entry;
    size_t len;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    for (int i = 0; i < TS_SSH_HOSTS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.host, host) == 0 && entry.port == port) {
                if (username == NULL || strcmp(entry.username, username) == 0) {
                    memset(config, 0, sizeof(*config));
                    strncpy(config->id, entry.id, sizeof(config->id) - 1);
                    strncpy(config->host, entry.host, sizeof(config->host) - 1);
                    config->port = entry.port;
                    strncpy(config->username, entry.username, sizeof(config->username) - 1);
                    config->auth_type = (ts_ssh_host_auth_type_t)entry.auth_type;
                    strncpy(config->keyid, entry.keyid, sizeof(config->keyid) - 1);
                    config->created_time = entry.created_time;
                    config->last_used_time = entry.last_used_time;
                    config->enabled = entry.enabled != 0;
                    ret = ESP_OK;
                    break;
                }
            }
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t ts_ssh_hosts_config_list(ts_ssh_host_config_t *configs, 
                                    size_t max_count, 
                                    size_t *count)
{
    if (!s_state.initialized || !configs || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_host_entry_t entry;
    size_t len;
    size_t found = 0;
    
    for (int i = 0; i < TS_SSH_HOSTS_MAX && found < max_count; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            ts_ssh_host_config_t *cfg = &configs[found];
            memset(cfg, 0, sizeof(*cfg));
            strncpy(cfg->id, entry.id, sizeof(cfg->id) - 1);
            strncpy(cfg->host, entry.host, sizeof(cfg->host) - 1);
            cfg->port = entry.port;
            strncpy(cfg->username, entry.username, sizeof(cfg->username) - 1);
            cfg->auth_type = (ts_ssh_host_auth_type_t)entry.auth_type;
            strncpy(cfg->keyid, entry.keyid, sizeof(cfg->keyid) - 1);
            cfg->created_time = entry.created_time;
            cfg->last_used_time = entry.last_used_time;
            cfg->enabled = entry.enabled != 0;
            found++;
        }
    }
    
    *count = found;
    xSemaphoreGive(s_state.mutex);
    return ESP_OK;
}

int ts_ssh_hosts_config_count(void)
{
    if (!s_state.initialized) {
        return 0;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_host_entry_t entry;
    size_t len;
    int count = 0;
    
    for (int i = 0; i < TS_SSH_HOSTS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            count++;
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    return count;
}

esp_err_t ts_ssh_hosts_config_touch(const char *id)
{
    if (!s_state.initialized || !id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_host_entry_t entry;
    size_t len;
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    
    for (int i = 0; i < TS_SSH_HOSTS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            if (strcmp(entry.id, id) == 0) {
                entry.last_used_time = get_current_time();
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

esp_err_t ts_ssh_hosts_config_clear(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    esp_err_t ret = nvs_erase_all(s_state.nvs);
    if (ret == ESP_OK) {
        nvs_commit(s_state.nvs);
        ESP_LOGI(TAG, "Cleared all SSH host configs");
    }
    
    xSemaphoreGive(s_state.mutex);
    return ret;
}
