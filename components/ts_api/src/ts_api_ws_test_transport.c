#include "ts_api_ws_test_transport.h"
#include "ts_http_url.h"
#include "ts_dns.h"
#include "esp_transport_tcp.h"
#include "esp_transport_ssl.h"
#include "esp_transport_ws.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* Use the same nonblocking SDK primitives as the existing HTTP owner. Neither
 * a stalled DNS lookup nor another partial frame may renew the operation. */
static int64_t call_deadline(ts_api_ws_test_transport_t *owner, int timeout_ms)
{
    if (timeout_ms < 0) return owner->deadline;
    int64_t requested = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    return requested < owner->deadline ? requested : owner->deadline;
}

static int wait_ready(ts_api_ws_test_transport_t *owner, bool writing, int64_t until)
{
    for (;;) {
        if (esp_timer_get_time() >= owner->deadline) { errno = ETIMEDOUT; return -1; }
        int ready = writing ? esp_transport_poll_write(owner->socket, 0) :
                              esp_transport_poll_read(owner->socket, 0);
        if (ready != 0) return ready;
        if (esp_timer_get_time() >= until) return 0;
        vTaskDelay(1);
    }
}

static int bounded_connect(esp_transport_handle_t transport, const char *host, int port, int timeout_ms)
{
    ts_api_ws_test_transport_t *owner = esp_transport_get_context_data(transport);
    int64_t until = call_deadline(owner, timeout_ms);
    ip_addr_t address;
    if (ts_dns_resolve(host, until, NULL, NULL, &address) != ESP_OK) return -1;
    char numeric[64];
    if (!ipaddr_ntoa_r(&address, numeric, sizeof(numeric))) return -1;
    while (esp_timer_get_time() < until) {
        int ret = esp_transport_connect_async(owner->socket, numeric, port, 0);
        if (ret != 0) return ret > 0 && esp_timer_get_time() < until ? 0 : -1;
        vTaskDelay(1);
    }
    errno = ETIMEDOUT;
    return -1;
}

static int bounded_read(esp_transport_handle_t transport, char *buffer, int length, int timeout_ms)
{
    ts_api_ws_test_transport_t *owner = esp_transport_get_context_data(transport);
    int64_t until = call_deadline(owner, timeout_ms);
    for (;;) {
        int ready = wait_ready(owner, false, until);
        if (ready <= 0) { if (!ready) errno = EAGAIN; return -1; }
        int ret = esp_transport_read(owner->socket, buffer, length, 0);
        if (ret >= 0) return esp_timer_get_time() < owner->deadline ? ret : -1;
        if ((errno != EAGAIN && errno != EWOULDBLOCK) || esp_timer_get_time() >= until) return -1;
    }
}

static int bounded_write(esp_transport_handle_t transport, const char *buffer, int length, int timeout_ms)
{
    ts_api_ws_test_transport_t *owner = esp_transport_get_context_data(transport);
    int64_t until = call_deadline(owner, timeout_ms);
    for (;;) {
        int ready = wait_ready(owner, true, until);
        if (ready <= 0) { if (!ready) errno = EAGAIN; return -1; }
        int ret = esp_transport_write(owner->socket, buffer, length, 0);
        if (ret >= 0) return esp_timer_get_time() < owner->deadline ? ret : -1;
        if ((errno != EAGAIN && errno != EWOULDBLOCK) || esp_timer_get_time() >= until) return -1;
    }
}

static int bounded_poll_read(esp_transport_handle_t transport, int timeout_ms)
{
    ts_api_ws_test_transport_t *owner = esp_transport_get_context_data(transport);
    return wait_ready(owner, false, call_deadline(owner, timeout_ms));
}

static int bounded_poll_write(esp_transport_handle_t transport, int timeout_ms)
{
    ts_api_ws_test_transport_t *owner = esp_transport_get_context_data(transport);
    return wait_ready(owner, true, call_deadline(owner, timeout_ms));
}

static int bounded_close(esp_transport_handle_t transport)
{
    ts_api_ws_test_transport_t *owner = esp_transport_get_context_data(transport);
    return esp_transport_close(owner->socket);
}

static int bounded_destroy(esp_transport_handle_t transport)
{
    (void)transport; /* The diagnostic owner releases each handle once. */
    return 0;
}

void ts_api_ws_test_transport_destroy(ts_api_ws_test_transport_t *owner)
{
    if (owner->websocket) esp_transport_destroy(owner->websocket);
    if (owner->bounded) esp_transport_destroy(owner->bounded);
    if (owner->socket) esp_transport_destroy(owner->socket);
    memset(owner, 0, sizeof(*owner));
}

esp_err_t ts_api_ws_test_transport_init(ts_api_ws_test_transport_t *owner,
                                      const char *uri, int64_t deadline)
{
    memset(owner, 0, sizeof(*owner));
    struct http_parser_url parsed;
    if (!ts_ws_url_parse(uri, &parsed) || parsed.field_data[UF_HOST].len >= sizeof(owner->hostname))
        return ESP_ERR_INVALID_ARG;
    if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
    memcpy(owner->hostname, uri + parsed.field_data[UF_HOST].off, parsed.field_data[UF_HOST].len);
    bool secure = parsed.field_data[UF_SCHEMA].len == 3;
    owner->port = parsed.field_set & (1U << UF_PORT) ? parsed.port : secure ? 443 : 80;
    owner->deadline = deadline;
    owner->socket = secure ? esp_transport_ssl_init() : esp_transport_tcp_init();
    owner->bounded = esp_transport_init();
    esp_err_t result = ESP_ERR_NO_MEM;
    char *path = NULL, *credentials = NULL, *auth = NULL;
    if (!owner->socket || !owner->bounded) goto done;
    if (secure) {
        /* SDK borrows this name; the owner survives the joined client worker. */
        esp_transport_ssl_set_common_name(owner->socket, owner->hostname);
        esp_transport_ssl_crt_bundle_attach(owner->socket, esp_crt_bundle_attach);
    }
    esp_transport_set_context_data(owner->bounded, owner);
    esp_transport_set_func(owner->bounded, bounded_connect, bounded_read, bounded_write,
        bounded_close, bounded_poll_read, bounded_poll_write, bounded_destroy);
    owner->websocket = esp_transport_ws_init(owner->bounded);
    if (!owner->websocket) goto done;
    size_t path_len = parsed.field_data[UF_PATH].len;
    size_t query_len = parsed.field_data[UF_QUERY].len;
    path = malloc(path_len + query_len + 3);
    if (!path) goto done;
    size_t used = 0;
    if (path_len) { memcpy(path, uri + parsed.field_data[UF_PATH].off, path_len); used = path_len; }
    else path[used++] = '/';
    if (query_len) {
        path[used++] = '?';
        memcpy(path + used, uri + parsed.field_data[UF_QUERY].off, query_len); used += query_len;
    }
    path[used] = 0;
    size_t user_len = parsed.field_data[UF_USERINFO].len;
    if (user_len) {
        credentials = malloc(user_len + 2);
        if (!credentials) goto done;
        memcpy(credentials, uri + parsed.field_data[UF_USERINFO].off, user_len);
        if (!memchr(credentials, ':', user_len)) credentials[user_len++] = ':';
        credentials[user_len] = 0;
        size_t capacity = 4 * ((user_len + 2) / 3) + 1, written;
        auth = malloc(capacity + 6);
        if (!auth) goto done;
        memcpy(auth, "Basic ", 6);
        if (mbedtls_base64_encode((unsigned char *)auth + 6, capacity, &written,
                                 (const unsigned char *)credentials, user_len) != 0) {
            result = ESP_FAIL; goto done;
        }
        auth[6 + written] = 0;
    }
    const esp_transport_ws_config_t config = {.ws_path = path, .auth = auth, .propagate_control_frames = true};
    result = esp_transport_ws_set_config(owner->websocket, &config);
done:
    free(auth); free(credentials); free(path);
    if (result != ESP_OK) ts_api_ws_test_transport_destroy(owner);
    return result;
}
