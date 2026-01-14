/**
 * @file ts_platform_p4.h
 * @brief ESP32P4 Platform Adapter Header (Placeholder)
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_PLATFORM_P4_H
#define TS_PLATFORM_P4_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                         ESP32P4 Constants                                  */
/*===========================================================================*/

/* Placeholder - will be updated when ESP32P4 support is added */
#define TS_PLATFORM_P4_GPIO_COUNT       55
#define TS_PLATFORM_P4_PWM_CHANNELS     8
#define TS_PLATFORM_P4_I2C_PORTS        2
#define TS_PLATFORM_P4_SPI_HOSTS        2
#define TS_PLATFORM_P4_UART_PORTS       5

/*===========================================================================*/
/*                         Platform Functions                                 */
/*===========================================================================*/

/**
 * @brief Initialize ESP32P4-specific platform features
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_platform_p4_init(void);

/**
 * @brief Check if GPIO is valid for ESP32P4
 * 
 * @param gpio_num GPIO number
 * @return true if valid
 */
bool ts_platform_p4_gpio_valid(int gpio_num);

#ifdef __cplusplus
}
#endif

#endif /* TS_PLATFORM_P4_H */
