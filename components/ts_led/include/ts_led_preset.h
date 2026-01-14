/**
 * @file ts_led_preset.h
 * @brief TianShanOS LED Preset Device Definitions
 * 
 * Predefined LED device instances for touch, board, and matrix.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_LED_PRESET_H
#define TS_LED_PRESET_H

#include "ts_led.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                          Preset Device Names                               */
/*===========================================================================*/

#define TS_LED_TOUCH_NAME       "led_touch"
#define TS_LED_BOARD_NAME       "led_board"
#define TS_LED_MATRIX_NAME      "led_matrix"

/*===========================================================================*/
/*                          Preset Initialization                             */
/*===========================================================================*/

/**
 * @brief Initialize touch LED device
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_touch_init(void);

/**
 * @brief Initialize board LED device
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_board_init(void);

/**
 * @brief Initialize matrix LED device
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_matrix_init(void);

/**
 * @brief Initialize all preset LED devices
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_preset_init_all(void);

/*===========================================================================*/
/*                          Quick Access                                      */
/*===========================================================================*/

/**
 * @brief Get touch LED device
 */
ts_led_device_t ts_led_touch_get(void);

/**
 * @brief Get board LED device
 */
ts_led_device_t ts_led_board_get(void);

/**
 * @brief Get matrix LED device
 */
ts_led_device_t ts_led_matrix_get(void);

/*===========================================================================*/
/*                          Status Indicators                                 */
/*===========================================================================*/

/**
 * @brief LED status type
 */
typedef enum {
    TS_LED_STATUS_IDLE = 0,      /**< System idle */
    TS_LED_STATUS_BUSY,          /**< System busy */
    TS_LED_STATUS_SUCCESS,       /**< Operation success */
    TS_LED_STATUS_ERROR,         /**< Error occurred */
    TS_LED_STATUS_WARNING,       /**< Warning */
    TS_LED_STATUS_NETWORK,       /**< Network activity */
    TS_LED_STATUS_USB,           /**< USB activity */
    TS_LED_STATUS_BOOT,          /**< Boot sequence */
    TS_LED_STATUS_MAX
} ts_led_status_t;

/**
 * @brief Set status indicator
 * 
 * @param status Status type
 * @return ESP_OK on success
 */
esp_err_t ts_led_set_status(ts_led_status_t status);

/**
 * @brief Clear status indicator (return to idle)
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_clear_status(void);

/**
 * @brief Bind event to status indicator
 * 
 * @param event_id Event ID
 * @param status Status to show
 * @param duration_ms Duration in ms (0 = permanent)
 * @return ESP_OK on success
 */
esp_err_t ts_led_bind_event_status(uint32_t event_id, ts_led_status_t status,
                                    uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* TS_LED_PRESET_H */
