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
#include "cJSON.h"
#include "esp_http_server.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"

/* PSRAM 优先分配宏 */
#define TS_API_MALLOC(size) ({ void *p = heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); p ? p : malloc(size); })

#define TAG "webui_api"

#ifdef CONFIG_TS_WEBUI_API_PREFIX
#define API_PREFIX CONFIG_TS_WEBUI_API_PREFIX
#else
#define API_PREFIX "/api/v1"
#endif

static esp_err_t check_auth(ts_http_request_t *req, uint32_t *session_id, ts_perm_level_t required)
{
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
    char auth[128];
    if (ts_http_get_header(req, "Authorization", auth, sizeof(auth)) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    
    ts_perm_level_t level;
    extern esp_err_t ts_auth_validate_request(const char *, uint32_t *, ts_perm_level_t *);
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

static esp_err_t api_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif
    
    // Extract endpoint from URI
    const char *uri = req->uri;
    const char *endpoint = uri + strlen(API_PREFIX) + 1;
    
    // Convert slashes to dots for API lookup
    char api_name[64];
    strncpy(api_name, endpoint, sizeof(api_name) - 1);
    api_name[sizeof(api_name) - 1] = '\0';
    
    // Remove query string if present
    char *query = strchr(api_name, '?');
    if (query) {
        *query = '\0';
    }
    
    // Convert slashes to dots
    for (char *p = api_name; *p; p++) {
        if (*p == '/') *p = '.';
    }
    
    // API 请求日志使用 DEBUG 级别，避免干扰调试
    if (strncmp(api_name, "log.", 4) != 0) {
        TS_LOGD(TAG, "API request: method=%d uri=%s -> api_name=%s", 
                req->method, uri, api_name);
    }
    
    // TODO: 测试阶段暂时禁用认证检查
    // 功能测试完成后需要恢复以下代码：
    // Check authentication for write operations only
    // GET requests (read-only) are allowed without authentication
    uint32_t session_id = 0;
    
    // if (req->method == TS_HTTP_POST || req->method == TS_HTTP_PUT || 
    //     req->method == TS_HTTP_DELETE) {
    //     if (check_auth(req, &session_id, TS_PERM_WRITE) != ESP_OK) {
    //         return ts_http_send_error(req, 401, "Unauthorized");
    //     }
    // }
    
    // Build request JSON
    cJSON *request = cJSON_CreateObject();
    
    // Parse URL query string parameters (for GET requests)
    size_t query_len = httpd_req_get_url_query_len(req->req);
    if (query_len > 0) {
        char *query_buf = TS_API_MALLOC(query_len + 1);
        if (query_buf) {
            if (httpd_req_get_url_query_str(req->req, query_buf, query_len + 1) == ESP_OK) {
                // Parse query string parameters
                char *saveptr = NULL;
                char *pair = strtok_r(query_buf, "&", &saveptr);
                while (pair) {
                    char *eq = strchr(pair, '=');
                    if (eq) {
                        *eq = '\0';
                        char *key = pair;
                        char *value = eq + 1;
                        
                        // URL decode the value
                        char *src = value;
                        char *dst = value;
                        while (*src) {
                            if (*src == '%' && src[1] && src[2]) {
                                int high = src[1];
                                int low = src[2];
                                // Convert hex
                                if (high >= '0' && high <= '9') high -= '0';
                                else if (high >= 'A' && high <= 'F') high = high - 'A' + 10;
                                else if (high >= 'a' && high <= 'f') high = high - 'a' + 10;
                                else { *dst++ = *src++; continue; }
                                
                                if (low >= '0' && low <= '9') low -= '0';
                                else if (low >= 'A' && low <= 'F') low = low - 'A' + 10;
                                else if (low >= 'a' && low <= 'f') low = low - 'a' + 10;
                                else { *dst++ = *src++; continue; }
                                
                                *dst++ = (char)((high << 4) | low);
                                src += 3;
                            } else if (*src == '+') {
                                *dst++ = ' ';
                                src++;
                            } else {
                                *dst++ = *src++;
                            }
                        }
                        *dst = '\0';
                        
                        // Try to parse as number first
                        char *endptr;
                        long num = strtol(value, &endptr, 10);
                        if (*endptr == '\0' && value[0] != '\0') {
                            // It's a number
                            cJSON_AddNumberToObject(request, key, num);
                        } else {
                            // It's a string
                            cJSON_AddStringToObject(request, key, value);
                        }
                    }
                    pair = strtok_r(NULL, "&", &saveptr);
                }
            }
            free(query_buf);
        }
    }
    
    // Add body data if present (POST/PUT requests - overrides query params)
    if (req->body && req->body_len > 0) {
        cJSON *body = cJSON_Parse(req->body);
        if (body) {
            cJSON *item;
            cJSON_ArrayForEach(item, body) {
                cJSON_AddItemToObject(request, item->string, cJSON_Duplicate(item, true));
            }
            cJSON_Delete(body);
        }
    }
    
    // Call API
    ts_api_result_t result = {0};
    esp_err_t ret = ts_api_call(api_name, request, &result);
    
    // API 结果日志使用 DEBUG 级别
    if (strncmp(api_name, "log.", 4) != 0) {
        TS_LOGD(TAG, "API call result: api=%s ret=%d code=%d msg=%s", 
                api_name, ret, result.code, result.message ? result.message : "null");
    }
    
    cJSON_Delete(request);
    
    if (ret == ESP_OK || result.code == TS_API_OK) {
        // Build response JSON
        cJSON *response = cJSON_CreateObject();
        cJSON_AddNumberToObject(response, "code", result.code);
        if (result.message) {
            cJSON_AddStringToObject(response, "message", result.message);
        }
        if (result.data) {
            cJSON_AddItemToObject(response, "data", cJSON_Duplicate(result.data, true));
        }
        
        char *json = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        
        // Free result resources
        ts_api_result_free(&result);
        
        if (json) {
            esp_err_t send_ret = ts_http_send_json(req, 200, json);
            free(json);
            return send_ret;
        }
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
        cJSON_AddNumberToObject(response, "code", result.code);
        cJSON_AddStringToObject(response, "error", result.message ? result.message : "Internal error");
        
        char *json = cJSON_PrintUnformatted(response);
        cJSON_Delete(response);
        ts_api_result_free(&result);
        
        if (json) {
            // Always return HTTP 200 for API business errors
            // The error is indicated by the "code" field in the JSON response
            esp_err_t send_ret = ts_http_send_json(req, 200, json);
            free(json);
            return send_ret;
        }
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
    
    cJSON *body = cJSON_Parse(req->body);
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
    
    if (ret != ESP_OK) {
        return ts_http_send_error(req, 401, "Invalid credentials");
    }
    
    /* 获取用户权限等级 */
    ts_perm_level_t level = TS_PERM_ADMIN;  /* 默认 */
    ts_session_t session;
    if (ts_security_validate_session(session_id, &session) == ESP_OK) {
        level = session.level;
    }
    
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
 * @brief URL decode a string in place
 */
static void url_decode(char *str)
{
    char *src = str;
    char *dst = str;
    
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            int high = src[1];
            int low = src[2];
            
            // Convert hex chars to int
            if (high >= '0' && high <= '9') high -= '0';
            else if (high >= 'A' && high <= 'F') high = high - 'A' + 10;
            else if (high >= 'a' && high <= 'f') high = high - 'a' + 10;
            else { *dst++ = *src++; continue; }
            
            if (low >= '0' && low <= '9') low -= '0';
            else if (low >= 'A' && low <= 'F') low = low - 'A' + 10;
            else if (low >= 'a' && low <= 'f') low = low - 'a' + 10;
            else { *dst++ = *src++; continue; }
            
            *dst++ = (char)((high << 4) | low);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

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

    // 获取文件路径参数
    char path[256] = {0};
    esp_err_t ret = ts_http_get_query_param(req, "path", path, sizeof(path));
    if (ret != ESP_OK || strlen(path) == 0) {
        return ts_http_send_error(req, 400, "Missing 'path' parameter");
    }
    
    // URL decode path
    url_decode(path);
    TS_LOGD(TAG, "Download request: %s", path);
    
    // 安全检查：只允许访问 /sdcard 和 /spiffs
    if (strncmp(path, "/sdcard", 7) != 0 && strncmp(path, "/spiffs", 7) != 0) {
        return ts_http_send_error(req, 403, "Access denied: invalid path");
    }
    
    // 检查文件是否存在
    if (!ts_storage_exists(path)) {
        return ts_http_send_error(req, 404, "File not found");
    }
    
    // 检查是否是目录
    if (ts_storage_is_dir(path)) {
        return ts_http_send_error(req, 400, "Cannot download directory");
    }
    
    // 发送文件
    return ts_http_send_file(req, path);
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
    
    // URL 解码路径
    url_decode(path);
    TS_LOGD(TAG, "Upload request: path=%s, body_len=%d", path, (int)(req->body_len));
    
    // 安全检查：只允许写入 /sdcard
    if (strncmp(path, "/sdcard", 7) != 0) {
        return ts_http_send_error(req, 403, "Upload only allowed to /sdcard");
    }
    
    // 检查请求体
    if (!req->body || req->body_len == 0) {
        return ts_http_send_error(req, 400, "Empty file content");
    }
    
    // 确保目标目录存在
    char dir_path[256];
    strncpy(dir_path, path, sizeof(dir_path) - 1);
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash && last_slash != dir_path) {
        *last_slash = '\0';
        ts_storage_mkdir_p(dir_path);
    }
    
    // 写入文件
    ret = ts_storage_write_file(path, req->body, req->body_len);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to write file %s: %s", path, esp_err_to_name(ret));
        return ts_http_send_error(req, 500, "Failed to write file");
    }
    
    TS_LOGD(TAG, "File uploaded: %s (%zu bytes)", path, req->body_len);
    
    // 返回成功响应
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "path", path);
    cJSON_AddNumberToObject(response, "size", req->body_len);
    cJSON_AddStringToObject(response, "status", "uploaded");
    
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
 * 使用统一 recovery 目录模式：保存到 /sdcard/recovery/ 然后刷入
 */
static esp_err_t ota_firmware_upload_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif

    // 检查请求体
    if (!req->body || req->body_len == 0) {
        return ts_http_send_error(req, 400, "Empty firmware content");
    }
    
    // 获取 auto_reboot 参数
    char auto_reboot_str[8] = "true";
    ts_http_get_query_param(req, "auto_reboot", auto_reboot_str, sizeof(auto_reboot_str));
    bool auto_reboot = (strcmp(auto_reboot_str, "false") != 0);
    
    TS_LOGI(TAG, "OTA firmware upload: %zu bytes, auto_reboot=%d", req->body_len, auto_reboot);
    
    // 使用统一的 recovery 目录模式
    esp_err_t ret = ts_ota_save_upload(req->body, req->body_len, true, auto_reboot);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "ts_ota_save_upload failed: %s", esp_err_to_name(ret));
        if (ret == ESP_ERR_INVALID_STATE) {
            return ts_http_send_error(req, 409, "OTA already in progress");
        }
        return ts_http_send_error(req, 500, "Firmware save/flash failed");
    }
    
    // 如果 auto_reboot=true，设备会重启，不会执行到这里
    TS_LOGI(TAG, "OTA firmware upload successful");
    
    // 返回成功响应
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddNumberToObject(response, "size", req->body_len);
    cJSON_AddBoolToObject(response, "reboot_pending", auto_reboot);
    cJSON_AddStringToObject(response, "message", auto_reboot ? "Firmware uploaded, rebooting..." : "Firmware uploaded, pending reboot");
    
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    ret = ts_http_send_json(req, 200, json);
    free(json);
    return ret;
}

/**
 * @brief Handle WWW partition upload request
 * POST /api/v1/ota/www
 * Body: www.bin content
 * 
 * 使用统一 recovery 目录模式：保存到 /sdcard/recovery/www.bin 然后刷入
 */
static esp_err_t ota_www_upload_handler(ts_http_request_t *req, void *user_data)
{
    (void)user_data;
    
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif

    // 检查请求体
    if (!req->body || req->body_len == 0) {
        return ts_http_send_error(req, 400, "Empty www content");
    }
    
    TS_LOGI(TAG, "WWW partition upload: %zu bytes", req->body_len);
    
    // 使用统一的 recovery 目录模式
    esp_err_t ret = ts_ota_save_upload(req->body, req->body_len, false, true);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "ts_ota_save_upload failed: %s", esp_err_to_name(ret));
        return ts_http_send_error(req, 500, "WWW partition save/flash failed");
    }
    
    TS_LOGI(TAG, "WWW partition upload successful");
    
    // 返回成功响应
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "status", "success");
    cJSON_AddNumberToObject(response, "size", req->body_len);
    cJSON_AddStringToObject(response, "message", "WWW partition updated");
    
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    ret = ts_http_send_json(req, 200, json);
    free(json);
    return ret;
}

esp_err_t ts_webui_api_init(void)
{
    TS_LOGI(TAG, "Initializing API routes");
    
    // Auth routes
    ts_http_route_t login = {
        .uri = API_PREFIX "/auth/login",
        .method = TS_HTTP_POST,
        .handler = login_handler,
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
        .requires_auth = false,
        .user_data = NULL
    };
    ts_http_server_register_route(&file_download);
    
    // File upload route
    ts_http_route_t file_upload = {
        .uri = API_PREFIX "/file/upload",
        .method = TS_HTTP_POST,
        .handler = file_upload_handler,
        .requires_auth = false,
        .user_data = NULL
    };
    ts_http_server_register_route(&file_upload);
    
    // OTA firmware upload route (Browser Proxy Upgrade)
    ts_http_route_t ota_firmware = {
        .uri = API_PREFIX "/ota/firmware",
        .method = TS_HTTP_POST,
        .handler = ota_firmware_upload_handler,
        .requires_auth = false,  // TODO: 生产环境应设为 true
        .user_data = NULL
    };
    ts_http_server_register_route(&ota_firmware);
    
    // WWW partition upload route
    ts_http_route_t ota_www = {
        .uri = API_PREFIX "/ota/www",
        .method = TS_HTTP_POST,
        .handler = ota_www_upload_handler,
        .requires_auth = false,  // TODO: 生产环境应设为 true
        .user_data = NULL
    };
    ts_http_server_register_route(&ota_www);
    
    // Generic API handler for all other routes
    ts_http_method_t methods[] = {TS_HTTP_GET, TS_HTTP_POST, TS_HTTP_PUT, TS_HTTP_DELETE};
    const char *method_names[] = {"GET", "POST", "PUT", "DELETE"};
    for (int i = 0; i < 4; i++) {
        ts_http_route_t route = {
            .uri = API_PREFIX "/*",
            .method = methods[i],
            .handler = api_handler,
            .requires_auth = true,
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
