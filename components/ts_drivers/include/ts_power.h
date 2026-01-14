/**
 * @file ts_power.h
 * @brief Power Monitoring Driver
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Power rail IDs */
typedef enum {
    TS_POWER_RAIL_VIN = 0,      // Main input voltage
    TS_POWER_RAIL_5V,           // 5V rail
    TS_POWER_RAIL_3V3,          // 3.3V rail
    TS_POWER_RAIL_12V,          // 12V rail (if present)
    TS_POWER_RAIL_VBAT,         // Battery voltage
    TS_POWER_RAIL_MAX
} ts_power_rail_t;

/** Power chip types */
typedef enum {
    TS_POWER_CHIP_NONE,         // ADC-based monitoring only
    TS_POWER_CHIP_INA226,       // TI INA226 via I2C
    TS_POWER_CHIP_INA3221,      // TI INA3221 via I2C
    TS_POWER_CHIP_UART,         // Generic UART power monitor
} ts_power_chip_t;

/** Power rail configuration */
typedef struct {
    ts_power_chip_t chip;
    union {
        struct {
            int gpio;
            float divider_ratio;    // Voltage divider ratio
        } adc;
        struct {
            uint8_t i2c_addr;
            uint8_t channel;        // For multi-channel chips
            float shunt_ohms;
        } i2c;
        struct {
            int uart_num;
        } uart;
    };
} ts_power_rail_config_t;

/** Power measurements */
typedef struct {
    int32_t voltage_mv;         // Voltage in mV
    int32_t current_ma;         // Current in mA (-1 if not available)
    int32_t power_mw;           // Power in mW (-1 if not available)
    uint32_t timestamp;         // Measurement timestamp
} ts_power_data_t;

/**
 * @brief Initialize power monitoring
 */
esp_err_t ts_power_init(void);

/**
 * @brief Deinitialize power monitoring
 */
esp_err_t ts_power_deinit(void);

/**
 * @brief Configure a power rail
 */
esp_err_t ts_power_configure_rail(ts_power_rail_t rail, const ts_power_rail_config_t *config);

/**
 * @brief Read power rail measurements
 */
esp_err_t ts_power_read(ts_power_rail_t rail, ts_power_data_t *data);

/**
 * @brief Read all rails
 */
esp_err_t ts_power_read_all(ts_power_data_t data[TS_POWER_RAIL_MAX]);

/**
 * @brief Get total system power consumption
 */
esp_err_t ts_power_get_total(int32_t *total_mw);

/**
 * @brief Set power alert threshold
 */
esp_err_t ts_power_set_alert(ts_power_rail_t rail, int32_t low_mv, int32_t high_mv);

/**
 * @brief Clear power alert
 */
esp_err_t ts_power_clear_alert(ts_power_rail_t rail);

#ifdef __cplusplus
}
#endif
