#include "ts_http_url.h"
#include <string.h>
#include <strings.h>

static bool parse_url(const char *url, struct http_parser_url *parsed, bool websocket)
{
    if (!url || !parsed) return false;
    size_t length = strnlen(url, TS_HTTP_URL_LIMIT + 1);
    if (!length || length > TS_HTTP_URL_LIMIT) return false;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)url[i] <= 0x20 || (unsigned char)url[i] == 0x7f || url[i] == '\\') return false;
    http_parser_url_init(parsed);
    if (http_parser_parse_url(url, length, 0, parsed) ||
        !(parsed->field_set & (1U << UF_SCHEMA)) || !(parsed->field_set & (1U << UF_HOST)) ||
        ((parsed->field_set & (1U << UF_PORT)) && !parsed->port)) return false;
    unsigned start = parsed->field_data[UF_SCHEMA].off, size = parsed->field_data[UF_SCHEMA].len;
    if (websocket) return !(parsed->field_set & (1U << UF_FRAGMENT)) &&
        ((size == 2 && !strncasecmp(url + start, "ws", 2)) ||
         (size == 3 && !strncasecmp(url + start, "wss", 3)));
    return (size == 4 && !strncasecmp(url + start, "http", 4)) ||
        (size == 5 && !strncasecmp(url + start, "https", 5));
}

bool ts_http_url_parse(const char *url, struct http_parser_url *parsed)
{
    return parse_url(url, parsed, false);
}

bool ts_ws_url_parse(const char *url, struct http_parser_url *parsed)
{
    return parse_url(url, parsed, true);
}

bool ts_http_url_is_local_api(const char *url, const struct http_parser_url *parsed)
{
    unsigned start = parsed->field_data[UF_HOST].off, length = parsed->field_data[UF_HOST].len;
    bool local = (length == 9 && !strncasecmp(url + start, "localhost", 9)) ||
        (length == 9 && !memcmp(url + start, "127.0.0.1", 9)) ||
        (length == 3 && !memcmp(url + start, "::1", 3));
    unsigned path = parsed->field_data[UF_PATH].off, size = parsed->field_data[UF_PATH].len;
    return local && size >= 8 && !memcmp(url + path, "/api/v1/", 8);
}
