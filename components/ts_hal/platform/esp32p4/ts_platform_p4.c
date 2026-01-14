/**
 * @file ts_platform_p4.c
 * @brief ESP32P4 Platform Adapter Implementation (Placeholder)
 * 
 * This is a placeholder implementation for future ESP32P4 support.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_platform_p4.h"

#if CONFIG_IDF_TARGET_ESP32P4

#include "ts_log.h"
#include "esp_chip_info.h"

#define TAG "platform_p4"

esp_err_t ts_platform_p4_init(void)
{
    TS_LOGI(TAG, "Initializing ESP32P4 platform");
    
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    TS_LOGI(TAG, "ESP32P4 with %d cores, rev %d.%d",
            chip_info.cores,
            chip_info.revision / 100, chip_info.revision % 100);
    
    return ESP_OK;
}

bool ts_platform_p4_gpio_valid(int gpio_num)
{
    /* ESP32P4 has GPIO 0-54 */
    if (gpio_num < 0 || gpio_num > 54) {
        return false;
    }
    
    return true;
}

#else

/* Stub implementations for non-P4 builds */
#include "esp_err.h"

esp_err_t ts_platform_p4_init(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool ts_platform_p4_gpio_valid(int gpio_num)
{
    (void)gpio_num;
    return false;
}

#endif /* CONFIG_IDF_TARGET_ESP32P4 */
