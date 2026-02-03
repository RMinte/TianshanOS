/**
 * @file ts_ssh_hosts_config.c
 * @brief SSH Host Configuration Storage Implementation
 *
 * 实现 SSH 主机凭证配置的持久化存储（NVS + SD卡双写）。
 */

#include "ts_ssh_hosts_config.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>

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

/** 延迟导出标志 */
static bool s_hosts_pending_export = false;

/**
 * @brief 延迟加载/导出任务 - 在独立任务中处理 SD 卡操作（避免 main 任务栈溢出）
 * 
 * 配置加载优先级：SD 卡 > NVS > 硬编码默认值
 */
static void hosts_deferred_export_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2500));  /* 等待系统稳定，错开 commands 加载 */
    
    if (!s_state.initialized) {
        vTaskDelete(NULL);
        return;
    }
    
    size_t nvs_count = ts_ssh_hosts_config_count();
    
    /* 尝试从 SD 卡加载（优先级最高） */
    ESP_LOGI(TAG, "Deferred: trying to load from SD card...");
    esp_err_t import_ret = ts_ssh_hosts_config_import_from_sdcard(true);  /* merge=true 保留 NVS */
    
    size_t count = ts_ssh_hosts_config_count();
    
    if (import_ret == ESP_OK && count > nvs_count) {
        /* SD 卡有新数据，清空 NVS 后重新导入 */
        ESP_LOGI(TAG, "SD card has new data (%d > %d), reimporting...", (int)count, (int)nvs_count);
        ts_ssh_hosts_config_clear();
        ts_ssh_hosts_config_import_from_sdcard(false);
        count = ts_ssh_hosts_config_count();
        ESP_LOGI(TAG, "Loaded %d hosts from SD card", (int)count);
    } else if (import_ret == ESP_OK) {
        /* SD 卡加载成功，但数据量不比 NVS 多（已合并） */
        ESP_LOGI(TAG, "Merged SD card data with NVS (%d hosts total)", (int)count);
    } else if (import_ret == ESP_ERR_NOT_FOUND) {
        /* SD 卡文件不存在/无效，导出 NVS 数据到 SD 卡 */
        if (nvs_count > 0) {
            ESP_LOGI(TAG, "SD card file not found, exporting %d hosts from NVS", (int)nvs_count);
            ts_ssh_hosts_config_export_to_sdcard();
        } else {
            ESP_LOGI(TAG, "No hosts in SD card or NVS");
        }
    } else {
        /* 其他错误（如 SD 卡未挂载） */
        ESP_LOGW(TAG, "SD card import failed: %s", esp_err_to_name(import_ret));
    }
    
    s_hosts_pending_export = false;
    vTaskDelete(NULL);
}

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
    
    /*
     * 配置加载策略（避免在 main 任务中执行 SD 卡 I/O）：
     * 
     * 1. init 时只初始化 NVS，统计现有数据
     * 2. 创建延迟任务处理 SD 卡加载/导出
     */
    
    size_t nvs_count = ts_ssh_hosts_config_count();
    ESP_LOGI(TAG, "NVS has %d hosts, will load from SD card in background", (int)nvs_count);
    
    /* 创建延迟加载任务 */
    s_hosts_pending_export = true;
    xTaskCreate(hosts_deferred_export_task, "ssh_host_load", 8192, NULL, 2, NULL);
    
    ESP_LOGI(TAG, "SSH hosts config initialized (SD card loading deferred)");
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
        
        /* 同步到 SD 卡 */
        ts_ssh_hosts_config_sync_to_sdcard();
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
    
    /* 同步到 SD 卡 */
    if (ret == ESP_OK) {
        ts_ssh_hosts_config_sync_to_sdcard();
    }
    
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
    
    /* 同步到 SD 卡 */
    if (ret == ESP_OK) {
        ts_ssh_hosts_config_sync_to_sdcard();
    }
    
    return ret;
}

/*===========================================================================*/
/*                    Iterator/Pagination API (内存优化)                       */
/*===========================================================================*/

/**
 * @brief 内部辅助：从 NVS entry 填充 config 结构体
 */
static void host_entry_to_config(const nvs_host_entry_t *entry, ts_ssh_host_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->id, entry->id, sizeof(cfg->id) - 1);
    strncpy(cfg->host, entry->host, sizeof(cfg->host) - 1);
    cfg->port = entry->port;
    strncpy(cfg->username, entry->username, sizeof(cfg->username) - 1);
    cfg->auth_type = (ts_ssh_host_auth_type_t)entry->auth_type;
    strncpy(cfg->keyid, entry->keyid, sizeof(cfg->keyid) - 1);
    cfg->created_time = entry->created_time;
    cfg->last_used_time = entry->last_used_time;
    cfg->enabled = entry->enabled != 0;
}

esp_err_t ts_ssh_hosts_config_iterate(ts_ssh_host_iterator_cb_t callback,
                                       void *user_data,
                                       size_t offset,
                                       size_t limit,
                                       size_t *total_count)
{
    if (!s_state.initialized || !callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    char key[16];
    nvs_host_entry_t entry;
    ts_ssh_host_config_t cfg;
    size_t len;
    size_t total = 0;       /* 总有效条目数 */
    size_t returned = 0;    /* 实际返回给回调的数量 */
    size_t skipped = 0;     /* 跳过的条目数（offset） */
    
    for (int i = 0; i < TS_SSH_HOSTS_MAX; i++) {
        make_nvs_key(i, key, sizeof(key));
        len = sizeof(entry);
        if (nvs_get_blob(s_state.nvs, key, &entry, &len) == ESP_OK && len == sizeof(entry)) {
            total++;
            
            /* 处理分页偏移 */
            if (skipped < offset) {
                skipped++;
                continue;
            }
            
            /* 处理分页限制 */
            if (limit > 0 && returned >= limit) {
                continue;  /* 继续计数但不回调 */
            }
            
            /* 转换并回调 */
            host_entry_to_config(&entry, &cfg);
            bool cont = callback(&cfg, returned, user_data);
            returned++;
            
            if (!cont) {
                break;  /* 回调请求停止 */
            }
        }
    }
    
    xSemaphoreGive(s_state.mutex);
    
    if (total_count) {
        *total_count = total;
    }
    
    return ESP_OK;
}

/*===========================================================================*/
/*                    SD Card Export/Import (持久化备份)                       */
/*===========================================================================*/

/** SD 卡配置目录 */
#define SDCARD_CONFIG_DIR  "/sdcard/config"

/**
 * @brief 检查 SD 卡是否已挂载
 */
static bool is_sdcard_mounted(void)
{
    struct stat st;
    return (stat("/sdcard", &st) == 0 && S_ISDIR(st.st_mode));
}

/**
 * @brief 确保配置目录存在
 */
static esp_err_t ensure_config_dir(void)
{
    struct stat st;
    if (stat(SDCARD_CONFIG_DIR, &st) != 0) {
        if (mkdir(SDCARD_CONFIG_DIR, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create config dir: %s", SDCARD_CONFIG_DIR);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/**
 * @brief 将单条主机配置转换为 JSON 对象
 */
static cJSON *host_to_json(const ts_ssh_host_config_t *cfg)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;
    
    cJSON_AddStringToObject(obj, "id", cfg->id);
    cJSON_AddStringToObject(obj, "host", cfg->host);
    cJSON_AddNumberToObject(obj, "port", cfg->port);
    cJSON_AddStringToObject(obj, "username", cfg->username);
    cJSON_AddStringToObject(obj, "auth_type", cfg->auth_type == TS_SSH_HOST_AUTH_KEY ? "key" : "password");
    if (cfg->keyid[0]) cJSON_AddStringToObject(obj, "keyid", cfg->keyid);
    cJSON_AddBoolToObject(obj, "enabled", cfg->enabled);
    cJSON_AddNumberToObject(obj, "created_time", cfg->created_time);
    cJSON_AddNumberToObject(obj, "last_used_time", cfg->last_used_time);
    
    return obj;
}

/**
 * @brief 从 JSON 对象解析主机配置
 */
static esp_err_t json_to_host(const cJSON *obj, ts_ssh_host_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    
    cJSON *item;
    
    item = cJSON_GetObjectItem(obj, "id");
    if (item && cJSON_IsString(item)) strncpy(cfg->id, item->valuestring, sizeof(cfg->id) - 1);
    
    item = cJSON_GetObjectItem(obj, "host");
    if (item && cJSON_IsString(item)) strncpy(cfg->host, item->valuestring, sizeof(cfg->host) - 1);
    
    item = cJSON_GetObjectItem(obj, "port");
    if (item && cJSON_IsNumber(item)) cfg->port = (uint16_t)item->valueint;
    else cfg->port = 22;
    
    item = cJSON_GetObjectItem(obj, "username");
    if (item && cJSON_IsString(item)) strncpy(cfg->username, item->valuestring, sizeof(cfg->username) - 1);
    
    item = cJSON_GetObjectItem(obj, "auth_type");
    if (item && cJSON_IsString(item)) {
        cfg->auth_type = (strcmp(item->valuestring, "key") == 0) ? 
                         TS_SSH_HOST_AUTH_KEY : TS_SSH_HOST_AUTH_PASSWORD;
    }
    
    item = cJSON_GetObjectItem(obj, "keyid");
    if (item && cJSON_IsString(item)) strncpy(cfg->keyid, item->valuestring, sizeof(cfg->keyid) - 1);
    
    item = cJSON_GetObjectItem(obj, "enabled");
    if (item && cJSON_IsBool(item)) cfg->enabled = cJSON_IsTrue(item);
    else cfg->enabled = true;
    
    item = cJSON_GetObjectItem(obj, "created_time");
    if (item && cJSON_IsNumber(item)) cfg->created_time = (uint32_t)item->valueint;
    
    item = cJSON_GetObjectItem(obj, "last_used_time");
    if (item && cJSON_IsNumber(item)) cfg->last_used_time = (uint32_t)item->valueint;
    
    return ESP_OK;
}

/**
 * @brief 导出迭代器回调上下文
 */
typedef struct {
    FILE *fp;
    int count;
    bool first;
} host_export_ctx_t;

/**
 * @brief 导出迭代器回调 - 将单条主机写入文件
 */
static bool host_export_iterator_cb(const ts_ssh_host_config_t *config, size_t index, void *user_data)
{
    (void)index;
    host_export_ctx_t *ctx = (host_export_ctx_t *)user_data;
    
    cJSON *obj = host_to_json(config);
    if (!obj) return true;
    
    char *str = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    
    if (str) {
        if (!ctx->first) {
            fprintf(ctx->fp, ",\n");
        }
        fprintf(ctx->fp, "  %s", str);
        cJSON_free(str);
        ctx->first = false;
        ctx->count++;
    }
    
    return true;
}

esp_err_t ts_ssh_hosts_config_export_to_sdcard(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_sdcard_mounted()) {
        ESP_LOGD(TAG, "SD card not mounted, skip export");
        return ESP_ERR_NOT_FOUND;
    }
    
    esp_err_t ret = ensure_config_dir();
    if (ret != ESP_OK) {
        return ret;
    }
    
    FILE *fp = fopen(TS_SSH_HOSTS_SDCARD_PATH, "w");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", TS_SSH_HOSTS_SDCARD_PATH);
        return ESP_FAIL;
    }
    
    fprintf(fp, "[\n");
    
    host_export_ctx_t ctx = {
        .fp = fp,
        .count = 0,
        .first = true,
    };
    
    ret = ts_ssh_hosts_config_iterate(host_export_iterator_cb, &ctx, 0, 0, NULL);
    
    fprintf(fp, "\n]\n");
    fclose(fp);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Exported %d SSH hosts to %s", ctx.count, TS_SSH_HOSTS_SDCARD_PATH);
    }
    
    return ret;
}

esp_err_t ts_ssh_hosts_config_import_from_sdcard(bool merge)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!is_sdcard_mounted()) {
        return ESP_ERR_NOT_FOUND;
    }
    
    struct stat st;
    if (stat(TS_SSH_HOSTS_SDCARD_PATH, &st) != 0) {
        ESP_LOGD(TAG, "Config file not found: %s", TS_SSH_HOSTS_SDCARD_PATH);
        return ESP_ERR_NOT_FOUND;
    }
    
    /* 检查文件是否为空或太小（至少需要 "[]" = 2 字节） */
    if (st.st_size < 2) {
        ESP_LOGW(TAG, "Config file empty or too small (%ld bytes), treating as not found", (long)st.st_size);
        return ESP_ERR_NOT_FOUND;
    }
    
    FILE *fp = fopen(TS_SSH_HOSTS_SDCARD_PATH, "r");
    if (!fp) {
        ESP_LOGE(TAG, "Failed to open file: %s", TS_SSH_HOSTS_SDCARD_PATH);
        return ESP_FAIL;
    }
    
    /* 分配缓冲区（优先使用 PSRAM） */
    char *buffer = heap_caps_malloc(st.st_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        /* PSRAM 不可用，fallback 到 DRAM */
        buffer = malloc(st.st_size + 1);
    }
    if (!buffer) {
        fclose(fp);
        ESP_LOGE(TAG, "Failed to allocate %ld bytes for config file", (long)st.st_size);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(buffer, 1, st.st_size, fp);
    fclose(fp);
    buffer[read_size] = '\0';
    
    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON from %s, treating as not found", TS_SSH_HOSTS_SDCARD_PATH);
        return ESP_ERR_NOT_FOUND;  /* 无效 JSON 视为文件不存在，触发重新导出 */
    }
    
    if (!cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Root element is not an array");
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    
    if (!merge) {
        ts_ssh_hosts_config_clear();
    }
    
    int imported = 0;
    int skipped = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        ts_ssh_host_config_t cfg;
        if (json_to_host(item, &cfg) == ESP_OK && cfg.id[0] && cfg.host[0]) {
            esp_err_t add_ret = ts_ssh_hosts_config_add(&cfg);
            if (add_ret == ESP_OK) {
                imported++;
            } else {
                skipped++;
            }
        } else {
            skipped++;
        }
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Imported %d SSH hosts from %s (skipped %d)", 
             imported, TS_SSH_HOSTS_SDCARD_PATH, skipped);
    
    return ESP_OK;
}

/**
 * @brief 异步同步任务 - 延迟执行 SD 卡导出
 */
static void hosts_async_sync_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));  /* 短延迟 */
    
    if (s_state.initialized) {
        ESP_LOGI(TAG, "Async sync to SD card...");
        ts_ssh_hosts_config_export_to_sdcard();
    }
    
    vTaskDelete(NULL);
}

void ts_ssh_hosts_config_sync_to_sdcard(void)
{
    /* 异步执行 SD 卡同步（避免在 API 处理任务中执行导致栈溢出/超时） */
    xTaskCreate(hosts_async_sync_task, "ssh_host_sync", 8192, NULL, 2, NULL);
}
