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

/** Device IDs */
typedef enum {
    TS_DEVICE_AGX = 0,          // NVIDIA AGX
    TS_DEVICE_LPMU,             // Low-Power Management Unit
    TS_DEVICE_MAX
} ts_device_id_t;

/** Device power state */
typedef enum {
    TS_DEVICE_STATE_OFF,
    TS_DEVICE_STATE_STANDBY,
    TS_DEVICE_STATE_ON,
    TS_DEVICE_STATE_BOOTING,
    TS_DEVICE_STATE_ERROR,
} ts_device_state_t;

/** AGX control pins */
typedef struct {
    int gpio_power_en;          // Power enable
    int gpio_reset;             // Reset pin
    int gpio_force_off;         // Force off
    int gpio_sys_rst;           // System reset output
    int gpio_power_good;        // Power good input
    int gpio_carrier_pwr_on;    // Carrier power on
    int gpio_shutdown_req;      // Shutdown request input
    int gpio_sleep_wake;        // Sleep/wake control
} ts_agx_pins_t;

/** Device status */
typedef struct {
    ts_device_state_t state;
    bool power_good;
    uint32_t uptime_ms;
    uint32_t boot_count;
    int32_t last_error;
} ts_device_status_t;

/**
 * @brief Initialize device control
 */
esp_err_t ts_device_ctrl_init(void);

/**
 * @brief Deinitialize device control
 */
esp_err_t ts_device_ctrl_deinit(void);

/**
 * @brief Configure AGX control pins
 */
esp_err_t ts_device_configure_agx(const ts_agx_pins_t *pins);

/**
 * @brief Power on device
 */
esp_err_t ts_device_power_on(ts_device_id_t device);

/**
 * @brief Power off device
 */
esp_err_t ts_device_power_off(ts_device_id_t device);

/**
 * @brief Force power off device
 */
esp_err_t ts_device_force_off(ts_device_id_t device);

/**
 * @brief Reset device
 */
esp_err_t ts_device_reset(ts_device_id_t device);

/**
 * @brief Get device status
 */
esp_err_t ts_device_get_status(ts_device_id_t device, ts_device_status_t *status);

/**
 * @brief Check if device is powered
 */
bool ts_device_is_powered(ts_device_id_t device);

/**
 * @brief Request graceful shutdown
 */
esp_err_t ts_device_request_shutdown(ts_device_id_t device);

/**
 * @brief Handle shutdown request from device
 */
esp_err_t ts_device_handle_shutdown_request(ts_device_id_t device);

#ifdef __cplusplus
}
#endif
