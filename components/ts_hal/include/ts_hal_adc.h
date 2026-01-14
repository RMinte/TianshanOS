/**
 * @file ts_hal_adc.h
 * @brief TianShanOS ADC Abstraction Layer
 * 
 * Provides ADC functionality for analog measurements.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_HAL_ADC_H
#define TS_HAL_ADC_H

#include "esp_err.h"
#include "ts_pin_manager.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                               Type Definitions                             */
/*===========================================================================*/

/**
 * @brief ADC handle type
 */
typedef struct ts_adc_s *ts_adc_handle_t;

/**
 * @brief ADC unit
 */
typedef enum {
    TS_ADC_UNIT_1 = 0,              /**< ADC unit 1 */
    TS_ADC_UNIT_2,                  /**< ADC unit 2 */
    TS_ADC_UNIT_MAX
} ts_adc_unit_t;

/**
 * @brief ADC attenuation
 */
typedef enum {
    TS_ADC_ATTEN_0DB = 0,           /**< 0dB attenuation (0-950mV) */
    TS_ADC_ATTEN_2_5DB,             /**< 2.5dB attenuation (0-1250mV) */
    TS_ADC_ATTEN_6DB,               /**< 6dB attenuation (0-1750mV) */
    TS_ADC_ATTEN_11DB               /**< 11dB attenuation (0-3100mV) */
} ts_adc_atten_t;

/**
 * @brief ADC resolution
 */
typedef enum {
    TS_ADC_WIDTH_9BIT = 0,          /**< 9-bit resolution */
    TS_ADC_WIDTH_10BIT,             /**< 10-bit resolution */
    TS_ADC_WIDTH_11BIT,             /**< 11-bit resolution */
    TS_ADC_WIDTH_12BIT              /**< 12-bit resolution */
} ts_adc_width_t;

/**
 * @brief ADC configuration
 */
typedef struct {
    ts_pin_function_t function;     /**< Pin function */
    ts_adc_atten_t attenuation;     /**< Attenuation */
    ts_adc_width_t width;           /**< Resolution */
    bool use_calibration;           /**< Use factory calibration */
} ts_adc_config_t;

/**
 * @brief Default ADC configuration
 */
#define TS_ADC_CONFIG_DEFAULT() {           \
    .function = TS_PIN_FUNC_POWER_ADC,      \
    .attenuation = TS_ADC_ATTEN_11DB,       \
    .width = TS_ADC_WIDTH_12BIT,            \
    .use_calibration = true,                \
}

/*===========================================================================*/
/*                             ADC Functions                                  */
/*===========================================================================*/

/**
 * @brief Initialize the ADC subsystem
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t ts_adc_init(void);

/**
 * @brief Deinitialize the ADC subsystem
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_adc_deinit(void);

/**
 * @brief Create an ADC handle
 * 
 * @param config ADC configuration
 * @param owner Name of the owning service
 * @return ADC handle, or NULL on error
 */
ts_adc_handle_t ts_adc_create(const ts_adc_config_t *config, const char *owner);

/**
 * @brief Read raw ADC value
 * 
 * @param handle ADC handle
 * @return Raw ADC value (0-4095 for 12-bit), or -1 on error
 */
int ts_adc_read_raw(ts_adc_handle_t handle);

/**
 * @brief Read ADC voltage in millivolts
 * 
 * @param handle ADC handle
 * @return Voltage in mV, or -1 on error
 */
int ts_adc_read_mv(ts_adc_handle_t handle);

/**
 * @brief Read multiple samples and average
 * 
 * @param handle ADC handle
 * @param samples Number of samples to average
 * @return Averaged raw value, or -1 on error
 */
int ts_adc_read_average(ts_adc_handle_t handle, int samples);

/**
 * @brief Read multiple samples and get min/max/avg
 * 
 * @param handle ADC handle
 * @param samples Number of samples
 * @param min Output: minimum value
 * @param max Output: maximum value
 * @param avg Output: average value
 * @return ESP_OK on success
 */
esp_err_t ts_adc_read_stats(ts_adc_handle_t handle, int samples,
                             int *min, int *max, int *avg);

/**
 * @brief Get voltage reference
 * 
 * @param handle ADC handle
 * @return Voltage reference in mV
 */
int ts_adc_get_vref(ts_adc_handle_t handle);

/**
 * @brief Set attenuation
 * 
 * @param handle ADC handle
 * @param atten New attenuation
 * @return ESP_OK on success
 */
esp_err_t ts_adc_set_atten(ts_adc_handle_t handle, ts_adc_atten_t atten);

/**
 * @brief Convert raw value to millivolts
 * 
 * @param handle ADC handle
 * @param raw Raw ADC value
 * @return Voltage in mV
 */
int ts_adc_raw_to_mv(ts_adc_handle_t handle, int raw);

/**
 * @brief Destroy an ADC handle
 * 
 * @param handle ADC handle
 * @return ESP_OK on success
 */
esp_err_t ts_adc_destroy(ts_adc_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TS_HAL_ADC_H */
