/**
 * @file ts_https.c
 * @brief TianShanOS mTLS HTTPS Server Implementation
 * 
 * 使用 ESP-IDF esp_https_server 组件实现双向 TLS 认证。
 * 从 ts_cert 组件加载设备证书和私钥。
 */

#include "ts_https.h"
#include "ts_https_internal.h"
#include "ts_cert.h"
#include "esp_https_server.h"
#include "esp_tls.h"
#include "esp_log.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include <string.h>
#include <stdarg.h>
#include <time.h>

/*===========================================================================*/
/*                 esp_https_server 私有结构定义（仅用于获取 TLS 上下文）    */
/*                 来源: esp-idf/components/esp_https_server/src/https_server.c */
/*===========================================================================*/

/**
 * @brief esp_https_server 的会话传输上下文结构
 * 
 * 注意：这是 esp_https_server 的私有结构，在此复制定义是为了访问 esp_tls_t 
 * 以获取客户端证书。如果 ESP-IDF 更新此结构，可能需要同步更新。
 */
typedef struct ts_httpd_ssl_transport_ctx {
    esp_tls_t *tls;           /*!< TLS 上下文 */
    void *global_ctx;         /*!< 全局 SSL 上下文（httpd_ssl_ctx_t*，我们不直接使用）*/
} ts_httpd_ssl_transport_ctx_t;

static const char *TAG = "ts_https";

/*===========================================================================*/
/*                         Static Variables                                   */
/*===========================================================================*/

static httpd_handle_t s_server = NULL;
static bool s_initialized = false;
static ts_https_config_t s_config;

// Certificate buffers (loaded from ts_cert)
static char *s_server_cert = NULL;
static char *s_server_key = NULL;
static char *s_ca_chain = NULL;

// Endpoint registry (simple linked list)
#define MAX_ENDPOINTS 32
static ts_https_endpoint_t s_endpoints[MAX_ENDPOINTS];
static int s_endpoint_count = 0;

/*===========================================================================*/
/*                         Forward Declarations                               */
/*===========================================================================*/

static esp_err_t load_certificates(void);
static void free_certificates(void);
static esp_err_t generic_handler(httpd_req_t *req);

/*===========================================================================*/
/*                         Initialization                                     */
/*===========================================================================*/

esp_err_t ts_https_init(const ts_https_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    // Apply configuration
    if (config) {
        s_config = *config;
    } else {
        s_config = (ts_https_config_t)TS_HTTPS_CONFIG_DEFAULT();
    }
    
    // Check if PKI is ready
    ts_cert_pki_status_t pki_status;
    esp_err_t ret = ts_cert_get_status(&pki_status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get PKI status: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (pki_status.status != TS_CERT_STATUS_ACTIVATED) {
        ESP_LOGE(TAG, "PKI not activated (status=%d), cannot start HTTPS",
                 pki_status.status);
        return ESP_ERR_INVALID_STATE;
    }
    
    // Load certificates
    ret = load_certificates();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load certificates: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_endpoint_count = 0;
    s_initialized = true;
    
    ESP_LOGI(TAG, "HTTPS server initialized (port=%d, mTLS=%s)",
             s_config.port, s_config.require_client_cert ? "required" : "optional");
    
    return ESP_OK;
}

void ts_https_deinit(void)
{
    if (s_server) {
        ts_https_stop();
    }
    
    free_certificates();
    s_endpoint_count = 0;
    s_initialized = false;
    
    ESP_LOGI(TAG, "HTTPS server deinitialized");
}

/*===========================================================================*/
/*                         Certificate Loading                                */
/*===========================================================================*/

static esp_err_t load_certificates(void)
{
    esp_err_t ret;
    
    // Allocate buffers
    s_server_cert = heap_caps_malloc(TS_CERT_PEM_MAX_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_server_key = heap_caps_malloc(TS_CERT_KEY_MAX_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_ca_chain = heap_caps_malloc(TS_CERT_CA_CHAIN_MAX_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!s_server_cert || !s_server_key || !s_ca_chain) {
        ESP_LOGE(TAG, "Failed to allocate certificate buffers");
        free_certificates();
        return ESP_ERR_NO_MEM;
    }
    
    // Load server certificate
    size_t cert_len = TS_CERT_PEM_MAX_LEN;
    ret = ts_cert_get_certificate(s_server_cert, &cert_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load server certificate: %s", esp_err_to_name(ret));
        free_certificates();
        return ret;
    }
    ESP_LOGI(TAG, "Server certificate loaded (%d bytes)", (int)cert_len);
    
    // Load private key
    size_t key_len = TS_CERT_KEY_MAX_LEN;
    ret = ts_cert_get_private_key(s_server_key, &key_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load private key: %s", esp_err_to_name(ret));
        free_certificates();
        return ret;
    }
    ESP_LOGI(TAG, "Private key loaded (%d bytes)", (int)key_len);
    
    // Load CA chain (for client certificate verification)
    size_t ca_len = TS_CERT_CA_CHAIN_MAX_LEN;
    ret = ts_cert_get_ca_chain(s_ca_chain, &ca_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No CA chain loaded: %s", esp_err_to_name(ret));
        // This is okay if we don't require client certs
        if (s_config.require_client_cert) {
            ESP_LOGE(TAG, "CA chain required for mTLS but not available");
            free_certificates();
            return ret;
        }
    } else {
        ESP_LOGI(TAG, "CA chain loaded (%d bytes)", (int)ca_len);
    }
    
    return ESP_OK;
}

static void free_certificates(void)
{
    if (s_server_cert) {
        // Clear sensitive data before freeing
        memset(s_server_cert, 0, TS_CERT_PEM_MAX_LEN);
        free(s_server_cert);
        s_server_cert = NULL;
    }
    if (s_server_key) {
        memset(s_server_key, 0, TS_CERT_KEY_MAX_LEN);
        free(s_server_key);
        s_server_key = NULL;
    }
    if (s_ca_chain) {
        free(s_ca_chain);
        s_ca_chain = NULL;
    }
}

/*===========================================================================*/
/*                         Server Control                                     */
/*===========================================================================*/

esp_err_t ts_https_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_server) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    
    // Configure HTTPS server
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    
    config.httpd.server_port = 0;  // HTTPS uses its own port
    config.port_secure = s_config.port;
    config.httpd.max_open_sockets = s_config.max_clients;
    config.httpd.lru_purge_enable = true;
    config.httpd.recv_wait_timeout = 10;
    config.httpd.send_wait_timeout = 10;
    
    // Server certificate and key
    config.servercert = (const uint8_t *)s_server_cert;
    config.servercert_len = strlen(s_server_cert) + 1;
    config.prvtkey_pem = (const uint8_t *)s_server_key;
    config.prvtkey_len = strlen(s_server_key) + 1;
    
    // Client certificate verification (mTLS)
    if (s_config.require_client_cert && s_ca_chain) {
        config.cacert_pem = (const uint8_t *)s_ca_chain;
        config.cacert_len = strlen(s_ca_chain) + 1;
        ESP_LOGI(TAG, "mTLS enabled: client certificates required");
    } else {
        config.cacert_pem = NULL;
        config.cacert_len = 0;
        ESP_LOGW(TAG, "mTLS disabled: no client certificate verification");
    }
    
    // Session tickets for faster reconnection
    config.httpd.enable_so_linger = false;
    
    // Start server
    esp_err_t ret = httpd_ssl_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register all endpoints
    for (int i = 0; i < s_endpoint_count; i++) {
        httpd_uri_t uri_handler = {
            .uri = s_endpoints[i].uri,
            .method = s_endpoints[i].method,
            .handler = generic_handler,
            .user_ctx = &s_endpoints[i]
        };
        httpd_register_uri_handler(s_server, &uri_handler);
    }
    
    ESP_LOGI(TAG, "HTTPS server started on port %d with %d endpoints",
             s_config.port, s_endpoint_count);
    
    return ESP_OK;
}

esp_err_t ts_https_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }
    
    esp_err_t ret = httpd_ssl_stop(s_server);
    s_server = NULL;
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HTTPS server stopped");
    }
    
    return ret;
}

bool ts_https_is_running(void)
{
    return s_server != NULL;
}

httpd_handle_t ts_https_get_handle(void)
{
    return s_server;
}

/*===========================================================================*/
/*                         Endpoint Registration                              */
/*===========================================================================*/

esp_err_t ts_https_register_endpoint(const ts_https_endpoint_t *endpoint)
{
    if (!endpoint || !endpoint->uri || !endpoint->handler) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_endpoint_count >= MAX_ENDPOINTS) {
        ESP_LOGE(TAG, "Max endpoints reached");
        return ESP_ERR_NO_MEM;
    }
    
    // Copy endpoint
    s_endpoints[s_endpoint_count] = *endpoint;
    s_endpoint_count++;
    
    ESP_LOGI(TAG, "Registered endpoint: %s %s (min_role=%s)",
             endpoint->method == HTTP_GET ? "GET" :
             endpoint->method == HTTP_POST ? "POST" :
             endpoint->method == HTTP_PUT ? "PUT" :
             endpoint->method == HTTP_DELETE ? "DELETE" : "?",
             endpoint->uri,
             ts_https_role_to_str(endpoint->min_role));
    
    // If server is already running, register immediately
    if (s_server) {
        httpd_uri_t uri_handler = {
            .uri = endpoint->uri,
            .method = endpoint->method,
            .handler = generic_handler,
            .user_ctx = &s_endpoints[s_endpoint_count - 1]
        };
        return httpd_register_uri_handler(s_server, &uri_handler);
    }
    
    return ESP_OK;
}

esp_err_t ts_https_register_endpoints(const ts_https_endpoint_t *endpoints)
{
    if (!endpoints) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    for (const ts_https_endpoint_t *ep = endpoints; ep->uri != NULL; ep++) {
        ret = ts_https_register_endpoint(ep);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return ESP_OK;
}

esp_err_t ts_https_unregister_endpoint(const char *uri, httpd_method_t method)
{
    if (!uri) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Find and remove from registry
    for (int i = 0; i < s_endpoint_count; i++) {
        if (strcmp(s_endpoints[i].uri, uri) == 0 && 
            s_endpoints[i].method == method) {
            // Shift remaining endpoints
            for (int j = i; j < s_endpoint_count - 1; j++) {
                s_endpoints[j] = s_endpoints[j + 1];
            }
            s_endpoint_count--;
            
            // Unregister from server if running
            if (s_server) {
                httpd_unregister_uri_handler(s_server, uri, method);
            }
            
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

/*===========================================================================*/
/*                         Generic Request Handler                            */
/*===========================================================================*/

/**
 * @brief 从 HTTP 请求中获取客户端证书
 * 
 * 通过 esp_https_server 的传输上下文获取 TLS 会话，进而获取客户端证书。
 * 
 * @param req HTTP 请求
 * @return 客户端证书指针，如果不可用则返回 NULL
 */
static const mbedtls_x509_crt *get_client_cert(httpd_req_t *req)
{
    int sockfd = httpd_req_to_sockfd(req);
    if (sockfd < 0) {
        ESP_LOGD(TAG, "Invalid socket fd");
        return NULL;
    }
    
    // 获取传输上下文（httpd_ssl_transport_ctx_t）
    ts_httpd_ssl_transport_ctx_t *transport_ctx = 
        (ts_httpd_ssl_transport_ctx_t *)httpd_sess_get_transport_ctx(req->handle, sockfd);
    
    if (!transport_ctx) {
        ESP_LOGD(TAG, "No transport context available");
        return NULL;
    }
    
    if (!transport_ctx->tls) {
        ESP_LOGD(TAG, "No TLS context in transport");
        return NULL;
    }
    
    // 使用公开 API 获取 SSL context
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)esp_tls_get_ssl_context(transport_ctx->tls);
    if (!ssl) {
        ESP_LOGD(TAG, "No SSL context available");
        return NULL;
    }
    
    // 获取 peer 证书
    const mbedtls_x509_crt *peer_cert = mbedtls_ssl_get_peer_cert(ssl);
    if (!peer_cert) {
        ESP_LOGD(TAG, "No peer certificate");
        return NULL;
    }
    
    return peer_cert;
}

/**
 * @brief 从 X.509 证书 DN 中提取指定 OID 的值
 */
static int extract_dn_field(const mbedtls_x509_name *dn, const char *oid, char *buf, size_t buf_len)
{
    const mbedtls_x509_name *name = dn;
    while (name != NULL) {
        // 检查 OID 是否匹配
        if (name->oid.len == strlen(oid) && memcmp(name->oid.p, oid, name->oid.len) == 0) {
            size_t copy_len = (name->val.len < buf_len - 1) ? name->val.len : buf_len - 1;
            memcpy(buf, name->val.p, copy_len);
            buf[copy_len] = '\0';
            return 0;
        }
        name = name->next;
    }
    return -1;
}

// X.509 OID 常量
#define OID_CN  "\x55\x04\x03"  // 2.5.4.3 - Common Name
#define OID_O   "\x55\x04\x0a"  // 2.5.4.10 - Organization
#define OID_OU  "\x55\x04\x0b"  // 2.5.4.11 - Organizational Unit

/**
 * @brief 从证书 Subject 中提取认证信息
 */
static void extract_auth_from_cert(const mbedtls_x509_crt *cert, ts_https_auth_t *auth)
{
    char buf[64];
    
    // 提取 CN (Common Name) -> username
    if (extract_dn_field(&cert->subject, OID_CN, buf, sizeof(buf)) == 0) {
        strncpy(auth->username, buf, sizeof(auth->username) - 1);
        auth->username[sizeof(auth->username) - 1] = '\0';
    }
    
    // 提取 O (Organization) -> organization
    if (extract_dn_field(&cert->subject, OID_O, buf, sizeof(buf)) == 0) {
        strncpy(auth->organization, buf, sizeof(auth->organization) - 1);
        auth->organization[sizeof(auth->organization) - 1] = '\0';
    }
    
    // 提取 OU (Organizational Unit) -> role
    if (extract_dn_field(&cert->subject, OID_OU, buf, sizeof(buf)) == 0) {
        auth->role = ts_https_str_to_role(buf);
        ESP_LOGI(TAG, "Client cert OU='%s' -> role=%s", buf, ts_https_role_to_str(auth->role));
    }
    
    // 计算证书有效天数
    // mbedtls_x509_time 包含 year, mon, day, hour, min, sec
    // 这里简单估算剩余天数
    time_t now;
    time(&now);
    struct tm *tm_now = gmtime(&now);
    if (tm_now) {
        int years_diff = cert->valid_to.year - (tm_now->tm_year + 1900);
        int months_diff = cert->valid_to.mon - (tm_now->tm_mon + 1);
        int days_diff = cert->valid_to.day - tm_now->tm_mday;
        auth->cert_days_remaining = years_diff * 365 + months_diff * 30 + days_diff;
        if (auth->cert_days_remaining < 0) {
            auth->cert_days_remaining = 0;  // 已过期
        }
    }
    
    auth->authenticated = true;
}

static esp_err_t generic_handler(httpd_req_t *req)
{
    ts_https_endpoint_t *endpoint = (ts_https_endpoint_t *)req->user_ctx;
    if (!endpoint) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, 
                                   "Internal error");
    }
    
    // Build request context
    ts_https_req_t https_req = {
        .req = req,
        .auth = {
            .authenticated = false,
            .role = TS_HTTPS_ROLE_ANONYMOUS,
            .username = "anonymous",
            .organization = "",
            .cert_days_remaining = 0
        }
    };
    
    // Try to get client authentication from certificate
    if (s_config.require_client_cert) {
        const mbedtls_x509_crt *client_cert = get_client_cert(req);
        if (client_cert) {
            // 从证书中提取认证信息
            extract_auth_from_cert(client_cert, &https_req.auth);
            ESP_LOGI(TAG, "mTLS auth: user='%s', org='%s', role=%s",
                     https_req.auth.username, 
                     https_req.auth.organization,
                     ts_https_role_to_str(https_req.auth.role));
        } else {
            // 客户端证书不可用，但 TLS 握手成功，说明证书已被验证
            // 这种情况不应该发生，记录警告并使用默认值
            ESP_LOGW(TAG, "mTLS required but client cert not accessible, using fallback");
            https_req.auth.authenticated = true;
            https_req.auth.role = TS_HTTPS_ROLE_VIEWER;  // 降级到最低权限
            strncpy(https_req.auth.username, "unknown-mTLS-user", sizeof(https_req.auth.username) - 1);
        }
    }
    
    // Check permissions
    if (!ts_https_check_permission(&https_req.auth, endpoint->min_role)) {
        if (!https_req.auth.authenticated) {
            return ts_https_send_unauthorized(&https_req);
        } else {
            return ts_https_send_forbidden(&https_req);
        }
    }
    
    // Call endpoint handler
    esp_err_t ret = endpoint->handler(&https_req);
    
    return ret;
}

/*===========================================================================*/
/*                         Response Helpers                                   */
/*===========================================================================*/

esp_err_t ts_https_send_json(ts_https_req_t *req, int status, const char *json_str)
{
    if (!req || !req->req || !json_str) {
        return ESP_ERR_INVALID_ARG;
    }
    
    httpd_resp_set_type(req->req, "application/json");
    
    // Set status code
    char status_str[32];
    snprintf(status_str, sizeof(status_str), "%d", status);
    httpd_resp_set_status(req->req, 
        status == 200 ? HTTPD_200 :
        status == 201 ? "201 Created" :
        status == 400 ? HTTPD_400 :
        status == 401 ? "401 Unauthorized" :
        status == 403 ? "403 Forbidden" :
        status == 404 ? HTTPD_404 :
        status == 500 ? HTTPD_500 : status_str);
    
    return httpd_resp_send(req->req, json_str, strlen(json_str));
}

esp_err_t ts_https_send_jsonf(ts_https_req_t *req, int status, const char *fmt, ...)
{
    char *buf = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 1024, fmt, args);
    va_end(args);
    
    esp_err_t ret = ts_https_send_json(req, status, buf);
    free(buf);
    
    return ret;
}

esp_err_t ts_https_send_error(ts_https_req_t *req, int status, const char *message)
{
    return ts_https_send_jsonf(req, status, 
        "{\"error\":true,\"code\":%d,\"message\":\"%s\"}", 
        status, message ? message : "Error");
}

esp_err_t ts_https_send_forbidden(ts_https_req_t *req)
{
    return ts_https_send_error(req, 403, "Insufficient permissions");
}

esp_err_t ts_https_send_unauthorized(ts_https_req_t *req)
{
    return ts_https_send_error(req, 401, "Authentication required");
}

/*===========================================================================*/
/*                         Request Helpers                                    */
/*===========================================================================*/

esp_err_t ts_https_get_body(ts_https_req_t *req, char **body, size_t *len)
{
    if (!req || !req->req || !body || !len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t content_len = req->req->content_len;
    if (content_len == 0) {
        *body = NULL;
        *len = 0;
        return ESP_OK;
    }
    
    // Limit body size
    if (content_len > 8192) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    char *buf = heap_caps_malloc(content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    
    int received = httpd_req_recv(req->req, buf, content_len);
    if (received <= 0) {
        free(buf);
        return ESP_FAIL;
    }
    
    buf[received] = '\0';
    *body = buf;
    *len = received;
    
    return ESP_OK;
}

esp_err_t ts_https_get_query_param(ts_https_req_t *req, const char *key,
                                    char *value, size_t value_len)
{
    if (!req || !req->req || !key || !value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    size_t query_len = httpd_req_get_url_query_len(req->req);
    if (query_len == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    
    char *query = malloc(query_len + 1);
    if (!query) {
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = httpd_req_get_url_query_str(req->req, query, query_len + 1);
    if (ret != ESP_OK) {
        free(query);
        return ret;
    }
    
    ret = httpd_query_key_value(query, key, value, value_len);
    free(query);
    
    return ret;
}
