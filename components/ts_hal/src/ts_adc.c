/**
 * @file ts_adc.c
 * @brief TianShanOS ADC Abstraction Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_hal_adc.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ts_adc"

/*===========================================================================*/
/*                              Private Types                                 */
/*===========================================================================*/

struct ts_adc_s {
    ts_pin_function_t function;             /* Pin function */
    int gpio_num;                           /* GPIO number */
    ts_adc_config_t config;                 /* Configuration */
    adc_unit_t unit;                        /* ADC unit */
    adc_channel_t channel;                  /* ADC channel */
    adc_oneshot_unit_handle_t unit_handle;  /* ADC unit handle */
    adc_cali_handle_t cali_handle;          /* Calibration handle */
    bool has_calibration;                   /* Has valid calibration */
    bool configured;                        /* Is configured */
    char owner[32];                         /* Owner name */
};

/*===========================================================================*/
/*                              Private Data                                  */
/*===========================================================================*/

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static ts_adc_handle_t s_handles[CONFIG_TS_HAL_MAX_ADC_HANDLES];
static adc_oneshot_unit_handle_t s_adc1_handle = NULL;
static adc_oneshot_unit_handle_t s_adc2_handle = NULL;
static int s_adc1_ref_count = 0;
static int s_adc2_ref_count = 0;

/*===========================================================================*/
/*                          Private Functions                                 */
/*===========================================================================*/

static int get_handle_slot(ts_adc_handle_t handle)
{
    for (int i = 0; i < CONFIG_TS_HAL_MAX_ADC_HANDLES; i++) {
        if (s_handles[i] == handle) {
            return i;
        }
    }
    return -1;
}

static adc_atten_t convert_atten(ts_adc_atten_t atten)
{
    switch (atten) {
        case TS_ADC_ATTEN_0DB: return ADC_ATTEN_DB_0;
        case TS_ADC_ATTEN_2_5DB: return ADC_ATTEN_DB_2_5;
        case TS_ADC_ATTEN_6DB: return ADC_ATTEN_DB_6;
        default: return ADC_ATTEN_DB_12;
    }
}

static adc_bitwidth_t convert_width(ts_adc_width_t width)
{
    switch (width) {
        case TS_ADC_WIDTH_9BIT: return ADC_BITWIDTH_9;
        case TS_ADC_WIDTH_10BIT: return ADC_BITWIDTH_10;
        case TS_ADC_WIDTH_11BIT: return ADC_BITWIDTH_11;
        default: return ADC_BITWIDTH_12;
    }
}

/* GPIO to ADC channel mapping for ESP32S3 */
static bool gpio_to_adc_channel(int gpio_num, adc_unit_t *unit, adc_channel_t *channel)
{
#if CONFIG_IDF_TARGET_ESP32S3
    /* ADC1 channels */
    static const int adc1_gpios[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    for (int i = 0; i < sizeof(adc1_gpios)/sizeof(adc1_gpios[0]); i++) {
        if (gpio_num == adc1_gpios[i]) {
            *unit = ADC_UNIT_1;
            *channel = (adc_channel_t)i;
            return true;
        }
    }
    
    /* ADC2 channels */
    static const int adc2_gpios[] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
    for (int i = 0; i < sizeof(adc2_gpios)/sizeof(adc2_gpios[0]); i++) {
        if (gpio_num == adc2_gpios[i]) {
            *unit = ADC_UNIT_2;
            *channel = (adc_channel_t)i;
            return true;
        }
    }
#endif
    
    return false;
}

static esp_err_t init_calibration(ts_adc_handle_t handle)
{
    adc_cali_handle_t cali_handle = NULL;
    bool calibrated = false;
    
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = handle->unit,
        .chan = handle->channel,
        .atten = convert_atten(handle->config.attenuation),
        .bitwidth = convert_width(handle->config.width),
    };
    
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = handle->unit,
        .atten = convert_atten(handle->config.attenuation),
        .bitwidth = convert_width(handle->config.width),
    };
    
    esp_err_t ret = adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }
#endif
    
    if (calibrated) {
        handle->cali_handle = cali_handle;
        handle->has_calibration = true;
        TS_LOGD(TAG, "ADC calibration enabled for channel %d", handle->channel);
    } else {
        handle->has_calibration = false;
        TS_LOGW(TAG, "ADC calibration not available");
    }
    
    return ESP_OK;
}

/*===========================================================================*/
/*                         Public Functions                                   */
/*===========================================================================*/

esp_err_t ts_adc_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Initializing ADC subsystem");
    
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(s_handles, 0, sizeof(s_handles));
    s_adc1_handle = NULL;
    s_adc2_handle = NULL;
    s_adc1_ref_count = 0;
    s_adc2_ref_count = 0;
    s_initialized = true;
    
    return ESP_OK;
}

esp_err_t ts_adc_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Deinitializing ADC subsystem");
    
    /* Destroy all handles */
    for (int i = 0; i < CONFIG_TS_HAL_MAX_ADC_HANDLES; i++) {
        if (s_handles[i] != NULL) {
            ts_adc_destroy(s_handles[i]);
        }
    }
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    return ESP_OK;
}

ts_adc_handle_t ts_adc_create(const ts_adc_config_t *config, const char *owner)
{
    if (!s_initialized || config == NULL || owner == NULL) {
        return NULL;
    }
    
    /* Get GPIO number */
    int gpio_num = ts_pin_manager_get_gpio(config->function);
    if (gpio_num < 0) {
        TS_LOGE(TAG, "ADC function %d has no GPIO mapping", config->function);
        return NULL;
    }
    
    /* Map GPIO to ADC channel */
    adc_unit_t unit;
    adc_channel_t channel;
    if (!gpio_to_adc_channel(gpio_num, &unit, &channel)) {
        TS_LOGE(TAG, "GPIO%d is not an ADC pin", gpio_num);
        return NULL;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_TS_HAL_MAX_ADC_HANDLES; i++) {
        if (s_handles[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "No free ADC handles");
        return NULL;
    }
    
    /* Acquire pin */
    esp_err_t ret = ts_pin_manager_acquire(config->function, owner);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Get or create ADC unit handle */
    adc_oneshot_unit_handle_t *unit_handle_ptr = (unit == ADC_UNIT_1) ? 
                                                   &s_adc1_handle : &s_adc2_handle;
    int *ref_count = (unit == ADC_UNIT_1) ? &s_adc1_ref_count : &s_adc2_ref_count;
    
    if (*unit_handle_ptr == NULL) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = unit,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        
        ret = adc_oneshot_new_unit(&unit_cfg, unit_handle_ptr);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to create ADC unit: %s", esp_err_to_name(ret));
            ts_pin_manager_release(config->function);
            xSemaphoreGive(s_mutex);
            return NULL;
        }
    }
    
    /* Configure channel */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = convert_atten(config->attenuation),
        .bitwidth = convert_width(config->width),
    };
    
    ret = adc_oneshot_config_channel(*unit_handle_ptr, channel, &chan_cfg);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to configure ADC channel: %s", esp_err_to_name(ret));
        ts_pin_manager_release(config->function);
        if (*ref_count == 0) {
            adc_oneshot_del_unit(*unit_handle_ptr);
            *unit_handle_ptr = NULL;
        }
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Allocate handle */
    ts_adc_handle_t handle = calloc(1, sizeof(struct ts_adc_s));
    if (handle == NULL) {
        ts_pin_manager_release(config->function);
        if (*ref_count == 0) {
            adc_oneshot_del_unit(*unit_handle_ptr);
            *unit_handle_ptr = NULL;
        }
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    handle->function = config->function;
    handle->gpio_num = gpio_num;
    handle->config = *config;
    handle->unit = unit;
    handle->channel = channel;
    handle->unit_handle = *unit_handle_ptr;
    handle->configured = true;
    strncpy(handle->owner, owner, sizeof(handle->owner) - 1);
    
    (*ref_count)++;
    s_handles[slot] = handle;
    
    xSemaphoreGive(s_mutex);
    
    /* Initialize calibration if requested */
    if (config->use_calibration) {
        init_calibration(handle);
    }
    
    TS_LOGI(TAG, "Created ADC handle for GPIO%d (ADC%d, CH%d)", 
            gpio_num, unit + 1, channel);
    
    return handle;
}

int ts_adc_read_raw(ts_adc_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return -1;
    }
    
    int raw_value;
    esp_err_t ret = adc_oneshot_read(handle->unit_handle, handle->channel, &raw_value);
    
    return (ret == ESP_OK) ? raw_value : -1;
}

int ts_adc_read_mv(ts_adc_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return -1;
    }
    
    int raw_value;
    esp_err_t ret = adc_oneshot_read(handle->unit_handle, handle->channel, &raw_value);
    if (ret != ESP_OK) {
        return -1;
    }
    
    if (handle->has_calibration && handle->cali_handle != NULL) {
        int voltage;
        ret = adc_cali_raw_to_voltage(handle->cali_handle, raw_value, &voltage);
        if (ret == ESP_OK) {
            return voltage;
        }
    }
    
    /* Fallback: approximate conversion */
    return ts_adc_raw_to_mv(handle, raw_value);
}

int ts_adc_read_average(ts_adc_handle_t handle, int samples)
{
    if (!s_initialized || handle == NULL || samples <= 0) {
        return -1;
    }
    
    int64_t sum = 0;
    int valid_samples = 0;
    
    for (int i = 0; i < samples; i++) {
        int raw = ts_adc_read_raw(handle);
        if (raw >= 0) {
            sum += raw;
            valid_samples++;
        }
    }
    
    return (valid_samples > 0) ? (int)(sum / valid_samples) : -1;
}

esp_err_t ts_adc_read_stats(ts_adc_handle_t handle, int samples,
                             int *min, int *max, int *avg)
{
    if (!s_initialized || handle == NULL || samples <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int local_min = INT32_MAX;
    int local_max = INT32_MIN;
    int64_t sum = 0;
    int valid_samples = 0;
    
    for (int i = 0; i < samples; i++) {
        int raw = ts_adc_read_raw(handle);
        if (raw >= 0) {
            if (raw < local_min) local_min = raw;
            if (raw > local_max) local_max = raw;
            sum += raw;
            valid_samples++;
        }
    }
    
    if (valid_samples == 0) {
        return ESP_FAIL;
    }
    
    if (min) *min = local_min;
    if (max) *max = local_max;
    if (avg) *avg = (int)(sum / valid_samples);
    
    return ESP_OK;
}

int ts_adc_get_vref(ts_adc_handle_t handle)
{
    /* Default reference voltage for ESP32S3 with 11dB attenuation */
    return 3100;
}

esp_err_t ts_adc_set_atten(ts_adc_handle_t handle, ts_adc_atten_t atten)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = convert_atten(atten),
        .bitwidth = convert_width(handle->config.width),
    };
    
    esp_err_t ret = adc_oneshot_config_channel(handle->unit_handle, 
                                                 handle->channel, &chan_cfg);
    if (ret == ESP_OK) {
        handle->config.attenuation = atten;
        
        /* Reinitialize calibration if using it */
        if (handle->has_calibration && handle->cali_handle != NULL) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
            adc_cali_delete_scheme_curve_fitting(handle->cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
            adc_cali_delete_scheme_line_fitting(handle->cali_handle);
#endif
            handle->cali_handle = NULL;
            init_calibration(handle);
        }
    }
    
    return ret;
}

int ts_adc_raw_to_mv(ts_adc_handle_t handle, int raw)
{
    if (handle == NULL || raw < 0) {
        return -1;
    }
    
    /* Maximum value based on resolution */
    int max_raw;
    switch (handle->config.width) {
        case TS_ADC_WIDTH_9BIT: max_raw = 511; break;
        case TS_ADC_WIDTH_10BIT: max_raw = 1023; break;
        case TS_ADC_WIDTH_11BIT: max_raw = 2047; break;
        default: max_raw = 4095; break;
    }
    
    /* Voltage range based on attenuation */
    int vref_mv;
    switch (handle->config.attenuation) {
        case TS_ADC_ATTEN_0DB: vref_mv = 950; break;
        case TS_ADC_ATTEN_2_5DB: vref_mv = 1250; break;
        case TS_ADC_ATTEN_6DB: vref_mv = 1750; break;
        default: vref_mv = 3100; break;
    }
    
    return (raw * vref_mv) / max_raw;
}

esp_err_t ts_adc_destroy(ts_adc_handle_t handle)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Delete calibration */
    if (handle->cali_handle != NULL) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(handle->cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(handle->cali_handle);
#endif
    }
    
    /* Release pin */
    ts_pin_manager_release(handle->function);
    
    /* Decrement reference count and delete unit if zero */
    int *ref_count = (handle->unit == ADC_UNIT_1) ? &s_adc1_ref_count : &s_adc2_ref_count;
    adc_oneshot_unit_handle_t *unit_handle_ptr = (handle->unit == ADC_UNIT_1) ?
                                                   &s_adc1_handle : &s_adc2_handle;
    
    (*ref_count)--;
    if (*ref_count == 0 && *unit_handle_ptr != NULL) {
        adc_oneshot_del_unit(*unit_handle_ptr);
        *unit_handle_ptr = NULL;
    }
    
    /* Find and remove from handles array */
    int slot = get_handle_slot(handle);
    if (slot >= 0) {
        s_handles[slot] = NULL;
    }
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "Destroyed ADC handle for GPIO%d", handle->gpio_num);
    
    free(handle);
    return ESP_OK;
}
