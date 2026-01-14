/**
 * @file ts_fan.c
 * @brief Fan Control Driver Implementation
 */

#include "ts_fan.h"
#include "ts_hal_pwm.h"
#include "ts_hal_gpio.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "ts_fan"

typedef struct {
    bool initialized;
    ts_fan_config_t config;
    ts_pwm_handle_t pwm;
    ts_fan_mode_t mode;
    uint8_t current_duty;
    uint16_t rpm;
    int16_t temperature;
    volatile uint32_t tach_count;
    int64_t last_tach_time;
} fan_instance_t;

static fan_instance_t s_fans[TS_FAN_MAX];
static esp_timer_handle_t s_update_timer = NULL;
static bool s_initialized = false;

static void IRAM_ATTR tach_isr(void *arg)
{
    fan_instance_t *fan = (fan_instance_t *)arg;
    fan->tach_count++;
}

static uint8_t calc_duty_from_curve(fan_instance_t *fan, int16_t temp)
{
    if (fan->config.curve_points == 0) {
        return fan->config.max_duty;
    }
    
    const ts_fan_curve_point_t *curve = fan->config.curve;
    uint8_t points = fan->config.curve_points;
    
    if (temp <= curve[0].temp) return curve[0].duty;
    if (temp >= curve[points - 1].temp) return curve[points - 1].duty;
    
    for (int i = 0; i < points - 1; i++) {
        if (temp >= curve[i].temp && temp < curve[i + 1].temp) {
            int32_t t_range = curve[i + 1].temp - curve[i].temp;
            int32_t d_range = curve[i + 1].duty - curve[i].duty;
            return curve[i].duty + (temp - curve[i].temp) * d_range / t_range;
        }
    }
    return fan->config.max_duty;
}

static void fan_update_callback(void *arg)
{
    int64_t now = esp_timer_get_time();
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        fan_instance_t *fan = &s_fans[i];
        if (!fan->initialized) continue;
        
        // Calculate RPM
        if (fan->config.gpio_tach >= 0) {
            int64_t dt_us = now - fan->last_tach_time;
            if (dt_us > 0) {
                fan->rpm = (fan->tach_count * 60 * 1000000) / (dt_us * 2);
                fan->tach_count = 0;
                fan->last_tach_time = now;
            }
        }
        
        // Auto mode
        if (fan->mode == TS_FAN_MODE_AUTO) {
            uint8_t target = calc_duty_from_curve(fan, fan->temperature);
            if (target < fan->config.min_duty && target > 0) {
                target = fan->config.min_duty;
            }
            if (target > fan->config.max_duty) {
                target = fan->config.max_duty;
            }
            if (target != fan->current_duty) {
                fan->current_duty = target;
                ts_pwm_set_duty(fan->pwm, target);
            }
        }
    }
}

esp_err_t ts_fan_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(s_fans, 0, sizeof(s_fans));
    
    // Default curves for all fans
    ts_fan_curve_point_t default_curve[] = {
        {300, 20}, {400, 40}, {500, 60}, {600, 80}, {700, 100}
    };
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        s_fans[i].config.min_duty = 20;
        s_fans[i].config.max_duty = 100;
        memcpy(s_fans[i].config.curve, default_curve, sizeof(default_curve));
        s_fans[i].config.curve_points = 5;
    }
    
    // Start update timer
    esp_timer_create_args_t timer_args = {
        .callback = fan_update_callback,
        .name = "fan_update"
    };
    esp_timer_create(&timer_args, &s_update_timer);
    
#ifdef CONFIG_TS_DRIVERS_FAN_TEMP_UPDATE_MS
    esp_timer_start_periodic(s_update_timer, CONFIG_TS_DRIVERS_FAN_TEMP_UPDATE_MS * 1000);
#else
    esp_timer_start_periodic(s_update_timer, 1000000);
#endif
    
    s_initialized = true;
    TS_LOGI(TAG, "Fan driver initialized");
    return ESP_OK;
}

esp_err_t ts_fan_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    if (s_update_timer) {
        esp_timer_stop(s_update_timer);
        esp_timer_delete(s_update_timer);
        s_update_timer = NULL;
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (s_fans[i].pwm) {
            ts_pwm_deinit(s_fans[i].pwm);
            s_fans[i].pwm = NULL;
        }
    }
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_fan_configure(ts_fan_id_t fan, const ts_fan_config_t *config)
{
    if (fan >= TS_FAN_MAX || !config) return ESP_ERR_INVALID_ARG;
    
    fan_instance_t *f = &s_fans[fan];
    f->config = *config;
    
    // Initialize PWM
    ts_pwm_config_t pwm_cfg = {
        .gpio = config->gpio_pwm,
#ifdef CONFIG_TS_DRIVERS_FAN_PWM_FREQ
        .freq_hz = CONFIG_TS_DRIVERS_FAN_PWM_FREQ,
#else
        .freq_hz = 25000,
#endif
        .resolution_bits = 8,
        .duty_percent = 0
    };
    
    esp_err_t ret = ts_pwm_init(&pwm_cfg, &f->pwm);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to init PWM for fan %d", fan);
        return ret;
    }
    
    // Initialize tachometer
    if (config->gpio_tach >= 0) {
        ts_gpio_config_t gpio_cfg = {
            .gpio = config->gpio_tach,
            .direction = TS_GPIO_DIR_INPUT,
            .pull = TS_GPIO_PULL_UP,
            .intr_type = TS_GPIO_INTR_NEGEDGE
        };
        ts_gpio_init(&gpio_cfg);
        ts_gpio_set_isr_handler(config->gpio_tach, tach_isr, f);
        f->last_tach_time = esp_timer_get_time();
    }
    
    f->initialized = true;
    f->mode = TS_FAN_MODE_OFF;
    
    TS_LOGI(TAG, "Fan %d configured: PWM=%d, TACH=%d", fan, config->gpio_pwm, config->gpio_tach);
    return ESP_OK;
}

esp_err_t ts_fan_set_mode(ts_fan_id_t fan, ts_fan_mode_t mode)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    s_fans[fan].mode = mode;
    
    if (mode == TS_FAN_MODE_OFF) {
        ts_pwm_set_duty(s_fans[fan].pwm, 0);
        s_fans[fan].current_duty = 0;
    }
    
    return ESP_OK;
}

esp_err_t ts_fan_set_duty(ts_fan_id_t fan, uint8_t duty_percent)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    if (duty_percent > 100) duty_percent = 100;
    
    s_fans[fan].mode = TS_FAN_MODE_MANUAL;
    s_fans[fan].current_duty = duty_percent;
    
    return ts_pwm_set_duty(s_fans[fan].pwm, duty_percent);
}

esp_err_t ts_fan_set_temperature(ts_fan_id_t fan, int16_t temp_01c)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    s_fans[fan].temperature = temp_01c;
    return ESP_OK;
}

esp_err_t ts_fan_get_status(ts_fan_id_t fan, ts_fan_status_t *status)
{
    if (fan >= TS_FAN_MAX || !status) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    status->mode = s_fans[fan].mode;
    status->duty_percent = s_fans[fan].current_duty;
    status->rpm = s_fans[fan].rpm;
    status->temp = s_fans[fan].temperature;
    status->is_running = s_fans[fan].current_duty > 0;
    
    return ESP_OK;
}

esp_err_t ts_fan_set_curve(ts_fan_id_t fan, const ts_fan_curve_point_t *curve, uint8_t points)
{
    if (fan >= TS_FAN_MAX || !curve || points > 8) return ESP_ERR_INVALID_ARG;
    
    memcpy(s_fans[fan].config.curve, curve, points * sizeof(ts_fan_curve_point_t));
    s_fans[fan].config.curve_points = points;
    
    return ESP_OK;
}

esp_err_t ts_fan_emergency_full(void)
{
    TS_LOGW(TAG, "Emergency: All fans to 100%%");
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (s_fans[i].initialized) {
            s_fans[i].mode = TS_FAN_MODE_MANUAL;
            s_fans[i].current_duty = 100;
            ts_pwm_set_duty(s_fans[i].pwm, 100);
        }
    }
    return ESP_OK;
}
