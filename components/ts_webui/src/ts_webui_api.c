/**
 * @file ts_webui_api.c
 * @brief WebUI REST API Implementation
 */

#include "ts_webui.h"
#include "ts_http_server.h"
#include "ts_api.h"
#include "ts_security.h"
#include "ts_log.h"
#include "cJSON.h"
#include <string.h>

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
    return ESP_OK;
#endif
}

static esp_err_t api_handler(ts_http_request_t *req, void *user_data)
{
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif
    
    // Extract endpoint from URI
    const char *uri = req->uri;
    const char *endpoint = uri + strlen(API_PREFIX) + 1;
    
    // Convert slashes to dots for API lookup
    char api_name[64];
    strncpy(api_name, endpoint, sizeof(api_name) - 1);
    for (char *p = api_name; *p; p++) {
        if (*p == '/') *p = '.';
    }
    
    // Check authentication for non-public endpoints
    uint32_t session_id = 0;
    ts_perm_level_t required = TS_PERM_READ;
    
    if (req->method == TS_HTTP_POST || req->method == TS_HTTP_PUT || 
        req->method == TS_HTTP_DELETE) {
        required = TS_PERM_WRITE;
    }
    
    if (check_auth(req, &session_id, required) != ESP_OK) {
        return ts_http_send_error(req, 401, "Unauthorized");
    }
    
    // Build request JSON
    cJSON *request = cJSON_CreateObject();
    
    // Add body data if present
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
    char *result = NULL;
    esp_err_t ret = ts_api_call(api_name, request, &result);
    cJSON_Delete(request);
    
    if (ret == ESP_OK && result) {
        esp_err_t send_ret = ts_http_send_json(req, 200, result);
        free(result);
        return send_ret;
    } else if (ret == ESP_ERR_NOT_FOUND) {
        return ts_http_send_error(req, 404, "API not found");
    } else {
        return ts_http_send_error(req, 500, "Internal error");
    }
}

static esp_err_t login_handler(ts_http_request_t *req, void *user_data)
{
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
    
    uint32_t session_id;
    extern esp_err_t ts_auth_login(const char *, const char *, uint32_t *);
    esp_err_t ret = ts_auth_login(username->valuestring, password->valuestring, &session_id);
    cJSON_Delete(body);
    
    if (ret != ESP_OK) {
        return ts_http_send_error(req, 401, "Invalid credentials");
    }
    
    // Generate token
    char token[128];
    ts_security_generate_token(session_id, token, sizeof(token));
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "token", token);
    cJSON_AddNumberToObject(response, "session_id", session_id);
    
    char *json = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
    esp_err_t send_ret = ts_http_send_json(req, 200, json);
    free(json);
    return send_ret;
}

static esp_err_t logout_handler(ts_http_request_t *req, void *user_data)
{
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
#ifdef CONFIG_TS_WEBUI_CORS_ENABLE
    ts_http_set_cors(req, "*");
#endif
    return ts_http_send_response(req, 204, NULL, NULL);
}

esp_err_t ts_webui_api_init(void)
{
    TS_LOGI(TAG, "Initializing API routes");
    
    // Auth routes
    ts_http_route_t login = {
        .uri = API_PREFIX "/auth/login",
        .method = TS_HTTP_POST,
        .handler = login_handler,
        .requires_auth = false
    };
    ts_http_server_register_route(&login);
    
    ts_http_route_t logout = {
        .uri = API_PREFIX "/auth/logout",
        .method = TS_HTTP_POST,
        .handler = logout_handler,
        .requires_auth = true
    };
    ts_http_server_register_route(&logout);
    
    // Generic API handler for all other routes
    ts_http_method_t methods[] = {TS_HTTP_GET, TS_HTTP_POST, TS_HTTP_PUT, TS_HTTP_DELETE};
    for (int i = 0; i < 4; i++) {
        ts_http_route_t route = {
            .uri = API_PREFIX "/*",
            .method = methods[i],
            .handler = api_handler,
            .requires_auth = true
        };
        ts_http_server_register_route(&route);
    }
    
    // CORS preflight handler
    ts_http_route_t options = {
        .uri = API_PREFIX "/*",
        .method = HTTP_OPTIONS,
        .handler = options_handler
    };
    ts_http_server_register_route(&options);
    
    TS_LOGI(TAG, "API routes registered");
    return ESP_OK;
}
