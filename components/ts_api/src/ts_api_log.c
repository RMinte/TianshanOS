/**
 * @file ts_api_log.c
 * @brief Log API Handlers - 日志查询和管理 API
 *
 * 提供 WebUI 和 CLI 访问系统日志的接口
 * 日志缓冲区优先分配到 PSRAM
 *
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-23
 */

#include "ts_api.h"
#include "ts_core.h"  /* TS_MALLOC_PSRAM */
#include "ts_log.h"
#include "cJSON.h"
#include <string.h>

#define TAG "api_log"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 将日志条目转换为 JSON 对象
 */
static cJSON *log_entry_to_json(const ts_log_entry_t *entry)
{
    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) return NULL;

    cJSON_AddNumberToObject(obj, "timestamp", entry->timestamp_ms);
    cJSON_AddNumberToObject(obj, "level", entry->level);
    cJSON_AddStringToObject(obj, "levelName", ts_log_level_to_string(entry->level));
    cJSON_AddStringToObject(obj, "tag", entry->tag);
    cJSON_AddStringToObject(obj, "message", entry->message);
    cJSON_AddStringToObject(obj, "task", entry->task_name);

    return obj;
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief log.list - 获取日志列表
 *
 * 参数:
 *   - offset: 起始偏移（可选，默认 0）
 *   - limit: 返回数量（可选，默认 50，最大 200）
 *   - level: 日志级别过滤（可选，1-5，默认返回所有级别）
 *   - tag: TAG 过滤（可选，子字符串匹配）
 *   - keyword: 关键字搜索（可选）
 */
static esp_err_t api_log_list(const cJSON *params, ts_api_result_t *result)
{
    // 解析参数
    size_t offset = 0;
    size_t limit = 50;
    ts_log_level_t min_level = TS_LOG_ERROR;
    ts_log_level_t max_level = TS_LOG_VERBOSE;
    const char *tag_filter = NULL;
    const char *keyword = NULL;

    if (params) {

        cJSON *j_offset = cJSON_GetObjectItem(params, "offset");
        if (j_offset && cJSON_IsNumber(j_offset)) {
            offset = (size_t)j_offset->valueint;
        }

        cJSON *j_limit = cJSON_GetObjectItem(params, "limit");
        if (j_limit && cJSON_IsNumber(j_limit)) {
            limit = (size_t)j_limit->valueint;
            if (limit > 200) limit = 200;  // 限制最大返回数
        }

        cJSON *j_level = cJSON_GetObjectItem(params, "level");
        if (j_level && cJSON_IsNumber(j_level)) {
            // 如果指定了级别，只返回该级别
            min_level = max_level = (ts_log_level_t)j_level->valueint;
        }

        cJSON *j_min_level = cJSON_GetObjectItem(params, "minLevel");
        if (j_min_level && cJSON_IsNumber(j_min_level)) {
            min_level = (ts_log_level_t)j_min_level->valueint;
        }

        cJSON *j_max_level = cJSON_GetObjectItem(params, "maxLevel");
        if (j_max_level && cJSON_IsNumber(j_max_level)) {
            max_level = (ts_log_level_t)j_max_level->valueint;
        }

        cJSON *j_tag = cJSON_GetObjectItem(params, "tag");
        if (j_tag && cJSON_IsString(j_tag)) {
            tag_filter = j_tag->valuestring;
        }

        cJSON *j_keyword = cJSON_GetObjectItem(params, "keyword");
        if (j_keyword && cJSON_IsString(j_keyword)) {
            keyword = j_keyword->valuestring;
        }
    }

    // 分配临时缓冲区
    ts_log_entry_t *entries = TS_MALLOC_PSRAM(limit * sizeof(ts_log_entry_t));
    if (entries == NULL) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }

    // 搜索日志
    size_t count = ts_log_buffer_search(entries, limit, min_level, max_level,
                                        tag_filter, keyword);

    // 构建响应
    cJSON *data = cJSON_CreateObject();
    cJSON *logs = cJSON_AddArrayToObject(data, "logs");

    // 应用偏移（在搜索结果上）
    size_t start = (offset < count) ? offset : count;
    for (size_t i = start; i < count; i++) {
        cJSON *entry_json = log_entry_to_json(&entries[i]);
        if (entry_json) {
            cJSON_AddItemToArray(logs, entry_json);
        }
    }

    // 添加元信息
    ts_log_stats_t stats;
    ts_log_get_stats(&stats);

    cJSON_AddNumberToObject(data, "total", count);
    cJSON_AddNumberToObject(data, "offset", start);
    cJSON_AddNumberToObject(data, "returned", cJSON_GetArraySize(logs));
    cJSON_AddNumberToObject(data, "bufferCapacity", stats.buffer_capacity);
    cJSON_AddNumberToObject(data, "bufferCount", stats.buffer_count);

    free(entries);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief log.stats - 获取日志统计信息
 */
static esp_err_t api_log_stats(const cJSON *params, ts_api_result_t *result)
{
    (void)params;

    ts_log_stats_t stats;
    esp_err_t ret = ts_log_get_stats(&stats);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to get log stats");
        return ret;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "bufferCapacity", stats.buffer_capacity);
    cJSON_AddNumberToObject(data, "bufferCount", stats.buffer_count);
    cJSON_AddNumberToObject(data, "totalCaptured", stats.total_captured);
    cJSON_AddNumberToObject(data, "dropped", stats.dropped);
    cJSON_AddBoolToObject(data, "espLogCaptureEnabled", stats.esp_log_capture_enabled);
    cJSON_AddNumberToObject(data, "currentLevel", ts_log_get_level());
    cJSON_AddStringToObject(data, "currentLevelName", ts_log_level_to_string(ts_log_get_level()));

    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief log.clear - 清空日志缓冲区
 */
static esp_err_t api_log_clear(const cJSON *params, ts_api_result_t *result)
{
    (void)params;

    ts_log_buffer_clear();
    TS_LOGI(TAG, "Log buffer cleared via API");

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "message", "Log buffer cleared");

    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief log.setLevel - 设置日志级别
 *
 * 参数:
 *   - level: 日志级别（0-5 或字符串 "error"/"warn"/"info"/"debug"/"verbose"）
 *   - tag: 可选，针对特定 TAG 设置级别
 */
static esp_err_t api_log_set_level(const cJSON *params, ts_api_result_t *result)
{
    if (params == NULL) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }

    ts_log_level_t level = TS_LOG_INFO;
    const char *tag = NULL;

    cJSON *j_level = cJSON_GetObjectItem(params, "level");
    if (j_level) {
        if (cJSON_IsNumber(j_level)) {
            level = (ts_log_level_t)j_level->valueint;
        } else if (cJSON_IsString(j_level)) {
            level = ts_log_level_from_string(j_level->valuestring);
        }
    } else {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing level parameter");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *j_tag = cJSON_GetObjectItem(params, "tag");
    if (j_tag && cJSON_IsString(j_tag)) {
        tag = j_tag->valuestring;
    }

    if (tag) {
        ts_log_set_tag_level(tag, level);
        TS_LOGI(TAG, "Log level for tag '%s' set to %s", tag, ts_log_level_to_string(level));
    } else {
        ts_log_set_level(level);
        TS_LOGI(TAG, "Global log level set to %s", ts_log_level_to_string(level));
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "level", ts_log_level_to_string(level));
    if (tag) {
        cJSON_AddStringToObject(data, "tag", tag);
    }

    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief log.capture - 控制 ESP_LOG 捕获
 *
 * 参数:
 *   - enable: true/false
 */
static esp_err_t api_log_capture(const cJSON *params, ts_api_result_t *result)
{
    if (params == NULL) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *j_enable = cJSON_GetObjectItem(params, "enable");
    if (!j_enable || !cJSON_IsBool(j_enable)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing or invalid 'enable' parameter");
        return ESP_ERR_INVALID_ARG;
    }

    bool enable = cJSON_IsTrue(j_enable);
    ts_log_enable_esp_capture(enable);

    TS_LOGI(TAG, "ESP_LOG capture %s", enable ? "enabled" : "disabled");

    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddBoolToObject(data, "captureEnabled", enable);

    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          API Registration                                  */
/*===========================================================================*/

void ts_api_log_register(void)
{
    static const ts_api_endpoint_t endpoints[] = {
        {
            .name = "log.list",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_READ,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_log_list,
            .description = "Get log entries with filtering",
            .requires_auth = true,
        },
        {
            .name = "log.stats",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_READ,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_log_stats,
            .description = "Get log system statistics",
            .requires_auth = true,
        },
        {
            .name = "log.clear",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_log_clear,
            .description = "Clear log buffer",
            .requires_auth = true,
        },
        {
            .name = "log.setLevel",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_log_set_level,
            .description = "Set log level",
            .requires_auth = true,
        },
        {
            .name = "log.capture",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_log_capture,
            .description = "Enable/disable ESP_LOG capture",
            .requires_auth = true,
        },
    };

    for (size_t i = 0; i < sizeof(endpoints) / sizeof(endpoints[0]); i++) {
        ts_api_register(&endpoints[i]);
    }

    TS_LOGI(TAG, "Log API endpoints registered");
}
