/**
 * @file ts_usb_mux.h
 * @brief USB MUX Control Driver
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** USB MUX IDs */
typedef enum {
    TS_USB_MUX_1 = 0,
    TS_USB_MUX_2,
    TS_USB_MUX_MAX
} ts_usb_mux_id_t;

/** USB MUX target */
typedef enum {
    TS_USB_TARGET_HOST,         // Route to USB host (ESP32/PC)
    TS_USB_TARGET_DEVICE,       // Route to USB device (AGX)
    TS_USB_TARGET_DISCONNECT,   // Disconnect both
} ts_usb_target_t;

/** USB MUX pins configuration */
typedef struct {
    int gpio_sel;               // Select pin
    int gpio_oe;                // Output enable (optional, -1 if not used)
    bool sel_active_low;        // Select pin active low
    bool oe_active_low;         // OE pin active low
} ts_usb_mux_config_t;

/** USB MUX status */
typedef struct {
    ts_usb_target_t target;
    bool enabled;
} ts_usb_mux_status_t;

/**
 * @brief Initialize USB MUX subsystem
 */
esp_err_t ts_usb_mux_init(void);

/**
 * @brief Deinitialize USB MUX subsystem
 */
esp_err_t ts_usb_mux_deinit(void);

/**
 * @brief Configure a USB MUX
 */
esp_err_t ts_usb_mux_configure(ts_usb_mux_id_t mux, const ts_usb_mux_config_t *config);

/**
 * @brief Set USB MUX target
 */
esp_err_t ts_usb_mux_set_target(ts_usb_mux_id_t mux, ts_usb_target_t target);

/**
 * @brief Get USB MUX status
 */
esp_err_t ts_usb_mux_get_status(ts_usb_mux_id_t mux, ts_usb_mux_status_t *status);

/**
 * @brief Enable USB MUX
 */
esp_err_t ts_usb_mux_enable(ts_usb_mux_id_t mux, bool enable);

/**
 * @brief Switch to host mode with timeout
 */
esp_err_t ts_usb_mux_switch_to_host(ts_usb_mux_id_t mux, uint32_t timeout_ms);

/**
 * @brief Switch to device mode
 */
esp_err_t ts_usb_mux_switch_to_device(ts_usb_mux_id_t mux);

#ifdef __cplusplus
}
#endif
