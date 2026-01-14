/**
 * @file ts_power.c
 * @brief Power Monitoring Implementation
 */

#include "ts_power.h"
#include "ts_hal_adc.h"
#include "ts_hal_i2c.h"
#include "ts_log.h"
#include "ts_event.h"
#include "esp_timer.h"
#include <string.h>

#define TAG "ts_power"

/*===========================================================================*/
/*                          INA226 Registers                                  */
/*===========================================================================*/

#define INA226_REG_CONFIG       0x00
#define INA226_REG_SHUNT_VOLT   0x01
#define INA226_REG_BUS_VOLT     0x02
#define INA226_REG_POWER        0x03
#define INA226_REG_CURRENT      0x04
#define INA226_REG_CALIBRATION  0x05
#define INA226_REG_MASK_ENABLE  0x06
#define INA226_REG_ALERT_LIMIT  0x07
#define INA226_REG_MANUF_ID     0xFE
#define INA226_REG_DIE_ID       0xFF

#define INA226_MANUF_ID         0x5449  /* TI */
#define INA226_DIE_ID           0x2260

/* INA226 Config register bits */
#define INA226_CONFIG_RESET     (1 << 15)
#define INA226_CONFIG_AVG_1     (0 << 9)
#define INA226_CONFIG_AVG_4     (1 << 9)
#define INA226_CONFIG_AVG_16    (2 << 9)
#define INA226_CONFIG_AVG_64    (3 << 9)
#define INA226_CONFIG_VBUS_140US  (0 << 6)
#define INA226_CONFIG_VBUS_204US  (1 << 6)
#define INA226_CONFIG_VBUS_332US  (2 << 6)
#define INA226_CONFIG_VBUS_588US  (3 << 6)
#define INA226_CONFIG_VBUS_1100US (4 << 6)
#define INA226_CONFIG_VSHUNT_140US  (0 << 3)
#define INA226_CONFIG_VSHUNT_204US  (1 << 3)
#define INA226_CONFIG_VSHUNT_332US  (2 << 3)
#define INA226_CONFIG_VSHUNT_588US  (3 << 3)
#define INA226_CONFIG_VSHUNT_1100US (4 << 3)
#define INA226_CONFIG_MODE_CONT_SHUNT_BUS 7

/*===========================================================================*/
/*                          INA3221 Registers                                 */
/*===========================================================================*/

#define INA3221_REG_CONFIG      0x00
#define INA3221_REG_SHUNT1      0x01
#define INA3221_REG_BUS1        0x02
#define INA3221_REG_SHUNT2      0x03
#define INA3221_REG_BUS2        0x04
#define INA3221_REG_SHUNT3      0x05
#define INA3221_REG_BUS3        0x06
#define INA3221_REG_MANUF_ID    0xFE
#define INA3221_REG_DIE_ID      0xFF

#define INA3221_MANUF_ID        0x5449  /* TI */

typedef struct {
    bool configured;
    ts_power_rail_config_t config;
    ts_adc_handle_t adc;
    ts_i2c_handle_t i2c;
    uint16_t calibration;
    float current_lsb;
    ts_power_data_t last_data;
    int32_t alert_low;
    int32_t alert_high;
} power_rail_t;

static power_rail_t s_rails[TS_POWER_RAIL_MAX];
static ts_i2c_handle_t s_i2c = NULL;
static bool s_initialized = false;

/*===========================================================================*/
/*                          I2C Helper Functions                              */
/*===========================================================================*/

static esp_err_t ina_write_reg(uint8_t addr, uint8_t reg, uint16_t value)
{
    if (!s_i2c) return ESP_ERR_INVALID_STATE;
    
    uint8_t data[3] = {reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    return ts_i2c_write(s_i2c, addr, data, 3);
}

static esp_err_t ina_read_reg(uint8_t addr, uint8_t reg, uint16_t *value)
{
    if (!s_i2c || !value) return ESP_ERR_INVALID_ARG;
    
    uint8_t data[2];
    esp_err_t ret = ts_i2c_write_read(s_i2c, addr, &reg, 1, data, 2);
    if (ret == ESP_OK) {
        *value = ((uint16_t)data[0] << 8) | data[1];
    }
    return ret;
}

static esp_err_t ina226_init(power_rail_t *r)
{
    uint8_t addr = r->config.i2c.i2c_addr;
    
    /* Verify device ID */
    uint16_t manuf_id, die_id;
    if (ina_read_reg(addr, INA226_REG_MANUF_ID, &manuf_id) != ESP_OK ||
        ina_read_reg(addr, INA226_REG_DIE_ID, &die_id) != ESP_OK) {
        TS_LOGE(TAG, "INA226: Failed to read ID at 0x%02x", addr);
        return ESP_FAIL;
    }
    
    if (manuf_id != INA226_MANUF_ID) {
        TS_LOGE(TAG, "INA226: Invalid manufacturer ID 0x%04x", manuf_id);
        return ESP_FAIL;
    }
    
    TS_LOGI(TAG, "INA226 detected at 0x%02x (die_id=0x%04x)", addr, die_id);
    
    /* Reset device */
    ina_write_reg(addr, INA226_REG_CONFIG, INA226_CONFIG_RESET);
    vTaskDelay(pdMS_TO_TICKS(1));
    
    /* Configure: 16 samples averaging, 1.1ms conversion time, continuous mode */
    uint16_t config = INA226_CONFIG_AVG_16 | 
                      INA226_CONFIG_VBUS_1100US | 
                      INA226_CONFIG_VSHUNT_1100US |
                      INA226_CONFIG_MODE_CONT_SHUNT_BUS;
    ina_write_reg(addr, INA226_REG_CONFIG, config);
    
    /* Calculate calibration value:
     * Current_LSB = Max_Expected_Current / 2^15
     * CAL = 0.00512 / (Current_LSB * R_shunt)
     * 
     * For 10A max with 0.01 ohm shunt:
     * Current_LSB = 10 / 32768 = 0.000305 A = 0.305 mA
     * CAL = 0.00512 / (0.000305 * 0.01) = 1678
     */
    float shunt_ohms = r->config.i2c.shunt_ohms > 0 ? r->config.i2c.shunt_ohms : 0.01f;
    float max_current = 10.0f;  /* 10A max */
    r->current_lsb = max_current / 32768.0f;
    r->calibration = (uint16_t)(0.00512f / (r->current_lsb * shunt_ohms));
    
    ina_write_reg(addr, INA226_REG_CALIBRATION, r->calibration);
    
    TS_LOGI(TAG, "INA226 configured: shunt=%.3f ohm, cal=%u", shunt_ohms, r->calibration);
    return ESP_OK;
}

static esp_err_t ina3221_init(power_rail_t *r)
{
    uint8_t addr = r->config.i2c.i2c_addr;
    
    /* Verify manufacturer ID */
    uint16_t manuf_id;
    if (ina_read_reg(addr, INA3221_REG_MANUF_ID, &manuf_id) != ESP_OK) {
        TS_LOGE(TAG, "INA3221: Failed to read ID at 0x%02x", addr);
        return ESP_FAIL;
    }
    
    if (manuf_id != INA3221_MANUF_ID) {
        TS_LOGE(TAG, "INA3221: Invalid manufacturer ID 0x%04x", manuf_id);
        return ESP_FAIL;
    }
    
    TS_LOGI(TAG, "INA3221 detected at 0x%02x", addr);
    
    /* Configure for continuous operation, all channels enabled */
    uint16_t config = 0x7127;  /* Default config with all channels enabled */
    ina_write_reg(addr, INA3221_REG_CONFIG, config);
    
    /* INA3221 doesn't have calibration register - current calculated in software */
    float shunt_ohms = r->config.i2c.shunt_ohms > 0 ? r->config.i2c.shunt_ohms : 0.01f;
    r->current_lsb = 40e-6f / shunt_ohms;  /* 40uV LSB for shunt voltage */
    
    TS_LOGI(TAG, "INA3221 channel %d configured: shunt=%.3f ohm", 
            r->config.i2c.channel, shunt_ohms);
    return ESP_OK;
}

static esp_err_t ina226_read(power_rail_t *r, ts_power_data_t *data)
{
    uint8_t addr = r->config.i2c.i2c_addr;
    uint16_t bus_volt, power, current;
    
    /* Read bus voltage: LSB = 1.25mV */
    if (ina_read_reg(addr, INA226_REG_BUS_VOLT, &bus_volt) != ESP_OK) {
        return ESP_FAIL;
    }
    data->voltage_mv = (int32_t)(bus_volt * 1.25f);
    
    /* Read current register */
    if (ina_read_reg(addr, INA226_REG_CURRENT, &current) != ESP_OK) {
        return ESP_FAIL;
    }
    /* Current is signed 16-bit */
    int16_t current_signed = (int16_t)current;
    data->current_ma = (int32_t)(current_signed * r->current_lsb * 1000.0f);
    
    /* Read power register: LSB = 25 * current_lsb */
    if (ina_read_reg(addr, INA226_REG_POWER, &power) != ESP_OK) {
        return ESP_FAIL;
    }
    data->power_mw = (int32_t)(power * 25.0f * r->current_lsb * 1000.0f);
    
    return ESP_OK;
}

static esp_err_t ina3221_read(power_rail_t *r, ts_power_data_t *data)
{
    uint8_t addr = r->config.i2c.i2c_addr;
    uint8_t channel = r->config.i2c.channel;
    
    if (channel > 2) return ESP_ERR_INVALID_ARG;
    
    /* Calculate register addresses for this channel */
    uint8_t shunt_reg = INA3221_REG_SHUNT1 + (channel * 2);
    uint8_t bus_reg = INA3221_REG_BUS1 + (channel * 2);
    
    uint16_t bus_volt, shunt_volt;
    
    /* Read bus voltage: LSB = 8mV (lower 3 bits unused) */
    if (ina_read_reg(addr, bus_reg, &bus_volt) != ESP_OK) {
        return ESP_FAIL;
    }
    data->voltage_mv = (int32_t)((bus_volt >> 3) * 8);
    
    /* Read shunt voltage: LSB = 40uV (lower 3 bits unused) */
    if (ina_read_reg(addr, shunt_reg, &shunt_volt) != ESP_OK) {
        return ESP_FAIL;
    }
    int16_t shunt_signed = (int16_t)(shunt_volt);
    float shunt_mv = (shunt_signed >> 3) * 0.04f;  /* 40uV = 0.04mV */
    
    /* Calculate current: I = V_shunt / R_shunt */
    float shunt_ohms = r->config.i2c.shunt_ohms > 0 ? r->config.i2c.shunt_ohms : 0.01f;
    data->current_ma = (int32_t)(shunt_mv / shunt_ohms);
    
    /* Calculate power */
    data->power_mw = (data->voltage_mv * data->current_ma) / 1000;
    
    return ESP_OK;
}

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
            ts_adc_destroy(s_rails[i].adc);
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
                .function = TS_PIN_FUNC_POWER_ADC,
                .attenuation = TS_ADC_ATTEN_11DB,
                .width = TS_ADC_WIDTH_12BIT,
                .use_calibration = true
            };
            r->adc = ts_adc_create(&adc_cfg, "power");
            if (!r->adc) {
                TS_LOGE(TAG, "Failed to create ADC for rail %d", rail);
                return ESP_FAIL;
            }
            break;
        }
        
        case TS_POWER_CHIP_INA226:
        case TS_POWER_CHIP_INA3221: {
            /* Create I2C handle if not exists */
            if (!s_i2c) {
                ts_i2c_config_t i2c_cfg = TS_I2C_CONFIG_DEFAULT(TS_I2C_PORT_0);
                s_i2c = ts_i2c_create(&i2c_cfg, "power");
                if (!s_i2c) {
                    TS_LOGE(TAG, "Failed to create I2C for power monitor");
                    return ESP_FAIL;
                }
            }
            r->i2c = s_i2c;
            
            esp_err_t ret;
            if (config->chip == TS_POWER_CHIP_INA226) {
                ret = ina226_init(r);
            } else {
                ret = ina3221_init(r);
            }
            if (ret != ESP_OK) {
                return ret;
            }
            break;
        }
            
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
            int mv = ts_adc_read_mv(r->adc);
            if (mv < 0) return ESP_FAIL;
            
            // Apply divider ratio
            data->voltage_mv = (int32_t)(mv * r->config.adc.divider_ratio);
            break;
        }
        
        case TS_POWER_CHIP_INA226: {
            esp_err_t ret = ina226_read(r, data);
            if (ret != ESP_OK) return ret;
            break;
        }
        
        case TS_POWER_CHIP_INA3221: {
            esp_err_t ret = ina3221_read(r, data);
            if (ret != ESP_OK) return ret;
            break;
        }
        
        case TS_POWER_CHIP_UART:
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    r->last_data = *data;
    
    // Check alerts
    if (r->alert_high > 0 && data->voltage_mv > r->alert_high) {
        TS_LOGW(TAG, "Power rail %d voltage high: %ld mV", rail, (long)data->voltage_mv);
    }
    if (r->alert_low > 0 && data->voltage_mv < r->alert_low) {
        TS_LOGW(TAG, "Power rail %d voltage low: %ld mV", rail, (long)data->voltage_mv);
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
