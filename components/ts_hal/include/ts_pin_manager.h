/**
 * @file ts_pin_manager.h
 * @brief TianShanOS Pin Manager - Runtime Pin Configuration
 * 
 * The pin manager provides:
 * - Logical function to physical pin mapping
 * - Runtime configuration loading from files/NVS
 * - Pin conflict detection and management
 * - Pin allocation and release tracking
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_PIN_MANAGER_H
#define TS_PIN_MANAGER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                          Pin Function Definitions                          */
/*===========================================================================*/

/**
 * @brief Logical pin functions
 * 
 * These represent logical functions that can be mapped to physical GPIO pins
 * at runtime through configuration files.
 */
typedef enum {
    /* LED System (0-9) */
    TS_PIN_FUNC_LED_TOUCH = 0,      /**< Touch LED data line (WS2812) */
    TS_PIN_FUNC_LED_BOARD,          /**< Board LED data line (WS2812) */
    TS_PIN_FUNC_LED_MATRIX,         /**< Matrix LED data line (WS2812) */
    TS_PIN_FUNC_LED_STATUS,         /**< Status indicator LED */
    
    /* Fan System (10-19) */
    TS_PIN_FUNC_FAN_PWM_0 = 10,     /**< Fan 0 PWM control */
    TS_PIN_FUNC_FAN_PWM_1,          /**< Fan 1 PWM control */
    TS_PIN_FUNC_FAN_PWM_2,          /**< Fan 2 PWM control */
    TS_PIN_FUNC_FAN_PWM_3,          /**< Fan 3 PWM control */
    TS_PIN_FUNC_FAN_TACH_0,         /**< Fan 0 tachometer */
    TS_PIN_FUNC_FAN_TACH_1,         /**< Fan 1 tachometer */
    TS_PIN_FUNC_FAN_TACH_2,         /**< Fan 2 tachometer */
    TS_PIN_FUNC_FAN_TACH_3,         /**< Fan 3 tachometer */
    
    /* Ethernet W5500 (20-29) */
    TS_PIN_FUNC_ETH_MISO = 20,      /**< Ethernet SPI MISO */
    TS_PIN_FUNC_ETH_MOSI,           /**< Ethernet SPI MOSI */
    TS_PIN_FUNC_ETH_SCLK,           /**< Ethernet SPI SCLK */
    TS_PIN_FUNC_ETH_CS,             /**< Ethernet SPI CS */
    TS_PIN_FUNC_ETH_INT,            /**< Ethernet interrupt */
    TS_PIN_FUNC_ETH_RST,            /**< Ethernet reset */
    
    /* USB MUX (30-34) */
    TS_PIN_FUNC_USB_MUX_0 = 30,     /**< USB MUX select 0 */
    TS_PIN_FUNC_USB_MUX_1,          /**< USB MUX select 1 */
    TS_PIN_FUNC_USB_MUX_2,          /**< USB MUX select 2 */
    TS_PIN_FUNC_USB_MUX_3,          /**< USB MUX select 3 */
    
    /* Device Control (40-49) */
    TS_PIN_FUNC_AGX_POWER = 40,     /**< AGX power control */
    TS_PIN_FUNC_AGX_RESET,          /**< AGX reset */
    TS_PIN_FUNC_AGX_FORCE_RECOVERY, /**< AGX force recovery */
    TS_PIN_FUNC_LPMU_POWER,         /**< LPMU power control */
    TS_PIN_FUNC_LPMU_RESET,         /**< LPMU reset */
    
    /* Power Monitoring (50-54) */
    TS_PIN_FUNC_POWER_ADC = 50,     /**< Power ADC input */
    TS_PIN_FUNC_POWER_UART_TX,      /**< Power module UART TX */
    TS_PIN_FUNC_POWER_UART_RX,      /**< Power module UART RX */
    
    /* SD Card (60-69) */
    TS_PIN_FUNC_SD_CMD = 60,        /**< SD card CMD */
    TS_PIN_FUNC_SD_CLK,             /**< SD card CLK */
    TS_PIN_FUNC_SD_D0,              /**< SD card D0 */
    TS_PIN_FUNC_SD_D1,              /**< SD card D1 */
    TS_PIN_FUNC_SD_D2,              /**< SD card D2 */
    TS_PIN_FUNC_SD_D3,              /**< SD card D3 */
    TS_PIN_FUNC_SD_DETECT,          /**< SD card detect */
    
    /* I2C Buses (70-79) */
    TS_PIN_FUNC_I2C0_SDA = 70,      /**< I2C bus 0 SDA */
    TS_PIN_FUNC_I2C0_SCL,           /**< I2C bus 0 SCL */
    TS_PIN_FUNC_I2C1_SDA,           /**< I2C bus 1 SDA */
    TS_PIN_FUNC_I2C1_SCL,           /**< I2C bus 1 SCL */
    
    /* UART Ports (80-89) */
    TS_PIN_FUNC_UART1_TX = 80,      /**< UART1 TX */
    TS_PIN_FUNC_UART1_RX,           /**< UART1 RX */
    TS_PIN_FUNC_UART2_TX,           /**< UART2 TX */
    TS_PIN_FUNC_UART2_RX,           /**< UART2 RX */
    
    /* Debug/Reserved (90-99) */
    TS_PIN_FUNC_DEBUG_0 = 90,       /**< Debug pin 0 */
    TS_PIN_FUNC_DEBUG_1,            /**< Debug pin 1 */
    
    TS_PIN_FUNC_MAX = 100           /**< Maximum function ID */
} ts_pin_function_t;

/*===========================================================================*/
/*                              Pin State Types                               */
/*===========================================================================*/

/**
 * @brief Pin allocation state
 */
typedef enum {
    TS_PIN_STATE_FREE = 0,          /**< Pin is not allocated */
    TS_PIN_STATE_ALLOCATED,         /**< Pin is allocated but not configured */
    TS_PIN_STATE_CONFIGURED,        /**< Pin is configured and in use */
    TS_PIN_STATE_RESERVED           /**< Pin is reserved by system */
} ts_pin_state_t;

/**
 * @brief Pin capability flags
 */
typedef enum {
    TS_PIN_CAP_GPIO_IN      = (1 << 0),     /**< Can be used as GPIO input */
    TS_PIN_CAP_GPIO_OUT     = (1 << 1),     /**< Can be used as GPIO output */
    TS_PIN_CAP_GPIO_OD      = (1 << 2),     /**< Supports open-drain */
    TS_PIN_CAP_PULLUP       = (1 << 3),     /**< Has internal pull-up */
    TS_PIN_CAP_PULLDOWN     = (1 << 4),     /**< Has internal pull-down */
    TS_PIN_CAP_PWM          = (1 << 5),     /**< Can be used for PWM */
    TS_PIN_CAP_ADC          = (1 << 6),     /**< Has ADC function */
    TS_PIN_CAP_DAC          = (1 << 7),     /**< Has DAC function */
    TS_PIN_CAP_TOUCH        = (1 << 8),     /**< Has touch sensor */
    TS_PIN_CAP_RTC          = (1 << 9),     /**< Connected to RTC domain */
    TS_PIN_CAP_STRAPPING    = (1 << 10),    /**< Is a strapping pin */
} ts_pin_capability_t;

/**
 * @brief Pin mapping entry
 */
typedef struct {
    ts_pin_function_t function;     /**< Logical function */
    int8_t gpio_num;                /**< Physical GPIO number (-1 if not mapped) */
    const char *name;               /**< Function name string */
    const char *description;        /**< Human-readable description */
} ts_pin_mapping_t;

/**
 * @brief Pin status information
 */
typedef struct {
    int8_t gpio_num;                /**< Physical GPIO number */
    ts_pin_state_t state;           /**< Current state */
    ts_pin_function_t function;     /**< Assigned function (if any) */
    const char *owner;              /**< Owner service name */
    uint16_t capabilities;          /**< Pin capabilities flags */
} ts_pin_status_t;

/*===========================================================================*/
/*                           Pin Manager Functions                            */
/*===========================================================================*/

/**
 * @brief Initialize the pin manager
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 *      - ESP_ERR_NO_MEM if memory allocation fails
 */
esp_err_t ts_pin_manager_init(void);

/**
 * @brief Deinitialize the pin manager
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t ts_pin_manager_deinit(void);

/**
 * @brief Load pin configuration from JSON file
 * 
 * @param path Path to the JSON configuration file
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if file not found
 *      - ESP_ERR_INVALID_ARG if JSON is invalid
 */
esp_err_t ts_pin_manager_load_config(const char *path);

/**
 * @brief Load pin configuration from NVS
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if no config in NVS
 */
esp_err_t ts_pin_manager_load_nvs(void);

/**
 * @brief Save current pin configuration to NVS
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL on NVS write error
 */
esp_err_t ts_pin_manager_save_nvs(void);

/**
 * @brief Load default pin configuration for current board
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_pin_manager_load_defaults(void);

/**
 * @brief Set pin mapping for a function
 * 
 * @param function Logical function
 * @param gpio_num Physical GPIO number
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if function or gpio_num is invalid
 *      - ESP_ERR_INVALID_STATE if pin is already in use
 */
esp_err_t ts_pin_manager_set_mapping(ts_pin_function_t function, int gpio_num);

/**
 * @brief Get GPIO number for a function
 * 
 * @param function Logical function
 * @return GPIO number, or -1 if not mapped
 */
int ts_pin_manager_get_gpio(ts_pin_function_t function);

/**
 * @brief Get function assigned to a GPIO
 * 
 * @param gpio_num Physical GPIO number
 * @return Function ID, or TS_PIN_FUNC_MAX if not assigned
 */
ts_pin_function_t ts_pin_manager_get_function(int gpio_num);

/**
 * @brief Acquire a pin for use
 * 
 * This function claims ownership of a pin, preventing other services
 * from using it. Must be called before configuring a pin.
 * 
 * @param function Logical function to acquire
 * @param owner Name of the owning service
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if function has no GPIO mapping
 *      - ESP_ERR_INVALID_STATE if pin is already acquired
 */
esp_err_t ts_pin_manager_acquire(ts_pin_function_t function, const char *owner);

/**
 * @brief Release a previously acquired pin
 * 
 * @param function Logical function to release
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if pin is not acquired
 */
esp_err_t ts_pin_manager_release(ts_pin_function_t function);

/**
 * @brief Check if a GPIO is available
 * 
 * @param gpio_num Physical GPIO number
 * @return true if available, false if in use or reserved
 */
bool ts_pin_manager_is_available(int gpio_num);

/**
 * @brief Check if a function is mapped
 * 
 * @param function Logical function
 * @return true if function has a GPIO mapping
 */
bool ts_pin_manager_is_mapped(ts_pin_function_t function);

/**
 * @brief Get pin capabilities
 * 
 * @param gpio_num Physical GPIO number
 * @return Capability flags, or 0 if invalid GPIO
 */
uint16_t ts_pin_manager_get_capabilities(int gpio_num);

/**
 * @brief Get function name string
 * 
 * @param function Logical function
 * @return Function name string, or "UNKNOWN"
 */
const char *ts_pin_manager_get_name(ts_pin_function_t function);

/**
 * @brief Get all pin mappings
 * 
 * @param mappings Array to fill with mappings
 * @param count Pointer to count (in: array size, out: actual count)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if mappings or count is NULL
 */
esp_err_t ts_pin_manager_get_mappings(ts_pin_mapping_t *mappings, size_t *count);

/**
 * @brief Get all pin statuses
 * 
 * @param status Array to fill with statuses
 * @param count Pointer to count (in: array size, out: actual count)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if status or count is NULL
 */
esp_err_t ts_pin_manager_get_status(ts_pin_status_t *status, size_t *count);

/**
 * @brief Print pin configuration to console
 */
void ts_pin_manager_print_config(void);

/**
 * @brief Print pin usage summary to console
 */
void ts_pin_manager_print_usage(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_PIN_MANAGER_H */
