#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_client.h"
#include "cJSON.h"

/* One resolved hop. Address pinning does not replace Host/SNI verification. */
typedef struct {
    char *url, *hostname, *authority, *user_info;
    bool secure;
} ts_http_target_t;
esp_err_t ts_http_target_resolve(const char *url, int64_t deadline_us,
    bool (*current)(void *), void *context, ts_http_target_t *target);
void ts_http_target_free(ts_http_target_t *target);

typedef struct {
    const char *url, *cert_pem, *authorization, *content_type, *body;
    const cJSON *headers;
    esp_http_client_method_t method;
    unsigned timeout_ms;
    size_t maximum_body;
    /* Webhooks need only a complete HTTP status/header acknowledgment. */
    bool headers_only;
    bool (*current)(void *);
    void *context;
} ts_http_fetch_config_t;
typedef struct {
    int status;
    int64_t content_length;
    /* Actual connectivity phases, consumed by ota.test_connection. */
    bool dns_ok, connected;
    int64_t dns_time_ms, connect_time_ms;
    char resolved_ip[64];
    char *body;
    size_t length;
} ts_http_fetch_result_t;
/* Worker entry; callers enforce permission/configuration authority.
 * Nonblocking SDK polls; no new task or global job.
 * DNS, connect, headers and body share one deadline. Redirects are never followed.
 * On success the caller owns body; failure clears it. Release with free(). */
esp_err_t ts_http_fetch(const ts_http_fetch_config_t *request, ts_http_fetch_result_t *result);
