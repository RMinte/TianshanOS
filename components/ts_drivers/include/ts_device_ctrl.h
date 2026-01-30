/**
 * @file ts_device_ctrl.h
 * @brief Device Power Control (AGX/LPMU)
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                          Timing Constants                                  */
/*===========================================================================*/

/** AGX timing (compatible with robOS) */
#define TS_AGX_RESET_PULSE_MS       1000    /**< AGX reset pulse duration */
#define TS_AGX_POWER_PULSE_MS       500     /**< AGX power pulse duration */
#define TS_AGX_POWER_ON_DELAY_MS    100     /**< Delay after power on */
#define TS_AGX_RECOVERY_DELAY_MS    1000    /**< Recovery mode timing */

/** LPMU timing */
#define TS_LPMU_POWER_PULSE_MS      300     /**< LPMU power pulse duration */
#define TS_LPMU_RESET_PULSE_MS      300     /**< LPMU reset pulse duration */

/*===========================================================================*/
/*                          Type Definitions                                  */
/*===========================================================================*/

/** Device IDs */
typedef enum {
    TS_DEVICE_AGX = 0,          /**< NVIDIA AGX */
    TS_DEVICE_LPMU,             /**< Low-Power Management Unit */
    TS_DEVICE_MAX
} ts_device_id_t;

/** Device power state */
typedef enum {
    TS_DEVICE_STATE_OFF,        /**< Device is off */
    TS_DEVICE_STATE_STANDBY,    /**< Device in standby */
    TS_DEVICE_STATE_ON,         /**< Device is on/running */
    TS_DEVICE_STATE_BOOTING,    /**< Device is booting */
    TS_DEVICE_STATE_RECOVERY,   /**< Device in recovery mode */
    TS_DEVICE_STATE_ERROR,      /**< Device error */
} ts_device_state_t;

/** AGX control pins */
typedef struct {
    int gpio_power_en;          /**< Power enable (active high = on) */
    int gpio_reset;             /**< Reset pin (pulse high to reset) */
    int gpio_force_recovery;    /**< Force recovery mode */
    int gpio_sys_rst;           /**< System reset output (optional) */
    int gpio_power_good;        /**< Power good input (optional) */
    int gpio_carrier_pwr_on;    /**< Carrier power on (optional) */
    int gpio_shutdown_req;      /**< Shutdown request input (optional) */
    int gpio_sleep_wake;        /**< Sleep/wake control (optional) */
} ts_agx_pins_t;

/** LPMU control pins */
typedef struct {
    int gpio_power_btn;         /**< Power button (pulse to toggle) */
    int gpio_reset;             /**< Reset pin (pulse to reset) */
} ts_lpmu_pins_t;

/** Device status */
typedef struct {
    ts_device_state_t state;    /**< Current device state */
    bool power_good;            /**< Power good signal (if available) */
    uint32_t uptime_ms;         /**< Uptime since power on */
    uint32_t boot_count;        /**< Number of boot cycles */
    int32_t last_error;         /**< Last error code */
} ts_device_status_t;

/*===========================================================================*/
/*                          Initialization                                    */
/*===========================================================================*/

/**
 * @brief Initialize device control subsystem
 * @return ESP_OK on success
 */
esp_err_t ts_device_ctrl_init(void);

/**
 * @brief Deinitialize device control
 * @return ESP_OK on success
 */
esp_err_t ts_device_ctrl_deinit(void);

/*===========================================================================*/
/*                          Configuration                                     */
/*===========================================================================*/

/**
 * @brief Configure AGX control pins
 * @param pins Pin configuration struct
 * @return ESP_OK on success
 */
esp_err_t ts_device_configure_agx(const ts_agx_pins_t *pins);

/**
 * @brief Configure LPMU control pins
 * @param pins Pin configuration struct
 * @return ESP_OK on success
 */
esp_err_t ts_device_configure_lpmu(const ts_lpmu_pins_t *pins);

/*===========================================================================*/
/*                          Power Control                                     */
/*===========================================================================*/

/**
 * @brief Power on device
 * @param device Device ID
 * @return ESP_OK on success
 */
esp_err_t ts_device_power_on(ts_device_id_t device);

/**
 * @brief Power off device (graceful)
 * @param device Device ID
 * @return ESP_OK on success
 */
esp_err_t ts_device_power_off(ts_device_id_t device);

/**
 * @brief Force power off device
 * @param device Device ID
 * @return ESP_OK on success
 */
esp_err_t ts_device_force_off(ts_device_id_t device);

/**
 * @brief Toggle power (send pulse, for LPMU physical button simulation)
 * @param device Device ID
 * @return ESP_OK on success
 */
esp_err_t ts_device_power_toggle(ts_device_id_t device);

/**
 * @brief Reset device
 * @param device Device ID
 * @return ESP_OK on success
 */
esp_err_t ts_device_reset(ts_device_id_t device);

/**
 * @brief Enter recovery mode (AGX only)
 * @param device Device ID
 * @return ESP_OK on success, ESP_ERR_NOT_SUPPORTED for non-AGX
 */
esp_err_t ts_device_enter_recovery(ts_device_id_t device);

/*===========================================================================*/
/*                          Status                                            */
/*===========================================================================*/

/**
 * @brief Get device status
 * @param device Device ID
 * @param status Output status struct
 * @return ESP_OK on success
 */
esp_err_t ts_device_get_status(ts_device_id_t device, ts_device_status_t *status);

/**
 * @brief Check if device is powered on
 * @param device Device ID
 * @return true if powered on
 */
bool ts_device_is_powered(ts_device_id_t device);

/**
 * @brief Check if device is configured
 * @param device Device ID
 * @return true if configured
 */
bool ts_device_is_configured(ts_device_id_t device);

/*===========================================================================*/
/*                          Advanced Control                                  */
/*===========================================================================*/

/**
 * @brief Request graceful shutdown
 * @param device Device ID
 * @return ESP_OK on success
 */
esp_err_t ts_device_request_shutdown(ts_device_id_t device);

/**
 * @brief Handle shutdown request from device
 * @param device Device ID
 * @return ESP_OK on success
 */
esp_err_t ts_device_handle_shutdown_request(ts_device_id_t device);

/**
 * @brief Get device state as string
 * @param state Device state enum
 * @return State name string
 */
const char *ts_device_state_to_str(ts_device_state_t state);

/**
 * @brief Start LPMU startup detection task
 * 
 * This should be called after network is ready.
 * It will detect if LPMU is already online, or attempt to power it on.
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_device_lpmu_start_detection(void);

#ifdef __cplusplus
}
#endif
