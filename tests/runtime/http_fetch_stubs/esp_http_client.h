#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define ESP_ERR_HTTP_EAGAIN 0x7001
typedef struct fixture_client *esp_http_client_handle_t;
typedef enum {HTTP_METHOD_GET,HTTP_METHOD_POST,HTTP_METHOD_PUT,HTTP_METHOD_DELETE} esp_http_client_method_t;
enum {HTTP_EVENT_ON_CONNECTED,HTTP_EVENT_DISCONNECTED,HTTP_EVENT_ERROR,HTTP_EVENT_HEADERS_SENT,HTTP_EVENT_ON_HEADER,
      HTTP_EVENT_REDIRECT,HTTP_EVENT_ON_DATA,HTTP_EVENT_ON_FINISH};
typedef struct {int event_id;esp_http_client_handle_t client;void *user_data,*data;int data_len;char *header_key,*header_value;} esp_http_client_event_t;
typedef struct {
    const char *url,*cert_pem,*common_name,*username,*password;
    int timeout_ms,buffer_size,buffer_size_tx;
    esp_http_client_method_t method;
    bool is_async,disable_auto_redirect,skip_cert_common_name_check;
    esp_err_t(*event_handler)(esp_http_client_event_t*);
    void *user_data;
    int(*crt_bundle_attach)(void*);
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t,const char*,const char*);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t,int);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t,const char*,int);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
esp_err_t esp_http_client_perform(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int64_t esp_http_client_get_content_length(esp_http_client_handle_t);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
