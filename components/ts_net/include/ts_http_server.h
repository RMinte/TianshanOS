/**
 * @file ts_http_server.h
 * @brief HTTP/HTTPS Server
 */

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Seven accepted clients share the system socket budget. Reserve
 * two non-upgraded sessions for page/API traffic; the HTTPD build policy keeps
 * upgraded WebSockets out of LRU selection. */
#define TS_HTTP_MAX_CLIENTS 7
#define TS_HTTP_RESERVED_CLIENTS 2
#define TS_HTTP_MAX_WS_CLIENTS (TS_HTTP_MAX_CLIENTS - TS_HTTP_RESERVED_CLIENTS)

/* HTTPD owner only; run immediately after upgrade, before registering a peer. */
esp_err_t ts_http_server_admit_ws(httpd_req_t *req, unsigned client_limit);

/** HTTP method */
typedef enum {
    TS_HTTP_GET = HTTP_GET,
    TS_HTTP_POST = HTTP_POST,
    TS_HTTP_PUT = HTTP_PUT,
    TS_HTTP_DELETE = HTTP_DELETE,
    TS_HTTP_PATCH = HTTP_PATCH
} ts_http_method_t;

#define TS_HTTP_ORIGIN_MAX_BYTES 288

/** HTTP request context */
typedef struct {
    httpd_req_t *req;
    const char *uri;
    ts_http_method_t method;
    char *body;
    size_t body_len;
    char cors_origin[TS_HTTP_ORIGIN_MAX_BYTES]; /**< Response header lifetime is this request. */
} ts_http_request_t;

/** HTTP handler callback */
typedef esp_err_t (*ts_http_handler_t)(ts_http_request_t *req, void *user_data);

/** Admission runs before body allocation/receive. A rejection must send its
 * response and return false; transport closes the unread request. */
typedef bool (*ts_http_admission_t)(ts_http_request_t *req, void *user_data);

/** Route registration */
typedef struct {
    const char *uri;
    ts_http_method_t method;
    ts_http_handler_t handler;
    void *user_data;
    bool requires_auth;
    uint8_t required_level; /**< Nonzero also requires auth; 0 retains legacy READ minimum. */
    ts_http_admission_t admit; /**< Optional route-owned dynamic pre-body policy. */
    size_t max_body_bytes; /**< Zero selects 1 MiB; larger streams require explicit policy. */
    uint32_t body_timeout_ms; /**< Zero selects 30 seconds; SDK maximum is 300 seconds. */
} ts_http_route_t;

/** Shared Host/Origin policy. SDK dispatch invokes the strong admission hook
 * before route lookup, WS upgrade, authorization or application body allocation.
 * Optional origin output belongs to the caller and must survive response send. */
esp_err_t ts_http_validate_browser_request(httpd_req_t *req, char *origin, size_t capacity);
esp_err_t ts_httpd_admit_request(httpd_req_t *req);

/** Live route authorization before body allocation and again before dispatch.
 * Sends rejection; caller frees a received body and closes on failure. */
bool ts_http_authorize_route(ts_http_request_t *req, const ts_http_route_t *route);

/** Receive the complete body into an initially empty context, or send an error.
 * On failure the transport must close; on success the caller owns/free()s body.
 */
esp_err_t ts_http_receive_body(ts_http_request_t *req);
/* Validate declared bytes before allocation and select this request's one-time
 * body budget. SDK header/frame reads have an independent 15 second budget.
 * Bounds are checked before/after each recv; one socket read timeout may overrun. */
esp_err_t ts_http_admit_body(ts_http_request_t *req, const ts_http_route_t *route);
void ts_httpd_set_body_receive_budget(uint32_t milliseconds);

/**
 * @brief Initialize HTTP server
 */
esp_err_t ts_http_server_init(void);

/**
 * @brief Deinitialize HTTP server
 */
esp_err_t ts_http_server_deinit(void);

/**
 * @brief Start HTTP server
 */
esp_err_t ts_http_server_start(void);



/**
 * @brief Stop HTTP server
 */
esp_err_t ts_http_server_stop(void);

/**
 * @brief Register route
 */
esp_err_t ts_http_server_register_route(const ts_http_route_t *route);

/**
 * @brief Unregister route
 */
esp_err_t ts_http_server_unregister_route(const char *uri, ts_http_method_t method);

/**
 * @brief Sync all registered routes to HTTPS server
 * @note Call this function after HTTPS server starts to register all routes
 */
esp_err_t ts_http_server_sync_routes_to_https(void);

/**
 * @brief Send response
 */
esp_err_t ts_http_send_response(ts_http_request_t *req, int status, 
                                 const char *content_type, const char *body);

/**
 * @brief Send JSON response
 */
esp_err_t ts_http_send_json(ts_http_request_t *req, int status, const char *json);

/**
 * @brief Send file response
 */
esp_err_t ts_http_send_file(ts_http_request_t *req, const char *filepath);

/**
 * @brief Send an API file download with Cache-Control: no-store.
 * File bytes, content type, gzip and streaming behavior match ts_http_send_file.
 */
esp_err_t ts_http_send_private_file(ts_http_request_t *req, const char *filepath);

/**
 * @brief Send error response
 */
esp_err_t ts_http_send_error(ts_http_request_t *req, int status, const char *message);

/**
 * @brief Get query parameter
 */
esp_err_t ts_http_get_query_param(ts_http_request_t *req, const char *key, 
                                   char *value, size_t max_len);

/**
 * @brief Get header value
 */
esp_err_t ts_http_get_header(ts_http_request_t *req, const char *key,
                              char *value, size_t max_len);

/**
 * @brief Set CORS headers
 */
esp_err_t ts_http_set_cors(ts_http_request_t *req, const char *origin);

/**
 * @brief Get the underlying httpd handle for advanced operations
 * @return httpd_handle_t or NULL if not started
 */
httpd_handle_t ts_http_server_get_handle(void);

/* Owner hooks: before_stop must drain users; failure preserves the live handle. */
void ts_http_server_set_stop_hooks(esp_err_t (*before)(httpd_handle_t),
                                   void (*after)(httpd_handle_t));

bool ts_http_server_is_stopping(void);

#ifdef __cplusplus
}
#endif
