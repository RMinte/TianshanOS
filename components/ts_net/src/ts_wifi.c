/**
 * @file ts_wifi.c
 * @brief WiFi Manager Implementation
 */

#include "ts_wifi.h"
#include "ts_net.h"
#include "ts_log.h"
#include "ts_event.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include <string.h>

#define TAG "ts_wifi"

static bool s_initialized = false;
static ts_wifi_mode_t s_mode = TS_WIFI_MODE_OFF;
static bool s_sta_connected = false;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                TS_LOGI(TAG, "WiFi STA started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                TS_LOGI(TAG, "WiFi STA connected");
                s_sta_connected = true;
                // 不在事件处理器内发布事件，避免队列锁冲突
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                TS_LOGI(TAG, "WiFi STA disconnected");
                s_sta_connected = false;
                break;
            /* AP 事件由 ts_dhcp_server 处理 */
        }
    }
    /* IP 事件由 ts_net_manager 统一处理 */
}

esp_err_t ts_wifi_init(void)
{
    if (s_initialized) return ESP_OK;
    
    TS_LOGI(TAG, "Initializing WiFi");
    
    // Create default netifs
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    
    // Init WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "esp_wifi_init failed");
        return ret;
    }
    
    // Register event handlers - 只注册 STA 相关事件
    // AP 事件由 ts_dhcp_server 处理，这里不注册避免重复
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, wifi_event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, wifi_event_handler, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL);
    /* IP 事件由 ts_net_manager 统一处理，这里不注册 */
    
    s_initialized = true;
    TS_LOGI(TAG, "WiFi initialized");
    return ESP_OK;
}

esp_err_t ts_wifi_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    // Unregister event handlers (只有 STA 事件)
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_START, wifi_event_handler);
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, wifi_event_handler);
    esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler);
    /* IP 事件由 ts_net_manager 处理 */
    
    esp_wifi_stop();
    esp_wifi_deinit();
    
    if (s_sta_netif) {
        esp_netif_destroy(s_sta_netif);
        s_sta_netif = NULL;
    }
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = NULL;
    }
    
    s_initialized = false;
    s_mode = TS_WIFI_MODE_OFF;
    return ESP_OK;
}

esp_err_t ts_wifi_set_mode(ts_wifi_mode_t mode)
{
    wifi_mode_t esp_mode;
    esp_err_t ret;
    
    switch (mode) {
        case TS_WIFI_MODE_OFF:
            esp_wifi_stop();
            s_mode = mode;
            TS_LOGI(TAG, "WiFi stopped");
            return ESP_OK;
        case TS_WIFI_MODE_STA:
            esp_mode = WIFI_MODE_STA;
            break;
        case TS_WIFI_MODE_AP:
            esp_mode = WIFI_MODE_AP;
            break;
        case TS_WIFI_MODE_APSTA:
            esp_mode = WIFI_MODE_APSTA;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    // 先停止 WiFi（如果已启动）
    esp_wifi_stop();
    
    ret = esp_wifi_set_mode(esp_mode);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 启动 WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_mode = mode;
    TS_LOGI(TAG, "WiFi mode set to %d and started", mode);
    return ESP_OK;
}

ts_wifi_mode_t ts_wifi_get_mode(void)
{
    return s_mode;
}

esp_err_t ts_wifi_sta_config(const ts_wifi_sta_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password));
    
    if (config->bssid_set) {
        memcpy(wifi_config.sta.bssid, config->bssid, 6);
        wifi_config.sta.bssid_set = true;
    }
    
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

esp_err_t ts_wifi_sta_connect(void)
{
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) return ret;
    
    return esp_wifi_connect();
}

esp_err_t ts_wifi_sta_disconnect(void)
{
    return esp_wifi_disconnect();
}

bool ts_wifi_sta_is_connected(void)
{
    return s_sta_connected;
}

int8_t ts_wifi_sta_get_rssi(void)
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return 0;
}

esp_err_t ts_wifi_ap_config(const ts_wifi_ap_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.ap.ssid, config->ssid, sizeof(wifi_config.ap.ssid));
    strncpy((char *)wifi_config.ap.password, config->password, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len = strlen(config->ssid);
    wifi_config.ap.channel = config->channel;
    wifi_config.ap.max_connection = config->max_connections;
    wifi_config.ap.authmode = config->auth_mode;
    wifi_config.ap.ssid_hidden = config->hidden;
    
    if (strlen(config->password) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    
    return esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
}

esp_err_t ts_wifi_ap_start(void)
{
    return esp_wifi_start();
}

esp_err_t ts_wifi_ap_stop(void)
{
    return esp_wifi_stop();
}

uint8_t ts_wifi_ap_get_sta_count(void)
{
    wifi_sta_list_t sta_list;
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
        return sta_list.num;
    }
    return 0;
}

esp_err_t ts_wifi_ap_get_sta_list(ts_wifi_sta_info_t *list, uint8_t *count)
{
    if (!list || !count) return ESP_ERR_INVALID_ARG;
    
    wifi_sta_list_t sta_list;
    esp_err_t ret = esp_wifi_ap_get_sta_list(&sta_list);
    if (ret != ESP_OK) return ret;
    
    uint8_t max = *count;
    *count = 0;
    
    for (int i = 0; i < sta_list.num && i < max; i++) {
        memcpy(list[i].mac, sta_list.sta[i].mac, 6);
        list[i].rssi = sta_list.sta[i].rssi;
        (*count)++;
    }
    
    return ESP_OK;
}

esp_err_t ts_wifi_scan_start(bool block)
{
    // 检查 WiFi 是否已初始化
    if (!s_initialized) {
        TS_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 检查当前模式是否支持扫描（需要 STA 或 APSTA 模式）
    if (s_mode != TS_WIFI_MODE_STA && s_mode != TS_WIFI_MODE_APSTA) {
        TS_LOGW(TAG, "WiFi scan requires STA or APSTA mode (current: %d)", s_mode);
        return ESP_ERR_INVALID_STATE;
    }
    
    wifi_scan_config_t scan_config = {0};
    esp_err_t ret = esp_wifi_scan_start(&scan_config, block);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "esp_wifi_scan_start failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ts_wifi_scan_get_results(ts_wifi_scan_result_t *results, uint16_t *count)
{
    if (!results || !count) return ESP_ERR_INVALID_ARG;
    
    uint16_t ap_num = *count;
    wifi_ap_record_t *ap_list = malloc(ap_num * sizeof(wifi_ap_record_t));
    if (!ap_list) return ESP_ERR_NO_MEM;
    
    esp_err_t ret = esp_wifi_scan_get_ap_records(&ap_num, ap_list);
    if (ret != ESP_OK) {
        free(ap_list);
        return ret;
    }
    
    for (int i = 0; i < ap_num; i++) {
        strncpy(results[i].ssid, (char *)ap_list[i].ssid, 32);
        memcpy(results[i].bssid, ap_list[i].bssid, 6);
        results[i].rssi = ap_list[i].rssi;
        results[i].channel = ap_list[i].primary;
        results[i].auth_mode = ap_list[i].authmode;
    }
    
    *count = ap_num;
    free(ap_list);
    return ESP_OK;
}

bool ts_wifi_is_connected(void)
{
    return s_sta_connected;
}

esp_netif_t *ts_wifi_get_netif(ts_wifi_if_t iface)
{
    switch (iface) {
        case TS_WIFI_IF_STA:
            return s_sta_netif;
        case TS_WIFI_IF_AP:
            return s_ap_netif;
        default:
            return NULL;
    }
}
