/**
 * @file ts_rule_engine.c
 * @brief TianShanOS 自动化引擎 - 规则引擎实现
 *
 * 规则引擎负责：
 * - 条件评估（比较操作符、逻辑组合）
 * - 动作执行（LED、SSH、GPIO、Webhook 等）
 * - 冷却时间管理
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_rule_engine.h"
#include "ts_variable.h"
#include "ts_config_module.h"
#include "ts_storage.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// 动作执行依赖
#include "ts_led.h"
#include "ts_hal_gpio.h"
#include "ts_device_ctrl.h"
#include "ts_ssh_client.h"
#include "ts_action_manager.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "ts_rule_engine";

/*===========================================================================*/
/*                              配置常量                                      */
/*===========================================================================*/

#ifndef CONFIG_TS_AUTOMATION_MAX_RULES
#define CONFIG_TS_AUTOMATION_MAX_RULES  32
#endif

/** NVS namespace for rules */
#define NVS_NAMESPACE_RULES         "auto_rules"

/** NVS key for rule count */
#define NVS_KEY_RULE_COUNT          "count"

/** NVS key prefix for rules */
#define NVS_KEY_RULE_PREFIX         "rule_"

/*===========================================================================*/
/*                              内部状态                                      */
/*===========================================================================*/

typedef struct {
    ts_auto_rule_t *rules;               // 规则数组
    int count;                           // 当前规则数量
    int capacity;                        // 最大容量
    SemaphoreHandle_t mutex;             // 访问互斥锁
    bool initialized;                    // 初始化标志
    ts_rule_engine_stats_t stats;        // 统计信息
} ts_rule_engine_ctx_t;

static ts_rule_engine_ctx_t s_rule_ctx = {
    .rules = NULL,
    .count = 0,
    .capacity = 0,
    .mutex = NULL,
    .initialized = false,
};

/*===========================================================================*/
/*                          静态函数声明                                      */
/*===========================================================================*/

static int find_rule_index(const char *id);
static int compare_values(const ts_auto_value_t *a, const ts_auto_value_t *b);
static esp_err_t execute_led_action(const ts_auto_action_t *action);
static esp_err_t execute_gpio_action(const ts_auto_action_t *action);
static esp_err_t execute_device_action(const ts_auto_action_t *action);
static esp_err_t execute_ssh_action(const ts_auto_action_t *action);
static esp_err_t execute_ssh_ref_action(const ts_auto_action_t *action);
static esp_err_t execute_cli_action(const ts_auto_action_t *action);
static esp_err_t execute_webhook_action(const ts_auto_action_t *action);

/*===========================================================================*/
/*                              辅助函数                                      */
/*===========================================================================*/

/**
 * 查找规则索引
 */
static int find_rule_index(const char *id)
{
    for (int i = 0; i < s_rule_ctx.count; i++) {
        if (strcmp(s_rule_ctx.rules[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * 比较两个值
 * @return <0: a<b, 0: a==b, >0: a>b
 */
static int compare_values(const ts_auto_value_t *a, const ts_auto_value_t *b)
{
    // 类型不同时尝试转换比较
    if (a->type != b->type) {
        // 简化处理：都转为 float 比较
        double va = 0, vb = 0;
        
        switch (a->type) {
            case TS_AUTO_VAL_INT: va = a->int_val; break;
            case TS_AUTO_VAL_FLOAT: va = a->float_val; break;
            case TS_AUTO_VAL_BOOL: va = a->bool_val ? 1 : 0; break;
            default: return 0;
        }
        
        switch (b->type) {
            case TS_AUTO_VAL_INT: vb = b->int_val; break;
            case TS_AUTO_VAL_FLOAT: vb = b->float_val; break;
            case TS_AUTO_VAL_BOOL: vb = b->bool_val ? 1 : 0; break;
            default: return 0;
        }
        
        if (fabs(va - vb) < 0.0001) return 0;
        return (va < vb) ? -1 : 1;
    }

    // 同类型比较
    switch (a->type) {
        case TS_AUTO_VAL_BOOL:
            return (a->bool_val == b->bool_val) ? 0 : 
                   (a->bool_val ? 1 : -1);
        
        case TS_AUTO_VAL_INT:
            if (a->int_val == b->int_val) return 0;
            return (a->int_val < b->int_val) ? -1 : 1;
        
        case TS_AUTO_VAL_FLOAT:
            if (fabs(a->float_val - b->float_val) < 0.0001) return 0;
            return (a->float_val < b->float_val) ? -1 : 1;
        
        case TS_AUTO_VAL_STRING:
            return strcmp(a->str_val, b->str_val);
        
        default:
            return 0;
    }
}

/*===========================================================================*/
/*                              初始化                                        */
/*===========================================================================*/

esp_err_t ts_rule_engine_init(void)
{
    if (s_rule_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing rule engine (max %d rules)",
             CONFIG_TS_AUTOMATION_MAX_RULES);

    // 分配规则数组
    s_rule_ctx.capacity = CONFIG_TS_AUTOMATION_MAX_RULES;
    size_t alloc_size = s_rule_ctx.capacity * sizeof(ts_auto_rule_t);

    s_rule_ctx.rules = heap_caps_malloc(alloc_size, MALLOC_CAP_SPIRAM);
    if (!s_rule_ctx.rules) {
        s_rule_ctx.rules = malloc(alloc_size);
        if (!s_rule_ctx.rules) {
            ESP_LOGE(TAG, "Failed to allocate rule storage");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "Using DRAM for rule storage");
    }

    memset(s_rule_ctx.rules, 0, alloc_size);

    // 创建互斥锁
    s_rule_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_rule_ctx.mutex) {
        free(s_rule_ctx.rules);
        s_rule_ctx.rules = NULL;
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_rule_ctx.stats, 0, sizeof(s_rule_ctx.stats));
    s_rule_ctx.count = 0;
    s_rule_ctx.initialized = true;

    // 从 NVS 加载已保存的规则
    ts_rules_load();

    ESP_LOGI(TAG, "Rule engine initialized");
    return ESP_OK;
}

esp_err_t ts_rule_engine_deinit(void)
{
    if (!s_rule_ctx.initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing rule engine");

    // 释放规则中的动态分配内存
    for (int i = 0; i < s_rule_ctx.count; i++) {
        if (s_rule_ctx.rules[i].conditions.conditions) {
            free(s_rule_ctx.rules[i].conditions.conditions);
        }
        if (s_rule_ctx.rules[i].actions) {
            free(s_rule_ctx.rules[i].actions);
        }
    }

    if (s_rule_ctx.mutex) {
        vSemaphoreDelete(s_rule_ctx.mutex);
        s_rule_ctx.mutex = NULL;
    }

    if (s_rule_ctx.rules) {
        if (heap_caps_get_allocated_size(s_rule_ctx.rules) > 0) {
            heap_caps_free(s_rule_ctx.rules);
        } else {
            free(s_rule_ctx.rules);
        }
        s_rule_ctx.rules = NULL;
    }

    s_rule_ctx.count = 0;
    s_rule_ctx.capacity = 0;
    s_rule_ctx.initialized = false;

    return ESP_OK;
}

/*===========================================================================*/
/*                              规则管理                                      */
/*===========================================================================*/

esp_err_t ts_rule_register(const ts_auto_rule_t *rule)
{
    if (!rule || !rule->id[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);

    // 检查是否已存在
    int idx = find_rule_index(rule->id);
    if (idx >= 0) {
        // 释放旧规则的动态内存
        if (s_rule_ctx.rules[idx].conditions.conditions) {
            free(s_rule_ctx.rules[idx].conditions.conditions);
            s_rule_ctx.rules[idx].conditions.conditions = NULL;
        }
        if (s_rule_ctx.rules[idx].actions) {
            free(s_rule_ctx.rules[idx].actions);
            s_rule_ctx.rules[idx].actions = NULL;
        }
        
        // 复制基本数据
        strncpy(s_rule_ctx.rules[idx].id, rule->id, sizeof(s_rule_ctx.rules[idx].id) - 1);
        strncpy(s_rule_ctx.rules[idx].name, rule->name, sizeof(s_rule_ctx.rules[idx].name) - 1);
        strncpy(s_rule_ctx.rules[idx].icon, rule->icon, sizeof(s_rule_ctx.rules[idx].icon) - 1);
        s_rule_ctx.rules[idx].enabled = rule->enabled;
        s_rule_ctx.rules[idx].manual_trigger = rule->manual_trigger;
        s_rule_ctx.rules[idx].cooldown_ms = rule->cooldown_ms;
        s_rule_ctx.rules[idx].conditions.logic = rule->conditions.logic;
        s_rule_ctx.rules[idx].conditions.count = 0;
        s_rule_ctx.rules[idx].action_count = 0;
        
        // 深拷贝条件数组
        if (rule->conditions.count > 0 && rule->conditions.conditions) {
            s_rule_ctx.rules[idx].conditions.conditions = heap_caps_calloc(
                rule->conditions.count, sizeof(ts_auto_condition_t), 
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_rule_ctx.rules[idx].conditions.conditions) {
                memcpy(s_rule_ctx.rules[idx].conditions.conditions, 
                       rule->conditions.conditions, 
                       rule->conditions.count * sizeof(ts_auto_condition_t));
                s_rule_ctx.rules[idx].conditions.count = rule->conditions.count;
            }
        }
        
        // 深拷贝动作数组
        if (rule->action_count > 0 && rule->actions) {
            s_rule_ctx.rules[idx].actions = heap_caps_calloc(
                rule->action_count, sizeof(ts_auto_action_t), 
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (s_rule_ctx.rules[idx].actions) {
                memcpy(s_rule_ctx.rules[idx].actions, 
                       rule->actions, 
                       rule->action_count * sizeof(ts_auto_action_t));
                s_rule_ctx.rules[idx].action_count = rule->action_count;
            }
        }
        
        xSemaphoreGive(s_rule_ctx.mutex);
        
        // 保存到 NVS
        ts_rules_save();
        
        ESP_LOGD(TAG, "Updated rule: %s", rule->id);
        return ESP_OK;
    }

    // 检查容量
    if (s_rule_ctx.count >= s_rule_ctx.capacity) {
        xSemaphoreGive(s_rule_ctx.mutex);
        ESP_LOGE(TAG, "Rule storage full");
        return ESP_ERR_NO_MEM;
    }

    // 添加新规则 - 深拷贝
    ts_auto_rule_t *new_rule = &s_rule_ctx.rules[s_rule_ctx.count];
    memset(new_rule, 0, sizeof(ts_auto_rule_t));
    
    // 复制基本数据
    strncpy(new_rule->id, rule->id, sizeof(new_rule->id) - 1);
    strncpy(new_rule->name, rule->name, sizeof(new_rule->name) - 1);
    strncpy(new_rule->icon, rule->icon, sizeof(new_rule->icon) - 1);
    new_rule->enabled = rule->enabled;
    new_rule->manual_trigger = rule->manual_trigger;
    new_rule->cooldown_ms = rule->cooldown_ms;
    new_rule->conditions.logic = rule->conditions.logic;
    
    // 深拷贝条件数组
    if (rule->conditions.count > 0 && rule->conditions.conditions) {
        new_rule->conditions.conditions = heap_caps_calloc(
            rule->conditions.count, sizeof(ts_auto_condition_t), 
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (new_rule->conditions.conditions) {
            memcpy(new_rule->conditions.conditions, 
                   rule->conditions.conditions, 
                   rule->conditions.count * sizeof(ts_auto_condition_t));
            new_rule->conditions.count = rule->conditions.count;
        }
    }
    
    // 深拷贝动作数组
    if (rule->action_count > 0 && rule->actions) {
        new_rule->actions = heap_caps_calloc(
            rule->action_count, sizeof(ts_auto_action_t), 
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (new_rule->actions) {
            memcpy(new_rule->actions, 
                   rule->actions, 
                   rule->action_count * sizeof(ts_auto_action_t));
            new_rule->action_count = rule->action_count;
        }
    }
    
    s_rule_ctx.count++;

    xSemaphoreGive(s_rule_ctx.mutex);

    // 保存到 NVS
    ts_rules_save();

    ESP_LOGI(TAG, "Registered rule: %s (%s)", rule->id, rule->name);
    return ESP_OK;
}

esp_err_t ts_rule_unregister(const char *id)
{
    if (!id || !s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);

    int idx = find_rule_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_rule_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // 释放动态内存
    if (s_rule_ctx.rules[idx].conditions.conditions) {
        free(s_rule_ctx.rules[idx].conditions.conditions);
    }
    if (s_rule_ctx.rules[idx].actions) {
        free(s_rule_ctx.rules[idx].actions);
    }

    // 移动后续元素
    if (idx < s_rule_ctx.count - 1) {
        memmove(&s_rule_ctx.rules[idx],
                &s_rule_ctx.rules[idx + 1],
                (s_rule_ctx.count - idx - 1) * sizeof(ts_auto_rule_t));
    }
    s_rule_ctx.count--;

    xSemaphoreGive(s_rule_ctx.mutex);

    // 保存到 NVS
    ts_rules_save();

    ESP_LOGD(TAG, "Unregistered rule: %s", id);
    return ESP_OK;
}

esp_err_t ts_rule_enable(const char *id)
{
    if (!id || !s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);

    int idx = find_rule_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_rule_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    s_rule_ctx.rules[idx].enabled = true;

    xSemaphoreGive(s_rule_ctx.mutex);
    
    // 保存状态变更到 NVS
    ts_rules_save();
    
    return ESP_OK;
}

esp_err_t ts_rule_disable(const char *id)
{
    if (!id || !s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);

    int idx = find_rule_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_rule_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    s_rule_ctx.rules[idx].enabled = false;

    xSemaphoreGive(s_rule_ctx.mutex);
    
    // 保存状态变更到 NVS
    ts_rules_save();
    
    return ESP_OK;
}

const ts_auto_rule_t *ts_rule_get(const char *id)
{
    if (!id || !s_rule_ctx.initialized) {
        return NULL;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);

    int idx = find_rule_index(id);
    const ts_auto_rule_t *rule = (idx >= 0) ? &s_rule_ctx.rules[idx] : NULL;

    xSemaphoreGive(s_rule_ctx.mutex);
    return rule;
}

int ts_rule_count(void)
{
    if (!s_rule_ctx.initialized) {
        return 0;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
    int count = s_rule_ctx.count;
    xSemaphoreGive(s_rule_ctx.mutex);

    return count;
}

/*===========================================================================*/
/*                              条件评估                                      */
/*===========================================================================*/

bool ts_rule_eval_condition(const ts_auto_condition_t *condition)
{
    if (!condition) {
        return false;
    }

    // 获取变量当前值
    ts_auto_value_t var_value;
    esp_err_t ret = ts_variable_get(condition->variable, &var_value);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Variable '%s' not found", condition->variable);
        return false;
    }

    // 执行比较
    int cmp = compare_values(&var_value, &condition->value);

    switch (condition->op) {
        case TS_AUTO_OP_EQ:
            return (cmp == 0);
        
        case TS_AUTO_OP_NE:
            return (cmp != 0);
        
        case TS_AUTO_OP_LT:
            return (cmp < 0);
        
        case TS_AUTO_OP_LE:
            return (cmp <= 0);
        
        case TS_AUTO_OP_GT:
            return (cmp > 0);
        
        case TS_AUTO_OP_GE:
            return (cmp >= 0);
        
        case TS_AUTO_OP_CONTAINS:
            // 仅字符串支持
            if (var_value.type == TS_AUTO_VAL_STRING &&
                condition->value.type == TS_AUTO_VAL_STRING) {
                return (strstr(var_value.str_val, condition->value.str_val) != NULL);
            }
            return false;
        
        case TS_AUTO_OP_CHANGED:
            // TODO: 需要保存上一次的值来比较
            return false;
        
        case TS_AUTO_OP_CHANGED_TO:
            // TODO: 需要保存上一次的值来比较
            return false;
        
        default:
            return false;
    }
}

bool ts_rule_eval_condition_group(const ts_auto_condition_group_t *group)
{
    if (!group || !group->conditions || group->count == 0) {
        return false;  // 空条件组视为不满足（仅手动触发的规则）
    }

    if (group->logic == TS_AUTO_LOGIC_AND) {
        // AND: 所有条件都必须满足
        for (int i = 0; i < group->count; i++) {
            if (!ts_rule_eval_condition(&group->conditions[i])) {
                return false;
            }
        }
        return true;
    } else {
        // OR: 任一条件满足即可
        for (int i = 0; i < group->count; i++) {
            if (ts_rule_eval_condition(&group->conditions[i])) {
                return true;
            }
        }
        return false;
    }
}

/*===========================================================================*/
/*                              规则评估                                      */
/*===========================================================================*/

esp_err_t ts_rule_evaluate(const char *id, bool *triggered)
{
    if (!id || !triggered || !s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    *triggered = false;

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);

    int idx = find_rule_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_rule_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    ts_auto_rule_t *rule = &s_rule_ctx.rules[idx];

    // 检查是否启用
    if (!rule->enabled) {
        xSemaphoreGive(s_rule_ctx.mutex);
        return ESP_OK;
    }

    // 检查冷却时间
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (rule->cooldown_ms > 0 && rule->last_trigger_ms > 0) {
        if ((now_ms - rule->last_trigger_ms) < rule->cooldown_ms) {
            xSemaphoreGive(s_rule_ctx.mutex);
            return ESP_OK;  // 还在冷却中
        }
    }

    xSemaphoreGive(s_rule_ctx.mutex);

    // 评估条件（在锁外执行，避免死锁）
    bool conditions_met = ts_rule_eval_condition_group(&rule->conditions);

    if (conditions_met) {
        // 执行动作
        ESP_LOGI(TAG, "Rule '%s' triggered", rule->id);

        if (rule->actions && rule->action_count > 0) {
            ts_action_execute_array(rule->actions, rule->action_count, NULL, NULL);
        }

        // 更新触发时间和计数
        xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
        rule->last_trigger_ms = now_ms;
        rule->trigger_count++;
        s_rule_ctx.stats.total_triggers++;
        xSemaphoreGive(s_rule_ctx.mutex);

        *triggered = true;
    }

    return ESP_OK;
}

int ts_rule_evaluate_all(void)
{
    if (!s_rule_ctx.initialized) {
        return 0;
    }

    int triggered = 0;

    s_rule_ctx.stats.total_evaluations++;
    s_rule_ctx.stats.last_evaluation_ms = esp_timer_get_time() / 1000;

    // 获取规则 ID 列表（避免在评估过程中持有锁）
    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
    int count = s_rule_ctx.count;
    xSemaphoreGive(s_rule_ctx.mutex);

    for (int i = 0; i < count; i++) {
        xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
        
        if (i >= s_rule_ctx.count) {
            xSemaphoreGive(s_rule_ctx.mutex);
            break;  // 规则可能被删除
        }
        
        char id[TS_AUTO_NAME_MAX_LEN];
        strncpy(id, s_rule_ctx.rules[i].id, sizeof(id) - 1);
        id[sizeof(id) - 1] = '\0';
        
        xSemaphoreGive(s_rule_ctx.mutex);

        bool was_triggered = false;
        ts_rule_evaluate(id, &was_triggered);
        if (was_triggered) {
            triggered++;
        }
    }

    return triggered;
}

esp_err_t ts_rule_trigger(const char *id)
{
    if (!id || !s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);

    int idx = find_rule_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_rule_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    ts_auto_rule_t *rule = &s_rule_ctx.rules[idx];

    ESP_LOGI(TAG, "Manually triggering rule: %s", rule->id);

    // 执行动作
    if (rule->actions && rule->action_count > 0) {
        xSemaphoreGive(s_rule_ctx.mutex);
        ts_action_execute_array(rule->actions, rule->action_count, NULL, NULL);
        xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
    }

    rule->last_trigger_ms = esp_timer_get_time() / 1000;
    rule->trigger_count++;
    s_rule_ctx.stats.total_triggers++;

    xSemaphoreGive(s_rule_ctx.mutex);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Action 执行器实现                                 */
/*===========================================================================*/

/**
 * @brief 解析 LED 设备名称（支持简短别名）
 */
static const char *resolve_led_device_name(const char *name)
{
    if (!name) return NULL;
    
    /* 支持简短别名 */
    if (strcmp(name, "touch") == 0) return "led_touch";
    if (strcmp(name, "board") == 0) return "led_board";
    if (strcmp(name, "matrix") == 0) return "led_matrix";
    
    /* 也支持完整名 */
    return name;
}

/**
 * @brief 执行 LED 动作
 */
static esp_err_t execute_led_action(const ts_auto_action_t *action)
{
    if (!action || action->type != TS_AUTO_ACT_LED) {
        return ESP_ERR_INVALID_ARG;
    }

    // 解析设备名称
    const char *device_name = resolve_led_device_name(action->led.device);

    ESP_LOGI(TAG, "LED action: device=%s, index=%d, color=#%02X%02X%02X, effect=%s",
             device_name, action->led.index,
             action->led.r, action->led.g, action->led.b,
             action->led.effect[0] ? action->led.effect : "(none)");

    // 获取设备句柄
    ts_led_device_t device = ts_led_device_get(device_name);
    if (!device) {
        ESP_LOGW(TAG, "LED device '%s' not found", device_name);
        return ESP_ERR_NOT_FOUND;
    }

    // 获取默认 layer
    ts_led_layer_t layer = ts_led_layer_get(device, 0);
    if (!layer) {
        ESP_LOGW(TAG, "LED layer not found for device '%s'", device_name);
        return ESP_ERR_NOT_FOUND;
    }

    // 如果有效果名，启动效果动画
    if (action->led.effect[0]) {
        const ts_led_animation_def_t *anim = ts_led_animation_get_builtin(action->led.effect);
        if (anim) {
            ESP_LOGI(TAG, "Starting effect '%s' on device '%s'", action->led.effect, device_name);
            return ts_led_animation_start(layer, anim);
        } else {
            ESP_LOGW(TAG, "Effect '%s' not found", action->led.effect);
            // 继续尝试设置颜色
        }
    }

    ts_led_rgb_t color = TS_LED_RGB(action->led.r, action->led.g, action->led.b);

    // index = 0xFF 表示填充整个设备（根据 ts_automation_types.h）
    if (action->led.index == 0xFF) {
        return ts_led_fill(layer, color);
    }

    // 设置单个像素
    return ts_led_device_set_pixel(device, (uint16_t)action->led.index, color);
}

/**
 * @brief 执行 GPIO 动作
 */
static esp_err_t execute_gpio_action(const ts_auto_action_t *action)
{
    if (!action || action->type != TS_AUTO_ACT_GPIO) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "GPIO action: pin=%d, level=%d, pulse=%dms",
             action->gpio.pin, action->gpio.level, action->gpio.pulse_ms);

    // 使用 raw GPIO 方式（直接操作物理引脚）
    ts_gpio_handle_t handle = ts_gpio_create_raw(action->gpio.pin, "automation");
    if (!handle) {
        ESP_LOGE(TAG, "Failed to create GPIO handle for pin %d", action->gpio.pin);
        return ESP_ERR_NO_MEM;
    }

    // 配置为输出
    ts_gpio_config_t cfg = TS_GPIO_CONFIG_DEFAULT();
    cfg.direction = TS_GPIO_DIR_OUTPUT;
    esp_err_t ret = ts_gpio_configure(handle, &cfg);
    if (ret != ESP_OK) {
        ts_gpio_destroy(handle);
        return ret;
    }

    // 设置电平
    ret = ts_gpio_set_level(handle, action->gpio.level);

    // 如果是脉冲模式
    if (ret == ESP_OK && action->gpio.pulse_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(action->gpio.pulse_ms));
        ret = ts_gpio_set_level(handle, !action->gpio.level);
    }

    ts_gpio_destroy(handle);
    return ret;
}

/**
 * @brief 执行 SSH 命令引用动作
 * 
 * 通过 cmd_id 查找已注册的 SSH 命令并通过队列异步执行
 * 避免在 HTTP 任务中直接执行 SSH 操作导致栈溢出
 */
static esp_err_t execute_ssh_ref_action(const ts_auto_action_t *action)
{
    if (!action || action->type != TS_AUTO_ACT_SSH_CMD_REF) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *cmd_id = action->ssh_ref.cmd_id;
    ESP_LOGI(TAG, "SSH command ref action: cmd_id=%s (queued)", cmd_id);

    if (!cmd_id || cmd_id[0] == '\0') {
        ESP_LOGE(TAG, "Empty SSH command ID");
        return ESP_ERR_INVALID_ARG;
    }

    // 通过 action_manager 队列异步执行，避免栈溢出
    esp_err_t ret = ts_action_queue(action, NULL, NULL, 5);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue SSH action: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 执行 CLI 命令动作
 * 
 * 通过队列异步执行 TianShanOS CLI 命令
 */
static esp_err_t execute_cli_action(const ts_auto_action_t *action)
{
    if (!action || action->type != TS_AUTO_ACT_CLI) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *command = action->cli.command;
    ESP_LOGI(TAG, "CLI action: command=%s (queued)", command);

    if (!command || command[0] == '\0') {
        ESP_LOGE(TAG, "Empty CLI command");
        return ESP_ERR_INVALID_ARG;
    }

    // 通过 action_manager 队列异步执行
    esp_err_t ret = ts_action_queue(action, NULL, NULL, 5);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to queue CLI action: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 执行设备控制动作
 */
static esp_err_t execute_device_action(const ts_auto_action_t *action)
{
    if (!action || action->type != TS_AUTO_ACT_DEVICE_CTRL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Device action: device=%s, action=%s",
             action->device.device, action->device.action);

    // 解析设备 ID
    ts_device_id_t dev_id = TS_DEVICE_MAX;
    if (strcasecmp(action->device.device, "agx") == 0 ||
        strcasecmp(action->device.device, "AGX") == 0) {
        dev_id = TS_DEVICE_AGX;
    } else if (strcasecmp(action->device.device, "lpmu") == 0 ||
               strcasecmp(action->device.device, "LPMU") == 0) {
        dev_id = TS_DEVICE_LPMU;
    } else {
        ESP_LOGW(TAG, "Unknown device: %s", action->device.device);
        return ESP_ERR_NOT_FOUND;
    }

    // 执行动作
    const char *act = action->device.action;
    
    if (strcasecmp(act, "power_on") == 0 || strcasecmp(act, "on") == 0) {
        return ts_device_power_on(dev_id);
    } else if (strcasecmp(act, "power_off") == 0 || strcasecmp(act, "off") == 0) {
        return ts_device_power_off(dev_id);
    } else if (strcasecmp(act, "force_off") == 0) {
        return ts_device_force_off(dev_id);
    } else if (strcasecmp(act, "reset") == 0 || strcasecmp(act, "reboot") == 0) {
        return ts_device_reset(dev_id);
    } else if (strcasecmp(act, "recovery") == 0) {
        return ts_device_enter_recovery(dev_id);
    } else {
        ESP_LOGW(TAG, "Unknown device action: %s", act);
        return ESP_ERR_NOT_SUPPORTED;
    }
}

/**
 * @brief 执行 SSH 命令动作
 */
static esp_err_t execute_ssh_action(const ts_auto_action_t *action)
{
    if (!action || action->type != TS_AUTO_ACT_SSH_CMD) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "SSH action: host=%s, cmd=%s",
             action->ssh.host_ref, action->ssh.command);

    // 从变量系统获取主机配置
    // host_ref 格式: "hosts.<name>" 或直接 IP
    char host[64] = {0};
    uint16_t port = 22;
    char username[32] = {0};
    char password[64] = {0};

    // 尝试从变量获取主机配置
    char var_name[96];  // 足够容纳 "hosts.<name>.password"
    
    // 获取 host
    snprintf(var_name, sizeof(var_name), "hosts.%s.ip", action->ssh.host_ref);
    ts_auto_value_t val;
    if (ts_variable_get(var_name, &val) == ESP_OK && val.type == TS_AUTO_VAL_STRING) {
        strncpy(host, val.str_val, sizeof(host) - 1);
    } else {
        // 直接使用 host_ref 作为 IP
        strncpy(host, action->ssh.host_ref, sizeof(host) - 1);
    }

    // 获取 port
    snprintf(var_name, sizeof(var_name), "hosts.%s.port", action->ssh.host_ref);
    if (ts_variable_get(var_name, &val) == ESP_OK && val.type == TS_AUTO_VAL_INT) {
        port = (uint16_t)val.int_val;
    }

    // 获取 username
    snprintf(var_name, sizeof(var_name), "hosts.%s.username", action->ssh.host_ref);
    if (ts_variable_get(var_name, &val) == ESP_OK && val.type == TS_AUTO_VAL_STRING) {
        strncpy(username, val.str_val, sizeof(username) - 1);
    } else {
        strncpy(username, "root", sizeof(username) - 1);  // 默认
    }

    // 获取 password（可选）
    snprintf(var_name, sizeof(var_name), "hosts.%s.password", action->ssh.host_ref);
    if (ts_variable_get(var_name, &val) == ESP_OK && val.type == TS_AUTO_VAL_STRING) {
        strncpy(password, val.str_val, sizeof(password) - 1);
    }

    // 配置 SSH
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = username;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = password;
    config.timeout_ms = action->ssh.timeout_ms > 0 ? action->ssh.timeout_ms : 10000;

    // 执行命令
    ts_ssh_exec_result_t result = {0};
    esp_err_t ret = ts_ssh_exec_simple(&config, action->ssh.command, &result);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SSH command exit code: %d", result.exit_code);
        if (result.stdout_data && result.stdout_len > 0) {
            ESP_LOGD(TAG, "SSH stdout: %.*s", (int)result.stdout_len, result.stdout_data);
        }

        // 存储 exit_code 到变量（使用 host_ref 作为变量前缀）
        char result_var[TS_AUTO_NAME_MAX_LEN + 16];
        snprintf(result_var, sizeof(result_var), "ssh.%s.exit_code", action->ssh.host_ref);
        ts_auto_value_t res_val = {
            .type = TS_AUTO_VAL_INT,
            .int_val = result.exit_code
        };
        ts_variable_set(result_var, &res_val);
    } else {
        ESP_LOGE(TAG, "SSH command failed: %s", esp_err_to_name(ret));
    }

    ts_ssh_exec_result_free(&result);
    return ret;
}

/**
 * @brief 执行 Webhook 动作
 */
static esp_err_t execute_webhook_action(const ts_auto_action_t *action)
{
    if (!action || action->type != TS_AUTO_ACT_WEBHOOK) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Webhook action: url=%s, method=%s",
             action->webhook.url, action->webhook.method);

    esp_http_client_config_t config = {
        .url = action->webhook.url,
        .timeout_ms = 5000,  // 默认 5 秒超时
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        return ESP_ERR_NO_MEM;
    }

    // 设置方法
    if (strcasecmp(action->webhook.method, "POST") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
    } else if (strcasecmp(action->webhook.method, "PUT") == 0) {
        esp_http_client_set_method(client, HTTP_METHOD_PUT);
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_GET);
    }

    // 设置 Content-Type (对于 POST/PUT)
    if (strcasecmp(action->webhook.method, "POST") == 0 ||
        strcasecmp(action->webhook.method, "PUT") == 0) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
    }

    // 设置 body（使用 body_template）
    if (action->webhook.body_template[0] != '\0') {
        esp_http_client_set_post_field(client, action->webhook.body_template, 
                                       strlen(action->webhook.body_template));
    }

    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Webhook response: %d", status);
        
        if (status < 200 || status >= 300) {
            ret = ESP_FAIL;  // 非 2xx 视为失败
        }
    } else {
        ESP_LOGE(TAG, "Webhook request failed: %s", esp_err_to_name(ret));
    }

    esp_http_client_cleanup(client);
    return ret;
}

/*===========================================================================*/
/*                              动作执行                                      */
/*===========================================================================*/

esp_err_t ts_action_execute(const ts_auto_action_t *action)
{
    if (!action) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;

    ESP_LOGD(TAG, "Executing action type: %d", action->type);

    switch (action->type) {
        case TS_AUTO_ACT_LED:
            ret = execute_led_action(action);
            break;

        case TS_AUTO_ACT_SSH_CMD:
            ret = execute_ssh_action(action);
            break;

        case TS_AUTO_ACT_GPIO:
            ret = execute_gpio_action(action);
            break;

        case TS_AUTO_ACT_WEBHOOK:
            ret = execute_webhook_action(action);
            break;

        case TS_AUTO_ACT_LOG:
            ESP_LOG_LEVEL((esp_log_level_t)action->log.level, TAG, 
                          "Rule log: %s", action->log.message);
            break;

        case TS_AUTO_ACT_SET_VAR:
            ret = ts_variable_set(action->set_var.variable, &action->set_var.value);
            break;

        case TS_AUTO_ACT_DEVICE_CTRL:
            ret = execute_device_action(action);
            break;

        case TS_AUTO_ACT_SSH_CMD_REF:
            ret = execute_ssh_ref_action(action);
            break;

        case TS_AUTO_ACT_CLI:
            ret = execute_cli_action(action);
            break;

        default:
            ESP_LOGW(TAG, "Unknown action type: %d", action->type);
            ret = ESP_ERR_NOT_SUPPORTED;
    }

    s_rule_ctx.stats.total_actions++;
    if (ret != ESP_OK) {
        s_rule_ctx.stats.failed_actions++;
    }

    return ret;
}

/**
 * @brief Check if action's condition is met
 */
static bool check_action_condition(const ts_auto_action_t *action)
{
    if (!action->condition.has_condition) {
        return true;  // 没有条件，直接执行
    }
    
    // 构造条件结构并评估
    ts_auto_condition_t cond = {
        .op = action->condition.op,
        .value = action->condition.value,
    };
    strncpy(cond.variable, action->condition.variable, sizeof(cond.variable) - 1);
    
    bool result = ts_rule_eval_condition(&cond);
    
    ESP_LOGD(TAG, "Action condition check: %s %d %s -> %s",
             action->condition.variable, action->condition.op,
             action->condition.value.type == TS_AUTO_VAL_STRING ? 
                 action->condition.value.str_val : "(numeric)",
             result ? "PASS" : "SKIP");
    
    return result;
}

/**
 * @brief Execute a single action with repeat support
 */
static esp_err_t execute_action_with_repeat(const ts_auto_action_t *action,
                                             ts_action_result_cb_t callback,
                                             void *user_data)
{
    esp_err_t ret = ESP_OK;
    
    // 先检查动作级别的条件
    if (!check_action_condition(action)) {
        ESP_LOGI(TAG, "Action skipped: condition not met");
        return ESP_OK;  // 条件不满足，跳过执行
    }
    
    switch (action->repeat_mode) {
        case TS_AUTO_REPEAT_ONCE:
        default:
            // 单次执行
            ret = ts_action_execute(action);
            if (callback) {
                callback(action, ret, user_data);
            }
            break;
            
        case TS_AUTO_REPEAT_COUNT: {
            // 指定次数重复
            uint8_t count = action->repeat_count > 0 ? action->repeat_count : 1;
            uint16_t interval = action->repeat_interval_ms > 0 ? action->repeat_interval_ms : 1000;
            
            ESP_LOGI(TAG, "Repeat action %d times, interval=%dms", count, interval);
            
            for (uint8_t i = 0; i < count; i++) {
                // 每次重复前检查条件
                if (!check_action_condition(action)) {
                    ESP_LOGI(TAG, "Repeat stopped: condition no longer met");
                    break;
                }
                
                ret = ts_action_execute(action);
                if (callback) {
                    callback(action, ret, user_data);
                }
                
                // 最后一次不需要等待
                if (i < count - 1 && interval > 0) {
                    vTaskDelay(pdMS_TO_TICKS(interval));
                }
            }
            break;
        }
        
        case TS_AUTO_REPEAT_WHILE_TRUE: {
            // 条件持续时重复
            uint16_t interval = action->repeat_interval_ms > 0 ? action->repeat_interval_ms : 1000;
            uint8_t max_iterations = 100;  // 安全限制，防止无限循环
            uint8_t iterations = 0;
            
            ESP_LOGI(TAG, "Repeat while condition true, interval=%dms, max=%d", interval, max_iterations);
            
            while (check_action_condition(action) && iterations < max_iterations) {
                ret = ts_action_execute(action);
                if (callback) {
                    callback(action, ret, user_data);
                }
                iterations++;
                
                vTaskDelay(pdMS_TO_TICKS(interval));
            }
            
            if (iterations >= max_iterations) {
                ESP_LOGW(TAG, "Repeat stopped: max iterations reached (%d)", max_iterations);
            }
            break;
        }
    }
    
    return ret;
}

esp_err_t ts_action_execute_array(const ts_auto_action_t *actions, int count,
                                   ts_action_result_cb_t callback, void *user_data)
{
    if (!actions || count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Executing %d actions", count);

    for (int i = 0; i < count; i++) {
        // 延迟
        if (actions[i].delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(actions[i].delay_ms));
        }

        execute_action_with_repeat(&actions[i], callback, user_data);
    }

    return ESP_OK;
}

/*===========================================================================*/
/*                              规则访问                                       */
/*===========================================================================*/

esp_err_t ts_rule_get_by_index(int index, ts_auto_rule_t *rule)
{
    if (!rule) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);

    if (index < 0 || index >= s_rule_ctx.count) {
        xSemaphoreGive(s_rule_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(rule, &s_rule_ctx.rules[index], sizeof(ts_auto_rule_t));
    xSemaphoreGive(s_rule_ctx.mutex);

    return ESP_OK;
}

/*===========================================================================*/
/*                              统计                                          */
/*===========================================================================*/

esp_err_t ts_rule_engine_get_stats(ts_rule_engine_stats_t *stats)
{
    if (!stats || !s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
    *stats = s_rule_ctx.stats;
    xSemaphoreGive(s_rule_ctx.mutex);

    return ESP_OK;
}

esp_err_t ts_rule_engine_reset_stats(void)
{
    if (!s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
    memset(&s_rule_ctx.stats, 0, sizeof(s_rule_ctx.stats));
    xSemaphoreGive(s_rule_ctx.mutex);

    return ESP_OK;
}

/*===========================================================================*/
/*                              NVS 持久化                                    */
/*===========================================================================*/

/**
 * @brief 操作符转字符串
 */
static const char *operator_to_str(ts_auto_operator_t op)
{
    switch (op) {
        case TS_AUTO_OP_EQ:       return "eq";
        case TS_AUTO_OP_NE:       return "ne";
        case TS_AUTO_OP_GT:       return "gt";
        case TS_AUTO_OP_GE:       return "ge";
        case TS_AUTO_OP_LT:       return "lt";
        case TS_AUTO_OP_LE:       return "le";
        case TS_AUTO_OP_CONTAINS: return "contains";
        case TS_AUTO_OP_CHANGED:  return "changed";
        default:                  return "eq";
    }
}

/**
 * @brief 字符串转操作符
 */
static ts_auto_operator_t str_to_operator(const char *str)
{
    if (!str) return TS_AUTO_OP_EQ;
    if (strcmp(str, "ne") == 0) return TS_AUTO_OP_NE;
    if (strcmp(str, "gt") == 0) return TS_AUTO_OP_GT;
    if (strcmp(str, "ge") == 0) return TS_AUTO_OP_GE;
    if (strcmp(str, "lt") == 0) return TS_AUTO_OP_LT;
    if (strcmp(str, "le") == 0) return TS_AUTO_OP_LE;
    if (strcmp(str, "contains") == 0) return TS_AUTO_OP_CONTAINS;
    if (strcmp(str, "changed") == 0) return TS_AUTO_OP_CHANGED;
    return TS_AUTO_OP_EQ;
}

/**
 * @brief 动作类型转字符串
 */
static const char *action_type_to_str(ts_auto_action_type_t type)
{
    switch (type) {
        case TS_AUTO_ACT_LED:         return "led";
        case TS_AUTO_ACT_GPIO:        return "gpio";
        case TS_AUTO_ACT_DEVICE_CTRL: return "device_ctrl";
        case TS_AUTO_ACT_SSH_CMD_REF: return "ssh_cmd_ref";
        case TS_AUTO_ACT_CLI:         return "cli";
        case TS_AUTO_ACT_WEBHOOK:     return "webhook";
        case TS_AUTO_ACT_LOG:         return "log";
        case TS_AUTO_ACT_SET_VAR:     return "set_var";
        default:                      return "log";
    }
}

/**
 * @brief 字符串转动作类型
 */
static ts_auto_action_type_t str_to_action_type(const char *str)
{
    if (!str) return TS_AUTO_ACT_LOG;
    if (strcmp(str, "led") == 0)         return TS_AUTO_ACT_LED;
    if (strcmp(str, "gpio") == 0)        return TS_AUTO_ACT_GPIO;
    if (strcmp(str, "device_ctrl") == 0) return TS_AUTO_ACT_DEVICE_CTRL;
    if (strcmp(str, "ssh_cmd_ref") == 0) return TS_AUTO_ACT_SSH_CMD_REF;
    if (strcmp(str, "cli") == 0)         return TS_AUTO_ACT_CLI;
    if (strcmp(str, "webhook") == 0)     return TS_AUTO_ACT_WEBHOOK;
    if (strcmp(str, "log") == 0)         return TS_AUTO_ACT_LOG;
    if (strcmp(str, "set_var") == 0)     return TS_AUTO_ACT_SET_VAR;
    return TS_AUTO_ACT_LOG;
}

/**
 * @brief 将值序列化为 JSON 对象
 */
static cJSON *value_to_json(const ts_auto_value_t *val)
{
    switch (val->type) {
        case TS_AUTO_VAL_BOOL:
            return cJSON_CreateBool(val->bool_val);
        case TS_AUTO_VAL_INT:
            return cJSON_CreateNumber(val->int_val);
        case TS_AUTO_VAL_FLOAT:
            return cJSON_CreateNumber(val->float_val);
        case TS_AUTO_VAL_STRING:
            return cJSON_CreateString(val->str_val);
        default:
            return cJSON_CreateNull();
    }
}

/**
 * @brief 从 JSON 对象反序列化值
 */
static void json_to_value(cJSON *json, ts_auto_value_t *val)
{
    if (!json || !val) return;
    
    if (cJSON_IsBool(json)) {
        val->type = TS_AUTO_VAL_BOOL;
        val->bool_val = cJSON_IsTrue(json);
    } else if (cJSON_IsNumber(json)) {
        // 判断是整数还是浮点数
        double d = json->valuedouble;
        if (d == (int64_t)d && d >= INT32_MIN && d <= INT32_MAX) {
            val->type = TS_AUTO_VAL_INT;
            val->int_val = (int32_t)d;
        } else {
            val->type = TS_AUTO_VAL_FLOAT;
            val->float_val = d;
        }
    } else if (cJSON_IsString(json)) {
        val->type = TS_AUTO_VAL_STRING;
        strncpy(val->str_val, json->valuestring, sizeof(val->str_val) - 1);
    }
}

/**
 * @brief 将规则序列化为 JSON 字符串
 */
static char *rule_to_json(const ts_auto_rule_t *rule)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    
    // 基本信息
    cJSON_AddStringToObject(root, "id", rule->id);
    cJSON_AddStringToObject(root, "name", rule->name);
    if (rule->icon[0]) {
        cJSON_AddStringToObject(root, "icon", rule->icon);
    }
    cJSON_AddBoolToObject(root, "enabled", rule->enabled);
    cJSON_AddBoolToObject(root, "manual_trigger", rule->manual_trigger);
    cJSON_AddNumberToObject(root, "cooldown_ms", rule->cooldown_ms);
    
    // 条件组
    cJSON *conditions = cJSON_CreateObject();
    cJSON_AddStringToObject(conditions, "logic", 
                            rule->conditions.logic == TS_AUTO_LOGIC_OR ? "or" : "and");
    
    cJSON *cond_array = cJSON_CreateArray();
    for (int i = 0; i < rule->conditions.count; i++) {
        const ts_auto_condition_t *c = &rule->conditions.conditions[i];
        cJSON *cond = cJSON_CreateObject();
        cJSON_AddStringToObject(cond, "variable", c->variable);
        cJSON_AddStringToObject(cond, "operator", operator_to_str(c->op));
        cJSON_AddItemToObject(cond, "value", value_to_json(&c->value));
        cJSON_AddItemToArray(cond_array, cond);
    }
    cJSON_AddItemToObject(conditions, "items", cond_array);
    cJSON_AddItemToObject(root, "conditions", conditions);
    
    // 动作数组
    cJSON *actions = cJSON_CreateArray();
    for (int i = 0; i < rule->action_count; i++) {
        const ts_auto_action_t *a = &rule->actions[i];
        cJSON *action = cJSON_CreateObject();
        
        cJSON_AddStringToObject(action, "type", action_type_to_str(a->type));
        cJSON_AddNumberToObject(action, "delay_ms", a->delay_ms);
        
        // 根据类型保存特定字段
        switch (a->type) {
            case TS_AUTO_ACT_LED:
                cJSON_AddStringToObject(action, "device", a->led.device);
                cJSON_AddNumberToObject(action, "index", a->led.index);
                cJSON_AddNumberToObject(action, "r", a->led.r);
                cJSON_AddNumberToObject(action, "g", a->led.g);
                cJSON_AddNumberToObject(action, "b", a->led.b);
                if (a->led.effect[0]) {
                    cJSON_AddStringToObject(action, "effect", a->led.effect);
                }
                cJSON_AddNumberToObject(action, "duration_ms", a->led.duration_ms);
                break;
                
            case TS_AUTO_ACT_GPIO:
                cJSON_AddNumberToObject(action, "pin", a->gpio.pin);
                cJSON_AddBoolToObject(action, "level", a->gpio.level);
                cJSON_AddNumberToObject(action, "pulse_ms", a->gpio.pulse_ms);
                break;
                
            case TS_AUTO_ACT_DEVICE_CTRL:
                cJSON_AddStringToObject(action, "device", a->device.device);
                cJSON_AddStringToObject(action, "action", a->device.action);
                break;
                
            case TS_AUTO_ACT_CLI:
                cJSON_AddStringToObject(action, "command", a->cli.command);
                cJSON_AddStringToObject(action, "var_name", a->cli.var_name);
                cJSON_AddNumberToObject(action, "timeout_ms", a->cli.timeout_ms);
                break;
                
            case TS_AUTO_ACT_LOG:
                cJSON_AddStringToObject(action, "message", a->log.message);
                cJSON_AddNumberToObject(action, "level", a->log.level);
                break;
                
            case TS_AUTO_ACT_SET_VAR:
                cJSON_AddStringToObject(action, "variable", a->set_var.variable);
                cJSON_AddItemToObject(action, "value", value_to_json(&a->set_var.value));
                break;
                
            case TS_AUTO_ACT_WEBHOOK:
                cJSON_AddStringToObject(action, "url", a->webhook.url);
                cJSON_AddStringToObject(action, "method", a->webhook.method);
                cJSON_AddStringToObject(action, "body_template", a->webhook.body_template);
                break;
                
            case TS_AUTO_ACT_SSH_CMD_REF:
                cJSON_AddStringToObject(action, "cmd_id", a->ssh_ref.cmd_id);
                break;
                
            default:
                break;
        }
        
        // 保存 template_id（如果有）
        if (a->template_id[0]) {
            cJSON_AddStringToObject(action, "template_id", a->template_id);
        }
        
        cJSON_AddItemToArray(actions, action);
    }
    cJSON_AddItemToObject(root, "actions", actions);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

/**
 * @brief 从 JSON 字符串反序列化规则
 */
static esp_err_t json_to_rule(const char *json_str, ts_auto_rule_t *rule)
{
    if (!json_str || !rule) return ESP_ERR_INVALID_ARG;
    
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return ESP_ERR_INVALID_ARG;
    
    memset(rule, 0, sizeof(ts_auto_rule_t));
    
    // 基本信息
    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "id")) && cJSON_IsString(item)) {
        strncpy(rule->id, item->valuestring, sizeof(rule->id) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "name")) && cJSON_IsString(item)) {
        strncpy(rule->name, item->valuestring, sizeof(rule->name) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "icon")) && cJSON_IsString(item)) {
        strncpy(rule->icon, item->valuestring, sizeof(rule->icon) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "enabled"))) {
        rule->enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "manual_trigger"))) {
        rule->manual_trigger = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "cooldown_ms")) && cJSON_IsNumber(item)) {
        rule->cooldown_ms = (uint32_t)item->valueint;
    }
    
    // 条件组
    cJSON *conditions = cJSON_GetObjectItem(root, "conditions");
    if (conditions) {
        if ((item = cJSON_GetObjectItem(conditions, "logic")) && cJSON_IsString(item)) {
            rule->conditions.logic = strcmp(item->valuestring, "or") == 0 ? 
                                     TS_AUTO_LOGIC_OR : TS_AUTO_LOGIC_AND;
        }
        
        cJSON *items = cJSON_GetObjectItem(conditions, "items");
        if (items && cJSON_IsArray(items)) {
            int count = cJSON_GetArraySize(items);
            if (count > 0) {
                rule->conditions.conditions = heap_caps_calloc(count, 
                    sizeof(ts_auto_condition_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (rule->conditions.conditions) {
                    rule->conditions.count = count;
                    
                    int idx = 0;
                    cJSON *cond_item;
                    cJSON_ArrayForEach(cond_item, items) {
                        ts_auto_condition_t *c = &rule->conditions.conditions[idx];
                        
                        if ((item = cJSON_GetObjectItem(cond_item, "variable")) && cJSON_IsString(item)) {
                            strncpy(c->variable, item->valuestring, sizeof(c->variable) - 1);
                        }
                        if ((item = cJSON_GetObjectItem(cond_item, "operator")) && cJSON_IsString(item)) {
                            c->op = str_to_operator(item->valuestring);
                        }
                        if ((item = cJSON_GetObjectItem(cond_item, "value"))) {
                            json_to_value(item, &c->value);
                        }
                        idx++;
                    }
                }
            }
        }
    }
    
    // 动作数组
    cJSON *actions = cJSON_GetObjectItem(root, "actions");
    if (actions && cJSON_IsArray(actions)) {
        int count = cJSON_GetArraySize(actions);
        if (count > 0) {
            rule->actions = heap_caps_calloc(count, sizeof(ts_auto_action_t), 
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (rule->actions) {
                rule->action_count = count;
                
                int idx = 0;
                cJSON *act_item;
                cJSON_ArrayForEach(act_item, actions) {
                    ts_auto_action_t *a = &rule->actions[idx];
                    
                    if ((item = cJSON_GetObjectItem(act_item, "type")) && cJSON_IsString(item)) {
                        a->type = str_to_action_type(item->valuestring);
                    }
                    if ((item = cJSON_GetObjectItem(act_item, "delay_ms")) && cJSON_IsNumber(item)) {
                        a->delay_ms = (uint16_t)item->valueint;
                    }
                    
                    // 根据类型解析特定字段
                    switch (a->type) {
                        case TS_AUTO_ACT_LED:
                            if ((item = cJSON_GetObjectItem(act_item, "device")) && cJSON_IsString(item)) {
                                strncpy(a->led.device, item->valuestring, sizeof(a->led.device) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "index")) && cJSON_IsNumber(item)) {
                                a->led.index = (uint8_t)item->valueint;
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "r")) && cJSON_IsNumber(item)) {
                                a->led.r = (uint8_t)item->valueint;
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "g")) && cJSON_IsNumber(item)) {
                                a->led.g = (uint8_t)item->valueint;
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "b")) && cJSON_IsNumber(item)) {
                                a->led.b = (uint8_t)item->valueint;
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "effect")) && cJSON_IsString(item)) {
                                strncpy(a->led.effect, item->valuestring, sizeof(a->led.effect) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "duration_ms")) && cJSON_IsNumber(item)) {
                                a->led.duration_ms = (uint16_t)item->valueint;
                            }
                            break;
                            
                        case TS_AUTO_ACT_GPIO:
                            if ((item = cJSON_GetObjectItem(act_item, "pin")) && cJSON_IsNumber(item)) {
                                a->gpio.pin = (uint8_t)item->valueint;
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "level"))) {
                                a->gpio.level = cJSON_IsTrue(item);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "pulse_ms")) && cJSON_IsNumber(item)) {
                                a->gpio.pulse_ms = (uint32_t)item->valueint;
                            }
                            break;
                            
                        case TS_AUTO_ACT_DEVICE_CTRL:
                            if ((item = cJSON_GetObjectItem(act_item, "device")) && cJSON_IsString(item)) {
                                strncpy(a->device.device, item->valuestring, sizeof(a->device.device) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "action")) && cJSON_IsString(item)) {
                                strncpy(a->device.action, item->valuestring, sizeof(a->device.action) - 1);
                            }
                            break;
                            
                        case TS_AUTO_ACT_CLI:
                            if ((item = cJSON_GetObjectItem(act_item, "command")) && cJSON_IsString(item)) {
                                strncpy(a->cli.command, item->valuestring, sizeof(a->cli.command) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "var_name")) && cJSON_IsString(item)) {
                                strncpy(a->cli.var_name, item->valuestring, sizeof(a->cli.var_name) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "timeout_ms")) && cJSON_IsNumber(item)) {
                                a->cli.timeout_ms = (uint32_t)item->valueint;
                            }
                            break;
                            
                        case TS_AUTO_ACT_LOG:
                            if ((item = cJSON_GetObjectItem(act_item, "message")) && cJSON_IsString(item)) {
                                strncpy(a->log.message, item->valuestring, sizeof(a->log.message) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "level")) && cJSON_IsNumber(item)) {
                                a->log.level = (uint8_t)item->valueint;
                            }
                            break;
                            
                        case TS_AUTO_ACT_SET_VAR:
                            if ((item = cJSON_GetObjectItem(act_item, "variable")) && cJSON_IsString(item)) {
                                strncpy(a->set_var.variable, item->valuestring, sizeof(a->set_var.variable) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "value"))) {
                                json_to_value(item, &a->set_var.value);
                            }
                            break;
                            
                        case TS_AUTO_ACT_WEBHOOK:
                            if ((item = cJSON_GetObjectItem(act_item, "url")) && cJSON_IsString(item)) {
                                strncpy(a->webhook.url, item->valuestring, sizeof(a->webhook.url) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "method")) && cJSON_IsString(item)) {
                                strncpy(a->webhook.method, item->valuestring, sizeof(a->webhook.method) - 1);
                            }
                            if ((item = cJSON_GetObjectItem(act_item, "body_template")) && cJSON_IsString(item)) {
                                strncpy(a->webhook.body_template, item->valuestring, sizeof(a->webhook.body_template) - 1);
                            }
                            break;
                            
                        case TS_AUTO_ACT_SSH_CMD_REF:
                            if ((item = cJSON_GetObjectItem(act_item, "cmd_id")) && cJSON_IsString(item)) {
                                strncpy(a->ssh_ref.cmd_id, item->valuestring, sizeof(a->ssh_ref.cmd_id) - 1);
                            }
                            break;
                            
                        default:
                            break;
                    }
                    
                    // 加载 template_id（如果有）
                    if ((item = cJSON_GetObjectItem(act_item, "template_id")) && cJSON_IsString(item)) {
                        strncpy(a->template_id, item->valuestring, sizeof(a->template_id) - 1);
                    }
                    
                    idx++;
                }
            }
        }
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief 保存所有规则到 NVS
 */
esp_err_t ts_rules_save(void)
{
    if (!s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE_RULES, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for rules: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 先清除旧数据
    nvs_erase_all(handle);
    
    xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
    
    // 保存数量
    ret = nvs_set_u8(handle, NVS_KEY_RULE_COUNT, (uint8_t)s_rule_ctx.count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save rule count: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_rule_ctx.mutex);
        nvs_close(handle);
        return ret;
    }
    
    // 为 SD 卡导出准备 JSON 数组
    cJSON *rules_json = cJSON_CreateObject();
    cJSON *rules_array = cJSON_CreateArray();
    
    // 保存每条规则
    for (int i = 0; i < s_rule_ctx.count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_RULE_PREFIX, i);
        
        char *json = rule_to_json(&s_rule_ctx.rules[i]);
        if (!json) {
            ESP_LOGW(TAG, "Failed to serialize rule %d", i);
            continue;
        }
        
        // 保存到 NVS
        ret = nvs_set_str(handle, key, json);
        
        // 同时添加到 SD 卡 JSON 数组
        cJSON *rule_obj = cJSON_Parse(json);
        if (rule_obj) {
            cJSON_AddItemToArray(rules_array, rule_obj);
        }
        
        free(json);
        
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save rule %d: %s", i, esp_err_to_name(ret));
        }
    }
    
    xSemaphoreGive(s_rule_ctx.mutex);
    
    ret = nvs_commit(handle);
    nvs_close(handle);
    
    // 构建完整 JSON 并导出到 SD 卡
    cJSON_AddItemToObject(rules_json, "rules", rules_array);
    cJSON_AddNumberToObject(rules_json, "count", s_rule_ctx.count);
    
    // 使用新的 API 导出到 SD 卡
    esp_err_t sd_ret = ts_config_module_export_custom_json(TS_CONFIG_MODULE_RULES, rules_json);
    cJSON_Delete(rules_json);
    
    if (sd_ret != ESP_OK && sd_ret != TS_CONFIG_ERR_SD_NOT_MOUNTED) {
        ESP_LOGW(TAG, "Failed to export rules to SD card: %s", esp_err_to_name(sd_ret));
    }
    
    ESP_LOGI(TAG, "Saved %d rules to NVS", s_rule_ctx.count);
    return ret;
}

/**
 * @brief 从存储加载所有规则
 * 
 * Priority: SD card file > NVS > empty (遵循系统配置优先级原则)
 */
esp_err_t ts_rules_load(void)
{
    if (!s_rule_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    /* 1. 优先从 SD 卡加载 */
    if (ts_storage_sd_mounted()) {
        ret = ts_rules_load_from_file("/sdcard/config/rules.json");
        if (ret == ESP_OK && s_rule_ctx.count > 0) {
            ESP_LOGI(TAG, "Loaded %d rules from SD card", s_rule_ctx.count);
            return ESP_OK;  /* SD 卡配置已自动保存到 NVS */
        }
    }
    
    /* 2. SD 卡无配置，从 NVS 加载 */
    uint8_t count = 0;
    nvs_handle_t handle;
    ret = nvs_open(NVS_NAMESPACE_RULES, NVS_READONLY, &handle);
    
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No saved rules found");
        return ESP_OK;
    }
    
    ret = nvs_get_u8(handle, NVS_KEY_RULE_COUNT, &count);
    if (ret != ESP_OK || count == 0) {
        nvs_close(handle);
        ESP_LOGI(TAG, "No saved rules found");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Loading %d rules from NVS", count);
    
    /* 使用堆分配避免栈溢出 */
    ts_auto_rule_t *rule = heap_caps_malloc(sizeof(ts_auto_rule_t), MALLOC_CAP_SPIRAM);
    if (!rule) rule = malloc(sizeof(ts_auto_rule_t));
    if (!rule) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to allocate rule buffer");
        return ESP_ERR_NO_MEM;
    }

    // 加载每条规则
    for (int i = 0; i < count && s_rule_ctx.count < s_rule_ctx.capacity; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_RULE_PREFIX, i);
        
        // 获取字符串长度
        size_t len = 0;
        ret = nvs_get_str(handle, key, NULL, &len);
        if (ret != ESP_OK || len == 0) {
            continue;
        }
        
        char *json = malloc(len);
        if (!json) {
            ESP_LOGW(TAG, "Failed to allocate memory for rule %d", i);
            continue;
        }
        
        ret = nvs_get_str(handle, key, json, &len);
        if (ret == ESP_OK) {
            if (json_to_rule(json, rule) == ESP_OK) {
                // 直接添加到数组（不调用 ts_rule_register 避免重复保存）
                xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
                memcpy(&s_rule_ctx.rules[s_rule_ctx.count], rule, sizeof(ts_auto_rule_t));
                s_rule_ctx.count++;
                xSemaphoreGive(s_rule_ctx.mutex);
                ESP_LOGD(TAG, "Loaded rule: %s", rule->id);
            }
        }
        
        free(json);
    }

    free(rule);
    ESP_LOGI(TAG, "Loaded %d rules from NVS", s_rule_ctx.count);
    return ESP_OK;
}

/**
 * @brief Load rules from SD card JSON file
 */
esp_err_t ts_rules_load_from_file(const char *filepath)
{
    if (!s_rule_ctx.initialized || !filepath) return ESP_ERR_INVALID_ARG;
    
    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGD(TAG, "Cannot open file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0 || size > 32768) {
        fclose(f);
        ESP_LOGW(TAG, "File too large or empty: %s (%ld bytes)", filepath, size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    char *content = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
    if (!content) content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read = fread(content, 1, size, f);
    fclose(f);
    content[read] = '\0';
    
    cJSON *root = cJSON_Parse(content);
    free(content);
    
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON: %s", filepath);
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *rules = cJSON_GetObjectItem(root, "rules");
    if (!rules || !cJSON_IsArray(rules)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "No 'rules' array in file");
        return ESP_ERR_INVALID_ARG;
    }
    
    int loaded = 0;
    
    /* 使用堆分配避免栈溢出 */
    ts_auto_rule_t *rule = heap_caps_malloc(sizeof(ts_auto_rule_t), MALLOC_CAP_SPIRAM);
    if (!rule) rule = malloc(sizeof(ts_auto_rule_t));
    if (!rule) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to allocate rule buffer");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON *item;
    cJSON_ArrayForEach(item, rules) {
        if (s_rule_ctx.count >= s_rule_ctx.capacity) break;
        
        char *json_str = cJSON_PrintUnformatted(item);
        if (!json_str) continue;
        
        if (json_to_rule(json_str, rule) == ESP_OK) {
            xSemaphoreTake(s_rule_ctx.mutex, portMAX_DELAY);
            memcpy(&s_rule_ctx.rules[s_rule_ctx.count], rule, sizeof(ts_auto_rule_t));
            s_rule_ctx.count++;
            xSemaphoreGive(s_rule_ctx.mutex);
            loaded++;
        }
        cJSON_free(json_str);
    }

    free(rule);
    
    if (loaded > 0) {
        ESP_LOGI(TAG, "Loaded %d rules from SD card: %s", loaded, filepath);
        ts_rules_save();  /* Save to NVS for next boot */
    }
    
    return ESP_OK;
}
