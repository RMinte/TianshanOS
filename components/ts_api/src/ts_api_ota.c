/**
 * @file ts_api_ota.c
 * @brief TianShanOS OTA Core API Implementation
 *
 * Provides unified API endpoints for OTA operations.
 * Used by both CLI and WebUI.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "ts_api.h"
#include "ts_ota.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char *TAG = "ts_api_ota";

// NVS 存储键
#define NVS_NAMESPACE_OTA "ota_config"
#define NVS_KEY_SERVER_URL "server_url"

// 服务器 URL 缓存
static char s_ota_server_url[256] = {0};

// ============================================================================
//                           OTA Server Config
// ============================================================================

/**
 * @brief 从 NVS 加载 OTA 服务器配置
 */
static esp_err_t load_ota_server_config(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE_OTA, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // 没有保存的配置，使用默认值
        s_ota_server_url[0] = '\0';
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t len = sizeof(s_ota_server_url);
    ret = nvs_get_str(handle, NVS_KEY_SERVER_URL, s_ota_server_url, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_ota_server_url[0] = '\0';
        ret = ESP_OK;
    }

    nvs_close(handle);
    return ret;
}

/**
 * @brief 保存 OTA 服务器配置到 NVS
 */
static esp_err_t save_ota_server_config(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE_OTA, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    if (s_ota_server_url[0]) {
        ret = nvs_set_str(handle, NVS_KEY_SERVER_URL, s_ota_server_url);
    } else {
        ret = nvs_erase_key(handle, NVS_KEY_SERVER_URL);
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ret = ESP_OK;
        }
    }

    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

/**
 * @brief API: ota.server.get - 获取 OTA 服务器地址
 */
static esp_err_t api_ota_server_get(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(json, "url", s_ota_server_url);
    
    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief API: ota.server.set - 设置 OTA 服务器地址
 * Params: url (required), save (optional - 是否持久化)
 */
static esp_err_t api_ota_server_set(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *url_item = cJSON_GetObjectItem(params, "url");
    if (!url_item) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 url 参数");
        return ESP_ERR_INVALID_ARG;
    }

    // URL 可以是空字符串（表示清除）
    const char *url = cJSON_IsString(url_item) ? url_item->valuestring : "";
    
    // 验证 URL 长度
    if (strlen(url) >= sizeof(s_ota_server_url)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "URL 过长");
        return ESP_ERR_INVALID_ARG;
    }

    // 更新缓存
    strncpy(s_ota_server_url, url, sizeof(s_ota_server_url) - 1);
    s_ota_server_url[sizeof(s_ota_server_url) - 1] = '\0';

    ESP_LOGI(TAG, "OTA server URL set to: %s", s_ota_server_url[0] ? s_ota_server_url : "(empty)");

    // 检查是否需要持久化
    cJSON *save_item = cJSON_GetObjectItem(params, "save");
    bool do_save = save_item && cJSON_IsTrue(save_item);

    if (do_save) {
        esp_err_t ret = save_ota_server_config();
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "保存到 NVS 失败");
            return ret;
        }
        ESP_LOGI(TAG, "OTA server URL saved to NVS");
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "url", s_ota_server_url);
    cJSON_AddBoolToObject(json, "saved", do_save);
    
    ts_api_result_ok(result, json);
    return ESP_OK;
}

// ============================================================================
//                           API Handlers
// ============================================================================

/**
 * @brief Get OTA status
 * API: ota.status
 */
static esp_err_t api_ota_status(const cJSON *params, ts_api_result_t *result)
{
    ts_ota_status_t status;

    esp_err_t ret = ts_ota_get_status(&status);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "获取状态失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }
    
    // State
    const char *state_str;
    switch (status.state) {
        case TS_OTA_STATE_IDLE: state_str = "idle"; break;
        case TS_OTA_STATE_CHECKING: state_str = "checking"; break;
        case TS_OTA_STATE_DOWNLOADING: state_str = "downloading"; break;
        case TS_OTA_STATE_VERIFYING: state_str = "verifying"; break;
        case TS_OTA_STATE_WRITING: state_str = "writing"; break;
        case TS_OTA_STATE_PENDING_REBOOT: state_str = "pending_reboot"; break;
        case TS_OTA_STATE_ERROR: state_str = "error"; break;
        default: state_str = "unknown"; break;
    }
    cJSON_AddStringToObject(json, "state", state_str);

    // Running partition info
    cJSON *running = cJSON_AddObjectToObject(json, "running");
    cJSON_AddStringToObject(running, "label", status.running.label);
    cJSON_AddNumberToObject(running, "address", status.running.address);
    cJSON_AddNumberToObject(running, "size", status.running.size);
    cJSON_AddStringToObject(running, "version", status.running.version.version);
    cJSON_AddStringToObject(running, "project", status.running.version.project_name);
    cJSON_AddStringToObject(running, "compile_date", status.running.version.compile_date);
    cJSON_AddStringToObject(running, "compile_time", status.running.version.compile_time);
    cJSON_AddStringToObject(running, "idf_version", status.running.version.idf_version);

    // Next partition info
    cJSON *next = cJSON_AddObjectToObject(json, "next");
    cJSON_AddStringToObject(next, "label", status.next.label);
    cJSON_AddNumberToObject(next, "address", status.next.address);
    cJSON_AddNumberToObject(next, "size", status.next.size);
    cJSON_AddBoolToObject(next, "bootable", status.next.is_bootable);
    if (status.next.is_bootable) {
        cJSON_AddStringToObject(next, "version", status.next.version.version);
        cJSON_AddStringToObject(next, "project", status.next.version.project_name);
    }

    // Rollback info
    cJSON_AddBoolToObject(json, "pending_verify", status.pending_verify);
    if (status.pending_verify) {
        cJSON_AddNumberToObject(json, "rollback_timeout", status.rollback_timeout);
    }

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Get OTA progress
 * API: ota.progress
 */
static esp_err_t api_ota_progress(const cJSON *params, ts_api_result_t *result)
{
    ts_ota_progress_t progress;

    esp_err_t ret = ts_ota_get_progress(&progress);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "获取进度失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    // State
    const char *state_str;
    switch (progress.state) {
        case TS_OTA_STATE_IDLE: state_str = "idle"; break;
        case TS_OTA_STATE_CHECKING: state_str = "checking"; break;
        case TS_OTA_STATE_DOWNLOADING: state_str = "downloading"; break;
        case TS_OTA_STATE_VERIFYING: state_str = "verifying"; break;
        case TS_OTA_STATE_WRITING: state_str = "writing"; break;
        case TS_OTA_STATE_PENDING_REBOOT: state_str = "pending_reboot"; break;
        case TS_OTA_STATE_ERROR: state_str = "error"; break;
        default: state_str = "unknown"; break;
    }
    cJSON_AddStringToObject(json, "state", state_str);
    cJSON_AddNumberToObject(json, "percent", progress.progress_percent);
    cJSON_AddNumberToObject(json, "total_size", progress.total_size);
    cJSON_AddNumberToObject(json, "received_size", progress.received_size);
    cJSON_AddStringToObject(json, "message", progress.status_msg ? progress.status_msg : "");

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Get firmware version
 * API: ota.version
 */
static esp_err_t api_ota_version(const cJSON *params, ts_api_result_t *result)
{
    ts_ota_status_t status;
    
    esp_err_t ret = ts_ota_get_status(&status);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "获取版本失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    const ts_ota_version_info_t *version = &status.running.version;
    cJSON_AddStringToObject(json, "version", version->version);
    cJSON_AddStringToObject(json, "project", version->project_name);
    cJSON_AddStringToObject(json, "compile_date", version->compile_date);
    cJSON_AddStringToObject(json, "compile_time", version->compile_time);
    cJSON_AddStringToObject(json, "idf_version", version->idf_version);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Get partition information
 * API: ota.partitions
 */
static esp_err_t api_ota_partitions(const cJSON *params, ts_api_result_t *result)
{
    ts_ota_partition_info_t running, next;
    
    esp_err_t ret = ts_ota_get_running_partition_info(&running);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "获取分区信息失败");
        return ret;
    }
    
    ts_ota_get_next_partition_info(&next);

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    // Running partition
    cJSON *running_json = cJSON_AddObjectToObject(json, "running");
    cJSON_AddStringToObject(running_json, "label", running.label);
    cJSON_AddNumberToObject(running_json, "address", running.address);
    cJSON_AddNumberToObject(running_json, "size", running.size);
    cJSON_AddBoolToObject(running_json, "is_bootable", running.is_bootable);
    if (running.is_bootable) {
        cJSON_AddStringToObject(running_json, "version", running.version.version);
        cJSON_AddStringToObject(running_json, "project", running.version.project_name);
    }

    // Next partition
    cJSON *next_json = cJSON_AddObjectToObject(json, "next");
    cJSON_AddStringToObject(next_json, "label", next.label);
    cJSON_AddNumberToObject(next_json, "address", next.address);
    cJSON_AddNumberToObject(next_json, "size", next.size);
    cJSON_AddBoolToObject(next_json, "is_bootable", next.is_bootable);
    if (next.is_bootable) {
        cJSON_AddStringToObject(next_json, "version", next.version.version);
        cJSON_AddStringToObject(next_json, "project", next.version.project_name);
    }
    
    // Check if rollback is possible
    cJSON_AddBoolToObject(json, "can_rollback", ts_ota_can_rollback());

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Start OTA from URL
 * API: ota.start_url
 * Params: url (required), auto_reboot, allow_downgrade, skip_verify
 */
static esp_err_t api_ota_start_url(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *url_item = cJSON_GetObjectItem(params, "url");
    if (!url_item || !cJSON_IsString(url_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 url 参数");
        return ESP_ERR_INVALID_ARG;
    }

    ts_ota_config_t config = {
        .source = TS_OTA_SOURCE_HTTPS,
        .url = url_item->valuestring,
        .auto_reboot = true,
        .allow_downgrade = false,
        .skip_cert_verify = false,
    };

    // Optional parameters
    cJSON *auto_reboot = cJSON_GetObjectItem(params, "auto_reboot");
    if (auto_reboot && cJSON_IsBool(auto_reboot)) {
        config.auto_reboot = cJSON_IsTrue(auto_reboot);
    }
    
    // Support no_reboot parameter (inverse of auto_reboot)
    cJSON *no_reboot = cJSON_GetObjectItem(params, "no_reboot");
    if (no_reboot && cJSON_IsBool(no_reboot)) {
        config.auto_reboot = !cJSON_IsTrue(no_reboot);
    }

    cJSON *allow_downgrade = cJSON_GetObjectItem(params, "allow_downgrade");
    if (allow_downgrade && cJSON_IsBool(allow_downgrade)) {
        config.allow_downgrade = cJSON_IsTrue(allow_downgrade);
    }

    cJSON *skip_verify = cJSON_GetObjectItem(params, "skip_verify");
    if (skip_verify && cJSON_IsBool(skip_verify)) {
        config.skip_cert_verify = cJSON_IsTrue(skip_verify);
    }

    esp_err_t ret = ts_ota_start(&config);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "启动 OTA 失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "started", true);
    cJSON_AddStringToObject(json, "url", config.url);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Start OTA from SD card file
 * API: ota.start_file
 * Params: file (required), auto_reboot, no_reboot, allow_downgrade, skip_verify
 */
static esp_err_t api_ota_start_file(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    // Support both 'file' and 'path' parameter names
    cJSON *path_item = cJSON_GetObjectItem(params, "file");
    if (!path_item) {
        path_item = cJSON_GetObjectItem(params, "path");
    }
    if (!path_item || !cJSON_IsString(path_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 file 参数");
        return ESP_ERR_INVALID_ARG;
    }

    ts_ota_config_t config = {
        .source = TS_OTA_SOURCE_SDCARD,
        .url = path_item->valuestring,  // url 字段用于存储文件路径
        .auto_reboot = true,
        .allow_downgrade = false,
        .skip_cert_verify = false,
    };

    // Optional parameters
    cJSON *auto_reboot = cJSON_GetObjectItem(params, "auto_reboot");
    if (auto_reboot && cJSON_IsBool(auto_reboot)) {
        config.auto_reboot = cJSON_IsTrue(auto_reboot);
    }
    
    // Support no_reboot parameter (inverse of auto_reboot)
    cJSON *no_reboot = cJSON_GetObjectItem(params, "no_reboot");
    if (no_reboot && cJSON_IsBool(no_reboot)) {
        config.auto_reboot = !cJSON_IsTrue(no_reboot);
    }

    cJSON *allow_downgrade = cJSON_GetObjectItem(params, "allow_downgrade");
    if (allow_downgrade && cJSON_IsBool(allow_downgrade)) {
        config.allow_downgrade = cJSON_IsTrue(allow_downgrade);
    }
    
    cJSON *skip_verify = cJSON_GetObjectItem(params, "skip_verify");
    if (skip_verify && cJSON_IsBool(skip_verify)) {
        config.skip_cert_verify = cJSON_IsTrue(skip_verify);
    }

    esp_err_t ret = ts_ota_start(&config);
    if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "文件不存在");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "启动 OTA 失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "started", true);
    cJSON_AddStringToObject(json, "path", config.url);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Abort OTA
 * API: ota.abort
 */
static esp_err_t api_ota_abort(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_ota_abort();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "中止失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "aborted", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Mark firmware as valid (cancel rollback)
 * API: ota.validate
 */
static esp_err_t api_ota_validate(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_ota_mark_valid();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "验证失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "validated", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Rollback to previous firmware
 * API: ota.rollback
 */
static esp_err_t api_ota_rollback(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_ota_rollback();
    if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "无可用回滚分区");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "回滚失败");
        return ret;
    }

    // Should not reach here (device should reboot)
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "rolling_back", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Begin firmware upload
 * API: ota.upload_begin
 * Params: size (required)
 */
static esp_err_t api_ota_upload_begin(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *size_item = cJSON_GetObjectItem(params, "size");
    if (!size_item || !cJSON_IsNumber(size_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 size 参数");
        return ESP_ERR_INVALID_ARG;
    }

    size_t size = (size_t)size_item->valuedouble;

    esp_err_t ret = ts_ota_upload_begin(size);
    if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "OTA 正在进行中");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "启动上传失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ready", true);
    cJSON_AddNumberToObject(json, "expected_size", size);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief End firmware upload
 * API: ota.upload_end
 */
static esp_err_t api_ota_upload_end(const cJSON *params, ts_api_result_t *result)
{
    bool auto_reboot = true;

    if (params) {
        cJSON *reboot_item = cJSON_GetObjectItem(params, "auto_reboot");
        if (reboot_item && cJSON_IsBool(reboot_item)) {
            auto_reboot = cJSON_IsTrue(reboot_item);
        }
    }

    esp_err_t ret = ts_ota_upload_end(auto_reboot);
    if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "无活动上传");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "完成上传失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "completed", true);
    cJSON_AddBoolToObject(json, "reboot_pending", auto_reboot);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Abort firmware upload
 * API: ota.upload_abort
 */
static esp_err_t api_ota_upload_abort(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    esp_err_t ret = ts_ota_upload_abort();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "中止上传失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "aborted", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

// ============================================================================
//                           WWW Partition OTA APIs
// ============================================================================

/**
 * @brief Start WWW partition OTA from URL
 * API: ota.www.start
 * Params: url (required), skip_verify (optional)
 */
static esp_err_t api_ota_www_start(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    // Get URL - either from params or use server URL with www.bin suffix
    cJSON *url_item = cJSON_GetObjectItem(params, "url");
    char url[300] = {0};
    
    if (url_item && cJSON_IsString(url_item) && url_item->valuestring[0]) {
        strncpy(url, url_item->valuestring, sizeof(url) - 1);
    } else if (s_ota_server_url[0]) {
        // Use server URL and replace firmware.bin with www.bin
        strncpy(url, s_ota_server_url, sizeof(url) - 1);
        // Try to replace common firmware names
        char *bin_pos = strstr(url, "firmware.bin");
        if (bin_pos) {
            strcpy(bin_pos, "www.bin");
        } else {
            bin_pos = strstr(url, "TianShanOS.bin");
            if (bin_pos) {
                strcpy(bin_pos, "www.bin");
            } else {
                // URL doesn't contain known .bin name
                size_t len = strlen(url);
                if (len > 0 && url[len-1] == '/') {
                    // URL ends with /, append www.bin
                    strncat(url, "www.bin", sizeof(url) - len - 1);
                } else {
                    // URL doesn't end with /, append /www.bin
                    strncat(url, "/www.bin", sizeof(url) - len - 1);
                }
            }
        }
    } else {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 url 参数且未设置 OTA 服务器");
        return ESP_ERR_INVALID_ARG;
    }

    // Get skip_verify option
    bool skip_verify = false;
    cJSON *skip_item = cJSON_GetObjectItem(params, "skip_verify");
    if (skip_item && cJSON_IsBool(skip_item)) {
        skip_verify = cJSON_IsTrue(skip_item);
    }

    ESP_LOGI(TAG, "Starting WWW OTA from: %s", url);

    esp_err_t ret = ts_ota_www_start(url, skip_verify, NULL, NULL);
    if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "WWW OTA 已在运行");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "启动 WWW OTA 失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "started");
    cJSON_AddStringToObject(json, "url", url);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Get WWW OTA progress
 * API: ota.www.progress
 */
static esp_err_t api_ota_www_progress(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    ts_ota_progress_t progress = {0};
    esp_err_t ret = ts_ota_www_get_progress(&progress);
    if (ret != ESP_OK) {
        // Return idle state if not initialized
        progress.state = TS_OTA_STATE_IDLE;
    }

    cJSON *json = cJSON_CreateObject();
    
    const char *state_str = "unknown";
    switch (progress.state) {
        case TS_OTA_STATE_IDLE: state_str = "idle"; break;
        case TS_OTA_STATE_DOWNLOADING: state_str = "downloading"; break;
        case TS_OTA_STATE_WRITING: state_str = "writing"; break;
        case TS_OTA_STATE_PENDING_REBOOT: state_str = "completed"; break;
        case TS_OTA_STATE_ERROR: state_str = "error"; break;
        default: break;
    }
    
    cJSON_AddStringToObject(json, "state", state_str);
    cJSON_AddNumberToObject(json, "received", progress.received_size);
    cJSON_AddNumberToObject(json, "total", progress.total_size);
    cJSON_AddNumberToObject(json, "percent", progress.progress_percent);
    cJSON_AddStringToObject(json, "message", progress.status_msg ? progress.status_msg : "");
    cJSON_AddBoolToObject(json, "running", ts_ota_www_is_running());

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Abort WWW OTA
 * API: ota.www.abort
 */
static esp_err_t api_ota_www_abort(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    esp_err_t ret = ts_ota_www_abort();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "中止 WWW OTA 失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "aborted", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Start WWW partition OTA from SD card file
 * API: ota.www.start_sdcard
 * Params: file (required)
 */
static esp_err_t api_ota_www_start_sdcard(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    // Get file path
    cJSON *file_item = cJSON_GetObjectItem(params, "file");
    if (!file_item || !cJSON_IsString(file_item) || !file_item->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 file 参数");
        return ESP_ERR_INVALID_ARG;
    }

    const char *filepath = file_item->valuestring;
    ESP_LOGI(TAG, "Starting WWW OTA from SD card: %s", filepath);

    esp_err_t ret = ts_ota_www_start_sdcard(filepath, NULL, NULL);
    if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "WWW OTA 已在运行");
        return ret;
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "文件不存在");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "启动 WWW OTA 失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "started");
    cJSON_AddStringToObject(json, "file", filepath);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief API: ota.test_connection - 测试 OTA 服务器连接
 * Params: url (required) - 要测试的 URL
 * 
 * 此 API 从 ESP32 设备测试到 OTA 服务器的连接
 */
static esp_err_t api_ota_test_connection(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *url_item = cJSON_GetObjectItem(params, "url");
    if (!url_item || !cJSON_IsString(url_item) || !url_item->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 url 参数");
        return ESP_ERR_INVALID_ARG;
    }

    const char *url = url_item->valuestring;
    ESP_LOGI(TAG, "Testing connection to: %s", url);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "url", url);

    // 解析 URL 获取主机名
    const char *host_start = strstr(url, "://");
    if (!host_start) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "URL 格式无效");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }
    host_start += 3;

    char host[128] = {0};
    char port_str[8] = "80";
    const char *port_pos = strchr(host_start, ':');
    const char *path_pos = strchr(host_start, '/');
    
    if (port_pos && (!path_pos || port_pos < path_pos)) {
        // 有端口号
        size_t host_len = port_pos - host_start;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, host_start, host_len);
        
        const char *port_end = path_pos ? path_pos : port_pos + strlen(port_pos);
        size_t port_len = port_end - (port_pos + 1);
        if (port_len >= sizeof(port_str)) port_len = sizeof(port_str) - 1;
        strncpy(port_str, port_pos + 1, port_len);
    } else if (path_pos) {
        size_t host_len = path_pos - host_start;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, host_start, host_len);
    } else {
        strncpy(host, host_start, sizeof(host) - 1);
    }

    cJSON_AddStringToObject(json, "host", host);
    cJSON_AddStringToObject(json, "port", port_str);

    // DNS 解析测试
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    
    int64_t dns_start = esp_timer_get_time();
    int dns_err = getaddrinfo(host, port_str, &hints, &res);
    int64_t dns_time = (esp_timer_get_time() - dns_start) / 1000;  // ms
    
    cJSON_AddNumberToObject(json, "dns_time_ms", dns_time);

    if (dns_err != 0 || res == NULL) {
        cJSON_AddBoolToObject(json, "dns_ok", false);
        cJSON_AddStringToObject(json, "dns_error", "DNS 解析失败");
        ESP_LOGE(TAG, "DNS lookup failed for %s: %d", host, dns_err);
        ts_api_result_ok(result, json);
        return ESP_OK;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    char ip_str[16];
    inet_ntoa_r(addr->sin_addr, ip_str, sizeof(ip_str));
    cJSON_AddBoolToObject(json, "dns_ok", true);
    cJSON_AddStringToObject(json, "resolved_ip", ip_str);
    ESP_LOGI(TAG, "DNS resolved %s -> %s", host, ip_str);

    // TCP 连接测试
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        freeaddrinfo(res);
        cJSON_AddBoolToObject(json, "tcp_ok", false);
        cJSON_AddStringToObject(json, "tcp_error", "创建 socket 失败");
        ts_api_result_ok(result, json);
        return ESP_OK;
    }

    // 设置超时
    struct timeval tv = {
        .tv_sec = 5,
        .tv_usec = 0,
    };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int64_t tcp_start = esp_timer_get_time();
    int conn_err = connect(sock, res->ai_addr, res->ai_addrlen);
    int64_t tcp_time = (esp_timer_get_time() - tcp_start) / 1000;  // ms
    
    freeaddrinfo(res);
    cJSON_AddNumberToObject(json, "tcp_time_ms", tcp_time);

    if (conn_err != 0) {
        close(sock);
        cJSON_AddBoolToObject(json, "tcp_ok", false);
        cJSON_AddStringToObject(json, "tcp_error", "TCP 连接失败");
        ESP_LOGE(TAG, "TCP connect failed to %s:%s", host, port_str);
        ts_api_result_ok(result, json);
        return ESP_OK;
    }

    cJSON_AddBoolToObject(json, "tcp_ok", true);
    ESP_LOGI(TAG, "TCP connection successful to %s:%s in %lld ms", host, port_str, tcp_time);

    // HTTP 请求测试
    char request[512];
    snprintf(request, sizeof(request),
             "GET / HTTP/1.1\r\n"
             "Host: %s:%s\r\n"
             "Connection: close\r\n"
             "\r\n",
             host, port_str);

    int64_t http_start = esp_timer_get_time();
    int sent = send(sock, request, strlen(request), 0);
    if (sent <= 0) {
        close(sock);
        cJSON_AddBoolToObject(json, "http_ok", false);
        cJSON_AddStringToObject(json, "http_error", "HTTP 发送失败");
        ts_api_result_ok(result, json);
        return ESP_OK;
    }

    char response[1024];
    int received = recv(sock, response, sizeof(response) - 1, 0);
    int64_t http_time = (esp_timer_get_time() - http_start) / 1000;  // ms
    close(sock);

    cJSON_AddNumberToObject(json, "http_time_ms", http_time);

    if (received <= 0) {
        cJSON_AddBoolToObject(json, "http_ok", false);
        cJSON_AddStringToObject(json, "http_error", "HTTP 无响应");
        ts_api_result_ok(result, json);
        return ESP_OK;
    }

    response[received] = '\0';
    
    // 解析 HTTP 状态码
    int http_status = 0;
    if (strncmp(response, "HTTP/", 5) == 0) {
        sscanf(response + 9, "%d", &http_status);
    }

    cJSON_AddBoolToObject(json, "http_ok", true);
    cJSON_AddNumberToObject(json, "http_status", http_status);
    ESP_LOGI(TAG, "HTTP response: status=%d, time=%lld ms", http_status, http_time);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

// ============================================================================
//                           API Registration
// ============================================================================

esp_err_t ts_api_ota_register(void)
{
    ESP_LOGI(TAG, "Registering OTA APIs");

    // 确保 OTA 模块已初始化
    esp_err_t init_ret = ts_ota_init();
    if (init_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize OTA module: %s", esp_err_to_name(init_ret));
        return init_ret;
    }

    ts_api_endpoint_t endpoints[] = {
        {
            .name = "ota.status",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_status,
            .requires_auth = false,
            .description = "获取 OTA 状态",
        },
        {
            .name = "ota.progress",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_progress,
            .requires_auth = false,
            .description = "获取 OTA 进度",
        },
        {
            .name = "ota.version",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_version,
            .requires_auth = false,
            .description = "获取固件版本",
        },
        {
            .name = "ota.partitions",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_partitions,
            .requires_auth = false,
            .description = "获取分区信息",
        },
        {
            .name = "ota.start_url",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_url,
            .requires_auth = true,
            .description = "从 URL 启动 OTA",
        },
        {
            .name = "ota.upgrade_url",  // Alias for start_url
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_url,
            .requires_auth = true,
            .description = "从 URL 启动 OTA（别名）",
        },
        {
            .name = "ota.start_file",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_file,
            .requires_auth = true,
            .description = "从 SD 卡启动 OTA",
        },
        {
            .name = "ota.upgrade_file",  // Alias for start_file
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_file,
            .requires_auth = true,
            .description = "从 SD 卡启动 OTA（别名）",
        },
        {
            .name = "ota.abort",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_abort,
            .requires_auth = true,
            .description = "中止 OTA",
        },
        {
            .name = "ota.validate",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_validate,
            .requires_auth = true,
            .description = "标记固件有效",
        },
        {
            .name = "ota.rollback",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_rollback,
            .requires_auth = true,
            .description = "回滚到上一版本",
        },
        {
            .name = "ota.upload_begin",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_begin,
            .requires_auth = true,
            .description = "开始固件上传",
        },
        {
            .name = "ota.upload_end",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_end,
            .requires_auth = true,
            .description = "结束固件上传",
        },
        {
            .name = "ota.upload_abort",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_abort,
            .requires_auth = true,
            .description = "中止固件上传",
        },
        {
            .name = "ota.server.get",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_server_get,
            .requires_auth = false,
            .description = "获取 OTA 服务器地址",
        },
        {
            .name = "ota.server.set",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_server_set,
            .requires_auth = true,
            .description = "设置 OTA 服务器地址",
        },
        // WWW Partition OTA
        {
            .name = "ota.www.start",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_www_start,
            .requires_auth = true,
            .description = "启动 WWW 分区 OTA（HTTP）",
        },
        {
            .name = "ota.www.start_sdcard",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_www_start_sdcard,
            .requires_auth = true,
            .description = "启动 WWW 分区 OTA（SD卡）",
        },
        {
            .name = "ota.www.progress",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_www_progress,
            .requires_auth = false,
            .description = "获取 WWW OTA 进度",
        },
        {
            .name = "ota.www.abort",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_www_abort,
            .requires_auth = true,
            .description = "中止 WWW OTA",
        },
        // Network diagnostics
        {
            .name = "ota.test_connection",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_test_connection,
            .requires_auth = false,
            .description = "测试 OTA 服务器连接",
        },
    };

    size_t num_endpoints = sizeof(endpoints) / sizeof(endpoints[0]);
    
    // 加载保存的配置
    load_ota_server_config();
    ESP_LOGI(TAG, "Loaded OTA server URL: %s", s_ota_server_url[0] ? s_ota_server_url : "(not set)");
    
    for (size_t i = 0; i < num_endpoints; i++) {
        esp_err_t ret = ts_api_register(&endpoints[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s: %s", endpoints[i].name, esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGI(TAG, "Registered %zu OTA API endpoints", num_endpoints);
    return ESP_OK;
}
