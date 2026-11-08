/**
 * @file ts_api_wifi.c
 * @brief WiFi Management API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_api.h"
#include "ts_net_manager.h"
#include "ts_wifi.h"
#include "ts_log.h"
#include "esp_wifi.h"
#include <string.h>

#define TAG "api_wifi"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static const char *auth_mode_str(int auth_mode)
{
    switch (auth_mode) {
        case 0: return "OPEN";
        case 1: return "WEP";
        case 2: return "WPA_PSK";
        case 3: return "WPA2_PSK";
        case 4: return "WPA_WPA2_PSK";
        case 5: return "WPA2_ENTERPRISE";
        case 6: return "WPA3_PSK";
        case 7: return "WPA2_WPA3_PSK";
        default: return "UNKNOWN";
    }
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief wifi.status - Get WiFi status
 */
static esp_err_t api_wifi_status(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    ts_net_manager_status_t status;
    esp_err_t ret = ts_net_manager_get_status(&status);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to get WiFi status");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    /* AP status */
    cJSON *ap = cJSON_AddObjectToObject(data, "ap");
    cJSON_AddStringToObject(ap, "state", ts_net_state_to_str(status.wifi_ap.state));
    cJSON_AddBoolToObject(ap, "has_ip", status.wifi_ap.has_ip);
    if (status.wifi_ap.has_ip) {
        cJSON_AddStringToObject(ap, "ip", status.wifi_ap.ip_info.ip);
    }
    
    /* STA status */
    cJSON *sta = cJSON_AddObjectToObject(data, "sta");
    cJSON_AddStringToObject(sta, "state", ts_net_state_to_str(status.wifi_sta.state));
    cJSON_AddBoolToObject(sta, "has_ip", status.wifi_sta.has_ip);
    if (status.wifi_sta.has_ip) {
        cJSON_AddStringToObject(sta, "ip", status.wifi_sta.ip_info.ip);
        cJSON_AddStringToObject(sta, "gateway", status.wifi_sta.ip_info.gateway);
        
        int8_t rssi = ts_wifi_sta_get_rssi();
        cJSON_AddNumberToObject(sta, "rssi", rssi);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief wifi.scan - Scan for WiFi networks
 */
static esp_err_t api_wifi_scan(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    /* Start scan (blocking) */
    esp_err_t ret = ts_wifi_scan_start(true);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Scan failed");
        return ret;
    }
    
    /* Get scan results */
    ts_wifi_scan_result_t results[20];
    uint16_t count = 20;
    
    ret = ts_wifi_scan_get_results(results, &count);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to get scan results");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON *networks = cJSON_AddArrayToObject(data, "networks");
    cJSON_AddNumberToObject(data, "count", count);
    
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", results[i].ssid);
        
        char bssid[18];
        snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
            results[i].bssid[0], results[i].bssid[1], results[i].bssid[2],
            results[i].bssid[3], results[i].bssid[4], results[i].bssid[5]);
        cJSON_AddStringToObject(item, "bssid", bssid);
        
        cJSON_AddNumberToObject(item, "rssi", results[i].rssi);
        cJSON_AddNumberToObject(item, "channel", results[i].channel);
        cJSON_AddStringToObject(item, "auth", auth_mode_str(results[i].auth_mode));
        
        cJSON_AddItemToArray(networks, item);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief wifi.connect - Connect to WiFi AP
 * 
 * @param params { "ssid": "network", "password": "secret" }
 */
static esp_err_t api_wifi_connect(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *ssid = cJSON_GetObjectItem(params, "ssid");
    const cJSON *password = cJSON_GetObjectItem(params, "password");
    
    if (!ssid || !cJSON_IsString(ssid)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'ssid' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *pass = (password && cJSON_IsString(password)) ? password->valuestring : "";
    
    /* Connect using net manager */
    ts_net_if_config_t config = {0};
    config.enabled = true;
    config.ip_mode = TS_NET_IP_MODE_DHCP;
    strncpy(config.ssid, ssid->valuestring, sizeof(config.ssid) - 1);
    strncpy(config.password, pass, sizeof(config.password) - 1);
    
    esp_err_t ret = ts_net_manager_set_config(TS_NET_IF_WIFI_STA, &config);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to configure WiFi");
        return ret;
    }
    
    ret = ts_net_manager_start(TS_NET_IF_WIFI_STA);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to start WiFi");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "connecting", true);
    cJSON_AddStringToObject(data, "ssid", ssid->valuestring);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief wifi.disconnect - Disconnect from WiFi
 */
static esp_err_t api_wifi_disconnect(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    esp_err_t ret = ts_net_manager_stop(TS_NET_IF_WIFI_STA);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to disconnect");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "disconnected", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_api_wifi_register(void)
{
    static const ts_api_endpoint_t endpoints[] = {
        {
            .name = "wifi.status",
            .description = "Get WiFi status",
            .category = TS_API_CAT_NETWORK,
            .handler = api_wifi_status,
            .external_access = TS_API_ACCESS_PUBLIC,
            .allowed_methods = TS_API_METHOD_READ,
            .requires_auth = false,
        },
        {
            .name = "wifi.scan",
            .description = "Scan for WiFi networks",
            .category = TS_API_CAT_NETWORK,
            .handler = api_wifi_scan,
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .requires_auth = true,
        },
        {
            .name = "wifi.connect",
            .description = "Connect to WiFi network",
            .category = TS_API_CAT_NETWORK,
            .handler = api_wifi_connect,
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .requires_auth = true,
        },
        {
            .name = "wifi.disconnect",
            .description = "Disconnect from WiFi",
            .category = TS_API_CAT_NETWORK,
            .handler = api_wifi_disconnect,
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .requires_auth = true,
        },
    };
    
    return ts_api_register_multiple(endpoints, sizeof(endpoints) / sizeof(endpoints[0]));
}
