/**
 * @file ts_uart.c
 * @brief TianShanOS UART Abstraction Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_hal_uart.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ts_uart"

/*===========================================================================*/
/*                              Private Types                                 */
/*===========================================================================*/

struct ts_uart_s {
    ts_uart_port_t port;                    /* UART port number */
    ts_uart_config_t config;                /* Configuration */
    bool configured;                        /* Is configured */
    ts_uart_event_callback_t event_cb;      /* Event callback */
    void *event_user_data;                  /* Event callback user data */
    TaskHandle_t event_task;                /* Event task handle */
    QueueHandle_t event_queue;              /* Event queue */
    char owner[32];                         /* Owner name */
};

/*===========================================================================*/
/*                              Private Data                                  */
/*===========================================================================*/

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static ts_uart_handle_t s_handles[CONFIG_TS_HAL_MAX_UART_HANDLES];
static bool s_port_used[TS_UART_PORT_MAX] = {false};

/*===========================================================================*/
/*                          Private Functions                                 */
/*===========================================================================*/

static int get_handle_slot(ts_uart_handle_t handle)
{
    for (int i = 0; i < CONFIG_TS_HAL_MAX_UART_HANDLES; i++) {
        if (s_handles[i] == handle) {
            return i;
        }
    }
    return -1;
}

static uart_word_length_t convert_data_bits(ts_uart_data_bits_t data_bits)
{
    switch (data_bits) {
        case TS_UART_DATA_BITS_5: return UART_DATA_5_BITS;
        case TS_UART_DATA_BITS_6: return UART_DATA_6_BITS;
        case TS_UART_DATA_BITS_7: return UART_DATA_7_BITS;
        default: return UART_DATA_8_BITS;
    }
}

static uart_parity_t convert_parity(ts_uart_parity_t parity)
{
    switch (parity) {
        case TS_UART_PARITY_ODD: return UART_PARITY_ODD;
        case TS_UART_PARITY_EVEN: return UART_PARITY_EVEN;
        default: return UART_PARITY_DISABLE;
    }
}

static uart_stop_bits_t convert_stop_bits(ts_uart_stop_bits_t stop_bits)
{
    switch (stop_bits) {
        case TS_UART_STOP_BITS_1_5: return UART_STOP_BITS_1_5;
        case TS_UART_STOP_BITS_2: return UART_STOP_BITS_2;
        default: return UART_STOP_BITS_1;
    }
}

static uart_hw_flowcontrol_t convert_flow_ctrl(ts_uart_flow_ctrl_t flow_ctrl)
{
    switch (flow_ctrl) {
        case TS_UART_FLOW_CTRL_RTS: return UART_HW_FLOWCTRL_RTS;
        case TS_UART_FLOW_CTRL_CTS: return UART_HW_FLOWCTRL_CTS;
        case TS_UART_FLOW_CTRL_RTS_CTS: return UART_HW_FLOWCTRL_CTS_RTS;
        default: return UART_HW_FLOWCTRL_DISABLE;
    }
}

static void uart_event_task(void *arg)
{
    ts_uart_handle_t handle = (ts_uart_handle_t)arg;
    uart_event_t event;
    
    while (1) {
        if (xQueueReceive(handle->event_queue, &event, portMAX_DELAY)) {
            if (handle->event_cb == NULL) {
                continue;
            }
            
            ts_uart_event_t ts_event = {0};
            
            switch (event.type) {
                case UART_DATA:
                    ts_event.type = TS_UART_EVENT_DATA;
                    ts_event.size = event.size;
                    break;
                case UART_BREAK:
                    ts_event.type = TS_UART_EVENT_BREAK;
                    break;
                case UART_BUFFER_FULL:
                    ts_event.type = TS_UART_EVENT_BUFFER_FULL;
                    break;
                case UART_FIFO_OVF:
                    ts_event.type = TS_UART_EVENT_OVERFLOW;
                    break;
                case UART_FRAME_ERR:
                    ts_event.type = TS_UART_EVENT_FRAME_ERR;
                    break;
                case UART_PARITY_ERR:
                    ts_event.type = TS_UART_EVENT_PARITY_ERR;
                    break;
                default:
                    continue;
            }
            
            handle->event_cb(handle, &ts_event, handle->event_user_data);
        }
    }
}

/*===========================================================================*/
/*                         Public Functions                                   */
/*===========================================================================*/

esp_err_t ts_uart_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Initializing UART subsystem");
    
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(s_handles, 0, sizeof(s_handles));
    /* Mark UART0 as used (console) */
    s_port_used[TS_UART_PORT_0] = true;
    s_initialized = true;
    
    return ESP_OK;
}

esp_err_t ts_uart_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Deinitializing UART subsystem");
    
    /* Destroy all handles */
    for (int i = 0; i < CONFIG_TS_HAL_MAX_UART_HANDLES; i++) {
        if (s_handles[i] != NULL) {
            ts_uart_destroy(s_handles[i]);
        }
    }
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    return ESP_OK;
}

ts_uart_handle_t ts_uart_create(const ts_uart_config_t *config, const char *owner)
{
    if (!s_initialized || config == NULL || owner == NULL) {
        return NULL;
    }
    
    if (config->port >= TS_UART_PORT_MAX) {
        TS_LOGE(TAG, "Invalid UART port: %d", config->port);
        return NULL;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Check if port is already used */
    if (s_port_used[config->port]) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "UART port %d already in use", config->port);
        return NULL;
    }
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_TS_HAL_MAX_UART_HANDLES; i++) {
        if (s_handles[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "No free UART handles");
        return NULL;
    }
    
    /* Get GPIO numbers */
    int tx_gpio = ts_pin_manager_get_gpio(config->tx_function);
    int rx_gpio = ts_pin_manager_get_gpio(config->rx_function);
    
    if (tx_gpio < 0 || rx_gpio < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "UART pins not mapped");
        return NULL;
    }
    
    /* Acquire pins */
    esp_err_t ret = ts_pin_manager_acquire(config->tx_function, owner);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    ret = ts_pin_manager_acquire(config->rx_function, owner);
    if (ret != ESP_OK) {
        ts_pin_manager_release(config->tx_function);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Allocate handle */
    ts_uart_handle_t handle = calloc(1, sizeof(struct ts_uart_s));
    if (handle == NULL) {
        ts_pin_manager_release(config->tx_function);
        ts_pin_manager_release(config->rx_function);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Configure UART */
    uart_config_t uart_config = {
        .baud_rate = (int)config->baud_rate,
        .data_bits = convert_data_bits(config->data_bits),
        .parity = convert_parity(config->parity),
        .stop_bits = convert_stop_bits(config->stop_bits),
        .flow_ctrl = convert_flow_ctrl(config->flow_ctrl),
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    ret = uart_param_config((uart_port_t)config->port, &uart_config);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        ts_pin_manager_release(config->tx_function);
        ts_pin_manager_release(config->rx_function);
        free(handle);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    ret = uart_set_pin((uart_port_t)config->port, tx_gpio, rx_gpio, 
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        ts_pin_manager_release(config->tx_function);
        ts_pin_manager_release(config->rx_function);
        free(handle);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Create event queue if callback will be used */
    QueueHandle_t event_queue = NULL;
    
    ret = uart_driver_install((uart_port_t)config->port, 
                               config->rx_buffer_size,
                               config->tx_buffer_size,
                               20, &event_queue, 0);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        ts_pin_manager_release(config->tx_function);
        ts_pin_manager_release(config->rx_function);
        free(handle);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    handle->port = config->port;
    handle->config = *config;
    handle->configured = true;
    handle->event_queue = event_queue;
    handle->event_task = NULL;
    strncpy(handle->owner, owner, sizeof(handle->owner) - 1);
    
    s_handles[slot] = handle;
    s_port_used[config->port] = true;
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGI(TAG, "Created UART handle on port %d (TX=GPIO%d, RX=GPIO%d, %lu baud)",
            config->port, tx_gpio, rx_gpio, config->baud_rate);
    
    return handle;
}

int ts_uart_write(ts_uart_handle_t handle, const uint8_t *data,
                  size_t len, int timeout_ms)
{
    if (!s_initialized || handle == NULL || !handle->configured || data == NULL) {
        return -1;
    }
    
    return uart_write_bytes((uart_port_t)handle->port, data, len);
}

int ts_uart_read(ts_uart_handle_t handle, uint8_t *data,
                 size_t len, int timeout_ms)
{
    if (!s_initialized || handle == NULL || !handle->configured || data == NULL) {
        return -1;
    }
    
    TickType_t ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return uart_read_bytes((uart_port_t)handle->port, data, len, ticks);
}

int ts_uart_write_str(ts_uart_handle_t handle, const char *str)
{
    if (str == NULL) {
        return -1;
    }
    return ts_uart_write(handle, (const uint8_t *)str, strlen(str), -1);
}

int ts_uart_read_line(ts_uart_handle_t handle, char *buf,
                      size_t max_len, int timeout_ms)
{
    if (!s_initialized || handle == NULL || buf == NULL || max_len == 0) {
        return -1;
    }
    
    size_t pos = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    while (pos < max_len - 1) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (timeout != portMAX_DELAY && elapsed >= timeout) {
            break;
        }
        
        TickType_t remaining = (timeout == portMAX_DELAY) ? portMAX_DELAY : timeout - elapsed;
        int read = uart_read_bytes((uart_port_t)handle->port, 
                                   (uint8_t *)&buf[pos], 1, remaining);
        
        if (read <= 0) {
            break;
        }
        
        if (buf[pos] == '\n') {
            pos++;
            break;
        }
        
        if (buf[pos] != '\r') {
            pos++;
        }
    }
    
    buf[pos] = '\0';
    return (int)pos;
}

size_t ts_uart_available(ts_uart_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return 0;
    }
    
    size_t available = 0;
    uart_get_buffered_data_len((uart_port_t)handle->port, &available);
    return available;
}

esp_err_t ts_uart_flush_tx(ts_uart_handle_t handle, int timeout_ms)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return uart_wait_tx_done((uart_port_t)handle->port, 
                              timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms));
}

esp_err_t ts_uart_flush_rx(ts_uart_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return uart_flush_input((uart_port_t)handle->port);
}

esp_err_t ts_uart_set_baud_rate(ts_uart_handle_t handle, uint32_t baud_rate)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = uart_set_baudrate((uart_port_t)handle->port, baud_rate);
    if (ret == ESP_OK) {
        handle->config.baud_rate = baud_rate;
    }
    return ret;
}

uint32_t ts_uart_get_baud_rate(ts_uart_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return 0;
    }
    
    uint32_t baud_rate;
    if (uart_get_baudrate((uart_port_t)handle->port, &baud_rate) == ESP_OK) {
        return baud_rate;
    }
    return 0;
}

esp_err_t ts_uart_set_event_callback(ts_uart_handle_t handle,
                                      ts_uart_event_callback_t callback,
                                      void *user_data)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->event_cb = callback;
    handle->event_user_data = user_data;
    
    /* Create event task if callback is set and task doesn't exist */
    if (callback != NULL && handle->event_task == NULL && handle->event_queue != NULL) {
        xTaskCreate(uart_event_task, "uart_event", 2048, handle, 10, &handle->event_task);
    }
    
    /* Delete event task if callback is cleared */
    if (callback == NULL && handle->event_task != NULL) {
        vTaskDelete(handle->event_task);
        handle->event_task = NULL;
    }
    
    return ESP_OK;
}

esp_err_t ts_uart_send_break(ts_uart_handle_t handle, int duration_ms)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Send break signal by setting TX low for duration */
    (void)duration_ms;  /* Not directly supported in new API */
    /* TODO: Implement using gpio or other method if needed */
    ESP_LOGW("TS_UART", "uart_send_break not supported in ESP-IDF 5.5+");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ts_uart_destroy(ts_uart_handle_t handle)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Stop event task */
    if (handle->event_task != NULL) {
        vTaskDelete(handle->event_task);
        handle->event_task = NULL;
    }
    
    /* Uninstall driver */
    uart_driver_delete((uart_port_t)handle->port);
    
    /* Release pins */
    ts_pin_manager_release(handle->config.tx_function);
    ts_pin_manager_release(handle->config.rx_function);
    
    /* Free port */
    s_port_used[handle->port] = false;
    
    /* Find and remove from handles array */
    int slot = get_handle_slot(handle);
    if (slot >= 0) {
        s_handles[slot] = NULL;
    }
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "Destroyed UART handle on port %d", handle->port);
    
    free(handle);
    return ESP_OK;
}
