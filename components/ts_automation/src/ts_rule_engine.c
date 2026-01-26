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

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// 动作执行依赖
#include "ts_led.h"
#include "ts_hal_gpio.h"
#include "ts_device_ctrl.h"
#include "ts_ssh_client.h"

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
        }
        if (s_rule_ctx.rules[idx].actions) {
            free(s_rule_ctx.rules[idx].actions);
        }
        
        // 更新规则
        memcpy(&s_rule_ctx.rules[idx], rule, sizeof(ts_auto_rule_t));
        xSemaphoreGive(s_rule_ctx.mutex);
        ESP_LOGD(TAG, "Updated rule: %s", rule->id);
        return ESP_OK;
    }

    // 检查容量
    if (s_rule_ctx.count >= s_rule_ctx.capacity) {
        xSemaphoreGive(s_rule_ctx.mutex);
        ESP_LOGE(TAG, "Rule storage full");
        return ESP_ERR_NO_MEM;
    }

    // 添加新规则
    memcpy(&s_rule_ctx.rules[s_rule_ctx.count], rule, sizeof(ts_auto_rule_t));
    s_rule_ctx.count++;

    xSemaphoreGive(s_rule_ctx.mutex);

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
        return true;  // 空条件组视为始终满足
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
 * @brief 执行 LED 动作
 */
static esp_err_t execute_led_action(const ts_auto_action_t *action)
{
    if (!action || action->type != TS_AUTO_ACT_LED) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "LED action: device=%s, index=%d, color=#%02X%02X%02X",
             action->led.device, action->led.index,
             action->led.r, action->led.g, action->led.b);

    // 获取设备句柄
    ts_led_device_t device = ts_led_device_get(action->led.device);
    if (!device) {
        ESP_LOGW(TAG, "LED device '%s' not found", action->led.device);
        return ESP_ERR_NOT_FOUND;
    }

    ts_led_rgb_t color = TS_LED_RGB(action->led.r, action->led.g, action->led.b);

    // index = 0xFF 表示填充整个设备（根据 ts_automation_types.h）
    if (action->led.index == 0xFF) {
        // 获取默认 layer 并填充
        ts_led_layer_t layer = ts_led_layer_get(device, 0);
        if (layer) {
            return ts_led_fill(layer, color);
        }
        // 如果没有 layer，直接设置每个像素
        uint16_t count = ts_led_device_get_count(device);
        for (uint16_t i = 0; i < count; i++) {
            ts_led_device_set_pixel(device, i, color);
        }
        return ESP_OK;
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

        esp_err_t ret = ts_action_execute(&actions[i]);

        if (callback) {
            callback(&actions[i], ret, user_data);
        }
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
