/**
 * @file ts_webui_api.c
 * @brief WebUI REST API Implementation
 */

#include "ts_webui.h"
#include "ts_http_server.h"
#include "ts_api.h"
#include "ts_security.h"
#include "ts_storage.h"
#include "ts_log.h"
#include "ts_ota.h"
#include "ts_config_pack.h"
#include "ts_ws_subscriptions.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include <stdatomic.h>
#include "esp_timer.h"
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"

/* PSRAM 优先分配宏 */
#define TS_API_MALLOC(size) ({ void *p = heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); p ? p : malloc(size); })

#define TAG "webui_api"

#define OTA_UPLOAD_REBOOT_DELAY_MS 2000

#ifdef CONFIG_TS_WEBUI_API_PREFIX
#define API_PREFIX CONFIG_TS_WEBUI_API_PREFIX
#else
#define API_PREFIX "/api/v1"
#endif

static bool is_binary_upload_complete(ts_http_request_t *req)
{
    return req && req->req && req->body_len == (size_t)req->req->content_len;
}



static void delayed_reboot_task(void *arg)
{
    uint32_t delay_ms = (uint32_t)(uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_restart();
}


static void schedule_reboot_after_response(uint32_t delay_ms)
{
    BaseType_t ret = xTaskCreate(
        delayed_reboot_task,
        "ota_reboot",
        2048,
        (void *)(uintptr_t)delay_ms,
        1,
        NULL
    );
    if (ret != pdPASS) {
        TS_LOGE(TAG, "Failed to create OTA reboot task");
    }
}





static bool is_browser_ota_busy(void)
{
    return ts_ota_sdcard_is_running()
        || ts_ota_https_is_running()
        || ts_ota_download_is_running()
        || ts_ota_www_is_running();
}

/**
 * @brief Check authentication for API requests
 */
static esp_err_t check_auth(ts_http_request_t *req, uint32_t *session_id, ts_perm_level_t required)
{
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
    char auth[128];
    if (ts_http_get_header(req, "Authorization", auth, sizeof(auth)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    
    ts_perm_level_t level = TS_PERM_NONE;
    esp_err_t ret = ts_auth_validate_request(auth, session_id, &level);
    if (ret != ESP_OK) return ret;
    
    if (level < required) return ESP_ERR_NOT_ALLOWED;
    return ESP_OK;
#else
    (void)req;
    (void)session_id;
    (void)required;
    return ESP_OK;
#endif
}

/* key.generate: same envelope as the generic path (failure details included) plus per-request stage logging. */
static esp_err_t send_key_generate_result(ts_http_request_t *req, ts_api_result_t *result,
                                          const char *request_id, int64_t started)
{
    cJSON *response = cJSON_CreateObject();
    bool built = response && cJSON_AddNumberToObject(response, "code", result->code);
    if (built && result->message) {
        built = cJSON_AddStringToObject(response, result->code == TS_API_OK ? "message" : "error", result->message) != NULL;
    }
    if (built && result->data) {
        cJSON *data = cJSON_Duplicate(result->data, true);
        if (!data) built = false;
        else if (!cJSON_AddItemToObject(response, "data", data)) { cJSON_Delete(data); built = false; }
    }
    char *json = built ? cJSON_PrintUnformatted(response) : NULL;
    cJSON_Delete(response);
    ts_api_result_free(result);
    TS_LOGI(TAG, "key.generate request=%s response=%s elapsed_ms=%lld", request_id,
            json ? "built" : "allocation_failed", (long long)((esp_timer_get_time() - started) / 1000));
    esp_err_t ret = json ? ts_http_send_json(req, 200, json)
                        : ts_http_send_error(req, 500, "Key response serialization failed");
    free(json);
    TS_LOGI(TAG, "key.generate request=%s send=%s elapsed_ms=%lld", request_id,
            esp_err_to_name(ret), (long long)((esp_timer_get_time() - started) / 1000));
    return ret;
}

/* One URI/policy owner for pre-body admission and execution-time recheck.
 * TOKEN endpoints retain their existing bounded-parser/body-token contract;
 * body fields never supply the C session identity for protected operations. */
/* The role/session comes from the validated header, not receipt query fields. */

static bool api_request_access(ts_http_request_t *req, char api_name[64], uint32_t *session_id)
{
    const size_t prefix_len = strlen(API_PREFIX);
    if (!req->uri || strncmp(req->uri, API_PREFIX "/", prefix_len + 1)) {
        ts_http_send_error(req, 404, "API not found");
        return false;
    }
    const char *endpoint = req->uri + prefix_len + 1;
    size_t length = strcspn(endpoint, "?");
    if (!length || length >= 64) {
        ts_http_send_error(req, 404, "API not found");
        return false;
    }
    memcpy(api_name, endpoint, length);
    api_name[length] = '\0';
    for (char *p = api_name; *p; ++p) if (*p == '/') *p = '.';
    *session_id = 0;
    /* Only verified request headers supply the C session identity. */
    check_auth(req, session_id, TS_PERM_NONE);
    ts_api_access_t access;
    esp_err_t policy_ret = ts_api_get_access(api_name, &access);
    if (policy_ret != ESP_OK) {
        ts_http_send_error(req, policy_ret == ESP_ERR_NOT_FOUND ? 404 : 500, "API policy unavailable");
        return false;
    }
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
    if (access.requires_auth) {
        esp_err_t auth_ret = check_auth(req, session_id, (ts_perm_level_t)access.minimum_level);
        if (auth_ret != ESP_OK) {
            ts_http_send_error(req, auth_ret == ESP_ERR_NOT_ALLOWED ? 403 : 401,
                auth_ret == ESP_ERR_NOT_ALLOWED && access.minimum_level == TS_PERM_ROOT ? "Administrator permission required" : "Unauthorized");
            return false;
        }
    }
#endif
    uint8_t method = req->method == TS_HTTP_GET ? TS_API_METHOD_GET :
        req->method == TS_HTTP_POST ? TS_API_METHOD_POST : req->method == TS_HTTP_PUT ? TS_API_METHOD_PUT :
        req->method == TS_HTTP_DELETE ? TS_API_METHOD_DELETE : 0;
    if (!method || !(access.allowed_methods & method)) {
        ts_http_send_error(req, 405, "Method not allowed");
        return false;
    }

    return true;
}

static bool api_admit(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    char api_name[64];
    uint32_t session_id;
    if (!api_request_access(req, api_name, &session_id)) return false;
    if (!strncmp(api_name,"auth.",5)) {
        if (req->req->content_len>16U*1024U) {
            ts_http_send_error(req,413,"Authentication request too large");
            return false;
        }
        ts_httpd_set_body_receive_budget(10000);
    }
    return true;
}

static esp_err_t api_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;

#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif

    char api_name[64];
    uint32_t session_id;
    if (!api_request_access(req, api_name, &session_id)) return ESP_FAIL;
    if (strncmp(api_name, "log.", 4) != 0) {
        TS_LOGD(TAG, "API request: method=%d", req->method);
    }

    bool key_generation = strcmp(api_name, "key.generate") == 0;
    bool rule_import = strcmp(api_name,"automation.rules.import")==0;
    if(rule_import&&(!is_binary_upload_complete(req)||!req->body||httpd_req_get_url_query_len(req->req)>0))
        return ts_http_send_error(req,400,"invalid_pack");
    int64_t key_started = key_generation ? esp_timer_get_time() : 0;
    char key_request_id[32] = "legacy";
    if (key_generation) TS_LOGI(TAG, "key.generate received");
    // Build request JSON
    cJSON *request = cJSON_CreateObject();
    if (key_generation && !request) {
        TS_LOGE(TAG, "key.generate request=legacy request allocation failed");
        esp_err_t send_ret = ts_http_send_error(req, 500, "Key request allocation failed");
        TS_LOGI(TAG, "key.generate request=legacy send=%s", esp_err_to_name(send_ret));
        return send_ret;
    }
    
    if (!request) return ts_http_send_error(req, 500, "Request allocation failed");
    size_t query_len = httpd_req_get_url_query_len(req->req);
    if (query_len > 0) {
        if (query_len == SIZE_MAX) {
            cJSON_Delete(request);
            return ts_http_send_error(req, 400, "Invalid query length");
        }
        char *query_buf = TS_API_MALLOC(query_len + 1);
        if (!query_buf) {
            cJSON_Delete(request);
            return ts_http_send_error(req, 500, "Query allocation failed");
        }
        ts_api_params_status_t parsed = TS_API_PARAMS_INVALID;
        if (httpd_req_get_url_query_str(req->req, query_buf, query_len + 1) == ESP_OK)
            parsed = ts_api_params_add_query(request, query_buf);
        free(query_buf);
        if (parsed != TS_API_PARAMS_OK) {
            cJSON_Delete(request);
            return ts_http_send_error(req, parsed == TS_API_PARAMS_NO_MEM ? 500 : 400, "Invalid or unavailable query");
        }
    }

    if (req->body_len > 0) {
        cJSON *body = rule_import ? ts_config_pack_parse_import_request(req->body, req->body_len)
                                 : ts_api_params_parse_object(req->body, req->body_len);
        if (!body || !cJSON_IsObject(body) || !ts_api_params_unique(body, 0)) {
            cJSON_Delete(body); cJSON_Delete(request);
            return ts_http_send_error(req, 400, rule_import ? "invalid_pack" : "Invalid JSON parameters");
        }
        cJSON *item;
        cJSON_ArrayForEach(item, body) {
            if (cJSON_GetObjectItem(request, item->string)) {
                cJSON_Delete(body); cJSON_Delete(request);
                return ts_http_send_error(req, 400, "Conflicting query and body parameters");
            }
            cJSON *copy = cJSON_Duplicate(item, true);
            if (!copy || !cJSON_AddItemToObject(request, item->string, copy)) {
                cJSON_Delete(copy); cJSON_Delete(body); cJSON_Delete(request);
                return ts_http_send_error(req, 500, "Parameter allocation failed");
            }
        }
        cJSON_Delete(body);
    }

    if (key_generation) {
        const cJSON *rid = cJSON_GetObjectItem(request, "request_id");
        if (cJSON_IsString(rid) && strlen(rid->valuestring) < sizeof(key_request_id)) {
            bool valid = rid->valuestring[0] != '\0';
            for (const char *p = rid->valuestring; *p; ++p) {
                if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                       (*p >= '0' && *p <= '9') || *p == '-')) valid = false;
            }
            if (valid) strcpy(key_request_id, rid->valuestring);
        }
        TS_LOGI(TAG, "key.generate request=%s arrived", key_request_id);
    }
    ts_api_result_t result = {0};
    esp_err_t ret = !strcmp(api_name, "automation.proxy.fetch") ?
        ts_api_automation_proxy_fetch_for_session(request, session_id, &result) :
        ts_api_call_for_session(api_name, request, session_id, &result);
    if (ret == ESP_ERR_NOT_ALLOWED && result.code == TS_API_ERR_AUTH) {
        cJSON_Delete(request);
        ts_api_result_free(&result);
        return ts_http_send_error(req, 401, "Unauthorized");
    }

    if (ret == ESP_ERR_NOT_ALLOWED && result.code == TS_API_ERR_NO_PERMISSION &&
        result.message && !strcmp(result.message, "Root permission required")) {
        cJSON_Delete(request);
        ts_api_result_free(&result);
        return ts_http_send_error(req, 403, "Root permission required");
    }

    // API 结果日志使用 DEBUG 级别
    if (strncmp(api_name, "log.", 4) != 0) {
        TS_LOGD(TAG, "API call result: api=%s ret=%d code=%d",
                api_name, ret, result.code);
    }
    
    if (key_generation) {
        const cJSON *generated = cJSON_GetObjectItem(result.data, "generated");
        TS_LOGI(TAG, "key.generate request=%s returned ret=%s code=%d generated_result=%d elapsed_ms=%lld",
                key_request_id, esp_err_to_name(ret), result.code, cJSON_IsTrue(generated),
                (long long)((esp_timer_get_time() - key_started) / 1000));
    }
    cJSON_Delete(request);
    if (key_generation) return send_key_generate_result(req, &result, key_request_id, key_started);

    /* A reply allocation failure after a write cannot prove that no effect occurred. */
    const bool response_may_have_effect = req->method != TS_HTTP_GET;
    if (ret == ESP_OK || result.code == TS_API_OK) {
        cJSON *response = cJSON_CreateObject();
        bool built = response && cJSON_AddNumberToObject(response, "code", result.code);
        if (built && result.message) built = cJSON_AddStringToObject(response, "message", result.message) != NULL;
        if (built && result.data) {
            built = cJSON_AddItemToObject(response, "data", result.data);
            if (built) result.data = NULL; /* Transfer this result's sole ownership. */
        }
        char *json = built ? cJSON_PrintUnformatted(response) : NULL;
        cJSON_Delete(response);
        ts_api_result_free(&result);
        if (json) {
            esp_err_t send_ret = ts_http_send_json(req, 200, json);
            free(json);
            return send_ret;
        }
        if (response_may_have_effect)
            return ts_http_send_json(req, 409, "{\"code\":7,\"error\":\"result_unknown\"}");
        return ts_http_send_error(req, 500, "JSON serialization failed");
    } else if (ret == ESP_ERR_NOT_FOUND && result.code == TS_API_ERR_NOT_FOUND && 
               result.message && strcmp(result.message, "API not found") == 0) {
        // Only report "API not found" if it's actually a missing API endpoint
        ts_api_result_free(&result);
        return ts_http_send_error(req, 404, "API not found");
    } else {
        // Return the actual error from the API handler with HTTP 200
        // API errors (like "no data") are business logic errors, not HTTP errors
        cJSON *response = cJSON_CreateObject();
        bool built = response &&
                     cJSON_AddNumberToObject(response, "code", result.code) &&
                     cJSON_AddStringToObject(response, "error", result.message ? result.message : "Internal error");
        /* Details may carry the confirmation the UI needs, so send them whole. */
        if (built && result.data) {
            built = cJSON_AddItemToObject(response, "data", result.data);
            if (built) result.data = NULL;
        }

        char *json = built ? cJSON_PrintUnformatted(response) : NULL;
        cJSON_Delete(response);
        ts_api_result_free(&result);
        
        if (json) {
            // Always return HTTP 200 for API business errors
            // The error is indicated by the "code" field in the JSON response
            esp_err_t send_ret = ts_http_send_json(req, 200, json);
            free(json);
            return send_ret;
        }
        if (response_may_have_effect)
            return ts_http_send_json(req, 409, "{\"code\":7,\"error\":\"result_unknown\"}");
        return ts_http_send_error(req, 500, "Internal error");
    }
}

static esp_err_t login_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif
    
    if (!req->body) {
        return ts_http_send_error(req, 400, "Missing body");
    }
    
    cJSON *body = ts_api_params_parse_object(req->body, req->body_len);
    if (!body) {
        return ts_http_send_error(req, 400, "Invalid JSON");
    }
    
    cJSON *username = cJSON_GetObjectItem(body, "username");
    cJSON *password = cJSON_GetObjectItem(body, "password");
    
    if (!username || !password || !cJSON_IsString(username) || !cJSON_IsString(password)) {
        cJSON_Delete(body);
        return ts_http_send_error(req, 400, "Missing username or password");
    }
    
    /* 保存用户名副本，因为后续要删除 body */
    char username_copy[32];
    strncpy(username_copy, username->valuestring, sizeof(username_copy) - 1);
    username_copy[sizeof(username_copy) - 1] = '\0';
    
    uint32_t session_id;
    char token[128];
    esp_err_t ret = ts_auth_login(username->valuestring, password->valuestring, &session_id, token, sizeof(token));
    cJSON_Delete(body);
    
    if (ret == ESP_ERR_INVALID_STATE) {
        return ts_http_send_error(req, 401, "Account locked due to too many failed attempts");
    }
    if (ret != ESP_OK) {
        return ts_http_send_error(req, 401, "Invalid credentials");
    }
    
    /* 获取用户权限等级 */
    ts_session_t session;
    if (ts_security_validate_session(session_id, &session) != ESP_OK) {
        return ts_http_send_error(req, 401, "Session expired");
    }
    ts_perm_level_t level = session.level;
    
    /* 将权限级别转换为字符串 */
    const char *level_str = (level == TS_PERM_ROOT) ? "root" : "admin";
    
    /* 检查是否需要修改密码 */
    bool password_changed = ts_auth_password_changed(username_copy);
    
    /* 构建符合前端期望的响应格式: {code: 0, data: {...}} */
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "code", 0);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "token", token);
    cJSON_AddNumberToObject(data, "session_id", session_id);
    cJSON_AddStringToObject(data, "username", username_copy);
    cJSON_AddStringToObject(data, "level", level_str);
#ifdef CONFIG_TS_SECURITY_TOKEN_EXPIRE_SEC
    cJSON_AddNumberToObject(data, "expires_in", CONFIG_TS_SECURITY_TOKEN_EXPIRE_SEC);
#else
    cJSON_AddNumberToObject(data, "expires_in", 86400);  /* 24 hours */
#endif
    cJSON_AddBoolToObject(data, "password_changed", password_changed);
    cJSON_AddItemToObject(response, "data", data);
    
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    esp_err_t send_ret = ts_http_send_json(req, 200, json);
    free(json);
    return send_ret;
}

static esp_err_t logout_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif
    
    char auth[128];
    if (ts_http_get_header(req, "Authorization", auth, sizeof(auth)) == ESP_OK) {
        uint32_t session_id;
        ts_perm_level_t level;
        extern esp_err_t ts_auth_validate_request(const char *, uint32_t *, ts_perm_level_t *);
        if (ts_auth_validate_request(auth, &session_id, &level) == ESP_OK) {
            extern esp_err_t ts_auth_logout(uint32_t);
            ts_auth_logout(session_id);
        }
    }
    
    return ts_http_send_json(req, 200, "{\"success\":true}");
}

static esp_err_t options_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif
    return ts_http_send_response(req, 204, NULL, NULL);
}

/*===========================================================================*/
/*                        File Upload/Download Handlers                       */
/*===========================================================================*/

/**
 * @brief Handle file download request
 * GET /api/v1/file/download?path=/sdcard/xxx
 */
static esp_err_t file_download_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif

    uint32_t session_id = 0;
    if (check_auth(req, &session_id, TS_PERM_READ) != ESP_OK) return ts_http_send_error(req, 401, "Unauthorized");

    // 获取文件路径参数
    char path[256] = {0};
    esp_err_t ret = ts_http_get_query_param(req, "path", path, sizeof(path));
    if (ret != ESP_OK || strlen(path) == 0) {
        return ts_http_send_error(req, 400, "Missing 'path' parameter");
    }
    
    // ts_http_get_query_param already decodes the query value once.
    TS_LOGD(TAG, "Download request: %s", path);
    
    bool needs_admin;
    ret = ts_storage_external_read_requires_admin(path, &needs_admin);
    if (ret != ESP_OK) return ts_http_send_error(req, ret == ESP_ERR_INVALID_ARG ? 400 : 500,
        ret == ESP_ERR_INVALID_ARG ? "storage_path_invalid" : "storage_read_failed");
    ret = check_auth(req, &session_id, needs_admin ? TS_PERM_ADMIN : TS_PERM_READ);
    if (ret != ESP_OK) return ts_http_send_error(req, ret == ESP_ERR_NOT_ALLOWED ? 403 : 401,
        ret == ESP_ERR_NOT_ALLOWED ? "Administrator permission required" : "Unauthorized");

    // 检查文件是否存在
    if (!ts_storage_exists(path)) {
        return ts_http_send_error(req, 404, "File not found");
    }
    
    // 检查是否是目录
    if (ts_storage_is_dir(path)) {
        return ts_http_send_error(req, 400, "Cannot download directory");
    }
    
    // 发送文件
    return ts_http_send_private_file(req, path);
}

/**
 * @brief Handle file upload request
 * POST /api/v1/file/upload?path=/sdcard/xxx
 * Body: file content
 */
static esp_err_t file_upload_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif

    // 获取目标路径参数
    char path[256] = {0};
    esp_err_t ret = ts_http_get_query_param(req, "path", path, sizeof(path));
    if (ret != ESP_OK || strlen(path) == 0) {
        return ts_http_send_error(req, 400, "Missing 'path' parameter");
    }
    
    // ts_http_get_query_param already decodes the query value once.
    TS_LOGD(TAG, "Upload request: path=%s, body_len=%d", path, (int)(req->body_len));
    
    // 安全检查：只允许写入 /sdcard
    if (strncmp(path, "/sdcard/", 8) != 0) {
        return ts_http_send_error(req, 403, "Upload only allowed to /sdcard");
    }
    ret = ts_storage_check_external_write_path(path, false);
    if (ret == ESP_ERR_NOT_ALLOWED) return ts_http_send_error(req, 409, "automation_configuration_managed");
    if (ret != ESP_OK) return ts_http_send_error(req, 400, "storage_path_invalid");
    
    // 检查请求体
    if (!req->body || req->body_len == 0) {
        return ts_http_send_error(req, 400, "Empty file content");
    }
    
    // 确保目标目录存在
    char dir_path[256];
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash && last_slash != dir_path) {
        *last_slash = '\0';
        ret = ts_storage_mkdir_p(dir_path);
        if (ret == ESP_ERR_NOT_ALLOWED) return ts_http_send_error(req, 409, "automation_configuration_managed");
        if (ret != ESP_OK) return ts_http_send_error(req, 500, "Failed to create upload directory");
    }
    
    // 写入文件
    ret = ts_storage_replace_file(path, req->body, req->body_len);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NOT_ALLOWED) return ts_http_send_error(req, 409, "automation_configuration_managed");
        TS_LOGE(TAG, "Failed to write file %s: %s", path, esp_err_to_name(ret));
        return ts_http_send_error(req, ret == ESP_ERR_NOT_FINISHED ? 409 : 500,
            ret == ESP_ERR_NOT_FINISHED ? "file_replace_uncertain" : "Failed to write file");
    }
    
    TS_LOGD(TAG, "File uploaded: %s (%zu bytes)", path, req->body_len);
    
    // 返回成功响应
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "path", path);
    cJSON_AddNumberToObject(response, "size", req->body_len);
    cJSON_AddStringToObject(response, "status", "uploaded");
    
    // === 检测 .tscfg 配置包并自动验证 ===
    const char *ext = strrchr(path, '.');
    if (ext && strcasecmp(ext, TS_CONFIG_PACK_EXT) == 0) {
        TS_LOGI(TAG, "Detected config pack upload: %s", path);
        
        // 验证配置包
        ts_config_pack_sig_info_t sig_info = {0};
        ts_config_pack_result_t pack_result = ts_config_pack_verify(path, &sig_info);
        
        // 添加验证结果到响应
        cJSON *validation = cJSON_CreateObject();
        cJSON_AddBoolToObject(validation, "valid", pack_result == TS_CONFIG_PACK_OK);
        cJSON_AddNumberToObject(validation, "result_code", pack_result);
        cJSON_AddStringToObject(validation, "result_message", ts_config_pack_strerror(pack_result));
        cJSON_AddStringToObject(validation, "reason", ts_config_pack_reason(pack_result));
        
        if (pack_result == TS_CONFIG_PACK_OK || sig_info.signer_cn[0] != '\0') {
            cJSON *sig = cJSON_CreateObject();
            cJSON_AddBoolToObject(sig, "valid", sig_info.valid);
            cJSON_AddBoolToObject(sig, "is_official", sig_info.is_official);
            cJSON_AddStringToObject(sig, "signer_cn", sig_info.signer_cn);
            cJSON_AddStringToObject(sig, "signer_ou", sig_info.signer_ou);
            cJSON_AddNumberToObject(sig, "signed_at", (double)sig_info.signed_at);
            cJSON_AddItemToObject(validation, "signature", sig);
        }
        
        cJSON_AddItemToObject(response, "config_pack", validation);
        
        // 通过 WebSocket 广播验证结果
        cJSON *ws_data = cJSON_CreateObject();
        cJSON_AddStringToObject(ws_data, "path", path);
        cJSON_AddStringToObject(ws_data, "status", 
                               pack_result == TS_CONFIG_PACK_OK ? "success" : "error");
        cJSON_AddNumberToObject(ws_data, "result_code", pack_result);
        cJSON_AddStringToObject(ws_data, "result_message", ts_config_pack_strerror(pack_result));
        cJSON_AddStringToObject(ws_data, "reason", ts_config_pack_reason(pack_result));
        
        if (pack_result == TS_CONFIG_PACK_OK || sig_info.signer_cn[0] != '\0') {
            cJSON *ws_sig = cJSON_CreateObject();
            cJSON_AddBoolToObject(ws_sig, "valid", sig_info.valid);
            cJSON_AddBoolToObject(ws_sig, "is_official", sig_info.is_official);
            cJSON_AddStringToObject(ws_sig, "signer_cn", sig_info.signer_cn);
            cJSON_AddItemToObject(ws_data, "signature", ws_sig);
        }
        
        ts_ws_broadcast_to_topic("config.pack.validated", ws_data);
        cJSON_Delete(ws_data);
    }
    
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    ret = ts_http_send_json(req, 200, json);
    free(json);
    return ret;
}

/**
 * @brief Handle OTA firmware upload request (Browser Proxy Upgrade)
 * POST /api/v1/ota/firmware
 * Body: firmware binary content
 * Query: auto_reboot (optional, default true)
 * 
 * This allows browsers to upload firmware directly to ESP32 devices
 * that cannot access external networks.
 * 
 * Original firmware format and SD recovery cache; validate before changing the previous cache.
 */
/* Keep the original raw APP/WWW and /sdcard/recovery contract. Invalid or
 * failed uploads never delete the last recovery set. No publisher registration
 * is required; APP completeness is finally checked by the SDK installer. */
static esp_err_t ota_upload_reply(ts_http_request_t *req, bool firmware)
{
    if (!is_binary_upload_complete(req) || !req->body || !req->body_len)
        return ts_http_send_error(req, 400, "Incomplete or empty OTA upload");
    uint32_t caller = 0;
    if (check_auth(req, &caller, TS_PERM_ADMIN) != ESP_OK)
        return ts_http_send_error(req, 403, "Administrator login required");
    const esp_partition_t *partition = firmware ? esp_ota_get_next_update_partition(NULL) :
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (!partition) return ts_http_send_error(req, 500, "OTA partition unavailable");
    if (req->body_len > partition->size)
        return ts_http_send_error(req, 400, "OTA image exceeds partition capacity");
    if (firmware) {
        const size_t offset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
        esp_app_desc_t app;
        if (req->body_len < offset + sizeof(app))
            return ts_http_send_error(req, 400, "Invalid firmware image");
        memcpy(&app, (const uint8_t *)req->body + offset, sizeof(app));
        if (((const uint8_t *)req->body)[0] != ESP_IMAGE_HEADER_MAGIC ||
            app.magic_word != ESP_APP_DESC_MAGIC_WORD)
            return ts_http_send_error(req, 400, "Invalid firmware image");
    }
    if (is_browser_ota_busy()) return ts_http_send_error(req, 409, "OTA already in progress");
    static atomic_flag upload_busy = ATOMIC_FLAG_INIT;
    if (atomic_flag_test_and_set(&upload_busy)) return ts_http_send_error(req, 409, "OTA upload busy");
    char auto_reboot_text[8] = "true";
    ts_http_get_query_param(req, "auto_reboot", auto_reboot_text, sizeof(auto_reboot_text));
    bool auto_reboot = firmware && strcmp(auto_reboot_text, "false") != 0;
    cJSON *reply = cJSON_CreateObject();
    bool ready = reply && cJSON_AddStringToObject(reply, "status", "success") &&
        cJSON_AddNumberToObject(reply, "size", req->body_len) &&
        cJSON_AddStringToObject(reply, "message", firmware ? "Firmware uploaded and flashed, pending reboot" : "WWW partition updated");
    if (ready && firmware) ready = cJSON_AddBoolToObject(reply, "reboot_pending", true) &&
        cJSON_AddBoolToObject(reply, "auto_reboot_requested", auto_reboot);
    char *json = ready ? cJSON_PrintUnformatted(reply) : NULL;
    cJSON_Delete(reply);
    if (!json) {
        atomic_flag_clear(&upload_busy);
        return ts_http_send_error(req, 500, "Out of memory");
    }
    const char *pending = firmware ? "/sdcard/recovery/.firmware-upload.bin" : "/sdcard/recovery/.www-upload.bin";
    const char *destination = firmware ? TS_OTA_RECOVERY_FIRMWARE_PATH : TS_OTA_RECOVERY_WWW_PATH;
    esp_err_t ret = ts_storage_mkdir_p("/sdcard/recovery");
    if (ret == ESP_OK) ret = ts_storage_write_file(pending, req->body, req->body_len);
    if (ret == ESP_OK) ret = firmware ? ts_ota_flash_file_sync(pending, true, NULL, NULL) :
        ts_ota_www_flash_file_sync(pending, NULL, NULL);
    if (ret != ESP_OK) {
        unlink(pending);
        free(json);
        atomic_flag_clear(&upload_busy);
        return ts_http_send_error(req, ret == ESP_ERR_INVALID_STATE ? 409 :
            (ret == ESP_ERR_INVALID_ARG || ret == ESP_ERR_INVALID_SIZE || ret == ESP_ERR_OTA_VALIDATE_FAILED) ? 400 : 500,
            "OTA upload did not complete; previous recovery files retained");
    }
    /* Flash succeeded. Commit the ordinary recovery file, then its disabled
     * manifest. A cache failure must not be reported as complete success. */
    ret = ts_storage_replace_file(destination, req->body, req->body_len);
    unlink(pending);
    if (ret != ESP_OK ||
        (firmware || ts_storage_exists(TS_OTA_RECOVERY_FIRMWARE_PATH) ?
         ts_ota_write_recovery_manifest_from_files(!firmware, false, false) : ESP_OK) != ESP_OK) {
        free(json);
        atomic_flag_clear(&upload_busy);
        return ts_http_send_json(req, 409,
            "{\"status\":\"partial\",\"error\":\"recovery_cache_failed\",\"flash_written\":true}");
    }
    ret = ts_http_send_json(req, 200, json);
    free(json);
    atomic_flag_clear(&upload_busy);
    if (ret == ESP_OK && auto_reboot) schedule_reboot_after_response(OTA_UPLOAD_REBOOT_DELAY_MS);
    return ret;
}

static esp_err_t ota_firmware_upload_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    return ota_upload_reply(req, true);
}
/**
 * @brief Handle WWW partition upload request
 * POST /api/v1/ota/www
 * Body: www.bin content
 * 
 * Original www partition and raw image format.
 */
static esp_err_t ota_www_upload_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    return ota_upload_reply(req, false);
}
esp_err_t ts_webui_api_init(void)
{
    TS_LOGI(TAG, "Initializing API routes");
    
    // Auth routes
    ts_http_route_t login = {
        .uri = API_PREFIX "/auth/login",
        .method = TS_HTTP_POST,
        .handler = login_handler,
        .max_body_bytes = 16U*1024U,
        .body_timeout_ms = 10000,
        .requires_auth = false,
        .user_data = NULL
    };
    ts_http_server_register_route(&login);
    
    ts_http_route_t logout = {
        .uri = API_PREFIX "/auth/logout",
        .method = TS_HTTP_POST,
        .handler = logout_handler,
        .requires_auth = true,
        .user_data = NULL
    };
    ts_http_server_register_route(&logout);
    
    // File download route (must be registered BEFORE generic handler)
    ts_http_route_t file_download = {
        .uri = API_PREFIX "/file/download",
        .method = TS_HTTP_GET,
        .handler = file_download_handler,
        .requires_auth = true,
        .required_level = TS_PERM_READ,
        .user_data = NULL
    };
    ts_http_server_register_route(&file_download);
    
    // File upload route
    ts_http_route_t file_upload = {
        .uri = API_PREFIX "/file/upload",
        .method = TS_HTTP_POST,
        .handler = file_upload_handler,
        .max_body_bytes = 16U*1024U*1024U,
        .body_timeout_ms = 300000,
        .requires_auth = true,
        .required_level = TS_PERM_ADMIN,
        .user_data = NULL
    };
    ts_http_server_register_route(&file_upload);
    
    // OTA firmware upload route (Browser Proxy Upgrade)
    ts_http_route_t ota_firmware = {
        .uri = API_PREFIX "/ota/firmware",
        .method = TS_HTTP_POST,
        .handler = ota_firmware_upload_handler,
        .max_body_bytes = 16U*1024U*1024U,
        .body_timeout_ms = 300000,
        .requires_auth = true,
        .required_level = TS_PERM_ADMIN,
        .user_data = NULL
    };
    ts_http_server_register_route(&ota_firmware);
    
    // WWW partition upload route
    ts_http_route_t ota_www = {
        .uri = API_PREFIX "/ota/www",
        .method = TS_HTTP_POST,
        .handler = ota_www_upload_handler,
        .max_body_bytes = 16U*1024U*1024U,
        .body_timeout_ms = 300000,
        .requires_auth = true,
        .required_level = TS_PERM_ADMIN,
        .user_data = NULL
    };
    ts_http_server_register_route(&ota_www);
    
    // Generic API handler for all other routes
    size_t generic_body_limit=1024U*1024U;
#ifdef CONFIG_TS_CONFIG_PACK_MAX_SIZE
    /* Preserve the existing escaped import envelope of the configured pack size. */
    size_t pack_envelope=(size_t)CONFIG_TS_CONFIG_PACK_MAX_SIZE*6U+1024U;
    if (pack_envelope>generic_body_limit) generic_body_limit=pack_envelope;
#endif
    ts_http_method_t methods[] = {TS_HTTP_GET, TS_HTTP_POST, TS_HTTP_PUT, TS_HTTP_DELETE};
    const char *method_names[] = {"GET", "POST", "PUT", "DELETE"};
    for (int i = 0; i < 4; i++) {
        ts_http_route_t route = {
            .uri = API_PREFIX "/*",
            .method = methods[i],
            .handler = api_handler,
            .admit = api_admit,
            .max_body_bytes = generic_body_limit,
            .body_timeout_ms = 30000,
            .requires_auth = false,
            .user_data = NULL
        };
        esp_err_t ret = ts_http_server_register_route(&route);
        TS_LOGD(TAG, "Register API %s handler: %s", method_names[i], esp_err_to_name(ret));
    }
    
    // CORS preflight handler
    ts_http_route_t options = {
        .uri = API_PREFIX "/*",
        .method = HTTP_OPTIONS,
        .handler = options_handler,
        .requires_auth = false,
        .user_data = NULL
    };
    ts_http_server_register_route(&options);
    
    TS_LOGI(TAG, "API routes registered");
    return ESP_OK;
}
