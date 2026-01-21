/**
 * @file ts_https_server.c
 * @brief HTTPS Server Implementation with TLS and mTLS support
 */

#include "ts_https_server.h"
#include "ts_log.h"
#include "ts_storage.h"
#include <string.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_csr.h>

#define TAG "ts_https"
#define MAX_ROUTES 32

static httpd_handle_t s_https_server = NULL;
static bool s_initialized = false;
static ts_https_config_t s_config;

// Forward declaration
static esp_err_t https_handler_wrapper(httpd_req_t *req);

esp_err_t ts_https_server_init(const ts_https_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }
    
    if (!config || !config->server_cert || !config->server_key) {
        TS_LOGE(TAG, "Server certificate and key required");
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_config, config, sizeof(ts_https_config_t));
    s_initialized = true;
    
    TS_LOGI(TAG, "HTTPS server initialized");
    return ESP_OK;
}

esp_err_t ts_https_server_deinit(void)
{
    ts_https_server_stop();
    s_initialized = false;
    memset(&s_config, 0, sizeof(s_config));
    return ESP_OK;
}

esp_err_t ts_https_server_start(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_https_server) {
        return ESP_OK;  // Already running
    }
    
    httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
    
    // Set port
    ssl_config.port_secure = s_config.port;
    
    // Set server certificate and key
    ssl_config.servercert = s_config.server_cert;
    ssl_config.servercert_len = s_config.server_cert_len;
    ssl_config.prvtkey_pem = s_config.server_key;
    ssl_config.prvtkey_len = s_config.server_key_len;
    
    // Set client authentication mode (CA cert for mTLS)
    switch (s_config.auth_mode) {
        case TS_TLS_AUTH_NONE:
            ssl_config.cacert_pem = NULL;
            ssl_config.cacert_len = 0;
            break;
        case TS_TLS_AUTH_OPTIONAL:
        case TS_TLS_AUTH_REQUIRED:
            if (s_config.ca_cert && s_config.ca_cert_len > 0) {
                ssl_config.cacert_pem = s_config.ca_cert;
                ssl_config.cacert_len = s_config.ca_cert_len;
            }
            break;
    }
    
    // HTTP server configuration
    ssl_config.httpd.max_uri_handlers = MAX_ROUTES;
    ssl_config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    
    if (s_config.max_connections > 0) {
        ssl_config.httpd.max_open_sockets = s_config.max_connections;
    }
    
    esp_err_t ret = httpd_ssl_start(&s_https_server, &ssl_config);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "HTTPS server started on port %d (TLS auth: %s)", 
                s_config.port,
                s_config.auth_mode == TS_TLS_AUTH_REQUIRED ? "mTLS" :
                s_config.auth_mode == TS_TLS_AUTH_OPTIONAL ? "optional" : "none");
    } else {
        TS_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

esp_err_t ts_https_server_stop(void)
{
    if (!s_https_server) {
        return ESP_OK;
    }
    
    httpd_ssl_stop(s_https_server);
    s_https_server = NULL;
    
    TS_LOGI(TAG, "HTTPS server stopped");
    return ESP_OK;
}

bool ts_https_server_is_running(void)
{
    return s_https_server != NULL;
}

static esp_err_t https_handler_wrapper(httpd_req_t *req)
{
    ts_http_route_t *route = (ts_http_route_t *)req->user_ctx;
    if (!route || !route->handler) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No handler");
        return ESP_FAIL;
    }
    
    ts_http_request_t ts_req = {
        .req = req,
        .uri = req->uri,
        .method = req->method,
        .body = NULL,
        .body_len = 0
    };
    
    // Read body if present - must loop to receive all data
    if (req->content_len > 0) {
        ts_req.body = malloc(req->content_len + 1);
        if (ts_req.body) {
            size_t total_received = 0;
            while (total_received < req->content_len) {
                int ret = httpd_req_recv(req, ts_req.body + total_received, 
                                         req->content_len - total_received);
                if (ret <= 0) {
                    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                        // Timeout, continue trying
                        continue;
                    }
                    // Error or connection closed
                    break;
                }
                total_received += ret;
            }
            ts_req.body[total_received] = '\0';
            ts_req.body_len = total_received;
        }
    }
    
    esp_err_t result = route->handler(&ts_req, route->user_data);
    
    free(ts_req.body);
    return result;
}

esp_err_t ts_https_server_register_route(const ts_http_route_t *route)
{
    if (!s_https_server || !route) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Allocate persistent copy of route
    ts_http_route_t *route_copy = malloc(sizeof(ts_http_route_t));
    if (!route_copy) {
        return ESP_ERR_NO_MEM;
    }
    *route_copy = *route;
    
    httpd_uri_t uri = {
        .uri = route->uri,
        .method = route->method,
        .handler = https_handler_wrapper,
        .user_ctx = route_copy
    };
    
    return httpd_register_uri_handler(s_https_server, &uri);
}

esp_err_t ts_https_server_unregister_route(const char *uri, ts_http_method_t method)
{
    if (!s_https_server || !uri) {
        return ESP_ERR_INVALID_STATE;
    }
    return httpd_unregister_uri_handler(s_https_server, uri, method);
}

esp_err_t ts_https_load_certs_from_files(ts_https_config_t *config,
                                          const char *cert_path,
                                          const char *key_path,
                                          const char *ca_path)
{
    if (!config || !cert_path || !key_path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    // Load server certificate
    ssize_t cert_size = ts_storage_size(cert_path);
    if (cert_size < 0) {
        TS_LOGE(TAG, "Server certificate not found: %s", cert_path);
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t *cert = malloc(cert_size + 1);
    if (!cert) {
        return ESP_ERR_NO_MEM;
    }
    
    if (ts_storage_read_file(cert_path, cert, cert_size) != cert_size) {
        free(cert);
        return ESP_ERR_INVALID_STATE;
    }
    cert[cert_size] = '\0';
    config->server_cert = cert;
    config->server_cert_len = cert_size + 1;
    
    // Load server private key
    ssize_t key_size = ts_storage_size(key_path);
    if (key_size < 0) {
        TS_LOGE(TAG, "Server key not found: %s", key_path);
        free((void*)config->server_cert);
        config->server_cert = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t *key = malloc(key_size + 1);
    if (!key) {
        free((void*)config->server_cert);
        config->server_cert = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    if (ts_storage_read_file(key_path, key, key_size) != key_size) {
        free((void*)config->server_cert);
        config->server_cert = NULL;
        free(key);
        return ESP_ERR_INVALID_STATE;
    }
    key[key_size] = '\0';
    config->server_key = key;
    config->server_key_len = key_size + 1;
    
    // Load CA certificate (optional, for mTLS)
    if (ca_path) {
        ssize_t ca_size = ts_storage_size(ca_path);
        if (ca_size > 0) {
            uint8_t *ca = malloc(ca_size + 1);
            if (ca && ts_storage_read_file(ca_path, ca, ca_size) == ca_size) {
                ca[ca_size] = '\0';
                config->ca_cert = ca;
                config->ca_cert_len = ca_size + 1;
                TS_LOGI(TAG, "CA certificate loaded for mTLS");
            } else {
                free(ca);
            }
        }
    }
    
    TS_LOGI(TAG, "Certificates loaded from files");
    return ret;
}

void ts_https_free_certs(ts_https_config_t *config)
{
    if (!config) return;
    
    if (config->server_cert) {
        free((void*)config->server_cert);
        config->server_cert = NULL;
        config->server_cert_len = 0;
    }
    if (config->server_key) {
        free((void*)config->server_key);
        config->server_key = NULL;
        config->server_key_len = 0;
    }
    if (config->ca_cert) {
        free((void*)config->ca_cert);
        config->ca_cert = NULL;
        config->ca_cert_len = 0;
    }
}

esp_err_t ts_https_generate_self_signed(const char *common_name,
                                         int validity_days,
                                         uint8_t *cert_out, size_t cert_size,
                                         uint8_t *key_out, size_t key_size)
{
    if (!common_name || !cert_out || !key_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int ret;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    const char *pers = "self_signed";
    
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    
    // Seed RNG
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        TS_LOGE(TAG, "Failed to seed RNG: -0x%04x", -ret);
        goto cleanup;
    }
    
    // Generate RSA key pair
    ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        TS_LOGE(TAG, "Failed to setup key: -0x%04x", -ret);
        goto cleanup;
    }
    
    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg,
                               2048, 65537);
    if (ret != 0) {
        TS_LOGE(TAG, "Failed to generate RSA key: -0x%04x", -ret);
        goto cleanup;
    }
    
    // Write private key to PEM
    ret = mbedtls_pk_write_key_pem(&key, key_out, key_size);
    if (ret != 0) {
        TS_LOGE(TAG, "Failed to write key PEM: -0x%04x", -ret);
        goto cleanup;
    }
    
    // Setup certificate
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    
    // Set subject and issuer name
    char subject_name[128];
    snprintf(subject_name, sizeof(subject_name), "CN=%s,O=TianShanOS,C=CN", common_name);
    
    ret = mbedtls_x509write_crt_set_subject_name(&crt, subject_name);
    if (ret != 0) {
        TS_LOGE(TAG, "Failed to set subject name: -0x%04x", -ret);
        goto cleanup;
    }
    
    ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject_name);
    if (ret != 0) {
        TS_LOGE(TAG, "Failed to set issuer name: -0x%04x", -ret);
        goto cleanup;
    }
    
    // Set serial number (use raw bytes instead of deprecated MPI API)
    unsigned char serial_bytes[1] = {1};
    
    ret = mbedtls_x509write_crt_set_serial_raw(&crt, serial_bytes, sizeof(serial_bytes));
    if (ret != 0) goto cleanup;
    
    // Set validity period (YYYYMMDDHHMMSS format, 14 chars + null)
    char not_before[32], not_after[32];
    time_t now = time(NULL);
    struct tm *tm_now = gmtime(&now);
    int year_now = tm_now->tm_year + 1900;
    if (year_now < 1970) year_now = 2024;  // Fallback for invalid time
    if (year_now > 9999) year_now = 9999;
    snprintf(not_before, sizeof(not_before), "%04d%02d%02d000000",
             year_now, tm_now->tm_mon + 1, tm_now->tm_mday);
    
    time_t later = now + (time_t)validity_days * 24 * 3600;
    struct tm *tm_later = gmtime(&later);
    int year_later = tm_later->tm_year + 1900;
    if (year_later < 1970) year_later = 2025;
    if (year_later > 9999) year_later = 9999;
    snprintf(not_after, sizeof(not_after), "%04d%02d%02d235959",
             year_later, tm_later->tm_mon + 1, tm_later->tm_mday);
    
    ret = mbedtls_x509write_crt_set_validity(&crt, not_before, not_after);
    if (ret != 0) {
        TS_LOGE(TAG, "Failed to set validity: -0x%04x", -ret);
        goto cleanup;
    }
    
    // Set basic constraints (CA:FALSE)
    ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 0, 0);
    if (ret != 0) goto cleanup;
    
    // Set key usage
    ret = mbedtls_x509write_crt_set_key_usage(&crt, 
           MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
    if (ret != 0) goto cleanup;
    
    // Set signature algorithm
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    
    // Write certificate to PEM
    ret = mbedtls_x509write_crt_pem(&crt, cert_out, cert_size,
                                     mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        TS_LOGE(TAG, "Failed to write cert PEM: -0x%04x", -ret);
        goto cleanup;
    }
    
    TS_LOGI(TAG, "Self-signed certificate generated for CN=%s", common_name);
    ret = 0;
    
cleanup:
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ts_https_get_client_cert_cn(ts_http_request_t *req, 
                                       char *cn_out, size_t cn_size)
{
    if (!req || !req->req || !cn_out || cn_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get SSL session from request
    // Note: This requires access to internal esp_https_server structures
    // For now, return a placeholder implementation
    
    // In a full implementation, you would:
    // 1. Get the SSL context from the connection
    // 2. Get the peer certificate
    // 3. Parse the certificate to extract CN
    
    TS_LOGW(TAG, "Client certificate extraction not fully implemented");
    strncpy(cn_out, "unknown", cn_size);
    cn_out[cn_size - 1] = '\0';
    
    return ESP_ERR_NOT_SUPPORTED;
}
