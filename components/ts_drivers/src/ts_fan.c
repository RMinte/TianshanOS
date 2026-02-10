/**
 * @file ts_fan.c
 * @brief Fan Control Driver Implementation
 * 
 * 移植自 robOS fan_controller，增强功能：
 * - 温度曲线线性插值
 * - 温度迟滞控制（防止频繁调速）
 * - 完整配置持久化
 * - 自动订阅温度事件（ts_temp_source 集成）
 */

#include "ts_fan.h"
#include "ts_temp_source.h"
#include "ts_event.h"
#include "ts_hal_pwm.h"
#include "ts_hal_gpio.h"
#include "ts_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

#define TAG "ts_fan"
#define FAN_NVS_NAMESPACE "fan_config"
#define FAN_CONFIG_VERSION 2

/*===========================================================================*/
/*                          Internal Types                                    */
/*===========================================================================*/

typedef struct {
    bool initialized;
    bool enabled;
    ts_fan_config_t config;
    ts_pwm_handle_t pwm;
    ts_gpio_handle_t tach_gpio;
    ts_fan_mode_t mode;
    uint8_t current_duty;
    uint8_t target_duty;
    uint16_t rpm;
    int16_t temperature;
    int16_t last_stable_temp;
    uint32_t last_speed_change_time;
    volatile uint32_t tach_count;
    int64_t last_tach_time;
    bool fault;
} fan_instance_t;

/*===========================================================================*/
/*                          Static Variables                                  */
/*===========================================================================*/

static fan_instance_t s_fans[TS_FAN_MAX];
static esp_timer_handle_t s_update_timer = NULL;
static bool s_initialized = false;
static ts_event_handler_handle_t s_temp_event_handle = NULL;
static bool s_auto_temp_enabled = true;

/*===========================================================================*/
/*                          Forward Declarations                              */
/*===========================================================================*/

static void tach_isr_callback(ts_gpio_handle_t handle, void *arg);
static void fan_update_callback(void *arg);
static uint8_t calc_duty_from_curve(fan_instance_t *fan, int16_t temp);
static esp_err_t apply_curve_with_hysteresis(fan_instance_t *fan, uint8_t fan_id);
static esp_err_t update_pwm(fan_instance_t *fan, uint8_t duty);
static void temp_event_handler(const ts_event_t *event, void *user_data);

/*===========================================================================*/
/*                          Temperature Event Handler                         */
/*===========================================================================*/

/**
 * @brief 温度事件处理器
 * 
 * 自动接收 ts_temp_source 发布的温度更新事件，
 * 并同步到所有风扇实例
 */
static void temp_event_handler(const ts_event_t *event, void *user_data)
{
    (void)user_data;
    
    if (!s_auto_temp_enabled || !s_initialized) return;
    if (event == NULL || event->id != TS_EVT_TEMP_UPDATED) return;
    
    const ts_temp_event_data_t *temp_evt = (const ts_temp_event_data_t *)event->data;
    if (temp_evt == NULL) return;
    
    /* 更新所有处于自动/曲线模式的风扇温度 */
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (s_fans[i].initialized && 
            (s_fans[i].mode == TS_FAN_MODE_AUTO || s_fans[i].mode == TS_FAN_MODE_CURVE)) {
            s_fans[i].temperature = temp_evt->temp;
            TS_LOGD(TAG, "Fan %d temp updated: %d.%d°C (source=%d)", 
                    i, temp_evt->temp / 10, 
                    abs(temp_evt->temp % 10),
                    temp_evt->source);
        }
    }
}

/*===========================================================================*/
/*                          ISR Callback                                      */
/*===========================================================================*/

static void tach_isr_callback(ts_gpio_handle_t handle, void *arg)
{
    fan_instance_t *fan = (fan_instance_t *)arg;
    fan->tach_count++;
}

/*===========================================================================*/
/*                          Curve Calculation                                 */
/*===========================================================================*/

/**
 * @brief 线性插值计算曲线占空比
 */
static uint8_t calc_duty_from_curve(fan_instance_t *fan, int16_t temp)
{
    if (fan->config.curve_points == 0) {
        return fan->config.max_duty;
    }
    
    const ts_fan_curve_point_t *curve = fan->config.curve;
    uint8_t points = fan->config.curve_points;
    
    // 低于最低点
    if (temp <= curve[0].temp) {
        return curve[0].duty;
    }
    
    // 高于最高点
    if (temp >= curve[points - 1].temp) {
        return curve[points - 1].duty;
    }
    
    // 线性插值
    for (int i = 0; i < points - 1; i++) {
        if (temp >= curve[i].temp && temp < curve[i + 1].temp) {
            int32_t t_range = curve[i + 1].temp - curve[i].temp;
            int32_t d_range = (int32_t)curve[i + 1].duty - (int32_t)curve[i].duty;
            int32_t t_offset = temp - curve[i].temp;
            return curve[i].duty + (uint8_t)((t_offset * d_range) / t_range);
        }
    }
    
    return fan->config.max_duty;
}

/**
 * @brief 应用曲线（带迟滞控制）
 */
static esp_err_t apply_curve_with_hysteresis(fan_instance_t *fan, uint8_t fan_id)
{
    if (fan->config.curve_points == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 计算目标占空比
    uint8_t target = calc_duty_from_curve(fan, fan->temperature);
    
    // 应用最小/最大限制
    if (target < fan->config.min_duty && target > 0) {
        target = fan->config.min_duty;
    }
    if (target > fan->config.max_duty) {
        target = fan->config.max_duty;
    }
    
    fan->target_duty = target;
    
    // 温度迟滞控制
    int16_t temp_diff = fan->temperature > fan->last_stable_temp ?
                        fan->temperature - fan->last_stable_temp :
                        fan->last_stable_temp - fan->temperature;
    
    bool significant_change = temp_diff >= fan->config.hysteresis;
    bool enough_time = (current_time - fan->last_speed_change_time) >= fan->config.min_interval;
    
    // 决定是否调速
    if (significant_change && enough_time) {
        fan->last_stable_temp = fan->temperature;
        fan->last_speed_change_time = current_time;
        
        if (target != fan->current_duty) {
            return update_pwm(fan, target);
        }
    }
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          PWM Control                                       */
/*===========================================================================*/

static esp_err_t update_pwm(fan_instance_t *fan, uint8_t duty)
{
    if (!fan->pwm) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!fan->enabled) {
        duty = 0;
    }
    
    if (duty > 100) duty = 100;
    
    // PWM 反转
    uint8_t actual = fan->config.invert_pwm ? (100 - duty) : duty;
    
    esp_err_t ret = ts_pwm_set_duty(fan->pwm, (float)actual);
    if (ret == ESP_OK) {
        fan->current_duty = duty;
    } else {
        fan->fault = true;
    }
    
    return ret;
}

/*===========================================================================*/
/*                          Timer Callback                                    */
/*===========================================================================*/

static void fan_update_callback(void *arg)
{
    int64_t now = esp_timer_get_time();
    static uint32_t s_log_counter = 0;
    
    /* 主动获取最新温度（确保曲线模式能及时响应温度变化） */
    if (s_auto_temp_enabled) {
        ts_temp_data_t temp_data;
        int16_t current_temp = ts_temp_get_effective(&temp_data);
        
        /* 每 10 秒输出一次调试日志 */
        if (++s_log_counter >= 10) {
            s_log_counter = 0;
            TS_LOGD(TAG, "[FAN-CB] temp=%d.%d°C, source=%d, valid=%d",
                    current_temp / 10, current_temp % 10,
                    temp_data.source, temp_data.valid);
        }
        
        if (current_temp > TS_TEMP_MIN_VALID) {
            for (int i = 0; i < TS_FAN_MAX; i++) {
                if (s_fans[i].initialized && 
                    (s_fans[i].mode == TS_FAN_MODE_AUTO || s_fans[i].mode == TS_FAN_MODE_CURVE)) {
                    s_fans[i].temperature = current_temp;
                }
            }
        }
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        fan_instance_t *fan = &s_fans[i];
        if (!fan->initialized) continue;
        
        // 计算 RPM
        if (fan->tach_gpio) {
            int64_t dt_us = now - fan->last_tach_time;
            if (dt_us > 0) {
                fan->rpm = (fan->tach_count * 60 * 1000000) / (dt_us * 2);
                fan->tach_count = 0;
                fan->last_tach_time = now;
            }
        }
        
        // 根据模式更新
        if (!fan->enabled) {
            update_pwm(fan, 0);
            continue;
        }
        
        switch (fan->mode) {
        case TS_FAN_MODE_MANUAL:
            // 手动模式：保持当前设定
            update_pwm(fan, fan->current_duty);
            break;
            
        case TS_FAN_MODE_AUTO:
            // 简单自动模式：基于曲线但无迟滞
            {
                uint8_t target = calc_duty_from_curve(fan, fan->temperature);
                if (target < fan->config.min_duty && target > 0) {
                    target = fan->config.min_duty;
                }
                if (target > fan->config.max_duty) {
                    target = fan->config.max_duty;
                }
                if (target != fan->current_duty) {
                    update_pwm(fan, target);
                }
            }
            break;
            
        case TS_FAN_MODE_CURVE:
            // 曲线模式：带迟滞控制
            apply_curve_with_hysteresis(fan, i);
            break;
            
        case TS_FAN_MODE_OFF:
        default:
            update_pwm(fan, 0);
            break;
        }
    }
}

/*===========================================================================*/
/*                          Public API - Init                                 */
/*===========================================================================*/

esp_err_t ts_fan_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(s_fans, 0, sizeof(s_fans));
    
    // 默认曲线（30°C-20%, 50°C-40%, 70°C-80%, 80°C-100%）
    ts_fan_curve_point_t default_curve[] = {
        {300, 20}, {500, 40}, {700, 80}, {800, 100}
    };
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        s_fans[i].config.min_duty = 20;
        s_fans[i].config.max_duty = 100;
        s_fans[i].config.hysteresis = TS_FAN_DEFAULT_HYSTERESIS;
        s_fans[i].config.min_interval = TS_FAN_DEFAULT_MIN_INTERVAL;
        s_fans[i].config.invert_pwm = false;
        memcpy(s_fans[i].config.curve, default_curve, sizeof(default_curve));
        s_fans[i].config.curve_points = 4;
        s_fans[i].last_stable_temp = 250;  // 25.0°C
        s_fans[i].temperature = 250;
        s_fans[i].enabled = true;
    }
    
    // 启动更新定时器
    esp_timer_create_args_t timer_args = {
        .callback = fan_update_callback,
        .name = "fan_update"
    };
    esp_timer_create(&timer_args, &s_update_timer);
    
#ifdef CONFIG_TS_DRIVERS_FAN_TEMP_UPDATE_MS
    esp_timer_start_periodic(s_update_timer, CONFIG_TS_DRIVERS_FAN_TEMP_UPDATE_MS * 1000);
#else
    esp_timer_start_periodic(s_update_timer, 1000000);  // 1 秒
#endif
    
    /* 订阅温度更新事件 */
    esp_err_t ret = ts_event_register(
        TS_EVENT_BASE_TEMP,
        TS_EVT_TEMP_UPDATED,
        temp_event_handler,
        NULL,
        &s_temp_event_handle
    );
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Subscribed to temperature events");
    } else {
        TS_LOGW(TAG, "Failed to subscribe temp events: %s", esp_err_to_name(ret));
    }
    
    s_initialized = true;
    TS_LOGI(TAG, "Fan driver initialized");
    
    return ESP_OK;
}

esp_err_t ts_fan_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    /* 注销温度事件订阅 */
    if (s_temp_event_handle != NULL) {
        ts_event_unregister(s_temp_event_handle);
        s_temp_event_handle = NULL;
    }
    
    if (s_update_timer) {
        esp_timer_stop(s_update_timer);
        esp_timer_delete(s_update_timer);
        s_update_timer = NULL;
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (s_fans[i].pwm) {
            ts_pwm_destroy(s_fans[i].pwm);
            s_fans[i].pwm = NULL;
        }
        if (s_fans[i].tach_gpio) {
            ts_gpio_destroy(s_fans[i].tach_gpio);
            s_fans[i].tach_gpio = NULL;
        }
    }
    
    s_initialized = false;
    return ESP_OK;
}

bool ts_fan_is_initialized(void)
{
    return s_initialized;
}

/*===========================================================================*/
/*                          Public API - Configuration                        */
/*===========================================================================*/

void ts_fan_get_default_config(ts_fan_config_t *config)
{
    if (!config) return;
    
    memset(config, 0, sizeof(ts_fan_config_t));
    config->gpio_pwm = -1;
    config->gpio_tach = -1;
    config->min_duty = 20;
    config->max_duty = 100;
    config->hysteresis = TS_FAN_DEFAULT_HYSTERESIS;
    config->min_interval = TS_FAN_DEFAULT_MIN_INTERVAL;
    config->invert_pwm = false;
    
    // 默认曲线
    ts_fan_curve_point_t default_curve[] = {
        {300, 20}, {500, 40}, {700, 80}, {800, 100}
    };
    memcpy(config->curve, default_curve, sizeof(default_curve));
    config->curve_points = 4;
}

esp_err_t ts_fan_get_config(ts_fan_id_t fan, ts_fan_config_t *config)
{
    if (fan >= TS_FAN_MAX || !config) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    *config = s_fans[fan].config;
    return ESP_OK;
}

esp_err_t ts_fan_configure(ts_fan_id_t fan, const ts_fan_config_t *config)
{
    if (fan >= TS_FAN_MAX || !config) return ESP_ERR_INVALID_ARG;
    
    fan_instance_t *f = &s_fans[fan];
    f->config = *config;
    
    // 创建 PWM 句柄
    f->pwm = ts_pwm_create_raw(config->gpio_pwm, "fan");
    if (!f->pwm) {
        TS_LOGE(TAG, "Failed to create PWM for fan %d", fan);
        return ESP_FAIL;
    }
    
    // 配置 PWM (25kHz PC 风扇标准)
    ts_pwm_config_t pwm_cfg = {
        .frequency = 25000,
        .resolution_bits = 10,
        .timer = TS_PWM_TIMER_AUTO,
        .invert = config->invert_pwm,
        .initial_duty = 0.0f
    };
    esp_err_t ret = ts_pwm_configure(f->pwm, &pwm_cfg);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to configure PWM for fan %d", fan);
        ts_pwm_destroy(f->pwm);
        f->pwm = NULL;
        return ret;
    }
    
    // 配置转速计（如果有）
    if (config->gpio_tach >= 0) {
        f->tach_gpio = ts_gpio_create_raw(config->gpio_tach, "fan_tach");
        if (f->tach_gpio) {
            ts_gpio_config_t gpio_cfg = {
                .direction = TS_GPIO_DIR_INPUT,
                .pull_mode = TS_GPIO_PULL_UP,
                .intr_type = TS_GPIO_INTR_NEGEDGE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = -1
            };
            ts_gpio_configure(f->tach_gpio, &gpio_cfg);
            ts_gpio_set_isr_callback(f->tach_gpio, tach_isr_callback, f);
            ts_gpio_intr_enable(f->tach_gpio);
            f->last_tach_time = esp_timer_get_time();
        }
    }
    
    f->initialized = true;
    f->enabled = true;
    f->mode = TS_FAN_MODE_MANUAL;
    f->current_duty = config->min_duty;
    f->fault = false;
    
    TS_LOGI(TAG, "Fan %d configured: PWM=GPIO%d, TACH=%d, curve=%d points", 
            fan, config->gpio_pwm, config->gpio_tach, config->curve_points);
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API - Control                              */
/*===========================================================================*/

esp_err_t ts_fan_set_mode(ts_fan_id_t fan, ts_fan_mode_t mode)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    ts_fan_mode_t old_mode = s_fans[fan].mode;
    s_fans[fan].mode = mode;
    
    if (mode == TS_FAN_MODE_OFF) {
        update_pwm(&s_fans[fan], 0);
    }
    
    /* 切换到曲线/自动模式时，重置迟滞状态以允许立即生效 */
    if ((mode == TS_FAN_MODE_CURVE || mode == TS_FAN_MODE_AUTO) && old_mode != mode) {
        /* 将 last_stable_temp 设为一个远离实际温度的值，确保首次评估通过迟滞检查 */
        s_fans[fan].last_stable_temp = -1000;   /* -100.0°C，远低于任何实际温度 */
        s_fans[fan].last_speed_change_time = 0;  /* 允许立即调速 */
        
        /* 立即获取当前温度 */
        if (s_auto_temp_enabled) {
            ts_temp_data_t temp_data;
            int16_t current_temp = ts_temp_get_effective(&temp_data);
            if (current_temp > TS_TEMP_MIN_VALID) {
                s_fans[fan].temperature = current_temp;
            }
        }
        
        TS_LOGI(TAG, "Fan %d mode -> %d: hysteresis reset, temp=%d", 
                fan, mode, s_fans[fan].temperature);
    } else {
        TS_LOGI(TAG, "Fan %d mode set to %d", fan, mode);
    }
    
    return ESP_OK;
}

esp_err_t ts_fan_get_mode(ts_fan_id_t fan, ts_fan_mode_t *mode)
{
    if (fan >= TS_FAN_MAX || !mode) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    *mode = s_fans[fan].mode;
    return ESP_OK;
}

esp_err_t ts_fan_set_duty(ts_fan_id_t fan, uint8_t duty_percent)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    if (duty_percent > 100) duty_percent = 100;
    
    s_fans[fan].mode = TS_FAN_MODE_MANUAL;
    s_fans[fan].current_duty = duty_percent;
    
    return update_pwm(&s_fans[fan], duty_percent);
}

esp_err_t ts_fan_enable(ts_fan_id_t fan, bool enable)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    s_fans[fan].enabled = enable;
    
    if (!enable) {
        update_pwm(&s_fans[fan], 0);
    }
    
    TS_LOGI(TAG, "Fan %d %s", fan, enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t ts_fan_is_enabled(ts_fan_id_t fan, bool *enabled)
{
    if (fan >= TS_FAN_MAX || !enabled) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    *enabled = s_fans[fan].enabled;
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API - Temperature & Curve                  */
/*===========================================================================*/

esp_err_t ts_fan_set_temperature(ts_fan_id_t fan, int16_t temp_01c)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    s_fans[fan].temperature = temp_01c;
    return ESP_OK;
}

esp_err_t ts_fan_set_curve(ts_fan_id_t fan, const ts_fan_curve_point_t *curve, uint8_t points)
{
    if (fan >= TS_FAN_MAX || !curve || points > TS_FAN_MAX_CURVE_POINTS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 复制曲线点
    memcpy(s_fans[fan].config.curve, curve, points * sizeof(ts_fan_curve_point_t));
    s_fans[fan].config.curve_points = points;
    
    // 按温度排序（冒泡）
    for (int i = 0; i < points - 1; i++) {
        for (int j = 0; j < points - i - 1; j++) {
            if (s_fans[fan].config.curve[j].temp > s_fans[fan].config.curve[j + 1].temp) {
                ts_fan_curve_point_t tmp = s_fans[fan].config.curve[j];
                s_fans[fan].config.curve[j] = s_fans[fan].config.curve[j + 1];
                s_fans[fan].config.curve[j + 1] = tmp;
            }
        }
    }
    
    TS_LOGI(TAG, "Fan %d curve set with %d points", fan, points);
    return ESP_OK;
}

esp_err_t ts_fan_set_hysteresis(ts_fan_id_t fan, int16_t hysteresis, uint32_t min_interval)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    
    if (hysteresis < 0 || hysteresis > 200) {  // 0-20°C
        return ESP_ERR_INVALID_ARG;
    }
    if (min_interval < 100 || min_interval > 60000) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_fans[fan].config.hysteresis = hysteresis;
    s_fans[fan].config.min_interval = min_interval;
    
    TS_LOGI(TAG, "Fan %d hysteresis: %.1f°C, interval: %lums", 
            fan, hysteresis / 10.0f, (unsigned long)min_interval);
    
    return ESP_OK;
}

esp_err_t ts_fan_set_limits(ts_fan_id_t fan, uint8_t min_duty, uint8_t max_duty)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    
    if (min_duty > 100 || max_duty > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    if (min_duty > max_duty) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_fans[fan].config.min_duty = min_duty;
    s_fans[fan].config.max_duty = max_duty;
    
    TS_LOGI(TAG, "Fan %d limits: min=%d%%, max=%d%%", fan, min_duty, max_duty);
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API - Status                               */
/*===========================================================================*/

esp_err_t ts_fan_get_status(ts_fan_id_t fan, ts_fan_status_t *status)
{
    if (fan >= TS_FAN_MAX || !status) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    fan_instance_t *f = &s_fans[fan];
    
    /* 同步获取最新温度（确保从绑定变量读取最新值） */
    if (f->mode == TS_FAN_MODE_AUTO || f->mode == TS_FAN_MODE_CURVE) {
        ts_temp_data_t temp_data;
        int16_t temp = ts_temp_get_effective(&temp_data);
        if (temp > TS_TEMP_MIN_VALID) {
            f->temperature = temp;
            
            /* 计算基于当前温度的目标转速（用于显示） */
            uint8_t computed_target = calc_duty_from_curve(f, f->temperature);
            if (computed_target < f->config.min_duty && computed_target > 0) {
                computed_target = f->config.min_duty;
            }
            if (computed_target > f->config.max_duty) {
                computed_target = f->config.max_duty;
            }
            f->target_duty = computed_target;
        } else {
            /* 温度无效时，目标转速等于当前转速 */
            f->target_duty = f->current_duty;
        }
    } else {
        /* MANUAL/OFF 模式：目标转速等于当前转速 */
        f->target_duty = f->current_duty;
    }
    
    status->mode = f->mode;
    status->duty_percent = f->current_duty;
    status->target_duty = f->target_duty;
    status->rpm = f->rpm;
    status->temp = f->temperature;
    status->last_stable_temp = f->last_stable_temp;
    status->is_running = f->current_duty > 0 && f->enabled;
    status->enabled = f->enabled;
    status->fault = f->fault;
    
    return ESP_OK;
}

esp_err_t ts_fan_get_all_status(ts_fan_status_t *status_array, uint8_t array_size)
{
    if (!status_array || array_size < TS_FAN_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        ts_fan_get_status(i, &status_array[i]);
    }
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API - Emergency                            */
/*===========================================================================*/

esp_err_t ts_fan_emergency_full(void)
{
    TS_LOGW(TAG, "Emergency: All fans to 100%%");
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (s_fans[i].initialized) {
            s_fans[i].mode = TS_FAN_MODE_MANUAL;
            s_fans[i].enabled = true;
            s_fans[i].current_duty = 100;
            update_pwm(&s_fans[i], 100);
        }
    }
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API - Persistence                          */
/*===========================================================================*/

/**
 * @brief NVS 配置结构（包含完整运行时状态）
 */
typedef struct {
    uint32_t version;
    ts_fan_mode_t mode;
    uint8_t duty;
    bool enabled;
    uint8_t curve_points;
    ts_fan_curve_point_t curve[TS_FAN_MAX_CURVE_POINTS];
    int16_t hysteresis;
    uint32_t min_interval;
    uint8_t min_duty;
    uint8_t max_duty;
    bool invert_pwm;
} fan_nvs_config_t;

esp_err_t ts_fan_save_config(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(FAN_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (!s_fans[i].initialized) continue;
        
        char key[16];
        snprintf(key, sizeof(key), "fan%d", i);
        
        fan_nvs_config_t cfg = {
            .version = FAN_CONFIG_VERSION,
            .mode = s_fans[i].mode,
            .duty = s_fans[i].current_duty,
            .enabled = s_fans[i].enabled,
            .curve_points = s_fans[i].config.curve_points,
            .hysteresis = s_fans[i].config.hysteresis,
            .min_interval = s_fans[i].config.min_interval,
            .min_duty = s_fans[i].config.min_duty,
            .max_duty = s_fans[i].config.max_duty,
            .invert_pwm = s_fans[i].config.invert_pwm,
        };
        memcpy(cfg.curve, s_fans[i].config.curve, 
               s_fans[i].config.curve_points * sizeof(ts_fan_curve_point_t));
        
        ret = nvs_set_blob(nvs, key, &cfg, sizeof(cfg));
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to save fan %d: %s", i, esp_err_to_name(ret));
        } else {
            TS_LOGI(TAG, "Saved fan %d: mode=%d, duty=%d%%, curve=%d pts", 
                    i, cfg.mode, cfg.duty, cfg.curve_points);
        }
    }
    
    ret = nvs_commit(nvs);
    nvs_close(nvs);
    
    return ret;
}

esp_err_t ts_fan_save_full_config(ts_fan_id_t fan)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(FAN_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }
    
    char key[16];
    snprintf(key, sizeof(key), "fan%d", fan);
    
    fan_nvs_config_t cfg = {
        .version = FAN_CONFIG_VERSION,
        .mode = s_fans[fan].mode,
        .duty = s_fans[fan].current_duty,
        .enabled = s_fans[fan].enabled,
        .curve_points = s_fans[fan].config.curve_points,
        .hysteresis = s_fans[fan].config.hysteresis,
        .min_interval = s_fans[fan].config.min_interval,
        .min_duty = s_fans[fan].config.min_duty,
        .max_duty = s_fans[fan].config.max_duty,
        .invert_pwm = s_fans[fan].config.invert_pwm,
    };
    memcpy(cfg.curve, s_fans[fan].config.curve, 
           s_fans[fan].config.curve_points * sizeof(ts_fan_curve_point_t));
    
    ret = nvs_set_blob(nvs, key, &cfg, sizeof(cfg));
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    
    nvs_close(nvs);
    return ret;
}

esp_err_t ts_fan_load_config(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(FAN_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        TS_LOGD(TAG, "No saved fan config found");
        return ESP_ERR_NOT_FOUND;
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (!s_fans[i].initialized) continue;
        
        char key[16];
        snprintf(key, sizeof(key), "fan%d", i);
        
        fan_nvs_config_t cfg;
        size_t len = sizeof(cfg);
        
        if (nvs_get_blob(nvs, key, &cfg, &len) != ESP_OK) {
            continue;
        }
        
        // 版本检查
        if (cfg.version != FAN_CONFIG_VERSION) {
            TS_LOGW(TAG, "Fan %d config version mismatch, skipping", i);
            continue;
        }
        
        // 恢复配置
        s_fans[i].mode = cfg.mode;
        s_fans[i].current_duty = cfg.duty;
        s_fans[i].enabled = cfg.enabled;
        s_fans[i].config.curve_points = cfg.curve_points;
        s_fans[i].config.hysteresis = cfg.hysteresis;
        s_fans[i].config.min_interval = cfg.min_interval;
        s_fans[i].config.min_duty = cfg.min_duty;
        s_fans[i].config.max_duty = cfg.max_duty;
        s_fans[i].config.invert_pwm = cfg.invert_pwm;
        memcpy(s_fans[i].config.curve, cfg.curve, 
               cfg.curve_points * sizeof(ts_fan_curve_point_t));
        
        // 应用占空比
        if (s_fans[i].mode == TS_FAN_MODE_MANUAL && s_fans[i].pwm) {
            update_pwm(&s_fans[i], cfg.duty);
        }
        
        TS_LOGI(TAG, "Restored fan %d: mode=%d, duty=%d%%, curve=%d pts", 
                i, cfg.mode, cfg.duty, cfg.curve_points);
    }
    
    nvs_close(nvs);
    return ESP_OK;
}
/*===========================================================================*/
/*                          Temperature Source Integration                    */
/*===========================================================================*/

esp_err_t ts_fan_set_auto_temp_enabled(bool enable)
{
    s_auto_temp_enabled = enable;
    TS_LOGI(TAG, "Auto temperature subscription %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

bool ts_fan_is_auto_temp_enabled(void)
{
    return s_auto_temp_enabled;
}