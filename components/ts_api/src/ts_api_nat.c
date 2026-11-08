/**
 * @file ts_api_nat.c
 * @brief NAT Gateway API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_api.h"
#include "ts_nat.h"
#include "ts_log.h"
#include <string.h>

#define TAG "api_nat"

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief nat.status - Get NAT gateway status
 */
static esp_err_t api_nat_status(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    ts_nat_status_t status;
    ts_nat_get_status(&status);
    
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddBoolToObject(data, "enabled", status.state == TS_NAT_STATE_ENABLED);
    cJSON_AddStringToObject(data, "state", 
        status.state == TS_NAT_STATE_ENABLED ? "ENABLED" :
        status.state == TS_NAT_STATE_ERROR ? "ERROR" : "DISABLED");
    cJSON_AddBoolToObject(data, "wifi_connected", status.wifi_connected);
    cJSON_AddBoolToObject(data, "eth_up", status.eth_up);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief nat.enable - Enable NAT gateway
 */
static esp_err_t api_nat_enable(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    esp_err_t ret = ts_nat_enable();
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", ret == ESP_OK);
    
    if (ret != ESP_OK) {
        cJSON_AddStringToObject(data, "error", esp_err_to_name(ret));
        if (ret == ESP_ERR_INVALID_STATE) {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "WiFi STA not connected");
        } else {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to enable NAT");
        }
        cJSON_Delete(data);
        return ret;
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief nat.disable - Disable NAT gateway
 */
static esp_err_t api_nat_disable(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    esp_err_t ret = ts_nat_disable();
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", ret == ESP_OK);
    
    if (ret != ESP_OK) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to disable NAT");
        return ret;
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief nat.save - Save NAT configuration
 */
static esp_err_t api_nat_save(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    esp_err_t ret = ts_nat_save_config();
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", ret == ESP_OK);
    
    if (ret != ESP_OK) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to save config");
        return ret;
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_api_nat_register(void)
{
    static const ts_api_endpoint_t endpoints[] = {
        {
            .name = "nat.status",
            .external_access = TS_API_ACCESS_READ,
            .allowed_methods = TS_API_METHOD_READ,
            .description = "Get NAT gateway status",
            .category = TS_API_CAT_NETWORK,
            .handler = api_nat_status,
            .requires_auth = true,
        },
        {
            .name = "nat.enable",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .description = "Enable NAT gateway",
            .category = TS_API_CAT_NETWORK,
            .handler = api_nat_enable,
            .requires_auth = true,
        },
        {
            .name = "nat.disable",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .description = "Disable NAT gateway",
            .category = TS_API_CAT_NETWORK,
            .handler = api_nat_disable,
            .requires_auth = true,
        },
        {
            .name = "nat.save",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .description = "Save NAT configuration",
            .category = TS_API_CAT_NETWORK,
            .handler = api_nat_save,
            .requires_auth = true,
        },
    };
    
    return ts_api_register_multiple(endpoints, sizeof(endpoints) / sizeof(endpoints[0]));
}
