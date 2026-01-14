/**
 * @file ts_wifi.h
 * @brief WiFi Manager
 */

#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** WiFi mode */
typedef enum {
    TS_WIFI_MODE_OFF,
    TS_WIFI_MODE_STA,
    TS_WIFI_MODE_AP,
    TS_WIFI_MODE_APSTA
} ts_wifi_mode_t;

/** WiFi station config */
typedef struct {
    char ssid[32];
    char password[64];
    uint8_t bssid[6];
    bool bssid_set;
} ts_wifi_sta_config_t;

/** WiFi AP config */
typedef struct {
    char ssid[32];
    char password[64];
    uint8_t channel;
    uint8_t max_connections;
    bool hidden;
    wifi_auth_mode_t auth_mode;
} ts_wifi_ap_config_t;

/** WiFi scan result */
typedef struct {
    char ssid[32];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    wifi_auth_mode_t auth_mode;
} ts_wifi_scan_result_t;

/** Connected station info */
typedef struct {
    uint8_t mac[6];
    int8_t rssi;
} ts_wifi_sta_info_t;

/**
 * @brief Initialize WiFi
 */
esp_err_t ts_wifi_init(void);

/**
 * @brief Deinitialize WiFi
 */
esp_err_t ts_wifi_deinit(void);

/**
 * @brief Set WiFi mode
 */
esp_err_t ts_wifi_set_mode(ts_wifi_mode_t mode);

/**
 * @brief Get WiFi mode
 */
ts_wifi_mode_t ts_wifi_get_mode(void);

/**
 * @brief Configure station mode
 */
esp_err_t ts_wifi_sta_config(const ts_wifi_sta_config_t *config);

/**
 * @brief Connect to AP (station mode)
 */
esp_err_t ts_wifi_sta_connect(void);

/**
 * @brief Disconnect from AP
 */
esp_err_t ts_wifi_sta_disconnect(void);

/**
 * @brief Check if connected
 */
bool ts_wifi_sta_is_connected(void);

/**
 * @brief Get current RSSI
 */
int8_t ts_wifi_sta_get_rssi(void);

/**
 * @brief Configure AP mode
 */
esp_err_t ts_wifi_ap_config(const ts_wifi_ap_config_t *config);

/**
 * @brief Start AP
 */
esp_err_t ts_wifi_ap_start(void);

/**
 * @brief Stop AP
 */
esp_err_t ts_wifi_ap_stop(void);

/**
 * @brief Get connected stations count
 */
uint8_t ts_wifi_ap_get_sta_count(void);

/**
 * @brief Get connected stations list
 */
esp_err_t ts_wifi_ap_get_sta_list(ts_wifi_sta_info_t *list, uint8_t *count);

/**
 * @brief Start WiFi scan
 */
esp_err_t ts_wifi_scan_start(bool block);

/**
 * @brief Get scan results
 */
esp_err_t ts_wifi_scan_get_results(ts_wifi_scan_result_t *results, uint16_t *count);

#ifdef __cplusplus
}
#endif
