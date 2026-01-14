/**
 * @file ts_hal_spi.h
 * @brief TianShanOS SPI Abstraction Layer
 * 
 * Provides SPI master functionality for device communication.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_HAL_SPI_H
#define TS_HAL_SPI_H

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
 * @brief SPI bus handle type
 */
typedef struct ts_spi_bus_s *ts_spi_bus_handle_t;

/**
 * @brief SPI device handle type
 */
typedef struct ts_spi_device_s *ts_spi_device_handle_t;

/**
 * @brief SPI host
 */
typedef enum {
    TS_SPI_HOST_1 = 1,              /**< SPI2 host */
    TS_SPI_HOST_2 = 2,              /**< SPI3 host */
} ts_spi_host_t;

/**
 * @brief SPI mode
 */
typedef enum {
    TS_SPI_MODE_0 = 0,              /**< CPOL=0, CPHA=0 */
    TS_SPI_MODE_1,                  /**< CPOL=0, CPHA=1 */
    TS_SPI_MODE_2,                  /**< CPOL=1, CPHA=0 */
    TS_SPI_MODE_3                   /**< CPOL=1, CPHA=1 */
} ts_spi_mode_t;

/**
 * @brief SPI bus configuration
 */
typedef struct {
    ts_spi_host_t host;             /**< SPI host */
    ts_pin_function_t miso_function;/**< MISO pin function */
    ts_pin_function_t mosi_function;/**< MOSI pin function */
    ts_pin_function_t sclk_function;/**< SCLK pin function */
    uint32_t max_transfer_size;     /**< Maximum transfer size in bytes */
    bool dma_enabled;               /**< Enable DMA transfers */
} ts_spi_bus_config_t;

/**
 * @brief Default SPI bus configuration
 */
#define TS_SPI_BUS_CONFIG_DEFAULT() {       \
    .host = TS_SPI_HOST_1,                  \
    .miso_function = TS_PIN_FUNC_ETH_MISO,  \
    .mosi_function = TS_PIN_FUNC_ETH_MOSI,  \
    .sclk_function = TS_PIN_FUNC_ETH_SCLK,  \
    .max_transfer_size = 4096,              \
    .dma_enabled = true,                    \
}

/**
 * @brief SPI device configuration
 */
typedef struct {
    ts_pin_function_t cs_function;  /**< CS pin function */
    uint32_t clock_hz;              /**< Clock frequency in Hz */
    ts_spi_mode_t mode;             /**< SPI mode */
    uint8_t command_bits;           /**< Command phase bits */
    uint8_t address_bits;           /**< Address phase bits */
    uint8_t dummy_bits;             /**< Dummy phase bits */
    bool cs_active_high;            /**< CS is active high */
    uint8_t cs_pre_delay;           /**< CS setup time in half-clocks */
    uint8_t cs_post_delay;          /**< CS hold time in half-clocks */
} ts_spi_device_config_t;

/**
 * @brief Default SPI device configuration
 */
#define TS_SPI_DEVICE_CONFIG_DEFAULT() {    \
    .cs_function = TS_PIN_FUNC_ETH_CS,      \
    .clock_hz = 10000000,                   \
    .mode = TS_SPI_MODE_0,                  \
    .command_bits = 0,                      \
    .address_bits = 0,                      \
    .dummy_bits = 0,                        \
    .cs_active_high = false,                \
    .cs_pre_delay = 0,                      \
    .cs_post_delay = 0,                     \
}

/**
 * @brief SPI transaction structure
 */
typedef struct {
    uint16_t command;               /**< Command data */
    uint64_t address;               /**< Address data */
    const uint8_t *tx_buffer;       /**< TX data buffer */
    uint8_t *rx_buffer;             /**< RX data buffer */
    size_t length;                  /**< Data length in bytes */
} ts_spi_transaction_t;

/*===========================================================================*/
/*                             SPI Functions                                  */
/*===========================================================================*/

/**
 * @brief Initialize the SPI subsystem
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t ts_spi_init(void);

/**
 * @brief Deinitialize the SPI subsystem
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_spi_deinit(void);

/**
 * @brief Initialize an SPI bus
 * 
 * @param config Bus configuration
 * @param owner Name of the owning service
 * @return Bus handle, or NULL on error
 */
ts_spi_bus_handle_t ts_spi_bus_create(const ts_spi_bus_config_t *config, 
                                       const char *owner);

/**
 * @brief Add a device to an SPI bus
 * 
 * @param bus Bus handle
 * @param config Device configuration
 * @return Device handle, or NULL on error
 */
ts_spi_device_handle_t ts_spi_device_add(ts_spi_bus_handle_t bus,
                                          const ts_spi_device_config_t *config);

/**
 * @brief Perform an SPI transaction
 * 
 * @param device Device handle
 * @param transaction Transaction data
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_TIMEOUT on timeout
 */
esp_err_t ts_spi_transfer(ts_spi_device_handle_t device,
                          ts_spi_transaction_t *transaction);

/**
 * @brief Simple write (TX only)
 * 
 * @param device Device handle
 * @param data Data to write
 * @param len Length in bytes
 * @return ESP_OK on success
 */
esp_err_t ts_spi_write(ts_spi_device_handle_t device, 
                        const uint8_t *data, size_t len);

/**
 * @brief Simple read (RX only)
 * 
 * @param device Device handle
 * @param data Buffer for read data
 * @param len Length in bytes
 * @return ESP_OK on success
 */
esp_err_t ts_spi_read(ts_spi_device_handle_t device,
                       uint8_t *data, size_t len);

/**
 * @brief Full-duplex transfer
 * 
 * @param device Device handle
 * @param tx_data TX data buffer
 * @param rx_data RX data buffer
 * @param len Length in bytes
 * @return ESP_OK on success
 */
esp_err_t ts_spi_transfer_full_duplex(ts_spi_device_handle_t device,
                                       const uint8_t *tx_data,
                                       uint8_t *rx_data,
                                       size_t len);

/**
 * @brief Write to register
 * 
 * @param device Device handle
 * @param reg_addr Register address
 * @param data Data to write
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t ts_spi_write_reg(ts_spi_device_handle_t device, uint8_t reg_addr,
                            const uint8_t *data, size_t len);

/**
 * @brief Read from register
 * 
 * @param device Device handle
 * @param reg_addr Register address
 * @param data Buffer for read data
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t ts_spi_read_reg(ts_spi_device_handle_t device, uint8_t reg_addr,
                           uint8_t *data, size_t len);

/**
 * @brief Acquire bus for exclusive access
 * 
 * @param device Device handle
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t ts_spi_acquire_bus(ts_spi_device_handle_t device, uint32_t timeout_ms);

/**
 * @brief Release bus after exclusive access
 * 
 * @param device Device handle
 * @return ESP_OK on success
 */
esp_err_t ts_spi_release_bus(ts_spi_device_handle_t device);

/**
 * @brief Remove a device from bus
 * 
 * @param device Device handle
 * @return ESP_OK on success
 */
esp_err_t ts_spi_device_remove(ts_spi_device_handle_t device);

/**
 * @brief Destroy an SPI bus
 * 
 * All devices must be removed first.
 * 
 * @param bus Bus handle
 * @return ESP_OK on success
 */
esp_err_t ts_spi_bus_destroy(ts_spi_bus_handle_t bus);

#ifdef __cplusplus
}
#endif

#endif /* TS_HAL_SPI_H */
