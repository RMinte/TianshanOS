/**
 * @file ts_platform_s3.h
 * @brief ESP32S3 Platform Adapter Header
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_PLATFORM_S3_H
#define TS_PLATFORM_S3_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                         ESP32S3 Constants                                  */
/*===========================================================================*/

#define TS_PLATFORM_GPIO_COUNT          48
#define TS_PLATFORM_PWM_CHANNELS        8
#define TS_PLATFORM_I2C_PORTS           2
#define TS_PLATFORM_SPI_HOSTS           2
#define TS_PLATFORM_UART_PORTS          3
#define TS_PLATFORM_ADC1_CHANNELS       10
#define TS_PLATFORM_ADC2_CHANNELS       10

/* Strapping pins (should be used with caution) */
#define TS_PLATFORM_STRAPPING_PINS      {0, 3, 45, 46}

/* USB pins (cannot be used if USB is enabled) */
#define TS_PLATFORM_USB_DP_PIN          20
#define TS_PLATFORM_USB_DM_PIN          19

/* JTAG pins */
#define TS_PLATFORM_JTAG_MTCK           39
#define TS_PLATFORM_JTAG_MTDO           40
#define TS_PLATFORM_JTAG_MTDI           41
#define TS_PLATFORM_JTAG_MTMS           42

/*===========================================================================*/
/*                         Platform Functions                                 */
/*===========================================================================*/

/**
 * @brief Initialize ESP32S3-specific platform features
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_platform_s3_init(void);

/**
 * @brief Check if GPIO is valid for ESP32S3
 * 
 * @param gpio_num GPIO number
 * @return true if valid
 */
bool ts_platform_s3_gpio_valid(int gpio_num);

/**
 * @brief Check if GPIO is a strapping pin
 * 
 * @param gpio_num GPIO number
 * @return true if strapping pin
 */
bool ts_platform_s3_is_strapping(int gpio_num);

/**
 * @brief Check if GPIO supports ADC
 * 
 * @param gpio_num GPIO number
 * @return true if ADC capable
 */
bool ts_platform_s3_has_adc(int gpio_num);

/**
 * @brief Check if GPIO supports touch sensing
 * 
 * @param gpio_num GPIO number
 * @return true if touch capable
 */
bool ts_platform_s3_has_touch(int gpio_num);

/**
 * @brief Get CPU frequency
 * 
 * @return CPU frequency in MHz
 */
uint32_t ts_platform_s3_get_cpu_freq(void);

/**
 * @brief Get PSRAM size
 * 
 * @return PSRAM size in bytes, or 0 if not available
 */
uint32_t ts_platform_s3_get_psram_size(void);

/**
 * @brief Get flash size
 * 
 * @return Flash size in bytes
 */
uint32_t ts_platform_s3_get_flash_size(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_PLATFORM_S3_H */
