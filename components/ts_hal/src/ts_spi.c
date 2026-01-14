/**
 * @file ts_spi.c
 * @brief TianShanOS SPI Abstraction Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_hal_spi.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ts_spi"

/*===========================================================================*/
/*                              Private Types                                 */
/*===========================================================================*/

struct ts_spi_bus_s {
    ts_spi_host_t host;                     /* SPI host */
    ts_spi_bus_config_t config;             /* Configuration */
    bool initialized;                       /* Is initialized */
    int device_count;                       /* Number of attached devices */
    char owner[32];                         /* Owner name */
};

struct ts_spi_device_s {
    ts_spi_bus_handle_t bus;                /* Parent bus */
    ts_spi_device_config_t config;          /* Device configuration */
    spi_device_handle_t spi_dev;            /* ESP-IDF device handle */
    int cs_gpio;                            /* CS GPIO number */
    bool configured;                        /* Is configured */
};

/*===========================================================================*/
/*                              Private Data                                  */
/*===========================================================================*/

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static ts_spi_bus_handle_t s_buses[CONFIG_TS_HAL_MAX_SPI_HANDLES];
static bool s_host_used[3] = {false}; /* SPI1, SPI2, SPI3 */

/*===========================================================================*/
/*                          Private Functions                                 */
/*===========================================================================*/

static int get_bus_slot(ts_spi_bus_handle_t bus)
{
    for (int i = 0; i < CONFIG_TS_HAL_MAX_SPI_HANDLES; i++) {
        if (s_buses[i] == bus) {
            return i;
        }
    }
    return -1;
}

/*===========================================================================*/
/*                         Public Functions                                   */
/*===========================================================================*/

esp_err_t ts_spi_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Initializing SPI subsystem");
    
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(s_buses, 0, sizeof(s_buses));
    memset(s_host_used, 0, sizeof(s_host_used));
    s_initialized = true;
    
    return ESP_OK;
}

esp_err_t ts_spi_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Deinitializing SPI subsystem");
    
    /* Destroy all buses */
    for (int i = 0; i < CONFIG_TS_HAL_MAX_SPI_HANDLES; i++) {
        if (s_buses[i] != NULL) {
            ts_spi_bus_destroy(s_buses[i]);
        }
    }
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    return ESP_OK;
}

ts_spi_bus_handle_t ts_spi_bus_create(const ts_spi_bus_config_t *config,
                                       const char *owner)
{
    if (!s_initialized || config == NULL || owner == NULL) {
        return NULL;
    }
    
    if (config->host < TS_SPI_HOST_1 || config->host > TS_SPI_HOST_2) {
        TS_LOGE(TAG, "Invalid SPI host: %d", config->host);
        return NULL;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Check if host is already used */
    if (s_host_used[config->host]) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "SPI host %d already in use", config->host);
        return NULL;
    }
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_TS_HAL_MAX_SPI_HANDLES; i++) {
        if (s_buses[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "No free SPI bus slots");
        return NULL;
    }
    
    /* Get GPIO numbers */
    int miso_gpio = ts_pin_manager_get_gpio(config->miso_function);
    int mosi_gpio = ts_pin_manager_get_gpio(config->mosi_function);
    int sclk_gpio = ts_pin_manager_get_gpio(config->sclk_function);
    
    if (miso_gpio < 0 || mosi_gpio < 0 || sclk_gpio < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "SPI pins not mapped");
        return NULL;
    }
    
    /* Acquire pins */
    esp_err_t ret = ts_pin_manager_acquire(config->miso_function, owner);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    ret = ts_pin_manager_acquire(config->mosi_function, owner);
    if (ret != ESP_OK) {
        ts_pin_manager_release(config->miso_function);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    ret = ts_pin_manager_acquire(config->sclk_function, owner);
    if (ret != ESP_OK) {
        ts_pin_manager_release(config->miso_function);
        ts_pin_manager_release(config->mosi_function);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Allocate handle */
    ts_spi_bus_handle_t bus = calloc(1, sizeof(struct ts_spi_bus_s));
    if (bus == NULL) {
        ts_pin_manager_release(config->miso_function);
        ts_pin_manager_release(config->mosi_function);
        ts_pin_manager_release(config->sclk_function);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Configure SPI bus */
    spi_bus_config_t bus_cfg = {
        .miso_io_num = miso_gpio,
        .mosi_io_num = mosi_gpio,
        .sclk_io_num = sclk_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = config->max_transfer_size,
    };
    
    spi_host_device_t host = (config->host == TS_SPI_HOST_1) ? SPI2_HOST : SPI3_HOST;
    
    ret = spi_bus_initialize(host, &bus_cfg, 
                              config->dma_enabled ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        ts_pin_manager_release(config->miso_function);
        ts_pin_manager_release(config->mosi_function);
        ts_pin_manager_release(config->sclk_function);
        free(bus);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    bus->host = config->host;
    bus->config = *config;
    bus->initialized = true;
    bus->device_count = 0;
    strncpy(bus->owner, owner, sizeof(bus->owner) - 1);
    
    s_buses[slot] = bus;
    s_host_used[config->host] = true;
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGI(TAG, "Created SPI bus on host %d (MISO=%d, MOSI=%d, SCLK=%d)",
            config->host, miso_gpio, mosi_gpio, sclk_gpio);
    
    return bus;
}

ts_spi_device_handle_t ts_spi_device_add(ts_spi_bus_handle_t bus,
                                          const ts_spi_device_config_t *config)
{
    if (!s_initialized || bus == NULL || config == NULL || !bus->initialized) {
        return NULL;
    }
    
    /* Get CS GPIO */
    int cs_gpio = ts_pin_manager_get_gpio(config->cs_function);
    if (cs_gpio < 0) {
        TS_LOGE(TAG, "CS pin not mapped");
        return NULL;
    }
    
    /* Acquire CS pin */
    esp_err_t ret = ts_pin_manager_acquire(config->cs_function, bus->owner);
    if (ret != ESP_OK) {
        return NULL;
    }
    
    /* Allocate device */
    ts_spi_device_handle_t device = calloc(1, sizeof(struct ts_spi_device_s));
    if (device == NULL) {
        ts_pin_manager_release(config->cs_function);
        return NULL;
    }
    
    /* Configure device */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = config->clock_hz,
        .mode = (uint8_t)config->mode,
        .spics_io_num = cs_gpio,
        .queue_size = 4,
        .command_bits = config->command_bits,
        .address_bits = config->address_bits,
        .dummy_bits = config->dummy_bits,
        .cs_ena_pretrans = config->cs_pre_delay,
        .cs_ena_posttrans = config->cs_post_delay,
        .flags = config->cs_active_high ? SPI_DEVICE_POSITIVE_CS : 0,
    };
    
    spi_host_device_t host = (bus->host == TS_SPI_HOST_1) ? SPI2_HOST : SPI3_HOST;
    
    ret = spi_bus_add_device(host, &dev_cfg, &device->spi_dev);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        ts_pin_manager_release(config->cs_function);
        free(device);
        return NULL;
    }
    
    device->bus = bus;
    device->config = *config;
    device->cs_gpio = cs_gpio;
    device->configured = true;
    bus->device_count++;
    
    TS_LOGD(TAG, "Added SPI device: CS=GPIO%d, clock=%lu Hz", cs_gpio, config->clock_hz);
    
    return device;
}

esp_err_t ts_spi_transfer(ts_spi_device_handle_t device,
                          ts_spi_transaction_t *transaction)
{
    if (!s_initialized || device == NULL || !device->configured || transaction == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    spi_transaction_t spi_trans = {
        .cmd = transaction->command,
        .addr = transaction->address,
        .length = transaction->length * 8,
        .tx_buffer = transaction->tx_buffer,
        .rx_buffer = transaction->rx_buffer,
    };
    
    return spi_device_polling_transmit(device->spi_dev, &spi_trans);
}

esp_err_t ts_spi_write(ts_spi_device_handle_t device,
                        const uint8_t *data, size_t len)
{
    ts_spi_transaction_t trans = {
        .tx_buffer = data,
        .rx_buffer = NULL,
        .length = len,
    };
    
    return ts_spi_transfer(device, &trans);
}

esp_err_t ts_spi_read(ts_spi_device_handle_t device,
                       uint8_t *data, size_t len)
{
    ts_spi_transaction_t trans = {
        .tx_buffer = NULL,
        .rx_buffer = data,
        .length = len,
    };
    
    return ts_spi_transfer(device, &trans);
}

esp_err_t ts_spi_transfer_full_duplex(ts_spi_device_handle_t device,
                                       const uint8_t *tx_data,
                                       uint8_t *rx_data,
                                       size_t len)
{
    ts_spi_transaction_t trans = {
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
        .length = len,
    };
    
    return ts_spi_transfer(device, &trans);
}

esp_err_t ts_spi_write_reg(ts_spi_device_handle_t device, uint8_t reg_addr,
                            const uint8_t *data, size_t len)
{
    if (!s_initialized || device == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t *buf = malloc(len + 1);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);
    
    esp_err_t ret = ts_spi_write(device, buf, len + 1);
    
    free(buf);
    return ret;
}

esp_err_t ts_spi_read_reg(ts_spi_device_handle_t device, uint8_t reg_addr,
                           uint8_t *data, size_t len)
{
    if (!s_initialized || device == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t *buf = malloc(len + 1);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    buf[0] = reg_addr | 0x80; /* Read flag */
    memset(buf + 1, 0, len);
    
    esp_err_t ret = ts_spi_transfer_full_duplex(device, buf, buf, len + 1);
    if (ret == ESP_OK) {
        memcpy(data, buf + 1, len);
    }
    
    free(buf);
    return ret;
}

esp_err_t ts_spi_acquire_bus(ts_spi_device_handle_t device, uint32_t timeout_ms)
{
    if (!s_initialized || device == NULL || !device->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return spi_device_acquire_bus(device->spi_dev, portMAX_DELAY);
}

esp_err_t ts_spi_release_bus(ts_spi_device_handle_t device)
{
    if (!s_initialized || device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    spi_device_release_bus(device->spi_dev);
    return ESP_OK;
}

esp_err_t ts_spi_device_remove(ts_spi_device_handle_t device)
{
    if (!s_initialized || device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (device->spi_dev) {
        spi_bus_remove_device(device->spi_dev);
    }
    
    ts_pin_manager_release(device->config.cs_function);
    
    if (device->bus) {
        device->bus->device_count--;
    }
    
    TS_LOGD(TAG, "Removed SPI device: CS=GPIO%d", device->cs_gpio);
    
    free(device);
    return ESP_OK;
}

esp_err_t ts_spi_bus_destroy(ts_spi_bus_handle_t bus)
{
    if (!s_initialized || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (bus->device_count > 0) {
        TS_LOGE(TAG, "Cannot destroy bus with %d attached devices", bus->device_count);
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Free bus */
    spi_host_device_t host = (bus->host == TS_SPI_HOST_1) ? SPI2_HOST : SPI3_HOST;
    spi_bus_free(host);
    
    /* Release pins */
    ts_pin_manager_release(bus->config.miso_function);
    ts_pin_manager_release(bus->config.mosi_function);
    ts_pin_manager_release(bus->config.sclk_function);
    
    /* Free host */
    s_host_used[bus->host] = false;
    
    /* Find and remove from buses array */
    int slot = get_bus_slot(bus);
    if (slot >= 0) {
        s_buses[slot] = NULL;
    }
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "Destroyed SPI bus on host %d", bus->host);
    
    free(bus);
    return ESP_OK;
}
