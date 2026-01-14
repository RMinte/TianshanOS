/**
 * @file ts_webui.h
 * @brief TianShanOS Web UI API
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WebUI
 */
esp_err_t ts_webui_init(void);

/**
 * @brief Deinitialize WebUI
 */
esp_err_t ts_webui_deinit(void);

/**
 * @brief Start WebUI server
 */
esp_err_t ts_webui_start(void);

/**
 * @brief Stop WebUI server
 */
esp_err_t ts_webui_stop(void);

/**
 * @brief Check if WebUI is running
 */
bool ts_webui_is_running(void);

/**
 * @brief Broadcast message to all WebSocket clients
 */
esp_err_t ts_webui_broadcast(const char *message);

/**
 * @brief Broadcast event to WebSocket clients
 */
esp_err_t ts_webui_broadcast_event(const char *event_type, const char *data);

#ifdef __cplusplus
}
#endif
