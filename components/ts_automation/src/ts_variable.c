/**
 * @file ts_variable.c
 * @brief TianShanOS 自动化引擎 - 变量存储实现
 *
 * 提供层级化的变量存储系统：
 * - 支持 bool/int/float/string 类型
 * - 变量变化通过事件总线通知
 * - 可选持久化到 NVS
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_variable.h"
#include "ts_event.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "ts_variable";

/*===========================================================================*/
/*                              配置常量                                      */
/*===========================================================================*/

#ifndef CONFIG_TS_AUTOMATION_MAX_VARIABLES
#define CONFIG_TS_AUTOMATION_MAX_VARIABLES  128
#endif

/*===========================================================================*/
/*                              内部状态                                      */
/*===========================================================================*/

typedef struct {
    ts_auto_variable_t *variables;       // 变量数组
    int count;                           // 当前变量数量
    int capacity;                        // 最大容量
    SemaphoreHandle_t mutex;             // 访问互斥锁
    bool initialized;                    // 初始化标志
} ts_variable_ctx_t;

static ts_variable_ctx_t s_var_ctx = {
    .variables = NULL,
    .count = 0,
    .capacity = 0,
    .mutex = NULL,
    .initialized = false,
};

/*===========================================================================*/
/*                              辅助函数                                      */
/*===========================================================================*/

/**
 * 查找变量索引
 * @return 找到返回索引，否则返回 -1
 */
static int find_variable_index(const char *name)
{
    for (int i = 0; i < s_var_ctx.count; i++) {
        if (strcmp(s_var_ctx.variables[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * 比较两个值是否相等
 */
static bool value_equal(const ts_auto_value_t *a, const ts_auto_value_t *b)
{
    if (a->type != b->type) {
        return false;
    }

    switch (a->type) {
        case TS_AUTO_VAL_NULL:
            return true;
        case TS_AUTO_VAL_BOOL:
            return a->bool_val == b->bool_val;
        case TS_AUTO_VAL_INT:
            return a->int_val == b->int_val;
        case TS_AUTO_VAL_FLOAT:
            // 浮点比较使用小容差
            return (a->float_val - b->float_val) < 0.0001 && 
                   (a->float_val - b->float_val) > -0.0001;
        case TS_AUTO_VAL_STRING:
            return strcmp(a->str_val, b->str_val) == 0;
        default:
            return false;
    }
}

/**
 * 发送变量变化事件
 */
static void notify_change(const char *name, 
                          const ts_auto_value_t *old_val,
                          const ts_auto_value_t *new_val)
{
    (void)old_val;  // 暂未使用，预留接口
    (void)new_val;  // 暂未使用，预留接口

    // TODO: 通过事件总线通知（事件系统扩展后启用）
    // ts_variable_change_event_t event = {
    //     .name = name,
    //     .old_value = *old_val,
    //     .new_value = *new_val,
    // };
    // ts_event_post(TS_EVENT_BASE_AUTOMATION, TS_EVENT_VAR_CHANGED, 
    //               &event, sizeof(event), 0);

    ESP_LOGD(TAG, "Variable '%s' changed", name);
}

/*===========================================================================*/
/*                              初始化                                        */
/*===========================================================================*/

esp_err_t ts_variable_init(void)
{
    if (s_var_ctx.initialized) {
        ESP_LOGD(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGD(TAG, "Initializing variable storage (max %d)", 
             CONFIG_TS_AUTOMATION_MAX_VARIABLES);

    // 分配变量数组（使用 PSRAM）
    s_var_ctx.capacity = CONFIG_TS_AUTOMATION_MAX_VARIABLES;
    size_t alloc_size = s_var_ctx.capacity * sizeof(ts_auto_variable_t);
    
    s_var_ctx.variables = heap_caps_malloc(alloc_size, MALLOC_CAP_SPIRAM);
    if (!s_var_ctx.variables) {
        // Fallback 到 DRAM
        s_var_ctx.variables = malloc(alloc_size);
        if (!s_var_ctx.variables) {
            ESP_LOGE(TAG, "Failed to allocate variable storage");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGD(TAG, "Using DRAM for variable storage");
    }

    memset(s_var_ctx.variables, 0, alloc_size);

    // 创建互斥锁
    s_var_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_var_ctx.mutex) {
        free(s_var_ctx.variables);
        s_var_ctx.variables = NULL;
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    s_var_ctx.count = 0;
    s_var_ctx.initialized = true;

    ESP_LOGD(TAG, "Variable storage initialized");
    return ESP_OK;
}

esp_err_t ts_variable_deinit(void)
{
    if (!s_var_ctx.initialized) {
        return ESP_OK;
    }

    /* 清理所有变量和回调 */
    if (s_var_ctx.mutex) {
        xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);
    }

    if (s_var_ctx.variables) {
        free(s_var_ctx.variables);
        s_var_ctx.variables = NULL;
    }

    s_var_ctx.count = 0;
    s_var_ctx.initialized = false;

    if (s_var_ctx.mutex) {
        xSemaphoreGive(s_var_ctx.mutex);
        vSemaphoreDelete(s_var_ctx.mutex);
        s_var_ctx.mutex = NULL;
    }

    ESP_LOGD(TAG, "Variable storage deinitialized");
    return ESP_OK;
}

bool ts_variable_is_initialized(void)
{
    return s_var_ctx.initialized;
}

/*===========================================================================*/
/*                              变量注册                                      */
/*===========================================================================*/

esp_err_t ts_variable_register(const ts_auto_variable_t *var)
{
    if (!var || !var->name[0]) {
        ESP_LOGE(TAG, "ts_variable_register: invalid arg (var=%p, name=%s)", 
                 var, var ? var->name : "NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_var_ctx.initialized) {
        ESP_LOGE(TAG, "ts_variable_register: not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);
    int64_t now_ms = esp_timer_get_time() / 1000;

    // 检查是否已存在
    int idx = find_variable_index(var->name);
    if (idx >= 0) {
        // 更新现有变量
        int64_t last_change_ms = s_var_ctx.variables[idx].last_change_ms;
        int64_t last_update_ms = s_var_ctx.variables[idx].last_update_ms;
        if (!value_equal(&s_var_ctx.variables[idx].value, &var->value)) {
            last_change_ms = now_ms;
            last_update_ms = 0;
        }
        memcpy(&s_var_ctx.variables[idx], var, sizeof(ts_auto_variable_t));
        s_var_ctx.variables[idx].last_change_ms = last_change_ms;
        s_var_ctx.variables[idx].last_update_ms = last_update_ms;
        xSemaphoreGive(s_var_ctx.mutex);
        ESP_LOGD(TAG, "Updated variable: %s (type=%d)", var->name, var->value.type);
        return ESP_OK;
    }

    // 检查容量
    if (s_var_ctx.count >= s_var_ctx.capacity) {
        xSemaphoreGive(s_var_ctx.mutex);
        ESP_LOGE(TAG, "Variable storage full (count=%d, capacity=%d)", 
                 s_var_ctx.count, s_var_ctx.capacity);
        return ESP_ERR_NO_MEM;
    }

    // 添加新变量
    memcpy(&s_var_ctx.variables[s_var_ctx.count], var, sizeof(ts_auto_variable_t));
    s_var_ctx.variables[s_var_ctx.count].last_change_ms = now_ms;
    s_var_ctx.variables[s_var_ctx.count].last_update_ms = 0;
    s_var_ctx.count++;

    xSemaphoreGive(s_var_ctx.mutex);

    ESP_LOGD(TAG, "Registered variable: %s (type=%d, total: %d)", 
             var->name, var->value.type, s_var_ctx.count);
    return ESP_OK;
}

esp_err_t ts_variable_upsert(const ts_auto_variable_t *var)
{
    if (!var || !var->name[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_var_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ts_auto_value_t old_value = {0};
    bool changed = false;
    bool existed = false;
    int64_t now_ms = esp_timer_get_time() / 1000;

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);

    int idx = find_variable_index(var->name);
    if (idx >= 0) {
        existed = true;
        old_value = s_var_ctx.variables[idx].value;
        changed = !value_equal(&old_value, &var->value);

        int64_t last_change_ms = changed
                                 ? now_ms
                                 : s_var_ctx.variables[idx].last_change_ms;
        memcpy(&s_var_ctx.variables[idx], var, sizeof(ts_auto_variable_t));
        s_var_ctx.variables[idx].last_change_ms = last_change_ms;
        s_var_ctx.variables[idx].last_update_ms = now_ms;
    } else {
        if (s_var_ctx.count >= s_var_ctx.capacity) {
            xSemaphoreGive(s_var_ctx.mutex);
            return ESP_ERR_NO_MEM;
        }

        memcpy(&s_var_ctx.variables[s_var_ctx.count], var, sizeof(ts_auto_variable_t));
        s_var_ctx.variables[s_var_ctx.count].last_change_ms = now_ms;
        s_var_ctx.variables[s_var_ctx.count].last_update_ms = now_ms;
        s_var_ctx.count++;
    }

    xSemaphoreGive(s_var_ctx.mutex);

    if (existed && changed) {
        notify_change(var->name, &old_value, &var->value);
    }

    return ESP_OK;
}

esp_err_t ts_variable_unregister(const char *name)
{
    if (!name) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_var_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);

    int idx = find_variable_index(name);
    if (idx < 0) {
        xSemaphoreGive(s_var_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // 移动后续元素
    if (idx < s_var_ctx.count - 1) {
        memmove(&s_var_ctx.variables[idx], 
                &s_var_ctx.variables[idx + 1],
                (s_var_ctx.count - idx - 1) * sizeof(ts_auto_variable_t));
    }
    s_var_ctx.count--;

    xSemaphoreGive(s_var_ctx.mutex);

    ESP_LOGD(TAG, "Unregistered variable: %s", name);
    return ESP_OK;
}

int ts_variable_unregister_by_source(const char *source_id)
{
    if (!source_id || !s_var_ctx.initialized) {
        return 0;
    }

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);

    int removed = 0;
    // 从后向前遍历，避免删除时索引混乱
    for (int i = s_var_ctx.count - 1; i >= 0; i--) {
        if (strcmp(s_var_ctx.variables[i].source_id, source_id) == 0) {
            // 移动后续元素
            if (i < s_var_ctx.count - 1) {
                memmove(&s_var_ctx.variables[i],
                        &s_var_ctx.variables[i + 1],
                        (s_var_ctx.count - i - 1) * sizeof(ts_auto_variable_t));
            }
            s_var_ctx.count--;
            removed++;
        }
    }

    xSemaphoreGive(s_var_ctx.mutex);

    if (removed > 0) {
        ESP_LOGD(TAG, "Removed %d variables for source: %s", removed, source_id);
    }
    return removed;
}

bool ts_variable_exists(const char *name)
{
    if (!name || !s_var_ctx.initialized) {
        return false;
    }

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);
    bool exists = (find_variable_index(name) >= 0);
    xSemaphoreGive(s_var_ctx.mutex);

    return exists;
}

/*===========================================================================*/
/*                              值访问                                        */
/*===========================================================================*/

esp_err_t ts_variable_get(const char *name, ts_auto_value_t *value)
{
    if (!name || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_var_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);

    int idx = find_variable_index(name);
    if (idx < 0) {
        xSemaphoreGive(s_var_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    *value = s_var_ctx.variables[idx].value;

    xSemaphoreGive(s_var_ctx.mutex);
    return ESP_OK;
}

esp_err_t ts_variable_get_bool(const char *name, bool *value)
{
    ts_auto_value_t val;
    esp_err_t ret = ts_variable_get(name, &val);
    if (ret != ESP_OK) {
        return ret;
    }

    switch (val.type) {
        case TS_AUTO_VAL_BOOL:
            *value = val.bool_val;
            break;
        case TS_AUTO_VAL_INT:
            *value = (val.int_val != 0);
            break;
        case TS_AUTO_VAL_FLOAT:
            *value = (val.float_val != 0.0);
            break;
        default:
            return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t ts_variable_get_int(const char *name, int32_t *value)
{
    ts_auto_value_t val;
    esp_err_t ret = ts_variable_get(name, &val);
    if (ret != ESP_OK) {
        return ret;
    }

    switch (val.type) {
        case TS_AUTO_VAL_INT:
            *value = val.int_val;
            break;
        case TS_AUTO_VAL_FLOAT:
            *value = (int32_t)val.float_val;
            break;
        case TS_AUTO_VAL_BOOL:
            *value = val.bool_val ? 1 : 0;
            break;
        default:
            return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t ts_variable_get_float(const char *name, double *value)
{
    ts_auto_value_t val;
    esp_err_t ret = ts_variable_get(name, &val);
    if (ret != ESP_OK) {
        return ret;
    }

    switch (val.type) {
        case TS_AUTO_VAL_FLOAT:
            *value = val.float_val;
            break;
        case TS_AUTO_VAL_INT:
            *value = (double)val.int_val;
            break;
        default:
            return ESP_ERR_INVALID_STATE;
    }

    return ESP_OK;
}

esp_err_t ts_variable_get_string(const char *name, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ts_auto_value_t val;
    esp_err_t ret = ts_variable_get(name, &val);
    if (ret != ESP_OK) {
        return ret;
    }

    switch (val.type) {
        case TS_AUTO_VAL_STRING:
            strncpy(buffer, val.str_val, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            break;
        case TS_AUTO_VAL_INT:
            snprintf(buffer, buffer_size, "%"PRId32, val.int_val);
            break;
        case TS_AUTO_VAL_FLOAT:
            snprintf(buffer, buffer_size, "%.2f", val.float_val);
            break;
        case TS_AUTO_VAL_BOOL:
            strncpy(buffer, val.bool_val ? "true" : "false", buffer_size - 1);
            break;
        default:
            buffer[0] = '\0';
            break;
    }

    return ESP_OK;
}

static esp_err_t variable_get_info_impl(const char *name, ts_variable_info_t *info,
                                        TickType_t ticks_to_wait)
{
    if (!name || !info) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_var_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_var_ctx.mutex, ticks_to_wait) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    int idx = find_variable_index(name);
    if (idx < 0) {
        xSemaphoreGive(s_var_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    memset(info, 0, sizeof(ts_variable_info_t));
    strncpy(info->name, s_var_ctx.variables[idx].name, sizeof(info->name) - 1);
    strncpy(info->source_id, s_var_ctx.variables[idx].source_id, sizeof(info->source_id) - 1);
    info->value = s_var_ctx.variables[idx].value;
    info->flags = s_var_ctx.variables[idx].flags;
    info->last_change_ms = s_var_ctx.variables[idx].last_change_ms;
    info->last_update_ms = s_var_ctx.variables[idx].last_update_ms;

    xSemaphoreGive(s_var_ctx.mutex);
    return ESP_OK;
}

esp_err_t ts_variable_get_info(const char *name, ts_variable_info_t *info)
{
    return variable_get_info_impl(name, info, portMAX_DELAY);
}

esp_err_t ts_variable_get_info_try(const char *name, ts_variable_info_t *info)
{
    return variable_get_info_impl(name, info, 0);
}

/*===========================================================================*/
/*                              值修改                                        */
/*===========================================================================*/

/**
 * @brief 内部设置函数，可选择绕过只读检查
 */
static esp_err_t variable_set_impl(const char *name, const ts_auto_value_t *value, bool check_readonly)
{
    if (!name || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_var_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);

    int idx = find_variable_index(name);
    if (idx < 0) {
        xSemaphoreGive(s_var_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    ts_auto_variable_t *var = &s_var_ctx.variables[idx];

    // 检查只读标志（仅当 check_readonly 为 true 时）
    if (check_readonly && (var->flags & TS_AUTO_VAR_READONLY)) {
        xSemaphoreGive(s_var_ctx.mutex);
        ESP_LOGD(TAG, "Variable '%s' is read-only", name);
        return ESP_ERR_NOT_ALLOWED;
    }

    int64_t now_ms = esp_timer_get_time() / 1000;
    var->last_update_ms = now_ms;

    // 检查值是否变化
    if (!value_equal(&var->value, value)) {
        ts_auto_value_t old_value = var->value;
        var->value = *value;
        var->last_change_ms = now_ms;

        xSemaphoreGive(s_var_ctx.mutex);

        // 发送变化通知
        notify_change(name, &old_value, value);
    } else {
        xSemaphoreGive(s_var_ctx.mutex);
    }

    return ESP_OK;
}

esp_err_t ts_variable_set(const char *name, const ts_auto_value_t *value)
{
    return variable_set_impl(name, value, true);  // 检查只读
}

esp_err_t ts_variable_set_internal(const char *name, const ts_auto_value_t *value)
{
    return variable_set_impl(name, value, false);  // 不检查只读
}

esp_err_t ts_variable_set_bool(const char *name, bool value)
{
    ts_auto_value_t val = {
        .type = TS_AUTO_VAL_BOOL,
        .bool_val = value,
    };
    return ts_variable_set(name, &val);
}

esp_err_t ts_variable_set_int(const char *name, int32_t value)
{
    ts_auto_value_t val = {
        .type = TS_AUTO_VAL_INT,
        .int_val = value,
    };
    return ts_variable_set(name, &val);
}

esp_err_t ts_variable_set_float(const char *name, double value)
{
    ts_auto_value_t val = {
        .type = TS_AUTO_VAL_FLOAT,
        .float_val = value,
    };
    return ts_variable_set(name, &val);
}

esp_err_t ts_variable_set_string(const char *name, const char *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    ts_auto_value_t val = {
        .type = TS_AUTO_VAL_STRING,
    };
    strncpy(val.str_val, value, sizeof(val.str_val) - 1);
    val.str_val[sizeof(val.str_val) - 1] = '\0';

    return ts_variable_set(name, &val);
}

/*===========================================================================*/
/*                              枚举与计数                                    */
/*===========================================================================*/

int ts_variable_enumerate(const char *prefix, ts_variable_enum_cb_t callback, void *user_data)
{
    if (!callback || !s_var_ctx.initialized) {
        return 0;
    }

    int count = 0;
    size_t prefix_len = prefix ? strlen(prefix) : 0;

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);

    for (int i = 0; i < s_var_ctx.count; i++) {
        // 前缀过滤
        if (prefix_len > 0) {
            if (strncmp(s_var_ctx.variables[i].name, prefix, prefix_len) != 0) {
                continue;
            }
        }

        count++;
        
        // 回调
        if (!callback(&s_var_ctx.variables[i], user_data)) {
            break;  // 回调返回 false，停止枚举
        }
    }

    xSemaphoreGive(s_var_ctx.mutex);
    return count;
}

int ts_variable_count(void)
{
    if (!s_var_ctx.initialized) {
        return 0;
    }

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);
    int count = s_var_ctx.count;
    xSemaphoreGive(s_var_ctx.mutex);

    return count;
}

esp_err_t ts_variable_iterate(ts_variable_iterate_ctx_t *ctx, ts_auto_variable_t *var)
{
    if (!ctx || !var) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_var_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_var_ctx.mutex, portMAX_DELAY);

    if (ctx->index >= s_var_ctx.count) {
        xSemaphoreGive(s_var_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // 复制变量到输出
    memcpy(var, &s_var_ctx.variables[ctx->index], sizeof(ts_auto_variable_t));

    // 字符串需要深拷贝吗？这里只是浅拷贝，调用者需要注意
    // 对于安全性，可以在持有锁期间使用

    ctx->index++;
    xSemaphoreGive(s_var_ctx.mutex);

    return ESP_OK;
}

/*===========================================================================*/
/*                              持久化                                        */
/*===========================================================================*/

esp_err_t ts_variable_save_all(void)
{
    if (!s_var_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Saving persistent variables to NVS");

    // TODO: 实现 NVS 持久化
    // 遍历所有带 TS_AUTO_VAR_PERSISTENT 标志的变量
    // 保存到 NVS "ts_var" 命名空间

    return ESP_OK;
}

esp_err_t ts_variable_load_all(void)
{
    if (!s_var_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Loading persistent variables from NVS");

    // TODO: 实现 NVS 加载

    return ESP_OK;
}

/*===========================================================================*/
/*                              JSON 导入导出                                  */
/*===========================================================================*/

int ts_variable_export_json(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0 || !s_var_ctx.initialized) {
        return -1;
    }

    // TODO: 实现完整的 JSON 导出
    // 格式: {"variables":[{"name":"xxx","value":123,"type":"int"},...]}

    int written = snprintf(buffer, buffer_size, "{\"variables\":[]}");
    return (written < (int)buffer_size) ? written : -1;
}

esp_err_t ts_variable_import_json(const char *json)
{
    if (!json || !s_var_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    // TODO: 实现 JSON 导入

    return ESP_OK;
}
