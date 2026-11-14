/**
 * @file ts_api_ota.c
 * @brief TianShanOS OTA Core API Implementation
 *
 * Provides unified API endpoints for OTA operations.
 * Used by both CLI and WebUI.
 */

#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "ts_http_fetch.h"
#include "ts_http_url.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "ts_api.h"
#include "ts_ota.h"
#include "ts_security.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "ts_api_ota";
static atomic_flag s_ota_probe_busy = ATOMIC_FLAG_INIT;

// NVS 存储键
#define NVS_NAMESPACE_OTA "ota_config"
#define NVS_KEY_SERVER_URL "server_url"
#define LEGACY_OTA_SERVER_URL "http://a6000nvlink.aoseo.com:15207"

// 用户配置的服务器 URL，空字符串表示使用固件默认地址。
static char s_ota_server_url[256] = {0};

static const char *ota_server_url(void)
{
#ifdef CONFIG_TS_OTA_DEFAULT_URL
    if (!s_ota_server_url[0]) return CONFIG_TS_OTA_DEFAULT_URL;
#endif
    return s_ota_server_url;
}

static bool ota_server_is_legacy(const char *url)
{
    return strcmp(url, LEGACY_OTA_SERVER_URL) == 0 ||
           strcmp(url, LEGACY_OTA_SERVER_URL "/") == 0;
}

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
static esp_err_t save_ota_server_config(const char *url)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE_OTA, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    if (url[0]) {
        ret = nvs_set_str(handle, NVS_KEY_SERVER_URL, url);
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

static void init_ota_server_config(void)
{
    if (load_ota_server_config() == ESP_OK && ota_server_is_legacy(s_ota_server_url)) {
        s_ota_server_url[0] = '\0';
        if (save_ota_server_config("") != ESP_OK) {
            ESP_LOGW(TAG, "Could not clear legacy OTA server URL from NVS");
        }
    }
    ESP_LOGI(TAG, "OTA server URL: %s", ota_server_url());
}

/**
 * @brief API: ota.server.get - 获取 OTA 服务器地址
 */
static esp_err_t api_ota_server_get(const cJSON *params, ts_api_result_t *result)
{
    (void)params;

    const char *url = ota_server_url();
    cJSON *json = cJSON_CreateObject();
    if (!json || !cJSON_AddStringToObject(json, "url", url) ||
        !cJSON_AddBoolToObject(json, "configured", url[0] != '\0')) {
        cJSON_Delete(json);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }


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
    if (ota_server_is_legacy(url)) url = "";

    // 验证 URL 长度
    if (strlen(url) >= sizeof(s_ota_server_url)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "URL 过长");
        return ESP_ERR_INVALID_ARG;
    }

    // 检查是否需要持久化
    cJSON *save_item = cJSON_GetObjectItem(params, "save");
    bool do_save = save_item && cJSON_IsTrue(save_item);

    if (do_save) {
        esp_err_t ret = save_ota_server_config(url);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "保存到 NVS 失败");
            return ret;
        }
        ESP_LOGI(TAG, "OTA server URL saved to NVS");
    }

    strcpy(s_ota_server_url, url);
    ESP_LOGI(TAG, "OTA server configuration updated (configured=%d)", s_ota_server_url[0] != 0);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "url", ota_server_url());
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
    cJSON_AddStringToObject(json, "message", progress.status_msg);

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
static bool ota_partition_json(cJSON *json, const char *name, const ts_ota_partition_info_t *info)
{
    cJSON *node = cJSON_AddObjectToObject(json, name);
    bool complete = node && cJSON_AddStringToObject(node, "label", info->label) &&
        cJSON_AddNumberToObject(node, "address", info->address) &&
        cJSON_AddNumberToObject(node, "size", info->size) &&
        cJSON_AddBoolToObject(node, "is_bootable", info->is_bootable);
    if (complete && info->is_bootable) complete =
        cJSON_AddStringToObject(node, "version", info->version.version) &&
        cJSON_AddStringToObject(node, "project", info->version.project_name);
    return complete;
}


static esp_err_t api_ota_partitions(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    ts_ota_partition_info_t running = {0}, next = {0};
    esp_err_t ret = ts_ota_get_running_partition_info(&running);
    if (ret == ESP_OK) ret = ts_ota_get_next_partition_info(&next);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Partition information unavailable");
        return ret;
    }
    cJSON *json = cJSON_CreateObject();
    bool complete = json && ota_partition_json(json, "running", &running) &&
        ota_partition_json(json, "next", &next) &&
        cJSON_AddBoolToObject(json, "can_rollback", ts_ota_can_rollback());
    if (!complete) {
        cJSON_Delete(json);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    ts_api_result_ok(result, json);
    return ESP_OK;
}

/* Reboot confirmation must survive the RAM session it is confirming. Keep
 * complete partition management data behind READ; only running identity is public. */
static esp_err_t api_ota_partitions_for_session(const cJSON *params, uint32_t session_id,
                                               ts_api_result_t *result)
{
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
    if (ts_security_check_permission(session_id, TS_PERM_READ))
        return api_ota_partitions(params, result);

    ts_ota_partition_info_t running = {0};
    esp_err_t ret = ts_ota_get_running_partition_info(&running);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Partition information unavailable");
        return ret;
    }
    cJSON *json = cJSON_CreateObject();
    cJSON *identity = json ? cJSON_AddObjectToObject(json, "running") : NULL;
    if (!identity || !cJSON_AddStringToObject(identity, "label", running.label) ||
        !cJSON_AddStringToObject(identity, "version", running.version.version) ||
        !cJSON_AddStringToObject(identity, "project", running.version.project_name)) {
        cJSON_Delete(json);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    ts_api_result_ok(result, json);
    return ESP_OK;
#else
    (void)session_id;
    return api_ota_partitions(params, result);
#endif
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

    struct http_parser_url parsed;
    if (strnlen(url_item->valuestring, 2049) > 2048 || !ts_http_url_parse(url_item->valuestring, &parsed)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid HTTP or HTTPS URL");
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
    if (!cJSON_IsNumber(size_item) || !isfinite(size_item->valuedouble) ||
        size_item->valuedouble <= 0 || size_item->valuedouble > UINT32_MAX ||
        floor(size_item->valuedouble) != size_item->valuedouble) {
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
 * Params: url (optional if the OTA server is configured), skip_verify (optional)
 */
static esp_err_t api_ota_www_start(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *url_item = cJSON_GetObjectItem(params, "url");
    const char *prefix = cJSON_IsString(url_item) ? url_item->valuestring : "";
    char url[TS_OTA_WWW_URL_MAX_LEN + 1];
    size_t prefix_len = strlen(prefix);
    const char *suffix = "";
    if (prefix_len == 0) {
        prefix = ota_server_url();
        if (prefix == s_ota_server_url) {
            // Parse and substitute using one copy of the mutable server setting.
            _Static_assert(sizeof(s_ota_server_url) <= sizeof(url), "OTA server URL must fit the local copy");
            memcpy(url, s_ota_server_url, sizeof(s_ota_server_url));
            prefix = url;
        }
        prefix_len = strlen(prefix);
        if (prefix_len == 0) {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 url 参数且未设置 OTA 服务器");
            return ESP_ERR_INVALID_ARG;
        }
        suffix = prefix[prefix_len - 1] == '/' ? "www.bin" : "/www.bin";
        struct http_parser_url parsed;
        if (ts_http_url_parse(prefix, &parsed) && (parsed.field_set & (1U << UF_PATH))) {
            static const char *const firmware_names[] = {
                "/TianshanOS.bin", "/TianShanOS.bin", "/firmware.bin",
            };
            size_t path_len = parsed.field_data[UF_PATH].len;
            size_t path_end = parsed.field_data[UF_PATH].off + path_len;
            for (size_t i = 0; i < sizeof(firmware_names) / sizeof(firmware_names[0]); ++i) {
                size_t name_len = strlen(firmware_names[i]);
                if (path_len >= name_len &&
                    memcmp(prefix + path_end - name_len, firmware_names[i], name_len) == 0) {
                    // Drop the firmware URL's query and fragment for its companion image.
                    prefix_len = path_end - name_len;
                    suffix = "/www.bin";
                    break;
                }
            }
        }
    }
    size_t suffix_len = strlen(suffix);
    if (prefix_len + suffix_len > TS_OTA_WWW_URL_MAX_LEN) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "URL too long");
        return ESP_ERR_INVALID_ARG;
    }
    memmove(url, prefix, prefix_len);
    memcpy(url + prefix_len, suffix, suffix_len + 1);

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
    cJSON_AddStringToObject(json, "message", progress.status_msg);
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
static esp_err_t ota_test_connection_impl(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(params, "url");
    struct http_parser_url parsed;
    if (!cJSON_IsString(item) || !ts_http_url_parse(item->valuestring, &parsed) ||
        parsed.field_data[UF_HOST].len >= 256) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid HTTP or HTTPS URL");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Insufficient device memory");
        return ESP_ERR_NO_MEM;
    }
    char host[256], port[8];
    size_t length = parsed.field_data[UF_HOST].len;
    memcpy(host, item->valuestring + parsed.field_data[UF_HOST].off, length);
    host[length] = 0;
    snprintf(port, sizeof(port), "%u", parsed.field_set & (1U << UF_PORT) ? parsed.port :
        parsed.field_data[UF_SCHEMA].len == 5 ? 443U : 80U);
    int64_t started = esp_timer_get_time();
    ts_http_fetch_config_t config = {.url = item->valuestring, .method = HTTP_METHOD_GET,
        .timeout_ms = 5000, .headers_only = true, .maximum_body = 4096};
    ts_http_fetch_result_t reply;
    esp_err_t ret = ts_http_fetch(&config, &reply);
    bool http_ok = ret == ESP_OK && reply.connected && reply.status >= 100 && reply.status < 600;
    int64_t http_ms = (esp_timer_get_time() - started) / 1000 - reply.dns_time_ms - reply.connect_time_ms;
    bool complete = cJSON_AddStringToObject(data, "url", item->valuestring) &&
        cJSON_AddStringToObject(data, "host", host) && cJSON_AddStringToObject(data, "port", port) &&
        cJSON_AddBoolToObject(data, "dns_ok", reply.dns_ok) &&
        cJSON_AddNumberToObject(data, "dns_time_ms", (double)reply.dns_time_ms) &&
        cJSON_AddStringToObject(data, "resolved_ip", reply.resolved_ip) &&
        cJSON_AddBoolToObject(data, "tcp_ok", reply.connected) &&
        cJSON_AddNumberToObject(data, "tcp_time_ms", (double)reply.connect_time_ms) &&
        cJSON_AddBoolToObject(data, "http_ok", http_ok) &&
        cJSON_AddNumberToObject(data, "http_time_ms", (double)(http_ms > 0 ? http_ms : 0)) &&
        cJSON_AddNumberToObject(data, "http_status", reply.status);
    if (!reply.dns_ok) complete = complete && cJSON_AddStringToObject(data, "dns_error", esp_err_to_name(ret));
    else if (!reply.connected) complete = complete && cJSON_AddStringToObject(data, "tcp_error", esp_err_to_name(ret));
    else if (!http_ok) complete = complete && cJSON_AddStringToObject(data, "http_error", esp_err_to_name(ret));
    free(reply.body);
    if (!complete) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Insufficient device memory");
        return ESP_ERR_NO_MEM;
    }
    ts_api_result_ok(result, data);
    return ESP_OK;
}


static esp_err_t api_ota_test_connection(const cJSON *params, ts_api_result_t *result)
{
    if (atomic_flag_test_and_set(&s_ota_probe_busy)) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "Network diagnostic busy");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ota_test_connection_impl(params, result);
    atomic_flag_clear(&s_ota_probe_busy);
    return ret;
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
            .external_access = TS_API_ACCESS_READ,
            .allowed_methods = TS_API_METHOD_READ,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_status,
            .requires_auth = true,
            .description = "获取 OTA 状态",
        },
        {
            .name = "ota.progress",
            .external_access = TS_API_ACCESS_READ,
            .allowed_methods = TS_API_METHOD_READ,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_progress,
            .requires_auth = true,
            .description = "获取 OTA 进度",
        },
        {
            .name = "ota.version",
            .external_access = TS_API_ACCESS_PUBLIC,
            .allowed_methods = TS_API_METHOD_READ,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_version,
            .requires_auth = false,
            .description = "获取固件版本",
        },
        {
            .name = "ota.partitions",
            .external_access = TS_API_ACCESS_PUBLIC,
            .allowed_methods = TS_API_METHOD_READ,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_partitions,
            .requires_auth = false,
            .session_handler = api_ota_partitions_for_session,
            .description = "获取分区信息",
        },
        {
            .name = "ota.start_url",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_url,
            .requires_auth = true,
            .description = "从 URL 启动 OTA",
        },
        {
            .name = "ota.upgrade_url",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,  // Alias for start_url
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_url,
            .requires_auth = true,
            .description = "从 URL 启动 OTA（别名）",
        },
        {
            .name = "ota.start_file",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_file,
            .requires_auth = true,
            .description = "从 SD 卡启动 OTA",
        },
        {
            .name = "ota.upgrade_file",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,  // Alias for start_file
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_file,
            .requires_auth = true,
            .description = "从 SD 卡启动 OTA（别名）",
        },
        {
            .name = "ota.abort",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_abort,
            .requires_auth = true,
            .description = "中止 OTA",
        },
        {
            .name = "ota.validate",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_validate,
            .requires_auth = true,
            .description = "标记固件有效",
        },
        {
            .name = "ota.rollback",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_rollback,
            .requires_auth = true,
            .description = "回滚到上一版本",
        },
        {
            .name = "ota.upload_begin",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_begin,
            .requires_auth = true,
            .description = "开始固件上传",
        },
        {
            .name = "ota.upload_end",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_end,
            .requires_auth = true,
            .description = "结束固件上传",
        },
        {
            .name = "ota.upload_abort",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_abort,
            .requires_auth = true,
            .description = "中止固件上传",
        },
        {
            .name = "ota.server.get",
            .external_access = TS_API_ACCESS_READ,
            .allowed_methods = TS_API_METHOD_READ,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_server_get,
            .requires_auth = true,
            .description = "获取 OTA 服务器地址",
        },
        {
            .name = "ota.server.set",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_server_set,
            .requires_auth = true,
            .description = "设置 OTA 服务器地址",
        },
        // WWW Partition OTA
        {
            .name = "ota.www.start",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_www_start,
            .requires_auth = true,
            .description = "启动 WWW 分区 OTA（HTTP）",
        },
        {
            .name = "ota.www.start_sdcard",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_www_start_sdcard,
            .requires_auth = true,
            .description = "启动 WWW 分区 OTA（SD卡）",
        },
        {
            .name = "ota.www.progress",
            .external_access = TS_API_ACCESS_READ,
            .allowed_methods = TS_API_METHOD_READ,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_www_progress,
            .requires_auth = true,
            .description = "获取 WWW OTA 进度",
        },
        {
            .name = "ota.www.abort",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_www_abort,
            .requires_auth = true,
            .description = "中止 WWW OTA",
        },
        // Network diagnostics
        {
            .name = "ota.test_connection",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_test_connection,
            .requires_auth = true,
            .description = "测试 OTA 服务器连接",
        },
    };

    size_t num_endpoints = sizeof(endpoints) / sizeof(endpoints[0]);
    
    // 加载保存的配置
    init_ota_server_config();
    
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
