/**
 * @file ts_usb_mux.c
 * @brief USB MUX Control Implementation
 */

#include "ts_usb_mux.h"
#include "ts_hal_gpio.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "ts_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "ts_usb_mux"

typedef struct {
    bool configured;
    ts_usb_mux_config_t config;
    ts_usb_target_t current_target;
    bool enabled;
} usb_mux_instance_t;

static usb_mux_instance_t s_muxes[TS_USB_MUX_MAX];
static bool s_initialized = false;

esp_err_t ts_usb_mux_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(s_muxes, 0, sizeof(s_muxes));
    
    // Initialize with invalid pins
    for (int i = 0; i < TS_USB_MUX_MAX; i++) {
        s_muxes[i].config.gpio_sel = -1;
        s_muxes[i].config.gpio_oe = -1;
    }
    
    s_initialized = true;
    TS_LOGI(TAG, "USB MUX driver initialized");
    return ESP_OK;
}

esp_err_t ts_usb_mux_deinit(void)
{
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_usb_mux_configure(ts_usb_mux_id_t mux, const ts_usb_mux_config_t *config)
{
    if (mux >= TS_USB_MUX_MAX || !config) return ESP_ERR_INVALID_ARG;
    
    usb_mux_instance_t *m = &s_muxes[mux];
    m->config = *config;
    
    // Configure select pin
    if (config->gpio_sel >= 0) {
        ts_gpio_config_t cfg = {
            .gpio = config->gpio_sel,
            .direction = TS_GPIO_DIR_OUTPUT,
            .initial_level = config->sel_active_low ? 1 : 0
        };
        ts_gpio_init(&cfg);
    }
    
    // Configure OE pin
    if (config->gpio_oe >= 0) {
        ts_gpio_config_t cfg = {
            .gpio = config->gpio_oe,
            .direction = TS_GPIO_DIR_OUTPUT,
            .initial_level = config->oe_active_low ? 1 : 0  // Disabled initially
        };
        ts_gpio_init(&cfg);
    }
    
    m->configured = true;
    m->current_target = TS_USB_TARGET_DISCONNECT;
    m->enabled = false;
    
    TS_LOGI(TAG, "USB MUX %d configured: SEL=%d, OE=%d", mux, config->gpio_sel, config->gpio_oe);
    return ESP_OK;
}

esp_err_t ts_usb_mux_set_target(ts_usb_mux_id_t mux, ts_usb_target_t target)
{
    if (mux >= TS_USB_MUX_MAX) return ESP_ERR_INVALID_ARG;
    
    usb_mux_instance_t *m = &s_muxes[mux];
    if (!m->configured) return ESP_ERR_INVALID_STATE;
    
    int sel_level = 0;
    
    switch (target) {
        case TS_USB_TARGET_HOST:
            sel_level = m->config.sel_active_low ? 1 : 0;
            break;
        case TS_USB_TARGET_DEVICE:
            sel_level = m->config.sel_active_low ? 0 : 1;
            break;
        case TS_USB_TARGET_DISCONNECT:
            // Just disable the MUX
            return ts_usb_mux_enable(mux, false);
    }
    
    if (m->config.gpio_sel >= 0) {
        ts_gpio_set_level(m->config.gpio_sel, sel_level);
    }
    
    m->current_target = target;
    
    TS_LOGI(TAG, "USB MUX %d target: %s", mux, 
            target == TS_USB_TARGET_HOST ? "HOST" : "DEVICE");
    
    ts_event_post(TS_EVENT_USB, TS_EVT_USB_MUX_SWITCH, &mux, sizeof(mux), 0);
    
    return ESP_OK;
}

esp_err_t ts_usb_mux_get_status(ts_usb_mux_id_t mux, ts_usb_mux_status_t *status)
{
    if (mux >= TS_USB_MUX_MAX || !status) return ESP_ERR_INVALID_ARG;
    if (!s_muxes[mux].configured) return ESP_ERR_INVALID_STATE;
    
    status->target = s_muxes[mux].current_target;
    status->enabled = s_muxes[mux].enabled;
    
    return ESP_OK;
}

esp_err_t ts_usb_mux_enable(ts_usb_mux_id_t mux, bool enable)
{
    if (mux >= TS_USB_MUX_MAX) return ESP_ERR_INVALID_ARG;
    
    usb_mux_instance_t *m = &s_muxes[mux];
    if (!m->configured) return ESP_ERR_INVALID_STATE;
    
    if (m->config.gpio_oe >= 0) {
        int oe_level = enable ? (m->config.oe_active_low ? 0 : 1) 
                              : (m->config.oe_active_low ? 1 : 0);
        ts_gpio_set_level(m->config.gpio_oe, oe_level);
    }
    
    m->enabled = enable;
    
    if (!enable) {
        m->current_target = TS_USB_TARGET_DISCONNECT;
    }
    
    return ESP_OK;
}

esp_err_t ts_usb_mux_switch_to_host(ts_usb_mux_id_t mux, uint32_t timeout_ms)
{
    esp_err_t ret = ts_usb_mux_set_target(mux, TS_USB_TARGET_HOST);
    if (ret != ESP_OK) return ret;
    
    ret = ts_usb_mux_enable(mux, true);
    if (ret != ESP_OK) return ret;
    
    // TODO: Implement timeout and auto-revert
    
    return ESP_OK;
}

esp_err_t ts_usb_mux_switch_to_device(ts_usb_mux_id_t mux)
{
    esp_err_t ret = ts_usb_mux_set_target(mux, TS_USB_TARGET_DEVICE);
    if (ret != ESP_OK) return ret;
    
    return ts_usb_mux_enable(mux, true);
}
