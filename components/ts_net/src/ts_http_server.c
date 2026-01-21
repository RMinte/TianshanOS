/**
 * @file ts_http_server.c
 * @brief HTTP Server Implementation
 */

#include "ts_http_server.h"
#include "ts_log.h"
#include "ts_storage.h"
#include "esp_log.h"
#include <string.h>

#define TAG "ts_http"
#define MAX_ROUTES 32

static httpd_handle_t s_server = NULL;
static bool s_initialized = false;

esp_err_t ts_http_server_init(void)
{
    if (s_initialized) return ESP_OK;
    
    /* 降低 httpd 内部日志级别，避免连接重置警告刷屏 */
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    
    s_initialized = true;
    TS_LOGI(TAG, "HTTP server initialized");
    return ESP_OK;
}

esp_err_t ts_http_server_deinit(void)
{
    ts_http_server_stop();
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_http_server_start(void)
{
    if (s_server) return ESP_OK;
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#ifdef CONFIG_TS_NET_HTTP_PORT
    config.server_port = CONFIG_TS_NET_HTTP_PORT;
#endif
    config.max_uri_handlers = MAX_ROUTES;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.global_user_ctx = NULL;
    config.global_user_ctx_free_fn = NULL;
    config.global_transport_ctx = NULL;
    config.global_transport_ctx_free_fn = NULL;
    config.lru_purge_enable = true;  // 启用 LRU 清理，处理连接重置
    config.recv_wait_timeout = 5;    // 接收超时 5 秒
    config.send_wait_timeout = 5;    // 发送超时 5 秒
    config.stack_size = 8192;        // 增加栈大小，支持终端命令执行
    
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    }
    return ret;
}

esp_err_t ts_http_server_stop(void)
{
    if (!s_server) return ESP_OK;
    
    httpd_stop(s_server);
    s_server = NULL;
    TS_LOGI(TAG, "HTTP server stopped");
    return ESP_OK;
}

static esp_err_t http_handler_wrapper(httpd_req_t *req)
{
    ts_http_route_t *route = (ts_http_route_t *)req->user_ctx;
    if (!route || !route->handler) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No handler");
        return ESP_FAIL;
    }
    
    ts_http_request_t ts_req = {
        .req = req,
        .uri = req->uri,
        .method = req->method,
        .body = NULL,
        .body_len = 0
    };
    
    // Read body if present - must loop to receive all data
    if (req->content_len > 0) {
        ts_req.body = malloc(req->content_len + 1);
        if (ts_req.body) {
            size_t total_received = 0;
            while (total_received < req->content_len) {
                int ret = httpd_req_recv(req, ts_req.body + total_received, 
                                         req->content_len - total_received);
                if (ret <= 0) {
                    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                        // Timeout, continue trying
                        continue;
                    }
                    // Error or connection closed
                    TS_LOGW(TAG, "Body recv error at %zu/%zu bytes: %d", 
                            total_received, (size_t)req->content_len, ret);
                    break;
                }
                total_received += ret;
            }
            ts_req.body[total_received] = '\0';
            ts_req.body_len = total_received;
            
            if (total_received != req->content_len) {
                TS_LOGW(TAG, "Incomplete body: got %zu of %zu bytes", 
                        total_received, (size_t)req->content_len);
            }
        } else {
            TS_LOGE(TAG, "Failed to allocate %zu bytes for body", (size_t)req->content_len);
        }
    }
    
    esp_err_t result = route->handler(&ts_req, route->user_data);
    
    free(ts_req.body);
    return result;
}

esp_err_t ts_http_server_register_route(const ts_http_route_t *route)
{
    if (!s_server || !route) return ESP_ERR_INVALID_STATE;
    
    // Allocate persistent copy of route
    ts_http_route_t *route_copy = malloc(sizeof(ts_http_route_t));
    if (!route_copy) return ESP_ERR_NO_MEM;
    *route_copy = *route;
    
    httpd_uri_t uri = {
        .uri = route->uri,
        .method = route->method,
        .handler = http_handler_wrapper,
        .user_ctx = route_copy
    };
    
    return httpd_register_uri_handler(s_server, &uri);
}

esp_err_t ts_http_server_unregister_route(const char *uri, ts_http_method_t method)
{
    if (!s_server || !uri) return ESP_ERR_INVALID_STATE;
    return httpd_unregister_uri_handler(s_server, uri, method);
}

esp_err_t ts_http_send_response(ts_http_request_t *req, int status,
                                 const char *content_type, const char *body)
{
    if (!req || !req->req) return ESP_ERR_INVALID_ARG;
    
    httpd_resp_set_status(req->req, status == 200 ? "200 OK" :
                                    status == 201 ? "201 Created" :
                                    status == 400 ? "400 Bad Request" :
                                    status == 401 ? "401 Unauthorized" :
                                    status == 403 ? "403 Forbidden" :
                                    status == 404 ? "404 Not Found" :
                                    status == 500 ? "500 Internal Server Error" : "200 OK");
    
    if (content_type) {
        httpd_resp_set_type(req->req, content_type);
    }
    
    return httpd_resp_send(req->req, body, body ? strlen(body) : 0);
}

esp_err_t ts_http_send_json(ts_http_request_t *req, int status, const char *json)
{
    return ts_http_send_response(req, status, "application/json", json);
}

esp_err_t ts_http_send_file(ts_http_request_t *req, const char *filepath)
{
    if (!req || !filepath) return ESP_ERR_INVALID_ARG;
    
    ssize_t size = ts_storage_size(filepath);
    if (size < 0) {
        return ts_http_send_error(req, 404, "File not found");
    }
    
    char *buf = malloc(size);
    if (!buf) {
        return ts_http_send_error(req, 500, "Memory allocation failed");
    }
    
    if (ts_storage_read_file(filepath, buf, size) != size) {
        free(buf);
        return ts_http_send_error(req, 500, "Failed to read file");
    }
    
    // Detect content type
    const char *ext = strrchr(filepath, '.');
    const char *content_type = "application/octet-stream";
    if (ext) {
        if (strcmp(ext, ".html") == 0) content_type = "text/html";
        else if (strcmp(ext, ".css") == 0) content_type = "text/css";
        else if (strcmp(ext, ".js") == 0) content_type = "application/javascript";
        else if (strcmp(ext, ".json") == 0) content_type = "application/json";
        else if (strcmp(ext, ".png") == 0) content_type = "image/png";
        else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) content_type = "image/jpeg";
        else if (strcmp(ext, ".svg") == 0) content_type = "image/svg+xml";
        else if (strcmp(ext, ".ico") == 0) content_type = "image/x-icon";
    }
    
    httpd_resp_set_type(req->req, content_type);
    esp_err_t ret = httpd_resp_send(req->req, buf, size);
    
    free(buf);
    return ret;
}

esp_err_t ts_http_send_error(ts_http_request_t *req, int status, const char *message)
{
    char json[128];
    snprintf(json, sizeof(json), "{\"error\":\"%s\"}", message ? message : "Unknown error");
    return ts_http_send_json(req, status, json);
}

esp_err_t ts_http_get_query_param(ts_http_request_t *req, const char *key,
                                   char *value, size_t max_len)
{
    if (!req || !req->req || !key || !value) return ESP_ERR_INVALID_ARG;
    
    size_t buf_len = httpd_req_get_url_query_len(req->req) + 1;
    if (buf_len <= 1) return ESP_ERR_NOT_FOUND;
    
    char *buf = malloc(buf_len);
    if (!buf) return ESP_ERR_NO_MEM;
    
    esp_err_t ret = httpd_req_get_url_query_str(req->req, buf, buf_len);
    if (ret == ESP_OK) {
        ret = httpd_query_key_value(buf, key, value, max_len);
    }
    
    free(buf);
    return ret;
}

esp_err_t ts_http_get_header(ts_http_request_t *req, const char *key,
                              char *value, size_t max_len)
{
    if (!req || !req->req || !key || !value) return ESP_ERR_INVALID_ARG;
    
    size_t len = httpd_req_get_hdr_value_len(req->req, key);
    if (len == 0) return ESP_ERR_NOT_FOUND;
    
    return httpd_req_get_hdr_value_str(req->req, key, value, max_len);
}

esp_err_t ts_http_set_cors(ts_http_request_t *req, const char *origin)
{
    if (!req || !req->req) return ESP_ERR_INVALID_ARG;
    
    httpd_resp_set_hdr(req->req, "Access-Control-Allow-Origin", origin ? origin : "*");
    httpd_resp_set_hdr(req->req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req->req, "Access-Control-Allow-Headers", "Content-Type, Authorization");
    
    return ESP_OK;
}

httpd_handle_t ts_http_server_get_handle(void)
{
    return s_server;
}
