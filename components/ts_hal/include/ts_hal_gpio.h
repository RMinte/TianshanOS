/**
 * @file ts_hal_gpio.h
 * @brief TianShanOS GPIO Abstraction Layer
 * 
 * Provides a hardware-agnostic interface for GPIO operations.
 * Uses the pin manager for logical-to-physical pin mapping.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_HAL_GPIO_H
#define TS_HAL_GPIO_H

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
 * @brief GPIO handle type
 */
typedef struct ts_gpio_s *ts_gpio_handle_t;

/**
 * @brief GPIO direction
 */
typedef enum {
    TS_GPIO_DIR_INPUT = 0,          /**< Input mode */
    TS_GPIO_DIR_OUTPUT,             /**< Output mode */
    TS_GPIO_DIR_OUTPUT_OD,          /**< Output open-drain mode */
    TS_GPIO_DIR_BIDIRECTIONAL       /**< Bidirectional (input+output) */
} ts_gpio_dir_t;

/**
 * @brief GPIO pull mode
 */
typedef enum {
    TS_GPIO_PULL_NONE = 0,          /**< No pull-up/pull-down */
    TS_GPIO_PULL_UP,                /**< Pull-up enabled */
    TS_GPIO_PULL_DOWN,              /**< Pull-down enabled */
    TS_GPIO_PULL_UP_DOWN            /**< Both pull-up and pull-down enabled */
} ts_gpio_pull_t;

/**
 * @brief GPIO interrupt type
 */
typedef enum {
    TS_GPIO_INTR_DISABLE = 0,       /**< No interrupt */
    TS_GPIO_INTR_POSEDGE,           /**< Interrupt on rising edge */
    TS_GPIO_INTR_NEGEDGE,           /**< Interrupt on falling edge */
    TS_GPIO_INTR_ANYEDGE,           /**< Interrupt on any edge */
    TS_GPIO_INTR_LOW_LEVEL,         /**< Interrupt on low level */
    TS_GPIO_INTR_HIGH_LEVEL         /**< Interrupt on high level */
} ts_gpio_intr_t;

/**
 * @brief GPIO drive strength
 */
typedef enum {
    TS_GPIO_DRIVE_0 = 0,            /**< Weakest drive (~5mA) */
    TS_GPIO_DRIVE_1,                /**< Weak drive (~10mA) */
    TS_GPIO_DRIVE_2,                /**< Medium drive (~20mA) */
    TS_GPIO_DRIVE_3                 /**< Strongest drive (~40mA) */
} ts_gpio_drive_t;

/**
 * @brief GPIO configuration structure
 */
typedef struct {
    ts_gpio_dir_t direction;        /**< GPIO direction */
    ts_gpio_pull_t pull_mode;       /**< Pull-up/pull-down configuration */
    ts_gpio_intr_t intr_type;       /**< Interrupt type */
    ts_gpio_drive_t drive;          /**< Drive strength */
    bool invert;                    /**< Invert logic level */
    int initial_level;              /**< Initial output level (0 or 1, -1 for no change) */
} ts_gpio_config_t;

/**
 * @brief Default GPIO configuration (input, no pull)
 */
#define TS_GPIO_CONFIG_DEFAULT() {      \
    .direction = TS_GPIO_DIR_INPUT,     \
    .pull_mode = TS_GPIO_PULL_NONE,     \
    .intr_type = TS_GPIO_INTR_DISABLE,  \
    .drive = TS_GPIO_DRIVE_2,           \
    .invert = false,                    \
    .initial_level = -1,                \
}

/**
 * @brief GPIO interrupt callback function type
 */
typedef void (*ts_gpio_isr_callback_t)(ts_gpio_handle_t handle, void *user_data);

/*===========================================================================*/
/*                             GPIO Functions                                 */
/*===========================================================================*/

/**
 * @brief Initialize the GPIO subsystem
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t ts_gpio_init(void);

/**
 * @brief Deinitialize the GPIO subsystem
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_gpio_deinit(void);

/**
 * @brief Create a GPIO handle for a logical function
 * 
 * This function creates a GPIO handle using the pin manager to resolve
 * the logical function to a physical GPIO pin.
 * 
 * @param function Logical pin function
 * @param owner Name of the owning service (for conflict detection)
 * @return GPIO handle, or NULL on error
 */
ts_gpio_handle_t ts_gpio_create(ts_pin_function_t function, const char *owner);

/**
 * @brief Create a GPIO handle for a specific GPIO number
 * 
 * This bypasses the pin manager and uses a specific GPIO directly.
 * Use with caution - no conflict checking is performed.
 * 
 * @param gpio_num Physical GPIO number
 * @param owner Name of the owning service
 * @return GPIO handle, or NULL on error
 */
ts_gpio_handle_t ts_gpio_create_raw(int gpio_num, const char *owner);

/**
 * @brief Configure a GPIO
 * 
 * @param handle GPIO handle
 * @param config Configuration structure
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if handle or config is invalid
 */
esp_err_t ts_gpio_configure(ts_gpio_handle_t handle, const ts_gpio_config_t *config);

/**
 * @brief Set GPIO output level
 * 
 * @param handle GPIO handle
 * @param level Output level (0 or 1)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if handle is invalid
 *      - ESP_ERR_INVALID_STATE if GPIO is not configured as output
 */
esp_err_t ts_gpio_set_level(ts_gpio_handle_t handle, int level);

/**
 * @brief Get GPIO input level
 * 
 * @param handle GPIO handle
 * @return Input level (0 or 1), or -1 on error
 */
int ts_gpio_get_level(ts_gpio_handle_t handle);

/**
 * @brief Toggle GPIO output
 * 
 * @param handle GPIO handle
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if GPIO is not configured as output
 */
esp_err_t ts_gpio_toggle(ts_gpio_handle_t handle);

/**
 * @brief Set GPIO direction at runtime
 * 
 * @param handle GPIO handle
 * @param direction New direction
 * @return ESP_OK on success
 */
esp_err_t ts_gpio_set_direction(ts_gpio_handle_t handle, ts_gpio_dir_t direction);

/**
 * @brief Set GPIO pull mode at runtime
 * 
 * @param handle GPIO handle
 * @param pull_mode New pull mode
 * @return ESP_OK on success
 */
esp_err_t ts_gpio_set_pull(ts_gpio_handle_t handle, ts_gpio_pull_t pull_mode);

/**
 * @brief Register interrupt callback
 * 
 * @param handle GPIO handle
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if handle or callback is NULL
 */
esp_err_t ts_gpio_set_isr_callback(ts_gpio_handle_t handle, 
                                    ts_gpio_isr_callback_t callback,
                                    void *user_data);

/**
 * @brief Enable GPIO interrupt
 * 
 * @param handle GPIO handle
 * @return ESP_OK on success
 */
esp_err_t ts_gpio_intr_enable(ts_gpio_handle_t handle);

/**
 * @brief Disable GPIO interrupt
 * 
 * @param handle GPIO handle
 * @return ESP_OK on success
 */
esp_err_t ts_gpio_intr_disable(ts_gpio_handle_t handle);

/**
 * @brief Get GPIO number from handle
 * 
 * @param handle GPIO handle
 * @return GPIO number, or -1 on error
 */
int ts_gpio_get_num(ts_gpio_handle_t handle);

/**
 * @brief Destroy a GPIO handle
 * 
 * This releases the GPIO and frees associated resources.
 * 
 * @param handle GPIO handle
 * @return ESP_OK on success
 */
esp_err_t ts_gpio_destroy(ts_gpio_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TS_HAL_GPIO_H */
