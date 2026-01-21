/**
 * @file ts_config.c
 * @brief TianShanOS Configuration Management - Core Implementation
 *
 * 配置管理系统核心实现
 * 提供统一的配置管理接口，支持多后端、优先级和变更监听
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#include <string.h>
#include <stdlib.h>
#include "ts_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

static const char *TAG = "ts_config";

/* ============================================================================
 * 私有类型定义
 * ========================================================================== */

/**
 * @brief 配置项节点（链表节点）
 */
typedef struct ts_config_node {
    ts_config_item_t item;
    struct ts_config_node *next;
} ts_config_node_t;

/**
 * @brief 监听器节点
 */
typedef struct ts_config_listener_node {
    char *key_prefix;                       /**< 监听的键前缀 */
    ts_config_listener_t callback;          /**< 回调函数 */
    void *user_data;                        /**< 用户数据 */
    struct ts_config_listener_node *next;
} ts_config_listener_node_t;

/**
 * @brief 后端注册信息
 */
typedef struct {
    bool registered;
    const ts_config_backend_ops_t *ops;
    uint8_t priority;
} ts_config_backend_info_t;

/**
 * @brief 配置管理器上下文
 */
typedef struct {
    bool initialized;
    SemaphoreHandle_t mutex;                        /**< 访问互斥锁 */
    ts_config_node_t *config_list;                  /**< 配置项链表头 */
    ts_config_listener_node_t *listener_list;       /**< 监听器链表头 */
    ts_config_backend_info_t backends[TS_CONFIG_BACKEND_MAX];  /**< 后端信息 */
    size_t config_count;                            /**< 配置项数量 */
    size_t listener_count;                          /**< 监听器数量 */
    bool dirty;                                     /**< 是否有未保存的更改 */
    TimerHandle_t save_timer;                       /**< 自动保存定时器 */
} ts_config_context_t;

/* ============================================================================
 * 私有变量
 * ========================================================================== */

static ts_config_context_t s_config_ctx = {0};

/* ============================================================================
 * 私有函数声明
 * ========================================================================== */

static ts_config_node_t *find_config_node(const char *key);
static ts_config_node_t *create_config_node(const char *key, ts_config_type_t type);
static void free_config_node(ts_config_node_t *node);
static void notify_listeners(const ts_config_change_t *change);
static void auto_save_callback(TimerHandle_t timer);
static void schedule_auto_save(void);
static size_t get_type_size(ts_config_type_t type);
static bool copy_value(ts_config_value_t *dst, const ts_config_value_t *src, 
                       ts_config_type_t type, size_t size);
static void free_value(ts_config_value_t *value, ts_config_type_t type);

/* ============================================================================
 * 初始化和反初始化
 * ========================================================================== */

esp_err_t ts_config_init(void)
{
    if (s_config_ctx.initialized) {
        ESP_LOGW(TAG, "Configuration system already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing TianShanOS Configuration System...");

    // 创建互斥锁
    s_config_ctx.mutex = xSemaphoreCreateMutex();
    if (s_config_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 初始化链表
    s_config_ctx.config_list = NULL;
    s_config_ctx.listener_list = NULL;
    s_config_ctx.config_count = 0;
    s_config_ctx.listener_count = 0;
    s_config_ctx.dirty = false;

    // 创建自动保存定时器
#ifdef CONFIG_TS_CONFIG_AUTO_SAVE
    s_config_ctx.save_timer = xTimerCreate(
        "config_save",
        pdMS_TO_TICKS(CONFIG_TS_CONFIG_AUTO_SAVE_DELAY_MS),
        pdFALSE,  // 一次性定时器
        NULL,
        auto_save_callback
    );
    if (s_config_ctx.save_timer == NULL) {
        ESP_LOGW(TAG, "Failed to create auto-save timer");
    }
#endif

    // 初始化后端信息
    memset(s_config_ctx.backends, 0, sizeof(s_config_ctx.backends));

    s_config_ctx.initialized = true;
    ESP_LOGI(TAG, "Configuration system initialized successfully");
    return ESP_OK;
}

esp_err_t ts_config_deinit(void)
{
    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing configuration system...");

    // 保存未保存的更改
    if (s_config_ctx.dirty) {
        ts_config_save();
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    // 释放所有配置节点
    ts_config_node_t *node = s_config_ctx.config_list;
    while (node != NULL) {
        ts_config_node_t *next = node->next;
        free_config_node(node);
        node = next;
    }
    s_config_ctx.config_list = NULL;

    // 释放所有监听器
    ts_config_listener_node_t *listener = s_config_ctx.listener_list;
    while (listener != NULL) {
        ts_config_listener_node_t *next = listener->next;
        if (listener->key_prefix) {
            free(listener->key_prefix);
        }
        free(listener);
        listener = next;
    }
    s_config_ctx.listener_list = NULL;

    // 反初始化所有后端
    for (int i = 0; i < TS_CONFIG_BACKEND_MAX; i++) {
        if (s_config_ctx.backends[i].registered && 
            s_config_ctx.backends[i].ops && 
            s_config_ctx.backends[i].ops->deinit) {
            s_config_ctx.backends[i].ops->deinit();
        }
    }

    xSemaphoreGive(s_config_ctx.mutex);

    // 删除定时器
    if (s_config_ctx.save_timer != NULL) {
        xTimerDelete(s_config_ctx.save_timer, portMAX_DELAY);
        s_config_ctx.save_timer = NULL;
    }

    // 删除互斥锁
    vSemaphoreDelete(s_config_ctx.mutex);
    s_config_ctx.mutex = NULL;

    s_config_ctx.initialized = false;
    ESP_LOGI(TAG, "Configuration system deinitialized");
    return ESP_OK;
}

bool ts_config_is_initialized(void)
{
    return s_config_ctx.initialized;
}

/* ============================================================================
 * 基础配置读取 API
 * ========================================================================== */

esp_err_t ts_config_get_bool(const char *key, bool *value, bool default_value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        *value = default_value;
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node != NULL && node->item.type == TS_CONFIG_TYPE_BOOL) {
        *value = node->item.value.val_bool;
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_OK;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    *value = default_value;
    return (node == NULL) ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
}

esp_err_t ts_config_get_int32(const char *key, int32_t *value, int32_t default_value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        *value = default_value;
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node != NULL && node->item.type == TS_CONFIG_TYPE_INT32) {
        *value = node->item.value.val_i32;
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_OK;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    *value = default_value;
    return (node == NULL) ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
}

esp_err_t ts_config_get_uint32(const char *key, uint32_t *value, uint32_t default_value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        *value = default_value;
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node != NULL && node->item.type == TS_CONFIG_TYPE_UINT32) {
        *value = node->item.value.val_u32;
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_OK;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    *value = default_value;
    return (node == NULL) ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
}

esp_err_t ts_config_get_int64(const char *key, int64_t *value, int64_t default_value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        *value = default_value;
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node != NULL && node->item.type == TS_CONFIG_TYPE_INT64) {
        *value = node->item.value.val_i64;
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_OK;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    *value = default_value;
    return (node == NULL) ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
}

esp_err_t ts_config_get_float(const char *key, float *value, float default_value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        *value = default_value;
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node != NULL && node->item.type == TS_CONFIG_TYPE_FLOAT) {
        *value = node->item.value.val_float;
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_OK;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    *value = default_value;
    return (node == NULL) ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
}

esp_err_t ts_config_get_double(const char *key, double *value, double default_value)
{
    if (key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        *value = default_value;
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node != NULL && node->item.type == TS_CONFIG_TYPE_DOUBLE) {
        *value = node->item.value.val_double;
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_OK;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    *value = default_value;
    return (node == NULL) ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
}

esp_err_t ts_config_get_string(const char *key, char *buffer, size_t buffer_size, 
                                const char *default_value)
{
    if (key == NULL || buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        if (default_value != NULL) {
            strncpy(buffer, default_value, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
        } else {
            buffer[0] = '\0';
        }
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node != NULL && node->item.type == TS_CONFIG_TYPE_STRING) {
        if (node->item.value.val_string != NULL) {
            size_t len = strlen(node->item.value.val_string);
            if (len >= buffer_size) {
                xSemaphoreGive(s_config_ctx.mutex);
                return ESP_ERR_INVALID_SIZE;
            }
            strcpy(buffer, node->item.value.val_string);
            xSemaphoreGive(s_config_ctx.mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_config_ctx.mutex);

    if (default_value != NULL) {
        strncpy(buffer, default_value, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    } else {
        buffer[0] = '\0';
    }
    return (node == NULL) ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_ARG;
}

esp_err_t ts_config_get_blob(const char *key, void *buffer, size_t *size)
{
    if (key == NULL || buffer == NULL || size == NULL || *size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node != NULL && node->item.type == TS_CONFIG_TYPE_BLOB) {
        if (node->item.value.val_blob.data != NULL) {
            if (node->item.value.val_blob.size > *size) {
                *size = node->item.value.val_blob.size;
                xSemaphoreGive(s_config_ctx.mutex);
                return ESP_ERR_INVALID_SIZE;
            }
            memcpy(buffer, node->item.value.val_blob.data, node->item.value.val_blob.size);
            *size = node->item.value.val_blob.size;
            xSemaphoreGive(s_config_ctx.mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_NOT_FOUND;
}

/* ============================================================================
 * 基础配置写入 API
 * ========================================================================== */

/**
 * @brief 通用配置设置函数
 */
static esp_err_t config_set_value(const char *key, ts_config_type_t type,
                                   const ts_config_value_t *value, size_t size)
{
    if (key == NULL || strlen(key) >= TS_CONFIG_KEY_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    ts_config_value_t old_value = {0};
    bool had_old_value = false;

    if (node == NULL) {
        // 创建新节点
        node = create_config_node(key, type);
        if (node == NULL) {
            xSemaphoreGive(s_config_ctx.mutex);
            return ESP_ERR_NO_MEM;
        }
        // 添加到链表头
        node->next = s_config_ctx.config_list;
        s_config_ctx.config_list = node;
        s_config_ctx.config_count++;
    } else {
        // 保存旧值用于通知
        had_old_value = true;
        old_value = node->item.value;
        // 如果类型不同，先清理旧值
        if (node->item.type != type) {
            free_value(&node->item.value, node->item.type);
            memset(&node->item.value, 0, sizeof(ts_config_value_t));
            node->item.type = type;
            had_old_value = false;  // 类型变化，不传递旧值
        } else {
            // 类型相同，清零指针防止 copy_value 中 double-free
            // （旧值已保存在 old_value 中，会在最后统一释放）
            if (type == TS_CONFIG_TYPE_STRING) {
                node->item.value.val_string = NULL;
            } else if (type == TS_CONFIG_TYPE_BLOB) {
                node->item.value.val_blob.data = NULL;
                node->item.value.val_blob.size = 0;
            }
        }
    }

    // 复制新值
    if (!copy_value(&node->item.value, value, type, size)) {
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    node->item.source = TS_CONFIG_BACKEND_CLI;  // 默认来源为 CLI
    s_config_ctx.dirty = true;

    // 构造变更通知
    ts_config_change_t change = {
        .event_type = TS_CONFIG_EVENT_SET,
        .key = key,
        .value_type = type,
        .old_value = had_old_value ? &old_value : NULL,
        .new_value = &node->item.value,
        .source = node->item.source
    };

    xSemaphoreGive(s_config_ctx.mutex);

    // 通知监听器
    notify_listeners(&change);

    // 清理旧值内存
    if (had_old_value) {
        free_value(&old_value, type);
    }

    // 安排自动保存
    schedule_auto_save();

#ifdef CONFIG_TS_CONFIG_DEBUG
    ESP_LOGD(TAG, "Set config: %s (type=%d)", key, type);
#endif

    return ESP_OK;
}

esp_err_t ts_config_set_bool(const char *key, bool value)
{
    ts_config_value_t val = { .val_bool = value };
    return config_set_value(key, TS_CONFIG_TYPE_BOOL, &val, sizeof(bool));
}

esp_err_t ts_config_set_int32(const char *key, int32_t value)
{
    ts_config_value_t val = { .val_i32 = value };
    return config_set_value(key, TS_CONFIG_TYPE_INT32, &val, sizeof(int32_t));
}

esp_err_t ts_config_set_uint32(const char *key, uint32_t value)
{
    ts_config_value_t val = { .val_u32 = value };
    return config_set_value(key, TS_CONFIG_TYPE_UINT32, &val, sizeof(uint32_t));
}

esp_err_t ts_config_set_int64(const char *key, int64_t value)
{
    ts_config_value_t val = { .val_i64 = value };
    return config_set_value(key, TS_CONFIG_TYPE_INT64, &val, sizeof(int64_t));
}

esp_err_t ts_config_set_float(const char *key, float value)
{
    ts_config_value_t val = { .val_float = value };
    return config_set_value(key, TS_CONFIG_TYPE_FLOAT, &val, sizeof(float));
}

esp_err_t ts_config_set_double(const char *key, double value)
{
    ts_config_value_t val = { .val_double = value };
    return config_set_value(key, TS_CONFIG_TYPE_DOUBLE, &val, sizeof(double));
}

esp_err_t ts_config_set_string(const char *key, const char *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ts_config_value_t val = { .val_string = (char *)value };
    return config_set_value(key, TS_CONFIG_TYPE_STRING, &val, strlen(value) + 1);
}

esp_err_t ts_config_set_blob(const char *key, const void *value, size_t size)
{
    if (value == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    ts_config_value_t val = { .val_blob = { .data = (void *)value, .size = size } };
    return config_set_value(key, TS_CONFIG_TYPE_BLOB, &val, size);
}

/* ============================================================================
 * 高级配置操作
 * ========================================================================== */

esp_err_t ts_config_delete(const char *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *prev = NULL;
    ts_config_node_t *node = s_config_ctx.config_list;

    while (node != NULL) {
        if (strcmp(node->item.key, key) == 0) {
            // 找到节点，从链表移除
            if (prev == NULL) {
                s_config_ctx.config_list = node->next;
            } else {
                prev->next = node->next;
            }
            s_config_ctx.config_count--;

            // 构造变更通知
            ts_config_change_t change = {
                .event_type = TS_CONFIG_EVENT_DELETE,
                .key = key,
                .value_type = node->item.type,
                .old_value = &node->item.value,
                .new_value = NULL,
                .source = node->item.source
            };

            xSemaphoreGive(s_config_ctx.mutex);

            // 通知监听器
            notify_listeners(&change);

            // 释放节点
            free_config_node(node);

            s_config_ctx.dirty = true;
            schedule_auto_save();

            return ESP_OK;
        }
        prev = node;
        node = node->next;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_NOT_FOUND;
}

bool ts_config_exists(const char *key)
{
    if (key == NULL || !s_config_ctx.initialized) {
        return false;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);
    bool exists = (find_config_node(key) != NULL);
    xSemaphoreGive(s_config_ctx.mutex);

    return exists;
}

esp_err_t ts_config_get_type(const char *key, ts_config_type_t *type)
{
    if (key == NULL || type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node == NULL) {
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    *type = node->item.type;
    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_OK;
}

esp_err_t ts_config_get_source(const char *key, ts_config_backend_t *backend)
{
    if (key == NULL || backend == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = find_config_node(key);
    if (node == NULL) {
        xSemaphoreGive(s_config_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    *backend = node->item.source;
    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_OK;
}

esp_err_t ts_config_reset(const char *key)
{
    // TODO: 实现重置为默认值
    return ts_config_delete(key);
}

esp_err_t ts_config_reset_all(void)
{
    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    // 释放所有节点
    ts_config_node_t *node = s_config_ctx.config_list;
    while (node != NULL) {
        ts_config_node_t *next = node->next;
        free_config_node(node);
        node = next;
    }
    s_config_ctx.config_list = NULL;
    s_config_ctx.config_count = 0;

    xSemaphoreGive(s_config_ctx.mutex);

    s_config_ctx.dirty = true;
    schedule_auto_save();

    ESP_LOGI(TAG, "All configurations reset");
    return ESP_OK;
}

esp_err_t ts_config_save(void)
{
    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Saving configuration...");

    // 保存到 NVS 后端
    if (s_config_ctx.backends[TS_CONFIG_BACKEND_NVS].registered) {
        esp_err_t ret = ts_config_save_to_backend(TS_CONFIG_BACKEND_NVS);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save to NVS: %s", esp_err_to_name(ret));
        }
    }

    s_config_ctx.dirty = false;
    ESP_LOGI(TAG, "Configuration saved");
    return ESP_OK;
}

/* ============================================================================
 * 配置监听器
 * ========================================================================== */

esp_err_t ts_config_add_listener(const char *key_prefix,
                                  ts_config_listener_t listener,
                                  void *user_data,
                                  ts_config_listener_handle_t *handle)
{
    if (listener == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_config_ctx.listener_count >= TS_CONFIG_LISTENERS_MAX) {
        return ESP_ERR_NO_MEM;
    }

    ts_config_listener_node_t *node = calloc(1, sizeof(ts_config_listener_node_t));
    if (node == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (key_prefix != NULL) {
        node->key_prefix = strdup(key_prefix);
        if (node->key_prefix == NULL) {
            free(node);
            return ESP_ERR_NO_MEM;
        }
    }

    node->callback = listener;
    node->user_data = user_data;

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    node->next = s_config_ctx.listener_list;
    s_config_ctx.listener_list = node;
    s_config_ctx.listener_count++;

    xSemaphoreGive(s_config_ctx.mutex);

    if (handle != NULL) {
        *handle = (ts_config_listener_handle_t)node;
    }

    ESP_LOGD(TAG, "Added listener for prefix: %s", key_prefix ? key_prefix : "*");
    return ESP_OK;
}

esp_err_t ts_config_remove_listener(ts_config_listener_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_listener_node_t *prev = NULL;
    ts_config_listener_node_t *node = s_config_ctx.listener_list;
    ts_config_listener_node_t *target = (ts_config_listener_node_t *)handle;

    while (node != NULL) {
        if (node == target) {
            if (prev == NULL) {
                s_config_ctx.listener_list = node->next;
            } else {
                prev->next = node->next;
            }
            s_config_ctx.listener_count--;

            xSemaphoreGive(s_config_ctx.mutex);

            if (node->key_prefix) {
                free(node->key_prefix);
            }
            free(node);

            return ESP_OK;
        }
        prev = node;
        node = node->next;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    return ESP_ERR_NOT_FOUND;
}

/* ============================================================================
 * 后端管理
 * ========================================================================== */

esp_err_t ts_config_register_backend(ts_config_backend_t backend,
                                      const ts_config_backend_ops_t *ops,
                                      uint8_t priority)
{
    if (backend >= TS_CONFIG_BACKEND_MAX || ops == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_config_ctx.backends[backend].registered) {
        ESP_LOGW(TAG, "Backend %d already registered", backend);
        return ESP_ERR_INVALID_STATE;
    }

    s_config_ctx.backends[backend].ops = ops;
    s_config_ctx.backends[backend].priority = priority;
    s_config_ctx.backends[backend].registered = true;

    // 初始化后端
    if (ops->init != NULL) {
        esp_err_t ret = ops->init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize backend %d: %s", backend, esp_err_to_name(ret));
            s_config_ctx.backends[backend].registered = false;
            return ret;
        }
    }

    ESP_LOGI(TAG, "Registered backend %d with priority %d", backend, priority);
    return ESP_OK;
}

esp_err_t ts_config_load_from_backend(ts_config_backend_t backend)
{
    if (backend >= TS_CONFIG_BACKEND_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.backends[backend].registered) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Loading configuration from backend %d", backend);
    // TODO: 实现从后端加载配置
    return ESP_OK;
}

esp_err_t ts_config_save_to_backend(ts_config_backend_t backend)
{
    if (backend >= TS_CONFIG_BACKEND_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_config_ctx.backends[backend].registered) {
        return ESP_ERR_INVALID_STATE;
    }

    const ts_config_backend_ops_t *ops = s_config_ctx.backends[backend].ops;
    if (ops == NULL || ops->set == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = s_config_ctx.config_list;
    while (node != NULL) {
        size_t size = get_type_size(node->item.type);
        if (node->item.type == TS_CONFIG_TYPE_STRING && node->item.value.val_string) {
            size = strlen(node->item.value.val_string) + 1;
        } else if (node->item.type == TS_CONFIG_TYPE_BLOB) {
            size = node->item.value.val_blob.size;
        }

        esp_err_t ret = ops->set(node->item.key, node->item.type, &node->item.value, size);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save key %s: %s", node->item.key, esp_err_to_name(ret));
        }
        node = node->next;
    }

    if (ops->commit) {
        ops->commit();
    }

    xSemaphoreGive(s_config_ctx.mutex);

    ESP_LOGI(TAG, "Saved configuration to backend %d", backend);
    return ESP_OK;
}

/* ============================================================================
 * 调试和诊断
 * ========================================================================== */

void ts_config_dump(void)
{
    if (!s_config_ctx.initialized) {
        ESP_LOGW(TAG, "Configuration system not initialized");
        return;
    }

    ESP_LOGI(TAG, "=== Configuration Dump ===");
    ESP_LOGI(TAG, "Total items: %d", s_config_ctx.config_count);

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_node_t *node = s_config_ctx.config_list;
    while (node != NULL) {
        const char *type_str;
        switch (node->item.type) {
            case TS_CONFIG_TYPE_BOOL:   type_str = "bool"; break;
            case TS_CONFIG_TYPE_INT32:  type_str = "i32"; break;
            case TS_CONFIG_TYPE_UINT32: type_str = "u32"; break;
            case TS_CONFIG_TYPE_INT64:  type_str = "i64"; break;
            case TS_CONFIG_TYPE_FLOAT:  type_str = "float"; break;
            case TS_CONFIG_TYPE_DOUBLE: type_str = "double"; break;
            case TS_CONFIG_TYPE_STRING: type_str = "string"; break;
            case TS_CONFIG_TYPE_BLOB:   type_str = "blob"; break;
            default:                    type_str = "?"; break;
        }

        ESP_LOGI(TAG, "  [%s] %s = ...", type_str, node->item.key);
        node = node->next;
    }

    xSemaphoreGive(s_config_ctx.mutex);
    ESP_LOGI(TAG, "=========================");
}

void ts_config_get_stats(size_t *total_count, size_t *nvs_count, size_t *file_count)
{
    if (total_count) *total_count = s_config_ctx.config_count;
    // TODO: 分别统计各后端的配置数量
    if (nvs_count) *nvs_count = 0;
    if (file_count) *file_count = 0;
}

/* ============================================================================
 * 私有函数实现
 * ========================================================================== */

static ts_config_node_t *find_config_node(const char *key)
{
    ts_config_node_t *node = s_config_ctx.config_list;
    while (node != NULL) {
        if (strcmp(node->item.key, key) == 0) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

static ts_config_node_t *create_config_node(const char *key, ts_config_type_t type)
{
    ts_config_node_t *node = calloc(1, sizeof(ts_config_node_t));
    if (node == NULL) {
        return NULL;
    }

    strncpy(node->item.key, key, TS_CONFIG_KEY_MAX_LEN - 1);
    node->item.key[TS_CONFIG_KEY_MAX_LEN - 1] = '\0';
    node->item.type = type;
    node->item.source = TS_CONFIG_BACKEND_DEFAULT;
    node->next = NULL;

    return node;
}

static void free_config_node(ts_config_node_t *node)
{
    if (node == NULL) return;

    free_value(&node->item.value, node->item.type);
    free(node);
}

static void notify_listeners(const ts_config_change_t *change)
{
    if (change == NULL) return;

    xSemaphoreTake(s_config_ctx.mutex, portMAX_DELAY);

    ts_config_listener_node_t *listener = s_config_ctx.listener_list;
    while (listener != NULL) {
        bool match = false;
        if (listener->key_prefix == NULL) {
            match = true;  // 监听所有
        } else {
            match = (strncmp(change->key, listener->key_prefix, 
                            strlen(listener->key_prefix)) == 0);
        }

        if (match && listener->callback) {
            // 注意：回调在持有锁时调用，回调不应阻塞
            listener->callback(change, listener->user_data);
        }

        listener = listener->next;
    }

    xSemaphoreGive(s_config_ctx.mutex);
}

static void auto_save_callback(TimerHandle_t timer)
{
    (void)timer;
    if (s_config_ctx.dirty) {
        ts_config_save();
    }
}

static void schedule_auto_save(void)
{
#ifdef CONFIG_TS_CONFIG_AUTO_SAVE
    if (s_config_ctx.save_timer != NULL) {
        xTimerReset(s_config_ctx.save_timer, 0);
    }
#endif
}

static size_t get_type_size(ts_config_type_t type)
{
    switch (type) {
        case TS_CONFIG_TYPE_BOOL:   return sizeof(bool);
        case TS_CONFIG_TYPE_INT8:   return sizeof(int8_t);
        case TS_CONFIG_TYPE_UINT8:  return sizeof(uint8_t);
        case TS_CONFIG_TYPE_INT16:  return sizeof(int16_t);
        case TS_CONFIG_TYPE_UINT16: return sizeof(uint16_t);
        case TS_CONFIG_TYPE_INT32:  return sizeof(int32_t);
        case TS_CONFIG_TYPE_UINT32: return sizeof(uint32_t);
        case TS_CONFIG_TYPE_INT64:  return sizeof(int64_t);
        case TS_CONFIG_TYPE_UINT64: return sizeof(uint64_t);
        case TS_CONFIG_TYPE_FLOAT:  return sizeof(float);
        case TS_CONFIG_TYPE_DOUBLE: return sizeof(double);
        default: return 0;
    }
}

static bool copy_value(ts_config_value_t *dst, const ts_config_value_t *src,
                       ts_config_type_t type, size_t size)
{
    switch (type) {
        case TS_CONFIG_TYPE_BOOL:
            dst->val_bool = src->val_bool;
            break;
        case TS_CONFIG_TYPE_INT8:
            dst->val_i8 = src->val_i8;
            break;
        case TS_CONFIG_TYPE_UINT8:
            dst->val_u8 = src->val_u8;
            break;
        case TS_CONFIG_TYPE_INT16:
            dst->val_i16 = src->val_i16;
            break;
        case TS_CONFIG_TYPE_UINT16:
            dst->val_u16 = src->val_u16;
            break;
        case TS_CONFIG_TYPE_INT32:
            dst->val_i32 = src->val_i32;
            break;
        case TS_CONFIG_TYPE_UINT32:
            dst->val_u32 = src->val_u32;
            break;
        case TS_CONFIG_TYPE_INT64:
            dst->val_i64 = src->val_i64;
            break;
        case TS_CONFIG_TYPE_UINT64:
            dst->val_u64 = src->val_u64;
            break;
        case TS_CONFIG_TYPE_FLOAT:
            dst->val_float = src->val_float;
            break;
        case TS_CONFIG_TYPE_DOUBLE:
            dst->val_double = src->val_double;
            break;
        case TS_CONFIG_TYPE_STRING:
            if (src->val_string != NULL) {
                // 释放旧值
                if (dst->val_string != NULL) {
                    free(dst->val_string);
                }
                dst->val_string = strdup(src->val_string);
                if (dst->val_string == NULL) {
                    return false;
                }
            }
            break;
        case TS_CONFIG_TYPE_BLOB:
            if (src->val_blob.data != NULL && src->val_blob.size > 0) {
                // 释放旧值
                if (dst->val_blob.data != NULL) {
                    free(dst->val_blob.data);
                }
                dst->val_blob.data = malloc(src->val_blob.size);
                if (dst->val_blob.data == NULL) {
                    return false;
                }
                memcpy(dst->val_blob.data, src->val_blob.data, src->val_blob.size);
                dst->val_blob.size = src->val_blob.size;
            }
            break;
        default:
            return false;
    }
    return true;
}

static void free_value(ts_config_value_t *value, ts_config_type_t type)
{
    if (value == NULL) return;

    switch (type) {
        case TS_CONFIG_TYPE_STRING:
            if (value->val_string != NULL) {
                free(value->val_string);
                value->val_string = NULL;
            }
            break;
        case TS_CONFIG_TYPE_BLOB:
            if (value->val_blob.data != NULL) {
                free(value->val_blob.data);
                value->val_blob.data = NULL;
                value->val_blob.size = 0;
            }
            break;
        default:
            break;
    }
}
