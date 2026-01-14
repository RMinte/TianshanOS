/**
 * @file ts_drivers.h
 * @brief TianShanOS Device Drivers API
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all device drivers
 */
esp_err_t ts_drivers_init(void);

/**
 * @brief Deinitialize all device drivers
 */
esp_err_t ts_drivers_deinit(void);

#ifdef __cplusplus
}
#endif
