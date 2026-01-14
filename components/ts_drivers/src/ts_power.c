/**
 * @file ts_power.c
 * @brief Power Monitoring Implementation
 */

#include "ts_power.h"
#include "ts_hal_adc.h"
#include "ts_hal_i2c.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "ts_event.h"
#include "esp_timer.h"
#include <string.h>

#define TAG "ts_power"

typedef struct {
    bool configured;
    ts_power_rail_config_t config;
    ts_adc_handle_t adc;
    ts_power_data_t last_data;
    int32_t alert_low;
    int32_t alert_high;
} power_rail_t;

static power_rail_t s_rails[TS_POWER_RAIL_MAX];
static bool s_initialized = false;

esp_err_t ts_power_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(s_rails, 0, sizeof(s_rails));
    
    s_initialized = true;
    TS_LOGI(TAG, "Power monitor initialized");
    return ESP_OK;
}

esp_err_t ts_power_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    for (int i = 0; i < TS_POWER_RAIL_MAX; i++) {
        if (s_rails[i].adc) {
            ts_adc_deinit(s_rails[i].adc);
            s_rails[i].adc = NULL;
        }
    }
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_power_configure_rail(ts_power_rail_t rail, const ts_power_rail_config_t *config)
{
    if (rail >= TS_POWER_RAIL_MAX || !config) return ESP_ERR_INVALID_ARG;
    
    power_rail_t *r = &s_rails[rail];
    r->config = *config;
    
    switch (config->chip) {
        case TS_POWER_CHIP_NONE: {
            // ADC-based monitoring
            ts_adc_config_t adc_cfg = {
                .gpio = config->adc.gpio,
                .atten = TS_ADC_ATTEN_11DB,
                .bitwidth = TS_ADC_WIDTH_12BIT
            };
            esp_err_t ret = ts_adc_init(&adc_cfg, &r->adc);
            if (ret != ESP_OK) {
                TS_LOGE(TAG, "Failed to init ADC for rail %d", rail);
                return ret;
            }
            break;
        }
        
        case TS_POWER_CHIP_INA226:
        case TS_POWER_CHIP_INA3221:
            // TODO: Implement I2C power chip support
            TS_LOGW(TAG, "I2C power chips not yet implemented");
            break;
            
        case TS_POWER_CHIP_UART:
            // TODO: Implement UART power monitor
            TS_LOGW(TAG, "UART power monitor not yet implemented");
            break;
    }
    
    r->configured = true;
    TS_LOGI(TAG, "Power rail %d configured", rail);
    return ESP_OK;
}

esp_err_t ts_power_read(ts_power_rail_t rail, ts_power_data_t *data)
{
    if (rail >= TS_POWER_RAIL_MAX || !data) return ESP_ERR_INVALID_ARG;
    
    power_rail_t *r = &s_rails[rail];
    if (!r->configured) return ESP_ERR_INVALID_STATE;
    
    data->timestamp = esp_timer_get_time() / 1000;
    data->current_ma = -1;
    data->power_mw = -1;
    
    switch (r->config.chip) {
        case TS_POWER_CHIP_NONE: {
            int raw = 0;
            esp_err_t ret = ts_adc_read(r->adc, &raw);
            if (ret != ESP_OK) return ret;
            
            // Convert to mV with divider ratio
            int mv = 0;
            ts_adc_to_voltage(r->adc, raw, &mv);
            data->voltage_mv = (int32_t)(mv * r->config.adc.divider_ratio);
            break;
        }
        
        case TS_POWER_CHIP_INA226:
        case TS_POWER_CHIP_INA3221:
        case TS_POWER_CHIP_UART:
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    r->last_data = *data;
    
    // Check alerts
    if (r->alert_high > 0 && data->voltage_mv > r->alert_high) {
        ts_event_post(TS_EVENT_SYSTEM, TS_EVT_POWER_ALERT_HIGH, 
                      &rail, sizeof(rail), 0);
    }
    if (r->alert_low > 0 && data->voltage_mv < r->alert_low) {
        ts_event_post(TS_EVENT_SYSTEM, TS_EVT_POWER_ALERT_LOW,
                      &rail, sizeof(rail), 0);
    }
    
    return ESP_OK;
}

esp_err_t ts_power_read_all(ts_power_data_t data[TS_POWER_RAIL_MAX])
{
    if (!data) return ESP_ERR_INVALID_ARG;
    
    for (int i = 0; i < TS_POWER_RAIL_MAX; i++) {
        if (s_rails[i].configured) {
            ts_power_read(i, &data[i]);
        } else {
            memset(&data[i], 0, sizeof(ts_power_data_t));
            data[i].voltage_mv = -1;
        }
    }
    return ESP_OK;
}

esp_err_t ts_power_get_total(int32_t *total_mw)
{
    if (!total_mw) return ESP_ERR_INVALID_ARG;
    
    int32_t total = 0;
    for (int i = 0; i < TS_POWER_RAIL_MAX; i++) {
        if (s_rails[i].configured && s_rails[i].last_data.power_mw > 0) {
            total += s_rails[i].last_data.power_mw;
        }
    }
    *total_mw = total;
    return ESP_OK;
}

esp_err_t ts_power_set_alert(ts_power_rail_t rail, int32_t low_mv, int32_t high_mv)
{
    if (rail >= TS_POWER_RAIL_MAX) return ESP_ERR_INVALID_ARG;
    
    s_rails[rail].alert_low = low_mv;
    s_rails[rail].alert_high = high_mv;
    
    return ESP_OK;
}

esp_err_t ts_power_clear_alert(ts_power_rail_t rail)
{
    if (rail >= TS_POWER_RAIL_MAX) return ESP_ERR_INVALID_ARG;
    
    s_rails[rail].alert_low = 0;
    s_rails[rail].alert_high = 0;
    
    return ESP_OK;
}
