/**
 * @file ts_platform_s3.c
 * @brief ESP32S3 Platform Adapter Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_platform_s3.h"
#include "ts_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_clk_tree.h"
#include "soc/soc_caps.h"

#define TAG "platform_s3"

/*===========================================================================*/
/*                              Private Data                                  */
/*===========================================================================*/

/* Strapping pins for ESP32S3 */
static const int s_strapping_pins[] = {0, 3, 45, 46};
#define STRAPPING_PIN_COUNT (sizeof(s_strapping_pins) / sizeof(s_strapping_pins[0]))

/* ADC1 GPIO mapping (GPIO 1-10) */
static const int s_adc1_gpios[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
#define ADC1_GPIO_COUNT (sizeof(s_adc1_gpios) / sizeof(s_adc1_gpios[0]))

/* ADC2 GPIO mapping (GPIO 11-20) */
static const int s_adc2_gpios[] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
#define ADC2_GPIO_COUNT (sizeof(s_adc2_gpios) / sizeof(s_adc2_gpios[0]))

/* Touch sensor GPIO mapping (GPIO 1-14) */
static const int s_touch_gpios[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
#define TOUCH_GPIO_COUNT (sizeof(s_touch_gpios) / sizeof(s_touch_gpios[0]))

/*===========================================================================*/
/*                         Public Functions                                   */
/*===========================================================================*/

esp_err_t ts_platform_s3_init(void)
{
    TS_LOGI(TAG, "Initializing ESP32S3 platform");
    
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    TS_LOGI(TAG, "ESP32S3 with %d cores, WiFi%s%s, rev %d.%d",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
            chip_info.revision / 100, chip_info.revision % 100);
    
    TS_LOGI(TAG, "Flash: %lu MB, PSRAM: %lu KB",
            ts_platform_s3_get_flash_size() / (1024 * 1024),
            ts_platform_s3_get_psram_size() / 1024);
    
    TS_LOGI(TAG, "CPU frequency: %lu MHz", ts_platform_s3_get_cpu_freq());
    
    return ESP_OK;
}

bool ts_platform_s3_gpio_valid(int gpio_num)
{
    /* ESP32S3 has GPIO 0-48, but some are internal */
    if (gpio_num < 0 || gpio_num > 48) {
        return false;
    }
    
    /* GPIO 22-25 are used for internal flash (for most modules) */
    if (gpio_num >= 22 && gpio_num <= 25) {
        return false;
    }
    
    /* GPIO 26-32 are used for PSRAM (if equipped) */
#if CONFIG_SPIRAM
    if (gpio_num >= 26 && gpio_num <= 32) {
        return false;
    }
#endif
    
    return true;
}

bool ts_platform_s3_is_strapping(int gpio_num)
{
    for (int i = 0; i < STRAPPING_PIN_COUNT; i++) {
        if (s_strapping_pins[i] == gpio_num) {
            return true;
        }
    }
    return false;
}

bool ts_platform_s3_has_adc(int gpio_num)
{
    /* Check ADC1 */
    for (int i = 0; i < ADC1_GPIO_COUNT; i++) {
        if (s_adc1_gpios[i] == gpio_num) {
            return true;
        }
    }
    
    /* Check ADC2 */
    for (int i = 0; i < ADC2_GPIO_COUNT; i++) {
        if (s_adc2_gpios[i] == gpio_num) {
            return true;
        }
    }
    
    return false;
}

bool ts_platform_s3_has_touch(int gpio_num)
{
    for (int i = 0; i < TOUCH_GPIO_COUNT; i++) {
        if (s_touch_gpios[i] == gpio_num) {
            return true;
        }
    }
    return false;
}

uint32_t ts_platform_s3_get_cpu_freq(void)
{
    uint32_t freq_hz;
    esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &freq_hz);
    return freq_hz / 1000000;
}

uint32_t ts_platform_s3_get_psram_size(void)
{
#if CONFIG_SPIRAM
    return esp_psram_get_size();
#else
    return 0;
#endif
}

uint32_t ts_platform_s3_get_flash_size(void)
{
    uint32_t size;
    if (esp_flash_get_size(NULL, &size) == ESP_OK) {
        return size;
    }
    return 0;
}
