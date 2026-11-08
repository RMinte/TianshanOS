#include "ts_http_fetch.h"
#include "ts_http_url.h"
#include "ts_dns.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ts_http_target_free(ts_http_target_t *target)
{
    if (!target) return;
    free(target->url); free(target->hostname); free(target->authority); free(target->user_info);
    memset(target, 0, sizeof(*target));
}

esp_err_t ts_http_target_resolve(const char *url, int64_t deadline,
    bool (*current)(void *), void *context, ts_http_target_t *target)
{
    if (!target) return ESP_ERR_INVALID_ARG;
    memset(target, 0, sizeof(*target));
    struct http_parser_url parsed;
    if (!ts_http_url_parse(url, &parsed)) return ESP_ERR_INVALID_ARG;
    target->secure = parsed.field_data[UF_SCHEMA].len == 5;
    unsigned start = parsed.field_data[UF_HOST].off, length = parsed.field_data[UF_HOST].len;
    target->hostname = strndup(url + start, length);
    if (!target->hostname) return ESP_ERR_NO_MEM;
    ip_addr_t address;
    esp_err_t result = ts_dns_resolve(target->hostname, deadline, current, context, &address);
    if (result != ESP_OK) return result;
    char numeric[64];
    if (!ipaddr_ntoa_r(&address, numeric, sizeof(numeric))) return ESP_FAIL;
    unsigned port = parsed.field_set & (1U << UF_PORT) ? parsed.port : target->secure ? 443 : 80;
    target->authority = malloc(length + 16);
    target->url = malloc(TS_HTTP_URL_LIMIT + 96);
    if (!target->authority || !target->url) return ESP_ERR_NO_MEM;
    bool original_ipv6 = memchr(target->hostname, ':', length) != NULL;
    if (parsed.field_set & (1U << UF_PORT))
        snprintf(target->authority, length + 16, original_ipv6 ? "[%s]:%u" : "%s:%u", target->hostname, port);
    else snprintf(target->authority, length + 16, original_ipv6 ? "[%s]" : "%s", target->hostname);
    const char *origin = strstr(url, "://") + 3;
    const char *suffix = origin + strcspn(origin, "/?#");
    snprintf(target->url, TS_HTTP_URL_LIMIT + 96, IP_IS_V6(&address) ? "%s://[%s]:%u%s%s" : "%s://%s:%u%s%s",
        target->secure ? "https" : "http", numeric, port, *suffix == '/' ? "" : "/", suffix);
    if (parsed.field_set & (1U << UF_USERINFO)) {
        target->user_info = strndup(url + parsed.field_data[UF_USERINFO].off, parsed.field_data[UF_USERINFO].len);
        if (!target->user_info) return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
