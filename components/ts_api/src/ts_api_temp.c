/**
 * @file ts_api_temp.c
 * @brief Temperature API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-20
 */

#include "ts_api.h"
#include "ts_temp_source.h"
#include "ts_log.h"
#include <string.h>

#define TAG "api_temp"

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief temp.sources - Get all temperature sources info
 */
static esp_err_t api_temp_sources(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    ts_temp_status_t status;
    esp_err_t ret = ts_temp_get_status(&status);
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to get temp status");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "initialized", status.initialized);
    cJSON_AddStringToObject(data, "active_source", ts_temp_source_type_to_str(status.active_source));
    cJSON_AddNumberToObject(data, "current_temp_01c", status.current_temp);
    cJSON_AddNumberToObject(data, "current_temp_c", status.current_temp / 10.0);
    cJSON_AddBoolToObject(data, "manual_mode", status.manual_mode);
    cJSON_AddNumberToObject(data, "provider_count", status.provider_count);
    
    /* Provider list */
    cJSON *providers = cJSON_AddArrayToObject(data, "providers");
    for (uint32_t i = 0; i < status.provider_count; i++) {
        ts_temp_provider_info_t *p = &status.providers[i];
        cJSON *provider = cJSON_CreateObject();
        cJSON_AddStringToObject(provider, "name", p->name ? p->name : "unknown");
        cJSON_AddStringToObject(provider, "type", ts_temp_source_type_to_str(p->type));
        cJSON_AddBoolToObject(provider, "active", p->active);
        cJSON_AddNumberToObject(provider, "last_value_01c", p->last_value);
        cJSON_AddNumberToObject(provider, "last_value_c", p->last_value / 10.0);
        cJSON_AddNumberToObject(provider, "last_update_ms", p->last_update_ms);
        cJSON_AddNumberToObject(provider, "update_count", p->update_count);
        cJSON_AddItemToArray(providers, provider);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief temp.read - Read current effective temperature
 * 
 * Params: { "source": "agx_auto" } for specific source
 */
static esp_err_t api_temp_read(const cJSON *params, ts_api_result_t *result)
{
    ts_temp_data_t data;
    
    const cJSON *source_item = cJSON_GetObjectItem(params, "source");
    
    if (cJSON_IsString(source_item) && source_item->valuestring) {
        /* Read from specific source */
        ts_temp_source_type_t source_type = TS_TEMP_SOURCE_DEFAULT;
        const char *src_str = source_item->valuestring;
        
        if (strcmp(src_str, "default") == 0) {
            source_type = TS_TEMP_SOURCE_DEFAULT;
        } else if (strcmp(src_str, "sensor_local") == 0 || strcmp(src_str, "local") == 0) {
            source_type = TS_TEMP_SOURCE_SENSOR_LOCAL;
        } else if (strcmp(src_str, "agx_auto") == 0 || strcmp(src_str, "agx") == 0) {
            source_type = TS_TEMP_SOURCE_AGX_AUTO;
        } else if (strcmp(src_str, "manual") == 0) {
            source_type = TS_TEMP_SOURCE_MANUAL;
        } else {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid source type");
            return ESP_ERR_INVALID_ARG;
        }
        
        esp_err_t ret = ts_temp_get_by_source(source_type, &data);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Source not found or no data");
            return ret;
        }
    } else {
        /* Read effective temperature */
        int16_t temp = ts_temp_get_effective(&data);
        (void)temp;  // data already filled
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "temperature_01c", data.value);
    cJSON_AddNumberToObject(json, "temperature_c", data.value / 10.0);
    cJSON_AddStringToObject(json, "source", ts_temp_source_type_to_str(data.source));
    cJSON_AddNumberToObject(json, "timestamp_ms", data.timestamp_ms);
    cJSON_AddBoolToObject(json, "valid", data.valid);
    
    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief temp.manual - Set/get manual temperature mode
 * 
 * Params: { "enable": true, "temperature": 45.0 } or { "temperature_c": 45.0 }
 */
static esp_err_t api_temp_manual(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *enable_item = cJSON_GetObjectItem(params, "enable");
    const cJSON *temp_item = cJSON_GetObjectItem(params, "temperature_c");
    const cJSON *temp_alias = cJSON_GetObjectItem(params, "temperature");  /* 别名 */
    const cJSON *temp_01c_item = cJSON_GetObjectItem(params, "temperature_01c");
    
    /* 使用 temperature 作为 temperature_c 的别名 */
    if (!cJSON_IsNumber(temp_item) && cJSON_IsNumber(temp_alias)) {
        temp_item = temp_alias;
    }
    
    esp_err_t ret = ESP_OK;
    
    /* Set manual temperature if provided */
    if (cJSON_IsNumber(temp_item)) {
        int16_t temp_01c = (int16_t)(temp_item->valuedouble * 10.0);
        ret = ts_temp_set_manual(temp_01c);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set manual temperature");
            return ret;
        }
    } else if (cJSON_IsNumber(temp_01c_item)) {
        ret = ts_temp_set_manual((int16_t)temp_01c_item->valueint);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set manual temperature");
            return ret;
        }
    }
    
    /* Enable/disable manual mode if specified */
    if (cJSON_IsBool(enable_item)) {
        ret = ts_temp_set_manual_mode(cJSON_IsTrue(enable_item));
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set manual mode");
            return ret;
        }
    }
    
    /* Return current status */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "manual_mode", ts_temp_is_manual_mode());
    
    ts_temp_data_t temp_data;
    ts_temp_get_effective(&temp_data);
    cJSON_AddNumberToObject(data, "current_temp_01c", temp_data.value);
    cJSON_AddNumberToObject(data, "current_temp_c", temp_data.value / 10.0);
    cJSON_AddStringToObject(data, "active_source", ts_temp_source_type_to_str(temp_data.source));
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief temp.status - Get temperature system status summary
 */
static esp_err_t api_temp_status(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    cJSON *data = cJSON_CreateObject();
    
    /* Get full status for bound_variable */
    ts_temp_status_t status;
    ts_temp_get_status(&status);
    
    /* Basic status */
    cJSON_AddBoolToObject(data, "initialized", ts_temp_source_is_initialized());
    cJSON_AddBoolToObject(data, "manual_mode", ts_temp_is_manual_mode());
    cJSON_AddStringToObject(data, "active_source", ts_temp_source_type_to_str(ts_temp_get_active_source()));
    
    /* Preferred source */
    ts_temp_source_type_t preferred = ts_temp_get_preferred_source();
    cJSON_AddStringToObject(data, "preferred_source", 
                            preferred == TS_TEMP_SOURCE_DEFAULT ? "auto" : 
                            ts_temp_source_type_to_str(preferred));
    
    /* Bound variable */
    if (status.bound_variable[0] != '\0') {
        cJSON_AddStringToObject(data, "bound_variable", status.bound_variable);
    } else {
        cJSON_AddNullToObject(data, "bound_variable");
    }
    
    /* Current effective temperature */
    ts_temp_data_t temp_data;
    int16_t temp = ts_temp_get_effective(&temp_data);
    cJSON_AddNumberToObject(data, "temperature_01c", temp);
    cJSON_AddNumberToObject(data, "temperature_c", temp / 10.0);
    cJSON_AddBoolToObject(data, "valid", temp_data.valid);
    cJSON_AddNumberToObject(data, "timestamp_ms", temp_data.timestamp_ms);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief temp.select - Set/get preferred temperature source
 * 
 * Params: { "source": "agx" }  // or "sensor", "auto", "variable"
 * 
 * 当 source 为 "auto" 时，使用默认优先级自动选择
 * 当 source 为 "variable" 时，使用绑定的变量（需先通过 temp.bind 绑定）
 * 其他值设置为首选温度源（如果该源可用则优先使用）
 */
static esp_err_t api_temp_select(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *source_item = cJSON_GetObjectItem(params, "source");
    
    if (cJSON_IsString(source_item) && source_item->valuestring) {
        const char *src_str = source_item->valuestring;
        ts_temp_source_type_t source_type = TS_TEMP_SOURCE_DEFAULT;
        
        // 解析源类型
        if (strcmp(src_str, "auto") == 0 || strcmp(src_str, "default") == 0) {
            source_type = TS_TEMP_SOURCE_DEFAULT;  // 自动选择
        } else if (strcmp(src_str, "sensor_local") == 0 || strcmp(src_str, "sensor") == 0 || strcmp(src_str, "local") == 0) {
            source_type = TS_TEMP_SOURCE_SENSOR_LOCAL;
        } else if (strcmp(src_str, "agx_auto") == 0 || strcmp(src_str, "agx") == 0) {
            source_type = TS_TEMP_SOURCE_AGX_AUTO;
        } else if (strcmp(src_str, "variable") == 0) {
            source_type = TS_TEMP_SOURCE_VARIABLE;
        } else {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, 
                               "Invalid source. Use: auto, agx, sensor, variable");
            return ESP_ERR_INVALID_ARG;
        }
        
        // 设置首选源
        esp_err_t ret = ts_temp_set_preferred_source(source_type);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set preferred source");
            return ret;
        }
    }
    
    // 返回当前状态
    cJSON *data = cJSON_CreateObject();
    
    ts_temp_source_type_t preferred = ts_temp_get_preferred_source();
    ts_temp_source_type_t active = ts_temp_get_active_source();
    
    cJSON_AddStringToObject(data, "preferred_source", 
                            preferred == TS_TEMP_SOURCE_DEFAULT ? "auto" : 
                            ts_temp_source_type_to_str(preferred));
    cJSON_AddStringToObject(data, "active_source", ts_temp_source_type_to_str(active));
    
    // 获取当前温度
    ts_temp_data_t temp_data;
    ts_temp_get_effective(&temp_data);
    cJSON_AddNumberToObject(data, "temperature_c", temp_data.value / 10.0);
    cJSON_AddBoolToObject(data, "valid", temp_data.valid);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief temp.bind - Bind/unbind temperature to a system variable
 * 
 * Params: { "variable": "agx.cpu_temp" }  // 绑定到变量
 *         { "variable": null }            // 取消绑定
 *         {}                              // 查询当前绑定
 * 
 * 绑定变量后，温度源会自动从该变量读取温度值。
 * 变量必须为浮点类型，单位为摄氏度。
 */
static esp_err_t api_temp_bind(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *var_item = cJSON_GetObjectItem(params, "variable");
    
    /* 处理绑定/解绑请求 */
    if (var_item != NULL) {
        if (cJSON_IsString(var_item) && var_item->valuestring && var_item->valuestring[0] != '\0') {
            /* 绑定到变量 */
            esp_err_t ret = ts_temp_bind_variable(var_item->valuestring);
            if (ret != ESP_OK) {
                if (ret == ESP_ERR_INVALID_SIZE) {
                    ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Variable name too long");
                } else {
                    ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to bind variable");
                }
                return ret;
            }
        } else if (cJSON_IsNull(var_item)) {
            /* 取消绑定 */
            esp_err_t ret = ts_temp_unbind_variable();
            if (ret != ESP_OK) {
                ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to unbind variable");
                return ret;
            }
        } else {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Variable must be a string or null");
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    /* 返回当前状态 */
    cJSON *data = cJSON_CreateObject();
    
    char var_name[TS_TEMP_MAX_VARNAME_LEN];
    esp_err_t ret = ts_temp_get_bound_variable(var_name, sizeof(var_name));
    if (ret == ESP_OK && var_name[0] != '\0') {
        cJSON_AddStringToObject(data, "bound_variable", var_name);
    } else {
        cJSON_AddNullToObject(data, "bound_variable");
    }
    
    /* 添加活动源和当前温度信息 */
    ts_temp_source_type_t active = ts_temp_get_active_source();
    cJSON_AddStringToObject(data, "active_source", ts_temp_source_type_to_str(active));
    
    ts_temp_data_t temp_data;
    ts_temp_get_effective(&temp_data);
    cJSON_AddNumberToObject(data, "temperature_c", temp_data.value / 10.0);
    cJSON_AddBoolToObject(data, "valid", temp_data.valid);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t s_temp_endpoints[] = {
    {
        .name = "temp.sources",
        .description = "Get all temperature sources info",
        .category = TS_API_CAT_DEVICE,  // Use DEVICE category
        .handler = api_temp_sources,
        .requires_auth = false,
    },
    {
        .name = "temp.read",
        .description = "Read current temperature",
        .category = TS_API_CAT_DEVICE,
        .handler = api_temp_read,
        .requires_auth = false,
    },
    {
        .name = "temp.manual",
        .description = "Set/get manual temperature mode",
        .category = TS_API_CAT_DEVICE,
        .handler = api_temp_manual,
        .requires_auth = true,
    },
    {
        .name = "temp.status",
        .description = "Get temperature system status",
        .category = TS_API_CAT_DEVICE,
        .handler = api_temp_status,
        .requires_auth = false,
    },
    {
        .name = "temp.select",
        .description = "Set/get preferred temperature source",
        .category = TS_API_CAT_DEVICE,
        .handler = api_temp_select,
        .requires_auth = true,
    },
    {
        .name = "temp.bind",
        .description = "Bind/unbind temperature to a system variable",
        .category = TS_API_CAT_DEVICE,
        .handler = api_temp_bind,
        .requires_auth = true,
    },
};

esp_err_t ts_api_temp_register(void)
{
    TS_LOGI(TAG, "Registering temperature APIs");
    return ts_api_register_multiple(s_temp_endpoints, 
                                    sizeof(s_temp_endpoints) / sizeof(s_temp_endpoints[0]));
}
