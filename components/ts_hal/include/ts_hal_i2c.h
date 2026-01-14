/**
 * @file ts_hal_i2c.h
 * @brief TianShanOS I2C Abstraction Layer
 * 
 * Provides I2C master functionality with automatic bus management.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_HAL_I2C_H
#define TS_HAL_I2C_H

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
 * @brief I2C handle type
 */
typedef struct ts_i2c_s *ts_i2c_handle_t;

/**
 * @brief I2C port number
 */
typedef enum {
    TS_I2C_PORT_0 = 0,              /**< I2C port 0 */
    TS_I2C_PORT_1,                  /**< I2C port 1 */
    TS_I2C_PORT_MAX
} ts_i2c_port_t;

/**
 * @brief I2C configuration structure
 */
typedef struct {
    ts_i2c_port_t port;             /**< I2C port number */
    ts_pin_function_t sda_function; /**< SDA pin function */
    ts_pin_function_t scl_function; /**< SCL pin function */
    uint32_t clock_hz;              /**< Clock frequency in Hz */
    bool enable_pullup;             /**< Enable internal pull-ups */
    uint32_t timeout_ms;            /**< Transaction timeout in ms */
} ts_i2c_config_t;

/**
 * @brief Default I2C configuration
 */
#define TS_I2C_CONFIG_DEFAULT(port_num) {   \
    .port = port_num,                       \
    .sda_function = TS_PIN_FUNC_I2C0_SDA,   \
    .scl_function = TS_PIN_FUNC_I2C0_SCL,   \
    .clock_hz = 400000,                     \
    .enable_pullup = true,                  \
    .timeout_ms = 1000,                     \
}

/*===========================================================================*/
/*                             I2C Functions                                  */
/*===========================================================================*/

/**
 * @brief Initialize the I2C subsystem
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t ts_i2c_init(void);

/**
 * @brief Deinitialize the I2C subsystem
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_i2c_deinit(void);

/**
 * @brief Create an I2C handle
 * 
 * @param config I2C configuration
 * @param owner Name of the owning service
 * @return I2C handle, or NULL on error
 */
ts_i2c_handle_t ts_i2c_create(const ts_i2c_config_t *config, const char *owner);

/**
 * @brief Write data to an I2C device
 * 
 * @param handle I2C handle
 * @param dev_addr Device address (7-bit)
 * @param data Data buffer to write
 * @param len Number of bytes to write
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_TIMEOUT on timeout
 *      - ESP_FAIL on NACK
 */
esp_err_t ts_i2c_write(ts_i2c_handle_t handle, uint8_t dev_addr,
                        const uint8_t *data, size_t len);

/**
 * @brief Read data from an I2C device
 * 
 * @param handle I2C handle
 * @param dev_addr Device address (7-bit)
 * @param data Buffer to store read data
 * @param len Number of bytes to read
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_TIMEOUT on timeout
 *      - ESP_FAIL on NACK
 */
esp_err_t ts_i2c_read(ts_i2c_handle_t handle, uint8_t dev_addr,
                       uint8_t *data, size_t len);

/**
 * @brief Write then read (combined transaction)
 * 
 * @param handle I2C handle
 * @param dev_addr Device address (7-bit)
 * @param write_data Data to write
 * @param write_len Number of bytes to write
 * @param read_data Buffer for read data
 * @param read_len Number of bytes to read
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_TIMEOUT on timeout
 */
esp_err_t ts_i2c_write_read(ts_i2c_handle_t handle, uint8_t dev_addr,
                             const uint8_t *write_data, size_t write_len,
                             uint8_t *read_data, size_t read_len);

/**
 * @brief Write to a register
 * 
 * @param handle I2C handle
 * @param dev_addr Device address (7-bit)
 * @param reg_addr Register address
 * @param data Data to write
 * @param len Number of data bytes
 * @return ESP_OK on success
 */
esp_err_t ts_i2c_write_reg(ts_i2c_handle_t handle, uint8_t dev_addr,
                            uint8_t reg_addr, const uint8_t *data, size_t len);

/**
 * @brief Read from a register
 * 
 * @param handle I2C handle
 * @param dev_addr Device address (7-bit)
 * @param reg_addr Register address
 * @param data Buffer for read data
 * @param len Number of bytes to read
 * @return ESP_OK on success
 */
esp_err_t ts_i2c_read_reg(ts_i2c_handle_t handle, uint8_t dev_addr,
                           uint8_t reg_addr, uint8_t *data, size_t len);

/**
 * @brief Write single byte to a register
 * 
 * @param handle I2C handle
 * @param dev_addr Device address (7-bit)
 * @param reg_addr Register address
 * @param value Byte to write
 * @return ESP_OK on success
 */
esp_err_t ts_i2c_write_byte(ts_i2c_handle_t handle, uint8_t dev_addr,
                             uint8_t reg_addr, uint8_t value);

/**
 * @brief Read single byte from a register
 * 
 * @param handle I2C handle
 * @param dev_addr Device address (7-bit)
 * @param reg_addr Register address
 * @return Byte value, or -1 on error
 */
int ts_i2c_read_byte(ts_i2c_handle_t handle, uint8_t dev_addr, uint8_t reg_addr);

/**
 * @brief Scan I2C bus for devices
 * 
 * @param handle I2C handle
 * @param found_addrs Array to store found addresses
 * @param max_count Maximum number of addresses to find
 * @return Number of devices found
 */
int ts_i2c_scan(ts_i2c_handle_t handle, uint8_t *found_addrs, size_t max_count);

/**
 * @brief Check if device is present on bus
 * 
 * @param handle I2C handle
 * @param dev_addr Device address (7-bit)
 * @return true if device ACKs, false otherwise
 */
bool ts_i2c_device_present(ts_i2c_handle_t handle, uint8_t dev_addr);

/**
 * @brief Set I2C clock frequency
 * 
 * @param handle I2C handle
 * @param clock_hz New clock frequency
 * @return ESP_OK on success
 */
esp_err_t ts_i2c_set_clock(ts_i2c_handle_t handle, uint32_t clock_hz);

/**
 * @brief Destroy an I2C handle
 * 
 * @param handle I2C handle
 * @return ESP_OK on success
 */
esp_err_t ts_i2c_destroy(ts_i2c_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TS_HAL_I2C_H */
