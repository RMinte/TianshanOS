/**
 * @file ts_usb_mux.c
 * @brief USB MUX Control Implementation
 * 
 * 3 目标切换：ESP32 / AGX / LPMU
 * 使用 2 个 GPIO 控制选择
 * 状态持久化到 NVS，重启后自动恢复
 * 
 * Truth Table:
 *   SEL0=0, SEL1=0 -> ESP32 (default)
 *   SEL0=1, SEL1=0 -> AGX
 *   SEL0=1, SEL1=1 -> LPMU
 */

#include "ts_usb_mux.h"
#include "ts_hal_gpio.h"
#include "ts_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define TAG "ts_usb_mux"

#define NVS_NAMESPACE   "usb_mux"
#define NVS_KEY_TARGET  "target"

static struct {
    bool configured;
    bool initialized;
    ts_usb_mux_pins_t pins;
    ts_gpio_handle_t gpio_sel0;
    ts_gpio_handle_t gpio_sel1;
    ts_usb_mux_target_t current_target;
} s_mux = {0};

/*===========================================================================*/
/*                          NVS Persistence                                   */
/*===========================================================================*/

/**
 * @brief 从 NVS 加载保存的目标设置
 */
static ts_usb_mux_target_t load_target_from_nvs(void)
{
    nvs_handle_t handle;
    ts_usb_mux_target_t target = TS_USB_MUX_ESP32;  // 默认值
    
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_OK) {
        uint8_t value = 0;
        ret = nvs_get_u8(handle, NVS_KEY_TARGET, &value);
        if (ret == ESP_OK && value < 4) {
            target = (ts_usb_mux_target_t)value;
            TS_LOGI(TAG, "Loaded USB MUX target from NVS: %d", target);
        }
        nvs_close(handle);
    }
    
    return target;
}

/**
 * @brief 保存目标设置到 NVS
 */
static esp_err_t save_target_to_nvs(ts_usb_mux_target_t target)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = nvs_set_u8(handle, NVS_KEY_TARGET, (uint8_t)target);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
        if (ret == ESP_OK) {
            TS_LOGI(TAG, "Saved USB MUX target to NVS: %d", target);
        }
    }
    
    nvs_close(handle);
    return ret;
}

/*===========================================================================*/
/*                          Forward Declarations                              */
/*===========================================================================*/

static esp_err_t ts_usb_mux_set_target_internal(ts_usb_mux_target_t target);

/*===========================================================================*/
/*                          Public API                                        */
/*===========================================================================*/

esp_err_t ts_usb_mux_init(void)
{
    if (s_mux.initialized) return ESP_OK;
    
    memset(&s_mux, 0, sizeof(s_mux));
    s_mux.pins.gpio_sel0 = -1;
    s_mux.pins.gpio_sel1 = -1;
    s_mux.current_target = TS_USB_MUX_ESP32;
    
    s_mux.initialized = true;
    TS_LOGI(TAG, "USB MUX driver initialized");
    return ESP_OK;
}

esp_err_t ts_usb_mux_deinit(void)
{
    if (s_mux.gpio_sel0) {
        ts_gpio_destroy(s_mux.gpio_sel0);
        s_mux.gpio_sel0 = NULL;
    }
    if (s_mux.gpio_sel1) {
        ts_gpio_destroy(s_mux.gpio_sel1);
        s_mux.gpio_sel1 = NULL;
    }
    
    s_mux.configured = false;
    s_mux.initialized = false;
    return ESP_OK;
}

esp_err_t ts_usb_mux_configure(const ts_usb_mux_pins_t *pins)
{
    if (!pins) return ESP_ERR_INVALID_ARG;
    if (!s_mux.initialized) return ESP_ERR_INVALID_STATE;
    
    s_mux.pins = *pins;
    
    // Configure SEL0 pin
    if (pins->gpio_sel0 >= 0) {
        s_mux.gpio_sel0 = ts_gpio_create_raw(pins->gpio_sel0, "usb_sel0");
        if (s_mux.gpio_sel0) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_OUTPUT,
                .pull_mode = TS_GPIO_PULL_NONE,
                .intr_type = TS_GPIO_INTR_DISABLE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = 0  // Default to ESP32 (LOW)
            };
            ts_gpio_configure(s_mux.gpio_sel0, &cfg);
        }
    }
    
    // Configure SEL1 pin
    if (pins->gpio_sel1 >= 0) {
        s_mux.gpio_sel1 = ts_gpio_create_raw(pins->gpio_sel1, "usb_sel1");
        if (s_mux.gpio_sel1) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_OUTPUT,
                .pull_mode = TS_GPIO_PULL_NONE,
                .intr_type = TS_GPIO_INTR_DISABLE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = 0  // Default to ESP32 (LOW)
            };
            ts_gpio_configure(s_mux.gpio_sel1, &cfg);
        }
    }
    
    s_mux.configured = true;
    
    // 从 NVS 恢复之前保存的目标状态
    ts_usb_mux_target_t saved_target = load_target_from_nvs();
    
    // 应用保存的状态（不再保存，避免重复写入）
    esp_err_t ret = ts_usb_mux_set_target_internal(saved_target);
    if (ret != ESP_OK) {
        // 如果恢复失败，使用默认值
        ts_usb_mux_set_target_internal(TS_USB_MUX_ESP32);
    }
    
    TS_LOGI(TAG, "USB MUX configured (sel0=%d, sel1=%d), restored target: %d",
            pins->gpio_sel0, pins->gpio_sel1, s_mux.current_target);
    return ESP_OK;
}

/**
 * @brief 内部函数：设置目标（不保存到 NVS）
 */
static esp_err_t ts_usb_mux_set_target_internal(ts_usb_mux_target_t target)
{
    if (!s_mux.configured) {
        TS_LOGW(TAG, "USB MUX not configured");
        return ESP_ERR_INVALID_STATE;
    }
    
    int sel0 = 0, sel1 = 0;
    const char *target_str;
    
    switch (target) {
        case TS_USB_MUX_ESP32:
            // SEL0=0, SEL1=0
            sel0 = 0;
            sel1 = 0;
            target_str = "ESP32";
            break;
        case TS_USB_MUX_AGX:
            // SEL0=1, SEL1=0
            sel0 = 1;
            sel1 = 0;
            target_str = "AGX";
            break;
        case TS_USB_MUX_LPMU:
            // SEL0=1, SEL1=1
            sel0 = 1;
            sel1 = 1;
            target_str = "LPMU";
            break;
        case TS_USB_MUX_DISCONNECT:
            // SEL0=0, SEL1=1 (unused state, acts as disconnect)
            sel0 = 0;
            sel1 = 1;
            target_str = "DISCONNECT";
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    // Set GPIO levels
    if (s_mux.gpio_sel0) {
        ts_gpio_set_level(s_mux.gpio_sel0, sel0);
    }
    if (s_mux.gpio_sel1) {
        ts_gpio_set_level(s_mux.gpio_sel1, sel1);
    }
    
    s_mux.current_target = target;
    
    TS_LOGI(TAG, "USB MUX -> %s (sel0=%d, sel1=%d)", target_str, sel0, sel1);
    return ESP_OK;
}

esp_err_t ts_usb_mux_set_target(ts_usb_mux_target_t target)
{
    esp_err_t ret = ts_usb_mux_set_target_internal(target);
    if (ret == ESP_OK) {
        // 保存到 NVS 持久化
        save_target_to_nvs(target);
    }
    return ret;
}

ts_usb_mux_target_t ts_usb_mux_get_target(void)
{
    return s_mux.current_target;
}

bool ts_usb_mux_is_configured(void)
{
    return s_mux.configured;
}
