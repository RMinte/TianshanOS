/**
 * @file ts_hal_pwm.h
 * @brief TianShanOS PWM Abstraction Layer
 * 
 * Provides PWM functionality using the ESP-IDF LEDC driver.
 * Supports frequency, duty cycle, and fade control.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_HAL_PWM_H
#define TS_HAL_PWM_H

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
 * @brief PWM handle type
 */
typedef struct ts_pwm_s *ts_pwm_handle_t;

/**
 * @brief PWM timer source
 */
typedef enum {
    TS_PWM_TIMER_0 = 0,             /**< PWM timer 0 */
    TS_PWM_TIMER_1,                 /**< PWM timer 1 */
    TS_PWM_TIMER_2,                 /**< PWM timer 2 */
    TS_PWM_TIMER_3,                 /**< PWM timer 3 */
    TS_PWM_TIMER_AUTO               /**< Auto-select timer */
} ts_pwm_timer_t;

/**
 * @brief PWM configuration structure
 */
typedef struct {
    uint32_t frequency;             /**< PWM frequency in Hz */
    uint8_t resolution_bits;        /**< Duty resolution in bits (1-14) */
    ts_pwm_timer_t timer;           /**< Timer to use */
    bool invert;                    /**< Invert output signal */
    float initial_duty;             /**< Initial duty cycle (0.0 - 100.0) */
} ts_pwm_config_t;

/**
 * @brief Default PWM configuration
 */
#define TS_PWM_CONFIG_DEFAULT() {                       \
    .frequency = CONFIG_TS_HAL_PWM_DEFAULT_FREQ,        \
    .resolution_bits = CONFIG_TS_HAL_PWM_DEFAULT_RESOLUTION, \
    .timer = TS_PWM_TIMER_AUTO,                         \
    .invert = false,                                    \
    .initial_duty = 0.0f,                               \
}

/**
 * @brief PWM fade mode
 */
typedef enum {
    TS_PWM_FADE_NO_WAIT = 0,        /**< Start fade and return immediately */
    TS_PWM_FADE_WAIT                /**< Wait for fade to complete */
} ts_pwm_fade_mode_t;

/**
 * @brief PWM fade complete callback
 */
typedef void (*ts_pwm_fade_cb_t)(ts_pwm_handle_t handle, void *user_data);

/*===========================================================================*/
/*                             PWM Functions                                  */
/*===========================================================================*/

/**
 * @brief Initialize the PWM subsystem
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t ts_pwm_init(void);

/**
 * @brief Deinitialize the PWM subsystem
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_pwm_deinit(void);

/**
 * @brief Create a PWM handle for a logical function
 * 
 * @param function Logical pin function
 * @param owner Name of the owning service
 * @return PWM handle, or NULL on error
 */
ts_pwm_handle_t ts_pwm_create(ts_pin_function_t function, const char *owner);

/**
 * @brief Create a PWM handle for a specific GPIO number
 * 
 * @param gpio_num Physical GPIO number
 * @param owner Name of the owning service
 * @return PWM handle, or NULL on error
 */
ts_pwm_handle_t ts_pwm_create_raw(int gpio_num, const char *owner);

/**
 * @brief Configure PWM output
 * 
 * @param handle PWM handle
 * @param config Configuration structure
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if handle or config is invalid
 */
esp_err_t ts_pwm_configure(ts_pwm_handle_t handle, const ts_pwm_config_t *config);

/**
 * @brief Set PWM duty cycle (percentage)
 * 
 * @param handle PWM handle
 * @param duty_percent Duty cycle (0.0 - 100.0)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if handle is invalid or duty out of range
 */
esp_err_t ts_pwm_set_duty(ts_pwm_handle_t handle, float duty_percent);

/**
 * @brief Set PWM duty cycle (raw value)
 * 
 * @param handle PWM handle
 * @param duty Raw duty value (0 to 2^resolution - 1)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if handle is invalid
 */
esp_err_t ts_pwm_set_duty_raw(ts_pwm_handle_t handle, uint32_t duty);

/**
 * @brief Get current duty cycle (percentage)
 * 
 * @param handle PWM handle
 * @return Duty cycle (0.0 - 100.0), or -1.0 on error
 */
float ts_pwm_get_duty(ts_pwm_handle_t handle);

/**
 * @brief Get current duty cycle (raw value)
 * 
 * @param handle PWM handle
 * @return Raw duty value, or 0 on error
 */
uint32_t ts_pwm_get_duty_raw(ts_pwm_handle_t handle);

/**
 * @brief Set PWM frequency
 * 
 * @param handle PWM handle
 * @param frequency Frequency in Hz
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if frequency is invalid
 */
esp_err_t ts_pwm_set_frequency(ts_pwm_handle_t handle, uint32_t frequency);

/**
 * @brief Get current PWM frequency
 * 
 * @param handle PWM handle
 * @return Frequency in Hz, or 0 on error
 */
uint32_t ts_pwm_get_frequency(ts_pwm_handle_t handle);

/**
 * @brief Start PWM fade
 * 
 * @param handle PWM handle
 * @param target_duty Target duty cycle (0.0 - 100.0)
 * @param duration_ms Fade duration in milliseconds
 * @param mode Fade mode (wait or no-wait)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if parameters are invalid
 */
esp_err_t ts_pwm_fade_start(ts_pwm_handle_t handle, float target_duty, 
                            uint32_t duration_ms, ts_pwm_fade_mode_t mode);

/**
 * @brief Set fade complete callback
 * 
 * @param handle PWM handle
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return ESP_OK on success
 */
esp_err_t ts_pwm_set_fade_callback(ts_pwm_handle_t handle,
                                    ts_pwm_fade_cb_t callback,
                                    void *user_data);

/**
 * @brief Stop PWM output
 * 
 * Sets duty cycle to 0 and optionally holds the pin low.
 * 
 * @param handle PWM handle
 * @param hold_low If true, configure pin as GPIO output low
 * @return ESP_OK on success
 */
esp_err_t ts_pwm_stop(ts_pwm_handle_t handle, bool hold_low);

/**
 * @brief Start PWM output (after stop)
 * 
 * @param handle PWM handle
 * @return ESP_OK on success
 */
esp_err_t ts_pwm_start(ts_pwm_handle_t handle);

/**
 * @brief Get maximum duty value for current resolution
 * 
 * @param handle PWM handle
 * @return Maximum duty value
 */
uint32_t ts_pwm_get_max_duty(ts_pwm_handle_t handle);

/**
 * @brief Destroy a PWM handle
 * 
 * @param handle PWM handle
 * @return ESP_OK on success
 */
esp_err_t ts_pwm_destroy(ts_pwm_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TS_HAL_PWM_H */
