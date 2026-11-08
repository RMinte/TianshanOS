#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "esp_transport.h"

/* Private to the two one-shot API diagnostics. The SDK client borrows websocket;
 * stop/destroy the client before releasing this stack-owned transport context. */
typedef struct {
    esp_transport_handle_t socket, bounded, websocket;
    int64_t deadline;
    int port;
    char hostname[256];
} ts_api_ws_test_transport_t;

esp_err_t ts_api_ws_test_transport_init(ts_api_ws_test_transport_t *owner,
                                      const char *uri, int64_t deadline);
void ts_api_ws_test_transport_destroy(ts_api_ws_test_transport_t *owner);
