/**
 * @file ts_pwm.c
 * @brief TianShanOS PWM Abstraction Implementation
 * 
 * Uses ESP-IDF LEDC driver for PWM output.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_hal_pwm.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ts_pwm"

/*===========================================================================*/
/*                              Private Types                                 */
/*===========================================================================*/

struct ts_pwm_s {
    int gpio_num;                       /* Physical GPIO number */
    ts_pin_function_t function;         /* Logical function (if any) */
    ts_pwm_config_t config;             /* Current configuration */
    ledc_channel_t channel;             /* LEDC channel */
    ledc_timer_t timer;                 /* LEDC timer */
    bool configured;                    /* Is configured */
    bool using_function;                /* Using pin manager function */
    ts_pwm_fade_cb_t fade_cb;           /* Fade callback */
    void *fade_user_data;               /* Fade callback user data */
    char owner[32];                     /* Owner name */
};

/*===========================================================================*/
/*                              Private Data                                  */
/*===========================================================================*/

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static ts_pwm_handle_t s_handles[CONFIG_TS_HAL_MAX_PWM_HANDLES];
static uint8_t s_channel_used = 0;      /* Bitmask of used channels */
static uint8_t s_timer_used = 0;        /* Bitmask of used timers */
static bool s_fade_service_installed = false;

/*===========================================================================*/
/*                          Private Functions                                 */
/*===========================================================================*/

static ledc_channel_t alloc_channel(void)
{
    for (int i = 0; i < LEDC_CHANNEL_MAX; i++) {
        if (!(s_channel_used & (1 << i))) {
            s_channel_used |= (1 << i);
            return (ledc_channel_t)i;
        }
    }
    return LEDC_CHANNEL_MAX;
}

static void free_channel(ledc_channel_t channel)
{
    if (channel < LEDC_CHANNEL_MAX) {
        s_channel_used &= ~(1 << channel);
    }
}

static ledc_timer_t alloc_timer(void)
{
    for (int i = 0; i < LEDC_TIMER_MAX; i++) {
        if (!(s_timer_used & (1 << i))) {
            s_timer_used |= (1 << i);
            return (ledc_timer_t)i;
        }
    }
    return LEDC_TIMER_MAX;
}

static void free_timer(ledc_timer_t timer)
{
    if (timer < LEDC_TIMER_MAX) {
        s_timer_used &= ~(1 << timer);
    }
}

static int get_handle_slot(ts_pwm_handle_t handle)
{
    for (int i = 0; i < CONFIG_TS_HAL_MAX_PWM_HANDLES; i++) {
        if (s_handles[i] == handle) {
            return i;
        }
    }
    return -1;
}

/*===========================================================================*/
/*                         Public Functions                                   */
/*===========================================================================*/

esp_err_t ts_pwm_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Initializing PWM subsystem");
    
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(s_handles, 0, sizeof(s_handles));
    s_channel_used = 0;
    s_timer_used = 0;
    s_fade_service_installed = false;
    s_initialized = true;
    
    return ESP_OK;
}

esp_err_t ts_pwm_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Deinitializing PWM subsystem");
    
    /* Destroy all handles */
    for (int i = 0; i < CONFIG_TS_HAL_MAX_PWM_HANDLES; i++) {
        if (s_handles[i] != NULL) {
            ts_pwm_destroy(s_handles[i]);
        }
    }
    
    if (s_fade_service_installed) {
        ledc_fade_func_uninstall();
        s_fade_service_installed = false;
    }
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    return ESP_OK;
}

ts_pwm_handle_t ts_pwm_create(ts_pin_function_t function, const char *owner)
{
    if (!s_initialized || owner == NULL) {
        return NULL;
    }
    
    int gpio_num = ts_pin_manager_get_gpio(function);
    if (gpio_num < 0) {
        TS_LOGE(TAG, "Function %d has no GPIO mapping", function);
        return NULL;
    }
    
    /* Acquire pin from manager */
    esp_err_t ret = ts_pin_manager_acquire(function, owner);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to acquire pin for function %d", function);
        return NULL;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_TS_HAL_MAX_PWM_HANDLES; i++) {
        if (s_handles[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        ts_pin_manager_release(function);
        TS_LOGE(TAG, "No free PWM handles");
        return NULL;
    }
    
    /* Allocate channel */
    ledc_channel_t channel = alloc_channel();
    if (channel >= LEDC_CHANNEL_MAX) {
        xSemaphoreGive(s_mutex);
        ts_pin_manager_release(function);
        TS_LOGE(TAG, "No free PWM channels");
        return NULL;
    }
    
    /* Allocate handle */
    ts_pwm_handle_t handle = calloc(1, sizeof(struct ts_pwm_s));
    if (handle == NULL) {
        free_channel(channel);
        xSemaphoreGive(s_mutex);
        ts_pin_manager_release(function);
        return NULL;
    }
    
    handle->gpio_num = gpio_num;
    handle->function = function;
    handle->channel = channel;
    handle->timer = LEDC_TIMER_MAX;
    handle->using_function = true;
    handle->configured = false;
    strncpy(handle->owner, owner, sizeof(handle->owner) - 1);
    
    s_handles[slot] = handle;
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "Created PWM handle for function %d (GPIO%d, channel %d), owner: %s",
            function, gpio_num, channel, owner);
    
    return handle;
}

ts_pwm_handle_t ts_pwm_create_raw(int gpio_num, const char *owner)
{
    if (!s_initialized || owner == NULL || gpio_num < 0) {
        return NULL;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_TS_HAL_MAX_PWM_HANDLES; i++) {
        if (s_handles[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "No free PWM handles");
        return NULL;
    }
    
    /* Allocate channel */
    ledc_channel_t channel = alloc_channel();
    if (channel >= LEDC_CHANNEL_MAX) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "No free PWM channels");
        return NULL;
    }
    
    /* Allocate handle */
    ts_pwm_handle_t handle = calloc(1, sizeof(struct ts_pwm_s));
    if (handle == NULL) {
        free_channel(channel);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    handle->gpio_num = gpio_num;
    handle->function = TS_PIN_FUNC_MAX;
    handle->channel = channel;
    handle->timer = LEDC_TIMER_MAX;
    handle->using_function = false;
    handle->configured = false;
    strncpy(handle->owner, owner, sizeof(handle->owner) - 1);
    
    s_handles[slot] = handle;
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "Created raw PWM handle for GPIO%d (channel %d), owner: %s",
            gpio_num, channel, owner);
    
    return handle;
}

esp_err_t ts_pwm_configure(ts_pwm_handle_t handle, const ts_pwm_config_t *config)
{
    if (!s_initialized || handle == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Allocate timer if needed */
    ledc_timer_t timer;
    if (config->timer == TS_PWM_TIMER_AUTO) {
        timer = alloc_timer();
        if (timer >= LEDC_TIMER_MAX) {
            xSemaphoreGive(s_mutex);
            TS_LOGE(TAG, "No free PWM timers");
            return ESP_ERR_NO_MEM;
        }
    } else {
        timer = (ledc_timer_t)config->timer;
    }
    
    /* Configure timer */
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = timer,
        .duty_resolution = (ledc_timer_bit_t)config->resolution_bits,
        .freq_hz = config->frequency,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        if (config->timer == TS_PWM_TIMER_AUTO) {
            free_timer(timer);
        }
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "Timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Configure channel */
    uint32_t max_duty = (1 << config->resolution_bits) - 1;
    uint32_t initial_duty = (uint32_t)(config->initial_duty * max_duty / 100.0f);
    
    ledc_channel_config_t channel_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = handle->channel,
        .timer_sel = timer,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = handle->gpio_num,
        .duty = initial_duty,
        .hpoint = 0,
        .flags.output_invert = config->invert ? 1 : 0,
    };
    
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        if (config->timer == TS_PWM_TIMER_AUTO) {
            free_timer(timer);
        }
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "Channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Free old timer if different */
    if (handle->configured && handle->timer != timer && 
        handle->timer < LEDC_TIMER_MAX) {
        free_timer(handle->timer);
    }
    
    handle->timer = timer;
    handle->config = *config;
    handle->configured = true;
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "PWM configured: GPIO%d, freq=%lu, res=%d, duty=%.1f%%",
            handle->gpio_num, config->frequency, config->resolution_bits, 
            config->initial_duty);
    
    return ESP_OK;
}

esp_err_t ts_pwm_set_duty(ts_pwm_handle_t handle, float duty_percent)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (duty_percent < 0.0f) duty_percent = 0.0f;
    if (duty_percent > 100.0f) duty_percent = 100.0f;
    
    uint32_t max_duty = (1 << handle->config.resolution_bits) - 1;
    uint32_t duty = (uint32_t)(duty_percent * max_duty / 100.0f);
    
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    }
    
    return ret;
}

esp_err_t ts_pwm_set_duty_raw(ts_pwm_handle_t handle, uint32_t duty)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    }
    
    return ret;
}

float ts_pwm_get_duty(ts_pwm_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return -1.0f;
    }
    
    uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    uint32_t max_duty = (1 << handle->config.resolution_bits) - 1;
    
    return (float)duty * 100.0f / max_duty;
}

uint32_t ts_pwm_get_duty_raw(ts_pwm_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return 0;
    }
    
    return ledc_get_duty(LEDC_LOW_SPEED_MODE, handle->channel);
}

esp_err_t ts_pwm_set_frequency(ts_pwm_handle_t handle, uint32_t frequency)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ledc_set_freq(LEDC_LOW_SPEED_MODE, handle->timer, frequency);
    if (ret == ESP_OK) {
        handle->config.frequency = frequency;
    }
    
    return ret;
}

uint32_t ts_pwm_get_frequency(ts_pwm_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return 0;
    }
    
    return ledc_get_freq(LEDC_LOW_SPEED_MODE, handle->timer);
}

esp_err_t ts_pwm_fade_start(ts_pwm_handle_t handle, float target_duty,
                            uint32_t duration_ms, ts_pwm_fade_mode_t mode)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Install fade service if not done */
    if (!s_fade_service_installed) {
        esp_err_t ret = ledc_fade_func_install(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        s_fade_service_installed = true;
    }
    
    if (target_duty < 0.0f) target_duty = 0.0f;
    if (target_duty > 100.0f) target_duty = 100.0f;
    
    uint32_t max_duty = (1 << handle->config.resolution_bits) - 1;
    uint32_t duty = (uint32_t)(target_duty * max_duty / 100.0f);
    
    esp_err_t ret = ledc_set_fade_time_and_start(
        LEDC_LOW_SPEED_MODE,
        handle->channel,
        duty,
        duration_ms,
        mode == TS_PWM_FADE_WAIT ? LEDC_FADE_WAIT_DONE : LEDC_FADE_NO_WAIT
    );
    
    return ret;
}

esp_err_t ts_pwm_set_fade_callback(ts_pwm_handle_t handle,
                                    ts_pwm_fade_cb_t callback,
                                    void *user_data)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->fade_cb = callback;
    handle->fade_user_data = user_data;
    
    /* Note: ESP-IDF LEDC doesn't directly support fade callbacks per channel
     * in the simple API. Would need to use ledc_cbs_t for ISR-based callbacks.
     * This is a placeholder for future enhancement. */
    
    return ESP_OK;
}

esp_err_t ts_pwm_stop(ts_pwm_handle_t handle, bool hold_low)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ledc_stop(LEDC_LOW_SPEED_MODE, handle->channel, hold_low ? 0 : 1);
    return ret;
}

esp_err_t ts_pwm_start(ts_pwm_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Re-apply current duty to restart */
    uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, handle->channel, duty);
    if (ret == ESP_OK) {
        ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, handle->channel);
    }
    
    return ret;
}

uint32_t ts_pwm_get_max_duty(ts_pwm_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return 0;
    }
    
    return (1 << handle->config.resolution_bits) - 1;
}

esp_err_t ts_pwm_destroy(ts_pwm_handle_t handle)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Stop PWM */
    if (handle->configured) {
        ledc_stop(LEDC_LOW_SPEED_MODE, handle->channel, 0);
    }
    
    /* Free resources */
    free_channel(handle->channel);
    if (handle->timer < LEDC_TIMER_MAX) {
        free_timer(handle->timer);
    }
    
    /* Find and remove from handles array */
    int slot = get_handle_slot(handle);
    if (slot >= 0) {
        s_handles[slot] = NULL;
    }
    
    xSemaphoreGive(s_mutex);
    
    /* Release pin if using function */
    if (handle->using_function) {
        ts_pin_manager_release(handle->function);
    }
    
    TS_LOGD(TAG, "Destroyed PWM handle for GPIO%d", handle->gpio_num);
    
    free(handle);
    return ESP_OK;
}
