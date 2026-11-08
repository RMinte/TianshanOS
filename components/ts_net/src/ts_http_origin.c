/* Preserve the published device/proxy authority while rejecting foreign browser origins. */
#include "ts_http_server.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef TS_HTTP_TRUSTED_ORIGINS
#define TS_HTTP_TRUSTED_ORIGINS ""
#endif

/* Implemented in this project's generated HTTPD parse object, not the shared SDK.
 * Distinguishes missing/empty values and refuses duplicate security headers. */
extern esp_err_t ts_httpd_get_single_header(httpd_req_t *req, const char *field,
                                           char *out, size_t capacity);

typedef struct {
    uint8_t address[16]; /* Keep inet_pton output naturally aligned. */
    char host[254];
    uint16_t port;
    int family;
    bool explicit_port;
    bool secure;
} authority_t;

static bool authority_parse(const char *text, bool secure, authority_t *out)
{
    memset(out, 0, sizeof(*out));
    out->secure = secure; out->port = secure ? 443 : 80;
    if (!text || !text[0]) return false;
    const char *host = text, *end = NULL, *port = NULL;
    if (*host == '[') {
        host++; end = strchr(host, ']');
        if (!end || (end[1] && end[1] != ':')) return false;
        if (end[1]) port = end + 2;
        out->family = AF_INET6;
    } else {
        end = strchr(host, ':');
        if (end) port = end + 1; else end = host + strlen(host);
        out->family = AF_UNSPEC;
    }
    size_t length = (size_t)(end - host);
    if (!length || length >= sizeof(out->host)) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = host[i];
        if (c <= ' ' || c >= 0x7f) return false;
        out->host[i] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : (char)c;
    }
    if (out->family == AF_INET6) {
        if (inet_pton(AF_INET6, out->host, out->address) != 1) return false;
    } else if (inet_pton(AF_INET, out->host, out->address) == 1) out->family = AF_INET;
    else {
        /* Only DNS labels; no credentials, paths, percent escapes, lists or zones. */
        size_t label = 0;
        for (size_t i = 0; i < length; ++i) {
            unsigned char c = out->host[i];
            if (c == '.') { if (!label) return false; label = 0; }
            else {
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
                if (++label > 63) return false;
            }
        }
    }
    if (port) {
        if (!*port) return false;
        unsigned value = 0;
        while (*port) {
            if (*port < '0' || *port > '9') return false;
            unsigned digit = (unsigned)(*port++ - '0');
            if (value > (65535u - digit) / 10u) return false;
            value = value * 10u + digit;
        }
        if (!value) return false;
        out->port = value; out->explicit_port = true;
    }
    return true;
}

static bool origin_parse(const char *text, authority_t *out)
{
    if (!strncasecmp(text, "http://", 7)) return authority_parse(text + 7, false, out);
    if (!strncasecmp(text, "https://", 8)) return authority_parse(text + 8, true, out);
    return false; /* Includes opaque/null origins. */
}

static bool host_equal(const authority_t *a, const authority_t *b)
{
    if (a->family != b->family) return false;
    if (a->family == AF_INET) return !memcmp(a->address, b->address, 4);
    if (a->family == AF_INET6) return !memcmp(a->address, b->address, 16);
    return !strcmp(a->host, b->host);
}
static bool origin_equal(const authority_t *a, const authority_t *b)
{
    return a->secure == b->secure && a->port == b->port && host_equal(a, b);
}

static bool configured_matches(const authority_t *host, const authority_t *origin,
                                bool *host_match, bool *origin_match)
{
    const char *at = TS_HTTP_TRUSTED_ORIGINS;
    *host_match = *origin_match = false;
    unsigned count = 0;
    if (!*at) return true;
    do {
        const char *comma = strchr(at, ',');
        size_t length = comma ? (size_t)(comma - at) : strlen(at);
        if (!length || length >= TS_HTTP_ORIGIN_MAX_BYTES || ++count > 4) return false;
        char entry[TS_HTTP_ORIGIN_MAX_BYTES];
        memcpy(entry, at, length); entry[length] = 0;
        authority_t allowed;
        if (!origin_parse(entry, &allowed)) return false;
        /* A proxy can preserve a default-port public Host without a port even
         * when its backend listener has a different scheme/port. */
        if (host_equal(host, &allowed) &&
            (host->explicit_port ? host->port == allowed.port : allowed.port == (allowed.secure ? 443 : 80))) *host_match = true;
        if (origin && origin_equal(origin, &allowed)) *origin_match = true;
        if (!comma) break;
        at = comma + 1;
    } while (true);
    return true;
}





esp_err_t ts_http_validate_browser_request(httpd_req_t *req, char *origin_out, size_t capacity)
{
    if (!req || (origin_out && !capacity)) return ESP_ERR_INVALID_ARG;
    if (origin_out) origin_out[0] = 0;
    char host_text[TS_HTTP_ORIGIN_MAX_BYTES], origin_text[TS_HTTP_ORIGIN_MAX_BYTES];
    if (ts_httpd_get_single_header(req, "Host", host_text, sizeof(host_text)) != ESP_OK) return ESP_ERR_INVALID_ARG;
    esp_err_t origin_ret = ts_httpd_get_single_header(req, "Origin", origin_text, sizeof(origin_text));
    if (origin_ret != ESP_OK && origin_ret != ESP_ERR_NOT_FOUND) return ESP_ERR_INVALID_ARG;
    bool secure = httpd_get_global_transport_ctx(req->handle) != NULL;
    bool has_origin = origin_ret == ESP_OK;
    authority_t host, origin;
    if (has_origin && !origin_parse(origin_text, &origin)) return ESP_ERR_INVALID_ARG;
    /* A trusted forwarding service can terminate TLS before this HTTP listener.
     * Host retains its public authority; compare a browser's complete authority. */
    if (!authority_parse(host_text, has_origin ? origin.secure : secure, &host)) return ESP_ERR_INVALID_ARG;
    bool configured_host, configured_origin;
    if (!configured_matches(&host, has_origin ? &origin : NULL,
                            &configured_host, &configured_origin)) return ESP_ERR_INVALID_STATE;
    if (has_origin && !configured_origin && !origin_equal(&host, &origin)) return ESP_ERR_NOT_ALLOWED;
    if (origin_out && has_origin) {
        size_t size = strlen(origin_text) + 1;
        if (size > capacity) return ESP_ERR_INVALID_SIZE;
        memcpy(origin_out, origin_text, size);
    }
    return ESP_OK;
}

/* Strong symbol called from every generated HTTPD URI dispatch, including the
 * mTLS service and raw WebSocket routes. Failure closes unread request bodies. */
esp_err_t ts_httpd_admit_request(httpd_req_t *req)
{
    esp_err_t ret = ts_http_validate_browser_request(req, NULL, 0);
    if (ret == ESP_OK) return ESP_OK;
    httpd_err_code_t status = ret == ESP_ERR_NOT_ALLOWED ? HTTPD_403_FORBIDDEN :
        ret == ESP_ERR_INVALID_STATE ? HTTPD_500_INTERNAL_SERVER_ERROR : HTTPD_400_BAD_REQUEST;
    httpd_resp_send_err(req, status, status == HTTPD_403_FORBIDDEN ? "Browser origin or host is not allowed" :
        status == HTTPD_500_INTERNAL_SERVER_ERROR ? "Browser policy unavailable" : "Invalid browser request headers");
    return ESP_FAIL;
}
