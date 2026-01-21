/**
 * @file ts_api_time.c
 * @brief Time Sync Core API
 * 
 * 提供时间同步相关的 API：
 * - time.info: 获取时间同步状态
 * - time.sync: 从浏览器同步时间
 * - time.set_ntp: 设置 NTP 服务器
 * - time.set_timezone: 设置时区
 */

#include "ts_api.h"
#include "ts_time_sync.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "ts_api_time";

/**
 * @brief 获取时间源字符串
 */
static const char *time_source_to_string(ts_time_source_t source)
{
    switch (source) {
        case TS_TIME_SOURCE_NTP:    return "ntp";
        case TS_TIME_SOURCE_HTTP:   return "http";
        case TS_TIME_SOURCE_MANUAL: return "manual";
        default:                    return "none";
    }
}

/**
 * @brief 获取同步状态字符串
 */
static const char *sync_status_to_string(ts_time_sync_status_t status)
{
    switch (status) {
        case TS_TIME_SYNC_NOT_STARTED: return "not_started";
        case TS_TIME_SYNC_IN_PROGRESS: return "in_progress";
        case TS_TIME_SYNC_COMPLETED:   return "synced";
        case TS_TIME_SYNC_FAILED:      return "failed";
        default:                       return "unknown";
    }
}

/**
 * @brief time.info - 获取时间同步信息
 * 
 * @return 当前时间和同步状态
 */
static esp_err_t api_time_info(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    ts_time_sync_info_t info;
    ts_time_sync_get_info(&info);
    
    /* 获取当前时间 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t timestamp_ms = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    
    /* 格式化本地时间 */
    struct tm timeinfo;
    localtime_r(&tv.tv_sec, &timeinfo);
    char datetime_str[32];
    strftime(datetime_str, sizeof(datetime_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    cJSON *data = cJSON_CreateObject();
    
    /* 当前时间 */
    cJSON_AddNumberToObject(data, "timestamp", (double)tv.tv_sec);
    cJSON_AddNumberToObject(data, "timestamp_ms", (double)timestamp_ms);
    cJSON_AddStringToObject(data, "datetime", datetime_str);
    cJSON_AddBoolToObject(data, "synced", info.status == TS_TIME_SYNC_COMPLETED);
    
    /* 同步状态 */
    cJSON_AddStringToObject(data, "sync_status", sync_status_to_string(info.status));
    cJSON_AddStringToObject(data, "source", time_source_to_string(info.source));
    cJSON_AddNumberToObject(data, "last_sync", (double)info.last_sync);
    cJSON_AddNumberToObject(data, "sync_count", info.sync_count);
    
    /* NTP 配置 */
    cJSON_AddStringToObject(data, "ntp_server", info.ntp_server);
    
    /* 时区信息 */
    const char *tz = getenv("TZ");
    cJSON_AddStringToObject(data, "timezone", tz ? tz : "UTC");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief time.sync - 从浏览器同步时间
 * 
 * @param params { "timestamp_ms": 1234567890123 }
 * @return 同步结果
 */
static esp_err_t api_time_sync(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *ts_json = cJSON_GetObjectItem(params, "timestamp_ms");
    if (!ts_json || !cJSON_IsNumber(ts_json)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'timestamp_ms' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    int64_t timestamp_ms = (int64_t)ts_json->valuedouble;
    
    ESP_LOGI(TAG, "Syncing time from browser: %lld ms", (long long)timestamp_ms);
    
    esp_err_t ret = ts_time_sync_set_time(timestamp_ms, TS_TIME_SOURCE_HTTP);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set time");
        return ret;
    }
    
    /* 返回新时间 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm timeinfo;
    localtime_r(&tv.tv_sec, &timeinfo);
    char datetime_str[32];
    strftime(datetime_str, sizeof(datetime_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "synced", true);
    cJSON_AddStringToObject(data, "datetime", datetime_str);
    cJSON_AddStringToObject(data, "source", "http");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief time.set_ntp - 设置 NTP 服务器
 * 
 * @param params { "server": "pool.ntp.org", "index": 0 }
 * @return 设置结果
 */
static esp_err_t api_time_set_ntp(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *server = cJSON_GetObjectItem(params, "server");
    if (!server || !cJSON_IsString(server)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'server' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    int index = 0;
    const cJSON *index_json = cJSON_GetObjectItem(params, "index");
    if (index_json && cJSON_IsNumber(index_json)) {
        index = (int)index_json->valuedouble;
    }
    
    esp_err_t ret = ts_time_sync_set_ntp_server(server->valuestring, index);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set NTP server");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "server", server->valuestring);
    cJSON_AddNumberToObject(data, "index", index);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief time.set_timezone - 设置时区
 * 
 * @param params { "timezone": "CST-8" }
 * @return 设置结果
 */
static esp_err_t api_time_set_timezone(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *tz = cJSON_GetObjectItem(params, "timezone");
    if (!tz || !cJSON_IsString(tz)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'timezone' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ts_time_sync_set_timezone(tz->valuestring);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set timezone");
        return ret;
    }
    
    /* 返回新的本地时间 */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm timeinfo;
    localtime_r(&tv.tv_sec, &timeinfo);
    char datetime_str[32];
    strftime(datetime_str, sizeof(datetime_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "timezone", tz->valuestring);
    cJSON_AddStringToObject(data, "local_time", datetime_str);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief time.force_sync - 强制 NTP 同步
 * 
 * @return 同步结果
 */
static esp_err_t api_time_force_sync(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    esp_err_t ret = ts_time_sync_force_ntp();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to force NTP sync");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "syncing", true);
    cJSON_AddStringToObject(data, "message", "NTP sync initiated");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_api_time_register(void)
{
    static const ts_api_endpoint_t endpoints[] = {
        {
            .name = "time.info",
            .description = "Get time sync info",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_time_info,
            .requires_auth = false,
        },
        {
            .name = "time.sync",
            .description = "Sync time from browser",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_time_sync,
            .requires_auth = false,
        },
        {
            .name = "time.set_ntp",
            .description = "Set NTP server",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_time_set_ntp,
            .requires_auth = true,
        },
        {
            .name = "time.set_timezone",
            .description = "Set timezone",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_time_set_timezone,
            .requires_auth = true,
        },
        {
            .name = "time.force_sync",
            .description = "Force NTP sync",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_time_force_sync,
            .requires_auth = true,
        },
    };
    
    return ts_api_register_multiple(endpoints, sizeof(endpoints) / sizeof(endpoints[0]));
}
