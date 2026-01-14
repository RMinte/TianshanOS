/**
 * @file ts_i2c.c
 * @brief TianShanOS I2C Abstraction Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_hal_i2c.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ts_i2c"

/*===========================================================================*/
/*                              Private Types                                 */
/*===========================================================================*/

struct ts_i2c_s {
    ts_i2c_port_t port;                 /* I2C port number */
    ts_i2c_config_t config;             /* Configuration */
    i2c_master_bus_handle_t bus_handle; /* ESP-IDF bus handle */
    bool configured;                    /* Is configured */
    char owner[32];                     /* Owner name */
};

/*===========================================================================*/
/*                              Private Data                                  */
/*===========================================================================*/

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static ts_i2c_handle_t s_handles[CONFIG_TS_HAL_MAX_I2C_HANDLES];
static bool s_port_used[TS_I2C_PORT_MAX] = {false};

/*===========================================================================*/
/*                          Private Functions                                 */
/*===========================================================================*/

static int get_handle_slot(ts_i2c_handle_t handle)
{
    for (int i = 0; i < CONFIG_TS_HAL_MAX_I2C_HANDLES; i++) {
        if (s_handles[i] == handle) {
            return i;
        }
    }
    return -1;
}

/*===========================================================================*/
/*                         Public Functions                                   */
/*===========================================================================*/

esp_err_t ts_i2c_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Initializing I2C subsystem");
    
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(s_handles, 0, sizeof(s_handles));
    memset(s_port_used, 0, sizeof(s_port_used));
    s_initialized = true;
    
    return ESP_OK;
}

esp_err_t ts_i2c_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Deinitializing I2C subsystem");
    
    /* Destroy all handles */
    for (int i = 0; i < CONFIG_TS_HAL_MAX_I2C_HANDLES; i++) {
        if (s_handles[i] != NULL) {
            ts_i2c_destroy(s_handles[i]);
        }
    }
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    return ESP_OK;
}

ts_i2c_handle_t ts_i2c_create(const ts_i2c_config_t *config, const char *owner)
{
    if (!s_initialized || config == NULL || owner == NULL) {
        return NULL;
    }
    
    if (config->port >= TS_I2C_PORT_MAX) {
        TS_LOGE(TAG, "Invalid I2C port: %d", config->port);
        return NULL;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Check if port is already used */
    if (s_port_used[config->port]) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "I2C port %d already in use", config->port);
        return NULL;
    }
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_TS_HAL_MAX_I2C_HANDLES; i++) {
        if (s_handles[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "No free I2C handles");
        return NULL;
    }
    
    /* Get GPIO numbers */
    int sda_gpio = ts_pin_manager_get_gpio(config->sda_function);
    int scl_gpio = ts_pin_manager_get_gpio(config->scl_function);
    
    if (sda_gpio < 0 || scl_gpio < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "I2C pins not mapped: SDA=%d, SCL=%d", sda_gpio, scl_gpio);
        return NULL;
    }
    
    /* Acquire pins */
    esp_err_t ret = ts_pin_manager_acquire(config->sda_function, owner);
    if (ret != ESP_OK) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    ret = ts_pin_manager_acquire(config->scl_function, owner);
    if (ret != ESP_OK) {
        ts_pin_manager_release(config->sda_function);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Allocate handle */
    ts_i2c_handle_t handle = calloc(1, sizeof(struct ts_i2c_s));
    if (handle == NULL) {
        ts_pin_manager_release(config->sda_function);
        ts_pin_manager_release(config->scl_function);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    /* Configure I2C bus */
    i2c_master_bus_config_t bus_config = {
        .i2c_port = (i2c_port_t)config->port,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = config->enable_pullup,
    };
    
    ret = i2c_new_master_bus(&bus_config, &handle->bus_handle);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        ts_pin_manager_release(config->sda_function);
        ts_pin_manager_release(config->scl_function);
        free(handle);
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    handle->port = config->port;
    handle->config = *config;
    handle->configured = true;
    strncpy(handle->owner, owner, sizeof(handle->owner) - 1);
    
    s_handles[slot] = handle;
    s_port_used[config->port] = true;
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGI(TAG, "Created I2C handle on port %d (SDA=GPIO%d, SCL=GPIO%d, %lu Hz)",
            config->port, sda_gpio, scl_gpio, config->clock_hz);
    
    return handle;
}

esp_err_t ts_i2c_write(ts_i2c_handle_t handle, uint8_t dev_addr,
                        const uint8_t *data, size_t len)
{
    if (!s_initialized || handle == NULL || !handle->configured || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Create device handle for this transaction */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = handle->config.clock_hz,
    };
    
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(handle->bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = i2c_master_transmit(dev_handle, data, len, handle->config.timeout_ms);
    
    i2c_master_bus_rm_device(dev_handle);
    
    return ret;
}

esp_err_t ts_i2c_read(ts_i2c_handle_t handle, uint8_t dev_addr,
                       uint8_t *data, size_t len)
{
    if (!s_initialized || handle == NULL || !handle->configured || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = handle->config.clock_hz,
    };
    
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(handle->bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = i2c_master_receive(dev_handle, data, len, handle->config.timeout_ms);
    
    i2c_master_bus_rm_device(dev_handle);
    
    return ret;
}

esp_err_t ts_i2c_write_read(ts_i2c_handle_t handle, uint8_t dev_addr,
                             const uint8_t *write_data, size_t write_len,
                             uint8_t *read_data, size_t read_len)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = handle->config.clock_hz,
    };
    
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(handle->bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    ret = i2c_master_transmit_receive(dev_handle, write_data, write_len,
                                       read_data, read_len, handle->config.timeout_ms);
    
    i2c_master_bus_rm_device(dev_handle);
    
    return ret;
}

esp_err_t ts_i2c_write_reg(ts_i2c_handle_t handle, uint8_t dev_addr,
                            uint8_t reg_addr, const uint8_t *data, size_t len)
{
    if (!s_initialized || handle == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Combine reg_addr and data */
    uint8_t *buf = malloc(len + 1);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);
    
    esp_err_t ret = ts_i2c_write(handle, dev_addr, buf, len + 1);
    
    free(buf);
    return ret;
}

esp_err_t ts_i2c_read_reg(ts_i2c_handle_t handle, uint8_t dev_addr,
                           uint8_t reg_addr, uint8_t *data, size_t len)
{
    return ts_i2c_write_read(handle, dev_addr, &reg_addr, 1, data, len);
}

esp_err_t ts_i2c_write_byte(ts_i2c_handle_t handle, uint8_t dev_addr,
                             uint8_t reg_addr, uint8_t value)
{
    return ts_i2c_write_reg(handle, dev_addr, reg_addr, &value, 1);
}

int ts_i2c_read_byte(ts_i2c_handle_t handle, uint8_t dev_addr, uint8_t reg_addr)
{
    uint8_t value;
    esp_err_t ret = ts_i2c_read_reg(handle, dev_addr, reg_addr, &value, 1);
    return (ret == ESP_OK) ? value : -1;
}

int ts_i2c_scan(ts_i2c_handle_t handle, uint8_t *found_addrs, size_t max_count)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return 0;
    }
    
    int count = 0;
    
    for (uint8_t addr = 0x08; addr < 0x78 && count < (int)max_count; addr++) {
        if (ts_i2c_device_present(handle, addr)) {
            if (found_addrs != NULL) {
                found_addrs[count] = addr;
            }
            count++;
        }
    }
    
    return count;
}

bool ts_i2c_device_present(ts_i2c_handle_t handle, uint8_t dev_addr)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return false;
    }
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = handle->config.clock_hz,
    };
    
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(handle->bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return false;
    }
    
    /* Try to read one byte */
    uint8_t data;
    ret = i2c_master_receive(dev_handle, &data, 1, 50);
    
    i2c_master_bus_rm_device(dev_handle);
    
    return (ret == ESP_OK);
}

esp_err_t ts_i2c_set_clock(ts_i2c_handle_t handle, uint32_t clock_hz)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->config.clock_hz = clock_hz;
    /* Clock will be applied on next transaction */
    
    return ESP_OK;
}

esp_err_t ts_i2c_destroy(ts_i2c_handle_t handle)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Delete bus */
    if (handle->bus_handle) {
        i2c_del_master_bus(handle->bus_handle);
    }
    
    /* Release pins */
    ts_pin_manager_release(handle->config.sda_function);
    ts_pin_manager_release(handle->config.scl_function);
    
    /* Free port */
    s_port_used[handle->port] = false;
    
    /* Find and remove from handles array */
    int slot = get_handle_slot(handle);
    if (slot >= 0) {
        s_handles[slot] = NULL;
    }
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "Destroyed I2C handle on port %d", handle->port);
    
    free(handle);
    return ESP_OK;
}
