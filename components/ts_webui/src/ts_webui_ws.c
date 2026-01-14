/**
 * @file ts_webui_ws.c
 * @brief WebSocket Implementation
 */

#include "ts_webui.h"
#include "ts_log.h"
#include "ts_event.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <string.h>

#define TAG "webui_ws"

#ifdef CONFIG_TS_WEBUI_WS_MAX_CLIENTS
#define MAX_WS_CLIENTS CONFIG_TS_WEBUI_WS_MAX_CLIENTS
#else
#define MAX_WS_CLIENTS 4
#endif

typedef struct {
    bool active;
    int fd;
    httpd_handle_t hd;
} ws_client_t;

static ws_client_t s_clients[MAX_WS_CLIENTS];
static httpd_handle_t s_server = NULL;

static void add_client(httpd_handle_t hd, int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!s_clients[i].active) {
            s_clients[i].active = true;
            s_clients[i].fd = fd;
            s_clients[i].hd = hd;
            TS_LOGI(TAG, "WebSocket client connected (fd=%d)", fd);
            return;
        }
    }
    TS_LOGW(TAG, "No free WebSocket slots");
}

static void remove_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            s_clients[i].active = false;
            TS_LOGI(TAG, "WebSocket client disconnected (fd=%d)", fd);
            return;
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        TS_LOGI(TAG, "WebSocket handshake");
        return ESP_OK;
    }
    
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    // Get frame info
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (ws_pkt.len == 0) {
        return ESP_OK;
    }
    
    // Allocate buffer for payload
    uint8_t *buf = malloc(ws_pkt.len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;
    
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }
    buf[ws_pkt.len] = '\0';
    
    TS_LOGD(TAG, "WS recv: %s", (char *)buf);
    
    // Parse message
    cJSON *msg = cJSON_Parse((char *)buf);
    free(buf);
    
    if (msg) {
        cJSON *type = cJSON_GetObjectItem(msg, "type");
        if (type && cJSON_IsString(type)) {
            if (strcmp(type->valuestring, "ping") == 0) {
                // Respond to ping
                cJSON *pong = cJSON_CreateObject();
                cJSON_AddStringToObject(pong, "type", "pong");
                char *pong_str = cJSON_PrintUnformatted(pong);
                cJSON_Delete(pong);
                
                ws_pkt.payload = (uint8_t *)pong_str;
                ws_pkt.len = strlen(pong_str);
                httpd_ws_send_frame(req, &ws_pkt);
                free(pong_str);
            }
            else if (strcmp(type->valuestring, "subscribe") == 0) {
                // Add client to broadcast list
                add_client(req->handle, httpd_req_to_sockfd(req));
            }
        }
        cJSON_Delete(msg);
    }
    
    return ESP_OK;
}

esp_err_t ts_webui_ws_init(void)
{
    TS_LOGI(TAG, "Initializing WebSocket");
    
    memset(s_clients, 0, sizeof(s_clients));
    
    // Note: WebSocket registration requires the httpd_handle_t
    // This would typically be done through ts_http_server
    // For now, WebSocket is handled via esp_http_server directly
    
    return ESP_OK;
}

esp_err_t ts_webui_broadcast(const char *message)
{
    if (!message) return ESP_ERR_INVALID_ARG;
    
    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)message,
        .len = strlen(message)
    };
    
    int sent = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active) {
            esp_err_t ret = httpd_ws_send_frame_async(s_clients[i].hd, s_clients[i].fd, &ws_pkt);
            if (ret == ESP_OK) {
                sent++;
            } else {
                // Client probably disconnected
                s_clients[i].active = false;
            }
        }
    }
    
    TS_LOGD(TAG, "Broadcast to %d clients", sent);
    return ESP_OK;
}

esp_err_t ts_webui_broadcast_event(const char *event_type, const char *data)
{
    if (!event_type) return ESP_ERR_INVALID_ARG;
    
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "event");
    cJSON_AddStringToObject(msg, "event", event_type);
    if (data) {
        cJSON *data_obj = cJSON_Parse(data);
        if (data_obj) {
            cJSON_AddItemToObject(msg, "data", data_obj);
        } else {
            cJSON_AddStringToObject(msg, "data", data);
        }
    }
    
    char *json = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    
    if (json) {
        esp_err_t ret = ts_webui_broadcast(json);
        free(json);
        return ret;
    }
    
    return ESP_ERR_NO_MEM;
}
