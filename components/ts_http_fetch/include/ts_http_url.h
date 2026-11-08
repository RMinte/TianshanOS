#pragma once
#include <stdbool.h>
#include "http_parser.h"

#define TS_HTTP_URL_LIMIT 2048U
bool ts_http_url_parse(const char *url, struct http_parser_url *parsed);
/* WS/WSS use the same bounded SDK parser; fragments are not a WS target. */
bool ts_ws_url_parse(const char *url, struct http_parser_url *parsed);
/* parsed must be a successful parse of this same URL. */
bool ts_http_url_is_local_api(const char *url, const struct http_parser_url *parsed);
