/**
 * @file ts_hal.h
 * @brief TianShanOS Hardware Abstraction Layer - Main Header
 * 
 * This is the unified entry point for the HAL module. It provides
 * hardware-agnostic interfaces for GPIO, PWM, I2C, SPI, UART, and ADC.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_HAL_H
#define TS_HAL_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Version Info                                  */
/*===========================================================================*/

#define TS_HAL_VERSION_MAJOR    1
#define TS_HAL_VERSION_MINOR    0
#define TS_HAL_VERSION_PATCH    0

/*===========================================================================*/
/*                           Include Sub-modules                              */
/*===========================================================================*/

#include "ts_pin_manager.h"
#include "ts_hal_gpio.h"
#include "ts_hal_pwm.h"
#include "ts_hal_i2c.h"
#include "ts_hal_spi.h"
#include "ts_hal_uart.h"
#include "ts_hal_adc.h"

/*===========================================================================*/
/*                            HAL Initialization                              */
/*===========================================================================*/

/**
 * @brief HAL configuration structure
 */
typedef struct {
    const char *pin_config_path;    /**< Path to pin configuration file (NULL for defaults) */
    bool enable_conflict_check;     /**< Enable pin conflict checking */
    bool load_from_nvs;             /**< Load pin config from NVS if available */
} ts_hal_config_t;

/**
 * @brief Default HAL configuration
 */
#define TS_HAL_CONFIG_DEFAULT() {               \
    .pin_config_path = NULL,                    \
    .enable_conflict_check = true,              \
    .load_from_nvs = true,                      \
}

/**
 * @brief Initialize the HAL module
 * 
 * This function initializes the pin manager and all HAL sub-modules.
 * It should be called early in the boot process, after ts_config is initialized.
 * 
 * @param config HAL configuration, or NULL for defaults
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 *      - ESP_ERR_NO_MEM if memory allocation fails
 */
esp_err_t ts_hal_init(const ts_hal_config_t *config);

/**
 * @brief Deinitialize the HAL module
 * 
 * This function releases all HAL resources and clears pin mappings.
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t ts_hal_deinit(void);

/**
 * @brief Check if HAL is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool ts_hal_is_initialized(void);

/**
 * @brief Get HAL version string
 * 
 * @return Version string in format "major.minor.patch"
 */
const char *ts_hal_get_version(void);

/**
 * @brief Get current platform name
 * 
 * @return Platform name string (e.g., "ESP32S3", "ESP32P4")
 */
const char *ts_hal_get_platform(void);

/*===========================================================================*/
/*                            Platform Capabilities                           */
/*===========================================================================*/

/**
 * @brief Platform capability flags
 */
typedef struct {
    uint8_t gpio_count;         /**< Number of available GPIO pins */
    uint8_t pwm_channels;       /**< Number of PWM channels */
    uint8_t i2c_ports;          /**< Number of I2C ports */
    uint8_t spi_hosts;          /**< Number of SPI hosts */
    uint8_t uart_ports;         /**< Number of UART ports */
    uint8_t adc_channels;       /**< Number of ADC channels */
    bool has_psram;             /**< PSRAM available */
    bool has_usb_otg;           /**< USB OTG available */
    uint32_t cpu_freq_mhz;      /**< CPU frequency in MHz */
    uint32_t flash_size_mb;     /**< Flash size in MB */
} ts_hal_capabilities_t;

/**
 * @brief Get platform capabilities
 * 
 * @param caps Pointer to capabilities structure to fill
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if caps is NULL
 */
esp_err_t ts_hal_get_capabilities(ts_hal_capabilities_t *caps);

#ifdef __cplusplus
}
#endif

#endif /* TS_HAL_H */
