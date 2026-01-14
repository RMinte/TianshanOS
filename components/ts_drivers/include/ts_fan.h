/**
 * @file ts_fan.h
 * @brief Fan Control Driver
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fan IDs */
typedef enum {
    TS_FAN_1 = 0,
    TS_FAN_2,
    TS_FAN_3,
    TS_FAN_4,
    TS_FAN_MAX
} ts_fan_id_t;

/** Fan mode */
typedef enum {
    TS_FAN_MODE_OFF,
    TS_FAN_MODE_MANUAL,
    TS_FAN_MODE_AUTO,
} ts_fan_mode_t;

/** Temperature curve point */
typedef struct {
    int16_t temp;       // Temperature in 0.1°C
    uint8_t duty;       // Duty cycle 0-100%
} ts_fan_curve_point_t;

/** Fan configuration */
typedef struct {
    int gpio_pwm;
    int gpio_tach;
    uint8_t min_duty;
    uint8_t max_duty;
    ts_fan_curve_point_t curve[8];
    uint8_t curve_points;
} ts_fan_config_t;

/** Fan status */
typedef struct {
    ts_fan_mode_t mode;
    uint8_t duty_percent;
    uint16_t rpm;
    int16_t temp;       // Current temperature source in 0.1°C
    bool is_running;
} ts_fan_status_t;

/**
 * @brief Initialize fan subsystem
 */
esp_err_t ts_fan_init(void);

/**
 * @brief Deinitialize fan subsystem
 */
esp_err_t ts_fan_deinit(void);

/**
 * @brief Configure a fan
 */
esp_err_t ts_fan_configure(ts_fan_id_t fan, const ts_fan_config_t *config);

/**
 * @brief Set fan mode
 */
esp_err_t ts_fan_set_mode(ts_fan_id_t fan, ts_fan_mode_t mode);

/**
 * @brief Set fan duty cycle (manual mode)
 */
esp_err_t ts_fan_set_duty(ts_fan_id_t fan, uint8_t duty_percent);

/**
 * @brief Set temperature for auto mode
 */
esp_err_t ts_fan_set_temperature(ts_fan_id_t fan, int16_t temp_01c);

/**
 * @brief Get fan status
 */
esp_err_t ts_fan_get_status(ts_fan_id_t fan, ts_fan_status_t *status);

/**
 * @brief Update temperature curve
 */
esp_err_t ts_fan_set_curve(ts_fan_id_t fan, const ts_fan_curve_point_t *curve, uint8_t points);

/**
 * @brief Set all fans to maximum speed
 */
esp_err_t ts_fan_emergency_full(void);

#ifdef __cplusplus
}
#endif
