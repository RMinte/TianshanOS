#include "ts_http_fetch.h"
#include "ts_http_url.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define FETCH_BODY_LIMIT (64U * 1024U)
#define FETCH_HEADER_LIMIT 4096U

typedef struct {
    const ts_http_fetch_config_t *request;
    ts_http_fetch_result_t *result;
    int64_t deadline, started;
    size_t received;
    bool acknowledged, redirected;
    char *location, *username, *password;
    esp_err_t error, location_error;
} fetch_t;

static esp_err_t current(fetch_t *owner)
{
    if (owner->request->current && !owner->request->current(owner->request->context)) return ESP_ERR_INVALID_STATE;
    return esp_timer_get_time() < owner->deadline ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void stop(fetch_t *owner, esp_http_client_handle_t client, esp_err_t error)
{
    if (owner->error == ESP_OK) owner->error = error;
    esp_http_client_close(client);
}

static esp_err_t receive(esp_http_client_event_t *event)
{
    fetch_t *owner = event->user_data;
    if (event->event_id == HTTP_EVENT_DISCONNECTED || event->event_id == HTTP_EVENT_ERROR) return ESP_OK;
    if (owner->error != ESP_OK) return owner->error;
    esp_err_t allowed = current(owner);
    if (allowed != ESP_OK) { stop(owner, event->client, allowed); return allowed; }
    if (event->event_id == HTTP_EVENT_ON_CONNECTED) {
        owner->result->connected = true;
        owner->result->connect_time_ms = (esp_timer_get_time() - owner->started) / 1000 - owner->result->dns_time_ms;
    } else if (event->event_id == HTTP_EVENT_HEADERS_SENT) {
        /* Basic authentication may retry on this handle. Only the final body is
         * returned; the cumulative wire budget and deadline never reset. */
        owner->result->length = 0;
        if (owner->result->body) owner->result->body[0] = 0;
        owner->acknowledged = owner->redirected = false;
        free(owner->location); owner->location = NULL;
        owner->location_error = ESP_OK;
    } else if (event->event_id == HTTP_EVENT_ON_HEADER && owner->request->follow_redirects &&
               event->header_key && !strcasecmp(event->header_key, "Location")) {
        const char *value = event->header_value;
        size_t length = value ? strnlen(value, TS_HTTP_URL_LIMIT + 1) : 0;
        free(owner->location); owner->location = NULL;
        /* SDK publishes the response status after ON_HEADER. Defer any
         * Location error until an actual REDIRECT; 2xx Location is ordinary. */
        owner->location_error = ESP_OK;
        if (!length || length > TS_HTTP_URL_LIMIT) owner->location_error = ESP_ERR_INVALID_RESPONSE;
        else {
            owner->location = strndup(value, length);
            if (!owner->location) owner->location_error = ESP_ERR_NO_MEM;
        }
    } else if (event->event_id == HTTP_EVENT_REDIRECT && owner->request->follow_redirects) {
        if (owner->location_error != ESP_OK || !owner->location) {
            stop(owner, event->client, owner->location_error != ESP_OK ? owner->location_error : ESP_ERR_INVALID_RESPONSE);
            return owner->error;
        }
        owner->redirected = true;
        /* End this hop without draining an arbitrary redirect response body. */
        esp_http_client_close(event->client);
    } else if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        owner->result->status = esp_http_client_get_status_code(event->client);
        if (owner->request->headers_only && owner->result->status >= 200 && owner->result->status < 300) {
            owner->acknowledged = true;
            esp_http_client_close(event->client);
            return ESP_OK;
        }
        if ((size_t)event->data_len > owner->request->maximum_body - owner->received) {
            stop(owner, event->client, ESP_ERR_INVALID_SIZE);
            return owner->error;
        }
        owner->received += (size_t)event->data_len;
        if (esp_http_client_get_content_length(event->client) > (int64_t)owner->request->maximum_body) {
            stop(owner, event->client, ESP_ERR_INVALID_SIZE);
            return owner->error;
        }
        if (owner->result->body) {
            memcpy(owner->result->body + owner->result->length, event->data, (size_t)event->data_len);
            owner->result->length += (size_t)event->data_len;
            owner->result->body[owner->result->length] = 0;
        }
    } else if (event->event_id == HTTP_EVENT_ON_FINISH) {
        owner->result->status = esp_http_client_get_status_code(event->client);
        owner->acknowledged = owner->result->status >= 200 && owner->result->status < 300;
    }
    return ESP_OK;
}

static bool header_text(const char *text, bool name, size_t *bytes)
{
    if (!text) return false;
    size_t length = strnlen(text, FETCH_HEADER_LIMIT + 1);
    if ((name && !length) || length > FETCH_HEADER_LIMIT || length > FETCH_HEADER_LIMIT - *bytes) return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        bool token = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || strchr("!#$%&'*+-.^_`|~", c);
        if (c < 0x20 || c == 0x7f || (name && !token)) return false;
    }
    *bytes += length;
    return true;
}

static bool headers_valid(const ts_http_fetch_config_t *request)
{
    size_t bytes = 0, count = 0;
    if (request->authorization && !header_text(request->authorization, false, &bytes)) return false;
    if (request->content_type && !header_text(request->content_type, false, &bytes)) return false;
    if (!request->headers) return true;
    if (!cJSON_IsObject(request->headers)) return false;
    const cJSON *header;
    cJSON_ArrayForEach(header, request->headers) {
        if (++count > 16 || !cJSON_IsString(header) || !header_text(header->string, true, &bytes) ||
            !header_text(header->valuestring, false, &bytes) ||
            !strcasecmp(header->string, "Host") || !strcasecmp(header->string, "Content-Length") ||
            !strcasecmp(header->string, "Transfer-Encoding") || !strcasecmp(header->string, "Connection")) return false;
    }
    return true;
}

/* SDK set_url keeps credentials when Location omits userinfo. A username-only
 * replacement also retains the prior password. Keep that existing contract. */
static esp_err_t credentials(fetch_t *owner, const char *user_info)
{
    const char *colon = strchr(user_info, ':');
    char *username = colon ? strndup(user_info, (size_t)(colon - user_info)) : strdup(user_info);
    char *password = colon ? strdup(colon + 1) : NULL;
    if (!username || (colon && !password)) { free(username); free(password); return ESP_ERR_NO_MEM; }
    free(owner->username); owner->username = username;
    if (colon) { free(owner->password); owner->password = password; }
    return ESP_OK;
}

static esp_err_t redirect_url(const char *base, const char *location, char **next)
{
    struct http_parser_url parsed;
    size_t prefix = 0, length = strlen(location);
    if (!ts_http_url_parse(location, &parsed)) {
        /* SDK accepts root-relative paths, but rejects bare relative names.
         * Preserve the logical authority and query, never the resolved IP URL. */
        if (location[0] != '/') return ESP_ERR_INVALID_ARG;
        const char *authority = strstr(base, "://") + 3;
        prefix = (size_t)(authority - base) + strcspn(authority, "/?#");
    }
    if (prefix + length > TS_HTTP_URL_LIMIT) return ESP_ERR_INVALID_SIZE;
    char *url = malloc(prefix + length + 1);
    if (!url) return ESP_ERR_NO_MEM;
    memcpy(url, base, prefix); memcpy(url + prefix, location, length + 1);
    if (!ts_http_url_parse(url, &parsed)) { free(url); return ESP_ERR_INVALID_ARG; }
    *next = url;
    return ESP_OK;
}

static esp_err_t fetch_hop(fetch_t *owner, const char *url)
{
    const ts_http_fetch_config_t *request = owner->request;
    ts_http_fetch_result_t *result = owner->result;
    esp_err_t error;
    owner->acknowledged = owner->redirected = false;
    free(owner->location); owner->location = NULL;
    result->length = 0;
    if (result->body) result->body[0] = 0;
    ts_http_target_t target = {0};
    error = ts_http_target_resolve(url, owner->deadline, request->current, request->context, &target);
    esp_http_client_handle_t client = NULL;
    result->dns_time_ms = (esp_timer_get_time() - owner->started) / 1000;
    if (error != ESP_OK) goto done;
    result->dns_ok = true;
    struct http_parser_url numeric;
    if (!ts_http_url_parse(target.url, &numeric) || numeric.field_data[UF_HOST].len >= sizeof(result->resolved_ip)) {
        error = ESP_ERR_INVALID_ARG;
        goto done;
    }
    memcpy(result->resolved_ip, target.url + numeric.field_data[UF_HOST].off, numeric.field_data[UF_HOST].len);
    result->resolved_ip[numeric.field_data[UF_HOST].len] = 0;
    if (target.user_info) {
        error = credentials(owner, target.user_info);
        if (error != ESP_OK) goto done;
    }
    esp_http_client_config_t config = {.url = target.url, .method = request->method,
        .is_async = true, .timeout_ms = 1, .disable_auto_redirect = true,
        .buffer_size = 2048, .buffer_size_tx = 1024,
        .event_handler = receive, .user_data = owner,
        .cert_pem = request->cert_pem, .common_name = target.secure ? target.hostname : NULL,
        .crt_bundle_attach = request->cert_pem ? NULL : esp_crt_bundle_attach,
        .username = owner->username, .password = owner->password};
    client = esp_http_client_init(&config);
    if (!client) { error = ESP_ERR_NO_MEM; goto done; }
    error = esp_http_client_set_header(client, "Host", target.authority);
    if (error == ESP_OK) error = esp_http_client_set_timeout_ms(client, 0);
    if (error == ESP_OK && request->authorization)
        error = esp_http_client_set_header(client, "Authorization", request->authorization);
    if (error == ESP_OK && request->content_type)
        error = esp_http_client_set_header(client, "Content-Type", request->content_type);
    const cJSON *header;
    cJSON_ArrayForEach(header, request->headers) {
        if (error != ESP_OK) break;
        error = esp_http_client_set_header(client, header->string, header->valuestring);
    }
    if (error == ESP_OK && request->body)
        error = esp_http_client_set_post_field(client, request->body, (int)strlen(request->body));
    if (error == ESP_OK) {
        do {
            error = current(owner);
            if (error != ESP_OK) break;
            error = esp_http_client_perform(client);
            if (owner->error != ESP_OK) { error = owner->error; break; }
            if (owner->redirected) { error = ESP_OK; break; }
            if (request->headers_only && owner->acknowledged) { error = ESP_OK; break; }
            if (error == ESP_ERR_HTTP_EAGAIN) vTaskDelay(1);
        } while (error == ESP_ERR_HTTP_EAGAIN);
    }
    if (error == ESP_OK) error = current(owner);
    result->status = esp_http_client_get_status_code(client);
    result->content_length = esp_http_client_get_content_length(client);
    if (error == ESP_OK && !owner->redirected && !request->headers_only && !esp_http_client_is_complete_data_received(client))
        error = ESP_ERR_INVALID_RESPONSE;
done:
    if (client) esp_http_client_cleanup(client);
    ts_http_target_free(&target);
    return error;
}

esp_err_t ts_http_fetch(const ts_http_fetch_config_t *request, ts_http_fetch_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    if (!request || !request->timeout_ms || request->timeout_ms > 30000 ||
        !request->maximum_body || request->maximum_body > FETCH_BODY_LIMIT ||
        (request->method != HTTP_METHOD_GET && request->method != HTTP_METHOD_POST &&
         request->method != HTTP_METHOD_PUT && request->method != HTTP_METHOD_DELETE) ||
        (request->body && strnlen(request->body, FETCH_HEADER_LIMIT + 1) > FETCH_HEADER_LIMIT) ||
        !headers_valid(request)) return ESP_ERR_INVALID_ARG;
    struct http_parser_url parsed;
    if (!ts_http_url_parse(request->url, &parsed) ||
        (request->cert_pem && (parsed.field_data[UF_SCHEMA].len != 5 ||
            strnlen(request->cert_pem, 16385) > 16384))) return ESP_ERR_INVALID_ARG;
    int64_t started = esp_timer_get_time();
    fetch_t owner = {.request = request, .result = result, .started = started,
        .deadline = started + (int64_t)request->timeout_ms * 1000};
    esp_err_t error = current(&owner);
    if (error != ESP_OK) return error;
    if (!request->headers_only) {
        result->body = heap_caps_malloc(request->maximum_body + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!result->body) result->body = malloc(request->maximum_body + 1);
        if (!result->body) return ESP_ERR_NO_MEM;
        result->body[0] = 0;
    }
    char *redirected_url = NULL;
    const char *url = request->url;
    for (unsigned redirects = 0;; ++redirects) {
        error = current(&owner);
        if (error != ESP_OK) break;
        error = fetch_hop(&owner, url);
        if (error != ESP_OK || !owner.redirected) break;
        if (redirects == 10) { error = ESP_ERR_HTTP_MAX_REDIRECT; break; }
        char *next = NULL;
        error = redirect_url(url, owner.location, &next);
        if (error != ESP_OK) break;
        free(redirected_url); redirected_url = next; url = next;
    }
    free(redirected_url); free(owner.location); free(owner.username); free(owner.password);
    if (error != ESP_OK) {
        free(result->body); result->body = NULL; result->length = 0;
    }
    return error;
}
