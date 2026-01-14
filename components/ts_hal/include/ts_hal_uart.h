/**
 * @file ts_hal_uart.h
 * @brief TianShanOS UART Abstraction Layer
 * 
 * Provides UART communication functionality.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_HAL_UART_H
#define TS_HAL_UART_H

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
 * @brief UART handle type
 */
typedef struct ts_uart_s *ts_uart_handle_t;

/**
 * @brief UART port number
 */
typedef enum {
    TS_UART_PORT_0 = 0,             /**< UART0 (usually console) */
    TS_UART_PORT_1,                 /**< UART1 */
    TS_UART_PORT_2,                 /**< UART2 */
    TS_UART_PORT_MAX
} ts_uart_port_t;

/**
 * @brief UART parity
 */
typedef enum {
    TS_UART_PARITY_NONE = 0,        /**< No parity */
    TS_UART_PARITY_ODD,             /**< Odd parity */
    TS_UART_PARITY_EVEN             /**< Even parity */
} ts_uart_parity_t;

/**
 * @brief UART stop bits
 */
typedef enum {
    TS_UART_STOP_BITS_1 = 0,        /**< 1 stop bit */
    TS_UART_STOP_BITS_1_5,          /**< 1.5 stop bits */
    TS_UART_STOP_BITS_2             /**< 2 stop bits */
} ts_uart_stop_bits_t;

/**
 * @brief UART data bits
 */
typedef enum {
    TS_UART_DATA_BITS_5 = 0,        /**< 5 data bits */
    TS_UART_DATA_BITS_6,            /**< 6 data bits */
    TS_UART_DATA_BITS_7,            /**< 7 data bits */
    TS_UART_DATA_BITS_8             /**< 8 data bits */
} ts_uart_data_bits_t;

/**
 * @brief UART flow control
 */
typedef enum {
    TS_UART_FLOW_CTRL_NONE = 0,     /**< No flow control */
    TS_UART_FLOW_CTRL_RTS,          /**< RTS flow control */
    TS_UART_FLOW_CTRL_CTS,          /**< CTS flow control */
    TS_UART_FLOW_CTRL_RTS_CTS       /**< RTS+CTS flow control */
} ts_uart_flow_ctrl_t;

/**
 * @brief UART configuration
 */
typedef struct {
    ts_uart_port_t port;            /**< UART port number */
    ts_pin_function_t tx_function;  /**< TX pin function */
    ts_pin_function_t rx_function;  /**< RX pin function */
    uint32_t baud_rate;             /**< Baud rate */
    ts_uart_data_bits_t data_bits;  /**< Data bits */
    ts_uart_parity_t parity;        /**< Parity */
    ts_uart_stop_bits_t stop_bits;  /**< Stop bits */
    ts_uart_flow_ctrl_t flow_ctrl;  /**< Flow control */
    size_t rx_buffer_size;          /**< RX buffer size */
    size_t tx_buffer_size;          /**< TX buffer size (0 for blocking) */
} ts_uart_config_t;

/**
 * @brief Default UART configuration
 */
#define TS_UART_CONFIG_DEFAULT() {          \
    .port = TS_UART_PORT_1,                 \
    .tx_function = TS_PIN_FUNC_UART1_TX,    \
    .rx_function = TS_PIN_FUNC_UART1_RX,    \
    .baud_rate = 115200,                    \
    .data_bits = TS_UART_DATA_BITS_8,       \
    .parity = TS_UART_PARITY_NONE,          \
    .stop_bits = TS_UART_STOP_BITS_1,       \
    .flow_ctrl = TS_UART_FLOW_CTRL_NONE,    \
    .rx_buffer_size = 1024,                 \
    .tx_buffer_size = 0,                    \
}

/**
 * @brief UART event types
 */
typedef enum {
    TS_UART_EVENT_DATA = 0,         /**< Data received */
    TS_UART_EVENT_BREAK,            /**< Break condition */
    TS_UART_EVENT_BUFFER_FULL,      /**< RX buffer full */
    TS_UART_EVENT_OVERFLOW,         /**< FIFO overflow */
    TS_UART_EVENT_FRAME_ERR,        /**< Frame error */
    TS_UART_EVENT_PARITY_ERR        /**< Parity error */
} ts_uart_event_type_t;

/**
 * @brief UART event data
 */
typedef struct {
    ts_uart_event_type_t type;      /**< Event type */
    size_t size;                    /**< Data size (for DATA event) */
} ts_uart_event_t;

/**
 * @brief UART event callback
 */
typedef void (*ts_uart_event_callback_t)(ts_uart_handle_t handle,
                                          const ts_uart_event_t *event,
                                          void *user_data);

/*===========================================================================*/
/*                             UART Functions                                 */
/*===========================================================================*/

/**
 * @brief Initialize the UART subsystem
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 */
esp_err_t ts_uart_init(void);

/**
 * @brief Deinitialize the UART subsystem
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_uart_deinit(void);

/**
 * @brief Create a UART handle
 * 
 * @param config UART configuration
 * @param owner Name of the owning service
 * @return UART handle, or NULL on error
 */
ts_uart_handle_t ts_uart_create(const ts_uart_config_t *config, const char *owner);

/**
 * @brief Write data to UART
 * 
 * @param handle UART handle
 * @param data Data to write
 * @param len Number of bytes to write
 * @param timeout_ms Timeout in milliseconds (-1 for blocking)
 * @return Number of bytes written, or -1 on error
 */
int ts_uart_write(ts_uart_handle_t handle, const uint8_t *data, 
                  size_t len, int timeout_ms);

/**
 * @brief Read data from UART
 * 
 * @param handle UART handle
 * @param data Buffer for read data
 * @param len Maximum bytes to read
 * @param timeout_ms Timeout in milliseconds (-1 for blocking)
 * @return Number of bytes read, or -1 on error
 */
int ts_uart_read(ts_uart_handle_t handle, uint8_t *data,
                 size_t len, int timeout_ms);

/**
 * @brief Write string to UART
 * 
 * @param handle UART handle
 * @param str Null-terminated string
 * @return Number of bytes written
 */
int ts_uart_write_str(ts_uart_handle_t handle, const char *str);

/**
 * @brief Read line from UART
 * 
 * @param handle UART handle
 * @param buf Buffer for line data
 * @param max_len Maximum line length
 * @param timeout_ms Timeout in milliseconds
 * @return Line length, or -1 on error/timeout
 */
int ts_uart_read_line(ts_uart_handle_t handle, char *buf,
                      size_t max_len, int timeout_ms);

/**
 * @brief Get number of bytes available in RX buffer
 * 
 * @param handle UART handle
 * @return Number of bytes available
 */
size_t ts_uart_available(ts_uart_handle_t handle);

/**
 * @brief Flush TX buffer (wait for transmission complete)
 * 
 * @param handle UART handle
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success
 */
esp_err_t ts_uart_flush_tx(ts_uart_handle_t handle, int timeout_ms);

/**
 * @brief Flush RX buffer (discard received data)
 * 
 * @param handle UART handle
 * @return ESP_OK on success
 */
esp_err_t ts_uart_flush_rx(ts_uart_handle_t handle);

/**
 * @brief Set baud rate
 * 
 * @param handle UART handle
 * @param baud_rate New baud rate
 * @return ESP_OK on success
 */
esp_err_t ts_uart_set_baud_rate(ts_uart_handle_t handle, uint32_t baud_rate);

/**
 * @brief Get current baud rate
 * 
 * @param handle UART handle
 * @return Current baud rate
 */
uint32_t ts_uart_get_baud_rate(ts_uart_handle_t handle);

/**
 * @brief Set event callback
 * 
 * @param handle UART handle
 * @param callback Event callback function
 * @param user_data User data for callback
 * @return ESP_OK on success
 */
esp_err_t ts_uart_set_event_callback(ts_uart_handle_t handle,
                                      ts_uart_event_callback_t callback,
                                      void *user_data);

/**
 * @brief Send break signal
 * 
 * @param handle UART handle
 * @param duration_ms Break duration in milliseconds
 * @return ESP_OK on success
 */
esp_err_t ts_uart_send_break(ts_uart_handle_t handle, int duration_ms);

/**
 * @brief Destroy a UART handle
 * 
 * @param handle UART handle
 * @return ESP_OK on success
 */
esp_err_t ts_uart_destroy(ts_uart_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TS_HAL_UART_H */
