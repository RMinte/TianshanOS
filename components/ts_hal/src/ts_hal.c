/**
 * @file ts_hal.c
 * @brief TianShanOS HAL Module - Main Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_hal.h"
#include "ts_log.h"
#include "ts_config.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "ts_hal"

/*===========================================================================*/
/*                              Private Data                                  */
/*===========================================================================*/

static bool s_hal_initialized = false;
static SemaphoreHandle_t s_hal_mutex = NULL;
static ts_hal_config_t s_hal_config;

/*===========================================================================*/
/*                         Public Functions                                   */
/*===========================================================================*/

esp_err_t ts_hal_init(const ts_hal_config_t *config)
{
    if (s_hal_initialized) {
        TS_LOGW(TAG, "HAL already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Initializing HAL module v%d.%d.%d",
            TS_HAL_VERSION_MAJOR, TS_HAL_VERSION_MINOR, TS_HAL_VERSION_PATCH);
    
    /* Create mutex */
    s_hal_mutex = xSemaphoreCreateMutex();
    if (s_hal_mutex == NULL) {
        TS_LOGE(TAG, "Failed to create HAL mutex");
        return ESP_ERR_NO_MEM;
    }
    
    /* Apply configuration */
    if (config != NULL) {
        s_hal_config = *config;
    } else {
        ts_hal_config_t default_config = TS_HAL_CONFIG_DEFAULT();
        s_hal_config = default_config;
    }
    
    esp_err_t ret;
    
    /* Initialize pin manager */
    ret = ts_pin_manager_init();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Pin manager init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    
    /* Load pin configuration */
    if (s_hal_config.load_from_nvs) {
        ret = ts_pin_manager_load_nvs();
        if (ret != ESP_OK) {
            TS_LOGI(TAG, "No pin config in NVS, loading defaults");
        }
    }
    
    if (s_hal_config.pin_config_path != NULL) {
        ret = ts_pin_manager_load_config(s_hal_config.pin_config_path);
        if (ret != ESP_OK) {
            TS_LOGW(TAG, "Failed to load pin config from %s", s_hal_config.pin_config_path);
        }
    }
    
    /* If no config loaded, use defaults */
    ts_pin_manager_load_defaults();
    
    /* Initialize subsystems */
    ret = ts_gpio_init();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "GPIO init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    
    ret = ts_pwm_init();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "PWM init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    
    ret = ts_i2c_init();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    
    ret = ts_spi_init();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    
    ret = ts_uart_init();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "UART init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    
    ret = ts_adc_init();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "ADC init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    
    s_hal_initialized = true;
    TS_LOGI(TAG, "HAL initialization complete, platform: %s", ts_hal_get_platform());
    
    return ESP_OK;
    
fail:
    if (s_hal_mutex) {
        vSemaphoreDelete(s_hal_mutex);
        s_hal_mutex = NULL;
    }
    return ret;
}

esp_err_t ts_hal_deinit(void)
{
    if (!s_hal_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Deinitializing HAL module");
    
    /* Deinitialize subsystems in reverse order */
    ts_adc_deinit();
    ts_uart_deinit();
    ts_spi_deinit();
    ts_i2c_deinit();
    ts_pwm_deinit();
    ts_gpio_deinit();
    ts_pin_manager_deinit();
    
    if (s_hal_mutex) {
        vSemaphoreDelete(s_hal_mutex);
        s_hal_mutex = NULL;
    }
    
    s_hal_initialized = false;
    return ESP_OK;
}

bool ts_hal_is_initialized(void)
{
    return s_hal_initialized;
}

const char *ts_hal_get_version(void)
{
    static char version_str[16];
    snprintf(version_str, sizeof(version_str), "%d.%d.%d",
             TS_HAL_VERSION_MAJOR, TS_HAL_VERSION_MINOR, TS_HAL_VERSION_PATCH);
    return version_str;
}

const char *ts_hal_get_platform(void)
{
#if CONFIG_IDF_TARGET_ESP32S3
    return "ESP32S3";
#elif CONFIG_IDF_TARGET_ESP32P4
    return "ESP32P4";
#elif CONFIG_IDF_TARGET_ESP32
    return "ESP32";
#elif CONFIG_IDF_TARGET_ESP32C3
    return "ESP32C3";
#elif CONFIG_IDF_TARGET_ESP32C6
    return "ESP32C6";
#else
    return "UNKNOWN";
#endif
}

esp_err_t ts_hal_get_capabilities(ts_hal_capabilities_t *caps)
{
    if (caps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    memset(caps, 0, sizeof(ts_hal_capabilities_t));
    
    /* Platform-specific capabilities */
#if CONFIG_IDF_TARGET_ESP32S3
    caps->gpio_count = 48;
    caps->pwm_channels = 8;
    caps->i2c_ports = 2;
    caps->spi_hosts = 2;
    caps->uart_ports = 3;
    caps->adc_channels = 20;
    caps->has_usb_otg = true;
#elif CONFIG_IDF_TARGET_ESP32P4
    caps->gpio_count = 55;
    caps->pwm_channels = 8;
    caps->i2c_ports = 2;
    caps->spi_hosts = 2;
    caps->uart_ports = 5;
    caps->adc_channels = 16;
    caps->has_usb_otg = true;
#else
    caps->gpio_count = 34;
    caps->pwm_channels = 8;
    caps->i2c_ports = 2;
    caps->spi_hosts = 2;
    caps->uart_ports = 3;
    caps->adc_channels = 18;
    caps->has_usb_otg = false;
#endif
    
    /* Check PSRAM */
#if CONFIG_SPIRAM
    caps->has_psram = (esp_psram_get_size() > 0);
#else
    caps->has_psram = false;
#endif
    
    /* CPU frequency */
    caps->cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    
    /* Flash size */
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        caps->flash_size_mb = flash_size / (1024 * 1024);
    }
    
    return ESP_OK;
}
