/**
 * @file ts_pki_client.c
 * @brief TianShanOS PKI Auto-Enrollment Client Implementation
 *
 * ESP32 与 PKI 服务器通信的客户端实现:
 * - CSR 提交 (POST /api/csr)
 * - 状态轮询 (GET /api/csr/{device_id})
 * - 证书下载 (GET /api/csr/{device_id}/certificate)
 * - CA 链下载 (GET /api/ca/chain)
 *
 * @copyright Copyright (c) 2026 TianShanOS Project
 */

#include <stdatomic.h>
#include <limits.h>
#include <math.h>
#include "ts_http_url.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "ts_http_fetch.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#include "ts_pki_client.h"
#include "ts_cert.h"

static const char *TAG = "pki_client";

/*===========================================================================*/
/*                          Private Data                                      */
/*===========================================================================*/

/** Client state */
static struct {
    bool initialized;
    ts_pki_client_config_t config;
    char device_id[TS_CERT_DEVICE_ID_MAX_LEN];
    int request_id;  // CSR 提交后返回的请求 ID
    _Atomic(TaskHandle_t) enroll_task;
    _Atomic bool enroll_running;
    _Atomic bool stop_requested;
    ts_pki_enroll_callback_t callback;
    void *callback_data;
} s_client = {0};

/* One operation owner, retained across deinit so late callers cannot take a
 * freed mutex. Recursive only for the existing blocking enrollment composition.
 * Contending control/HTTP requests return busy rather than waiting behind I/O. */
static StaticSemaphore_t s_operation_storage;
static SemaphoreHandle_t s_operation;
static portMUX_TYPE s_operation_init_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool s_deinitializing;
static int64_t s_request_deadline; /* Held by the operation owner; zero for individual requests. */
static bool operation_take(void)
{
    portENTER_CRITICAL(&s_operation_init_lock);
    if (!s_operation) s_operation = xSemaphoreCreateRecursiveMutexStatic(&s_operation_storage);
    portEXIT_CRITICAL(&s_operation_init_lock);
    return xSemaphoreTakeRecursive(s_operation, 0) == pdTRUE;
}
static bool operation_enter(void)
{
    if (!operation_take()) return false;
    if (s_deinitializing) { xSemaphoreGiveRecursive(s_operation); return false; }
    return true;
}
static bool enrollment_owner(void)
{
    return !s_client.enroll_running || xTaskGetCurrentTaskHandle() == s_client.enroll_task;
}
static bool request_current(void *task)
{
    return !s_deinitializing && (!s_request_deadline || esp_timer_get_time() < s_request_deadline) && !(s_client.enroll_running &&
        task == s_client.enroll_task && s_client.stop_requested);
}

/* Only a host, never a URL/path/header; accept DNS and IPv4/IPv6 without
 * inventing a separate authority parser or changing the configured target. */
static bool valid_server(const char *host, uint16_t port)
{
    if (!host || !port || !host[0] || strnlen(host, sizeof(s_client.config.server_host)) >= sizeof(s_client.config.server_host)) return false;
    char url[96];
    bool brackets = strchr(host, ':') && host[0] != '[';
    int n = snprintf(url, sizeof(url), brackets ? "http://[%s]:%u/" : "http://%s:%u/", host, (unsigned)port);
    struct http_parser_url parsed;
    return n > 0 && (size_t)n < sizeof(url) && ts_http_url_parse(url, &parsed) &&
        !(parsed.field_set & ((1U << UF_USERINFO) | (1U << UF_QUERY) | (1U << UF_FRAGMENT))) &&
        parsed.field_data[UF_PATH].len == 1;
}
static void server_url(char *out, size_t size, const char *path)
{
    const char *host = s_client.config.server_host;
    bool brackets = strchr(host, ':') && host[0] != '[';
    snprintf(out, size, brackets ? "http%s://[%s]:%u%s" : "http%s://%s:%u%s",
        s_client.config.use_https ? "s" : "", host, (unsigned)s_client.config.server_port, path);
}

/** HTTP response buffer */
#define HTTP_RESP_BUF_SIZE 8192

/*===========================================================================*/
/*                       Forward Declarations                                 */
/*===========================================================================*/

static esp_err_t do_http_request(const char *url, esp_http_client_method_t method,
                                  const char *post_data, int *http_code, cJSON **response);
static void auto_enroll_task(void *arg);
static void enroll_wait(uint32_t ms);
static esp_err_t get_device_id(void);

/*===========================================================================*/
/*                         Initialization                                     */
/*===========================================================================*/

void ts_pki_client_get_default_config(ts_pki_client_config_t *config)
{
    if (!config) return;
    
    memset(config, 0, sizeof(*config));
    // 生产内网 PKI 服务器地址（也是出厂后用户网络中的地址）
    strncpy(config->server_host, "10.10.99.100", sizeof(config->server_host) - 1);
    config->server_port = TS_PKI_DEFAULT_PORT;
    config->use_https = false;  // PKI 服务器目前用 HTTP
    config->poll_interval_sec = TS_PKI_POLL_INTERVAL_SEC;
    config->max_poll_attempts = TS_PKI_MAX_POLL_ATTEMPTS;
    config->auto_start = false;
}

esp_err_t ts_pki_client_init(void)
{
    ts_pki_client_config_t config;
    ts_pki_client_get_default_config(&config);
    return ts_pki_client_init_with_config(&config);
}

static esp_err_t ts_pki_client_init_with_config_locked(const ts_pki_client_config_t *config)
{
    if (s_client.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    if (!config || !valid_server(config->server_host, config->server_port) || !config->poll_interval_sec) return ESP_ERR_INVALID_ARG;

    // 复制配置
    memcpy(&s_client.config, config, sizeof(ts_pki_client_config_t));
    s_client.initialized = true;
    s_client.stop_requested = false;
    
    // 获取设备 ID
    esp_err_t ret = get_device_id();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not get device ID, will use MAC address");
    }
    
    ESP_LOGI(TAG, "PKI client initialized, server=%s:%d, device_id=%s",
             s_client.config.server_host, s_client.config.server_port, 
             s_client.device_id);
    
    // 如果配置了自动启动
    if (config->auto_start) {
        return ts_pki_client_start_auto_enroll(NULL, NULL);
    }
    
    return ESP_OK;
}

esp_err_t ts_pki_client_deinit(void)
{
    s_deinitializing = true;
    ts_pki_client_stop_auto_enroll();
    if (!operation_take()) return ESP_ERR_INVALID_STATE;
    if (s_client.enroll_running) {
        xSemaphoreGiveRecursive(s_operation);
        return ESP_ERR_INVALID_STATE;
    }
    s_client.initialized = false;
    memset(&s_client.config, 0, sizeof(s_client.config));
    memset(s_client.device_id, 0, sizeof(s_client.device_id));
    s_client.request_id = 0;
    s_client.enroll_task = NULL;
    s_client.callback = NULL;
    s_client.callback_data = NULL;
    s_client.stop_requested = false;
    s_deinitializing = false;
    xSemaphoreGiveRecursive(s_operation);
    ESP_LOGI(TAG, "PKI client deinitialized");
    return ESP_OK;
}

/*===========================================================================*/
/*                          Server Configuration                              */
/*===========================================================================*/

static esp_err_t ts_pki_client_set_server_locked(const char *host, uint16_t port)
{
    if (!s_client.initialized || !host) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!valid_server(host, port)) return ESP_ERR_INVALID_ARG;
    if (s_client.enroll_running) return ESP_ERR_INVALID_STATE;
    if (strcmp(s_client.config.server_host, host) || s_client.config.server_port != port) {
        snprintf(s_client.config.server_host, sizeof(s_client.config.server_host), "%s", host);
        s_client.config.server_port = port;
        s_client.request_id = 0; /* Request IDs are scoped to their issuing server. */
    }

    ESP_LOGI(TAG, "PKI server set to %s:%d", host, port);
    return ESP_OK;
}

static esp_err_t ts_pki_client_get_server_locked(char *host, uint16_t *port)
{
    if (!s_client.initialized || !host || !port) {
        return ESP_ERR_INVALID_STATE;
    }
    
    strcpy(host, s_client.config.server_host);
    *port = s_client.config.server_port;
    
    return ESP_OK;
}

/*===========================================================================*/
/*                      Manual Enrollment Operations                          */
/*===========================================================================*/

static esp_err_t ts_pki_client_submit_csr_locked(void)
{
    if (!s_client.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Submitting CSR for device: %s", s_client.device_id);
    
    // 检查是否已有有效证书
    ts_cert_pki_status_t cert_status;
    esp_err_t ret = ts_cert_get_status(&cert_status);
    if (ret == ESP_OK && (cert_status.has_certificate && cert_status.has_private_key &&
        cert_status.cert_info.validity != TS_CERT_VALIDITY_EXPIRED)) {
        ESP_LOGI(TAG, "Stored certificate retained; no automatic replacement");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 确保有密钥对
    if (!ts_cert_has_keypair()) {
        ESP_LOGI(TAG, "Generating new key pair...");
        ret = ts_cert_generate_keypair();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to generate key pair: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // 生成 CSR
    char *csr_pem = heap_caps_malloc(TS_CERT_CSR_MAX_LEN, MALLOC_CAP_SPIRAM);
    if (!csr_pem) {
        csr_pem = malloc(TS_CERT_CSR_MAX_LEN);
        if (!csr_pem) {
            return ESP_ERR_NO_MEM;
        }
    }
    
    size_t csr_len = TS_CERT_CSR_MAX_LEN;
    ret = ts_cert_generate_csr_default(csr_pem, &csr_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate CSR: %s", esp_err_to_name(ret));
        free(csr_pem);
        return ret;
    }
    
    ESP_LOGI(TAG, "CSR generated, length=%zu", csr_len);
    
    // 构建 JSON 请求体
    cJSON *root = cJSON_CreateObject();
    bool complete = root && cJSON_AddStringToObject(root, "device_id", s_client.device_id) &&
        cJSON_AddStringToObject(root, "csr_pem", csr_pem) &&
        cJSON_AddStringToObject(root, "device_type", "ESP32-S3") &&
        cJSON_AddStringToObject(root, "device_ip", "10.10.99.97") &&
        cJSON_AddStringToObject(root, "description", "TianShanOS Rack Manager");
    cJSON *san_ips = complete ? cJSON_AddArrayToObject(root, "suggested_san_ips") : NULL;
    cJSON *ip = san_ips ? cJSON_CreateString("10.10.99.97") : NULL;
    if (!ip || !cJSON_AddItemToArray(san_ips, ip)) {
        cJSON_Delete(ip);
        cJSON_Delete(root);
        free(csr_pem);
        return ESP_ERR_NO_MEM;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(csr_pem);
    
    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }
    
    // 发送 HTTP 请求到 /api/csr/submit
    char url[256];
    server_url(url, sizeof(url), "/api/csr/submit");
    
    int http_code = 0;
    cJSON *resp = NULL;
    s_client.request_id = 0;
    ret = do_http_request(url, HTTP_METHOD_POST, json_str, &http_code, &resp);
    free(json_str);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (http_code == 200 || http_code == 201) {
        // 解析响应获取 request_id
        if (resp) {
            cJSON *req_id = cJSON_GetObjectItem(resp, "request_id");
            if (!cJSON_IsNumber(req_id) || !isfinite(req_id->valuedouble) || req_id->valuedouble < 1 ||
                req_id->valuedouble > INT_MAX || req_id->valuedouble != (int)req_id->valuedouble) {
                cJSON_Delete(resp);
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (req_id) {
                s_client.request_id = req_id->valueint;
                ESP_LOGI(TAG, "CSR submitted successfully, request_id=%d", s_client.request_id);
            }
            // 检查是否自动签发（设备在白名单且启用自动签发）
            cJSON *status = cJSON_GetObjectItem(resp, "status");
            cJSON *cert = cJSON_GetObjectItem(resp, "certificate");
            if (status && cJSON_IsString(status) && 
                strcmp(status->valuestring, "approved") == 0 &&
                cert && cJSON_IsString(cert)) {
                ESP_LOGI(TAG, "Certificate auto-issued!");
                ret = ts_cert_install_issued_certificate_ex(cert->valuestring, strlen(cert->valuestring) + 1, NULL);
                if (ret != ESP_OK) { cJSON_Delete(resp); return ret; }
            }
            cJSON_Delete(resp);
        }
        return ESP_OK;
    } else if (http_code == 409) {
        ESP_LOGW(TAG, "CSR already exists for this device");
        return ESP_ERR_INVALID_STATE; /* Conflict without a bound request ID is not success. */
    } else {
        ESP_LOGE(TAG, "Server returned error: %d", http_code);
        return ESP_FAIL;
    }
}

static esp_err_t ts_pki_client_check_status_locked(ts_pki_enroll_status_t *status)
{
    if (!s_client.initialized || !status) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_client.request_id <= 0) {
        *status = TS_PKI_ENROLL_NOT_FOUND;
        return ESP_OK;
    }
    
    char url[256];
    char path[64];
    snprintf(path, sizeof(path), "/api/csr/status/%d", s_client.request_id);
    server_url(url, sizeof(url), path);
    
    int http_code = 0;
    cJSON *root = NULL;
    esp_err_t ret = do_http_request(url, HTTP_METHOD_GET, NULL, &http_code, &root);
    
    if (ret != ESP_OK) {
        *status = TS_PKI_ENROLL_ERROR;
        return ret;
    }
    
    if (http_code == 404) {
        *status = TS_PKI_ENROLL_NOT_FOUND;
        return ESP_OK;
    }
    
    if (http_code != 200) {
        cJSON_Delete(root);
        *status = TS_PKI_ENROLL_ERROR;
        return ESP_OK;
    }
    
    // 解析 JSON 响应
    if (!root) {
        *status = TS_PKI_ENROLL_ERROR;
        return ESP_OK;
    }
    
    cJSON *status_obj = cJSON_GetObjectItem(root, "status");
    if (status_obj && cJSON_IsString(status_obj)) {
        const char *status_str = status_obj->valuestring;
        if (strcmp(status_str, "approved") == 0) {
            *status = TS_PKI_ENROLL_APPROVED;
        } else if (strcmp(status_str, "rejected") == 0) {
            *status = TS_PKI_ENROLL_REJECTED;
        } else if (strcmp(status_str, "pending") == 0) {
            *status = TS_PKI_ENROLL_PENDING;
        } else {
            *status = TS_PKI_ENROLL_ERROR;
        }
    } else {
        *status = TS_PKI_ENROLL_ERROR;
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t ts_pki_client_install_certificate_locked(void)
{
    if (!s_client.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_client.request_id <= 0) {
        ESP_LOGE(TAG, "No request_id, submit CSR first");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Downloading certificate for request_id: %d", s_client.request_id);
    
    // 从 /api/csr/status/{request_id} 获取证书
    char url[256];
    char path[64];
    snprintf(path, sizeof(path), "/api/csr/status/%d", s_client.request_id);
    server_url(url, sizeof(url), path);
    
    int http_code = 0;
    cJSON *root = NULL;
    esp_err_t ret = do_http_request(url, HTTP_METHOD_GET, NULL, &http_code, &root);
    
    if (ret != ESP_OK || http_code != 200) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to get certificate: http_code=%d", http_code);
        return ESP_FAIL;
    }
    
    // 解析 JSON 响应
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse certificate response");
        return ESP_FAIL;
    }
    
    // 检查状态
    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!status || !cJSON_IsString(status) || strcmp(status->valuestring, "approved") != 0) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Certificate not approved yet");
        return ESP_ERR_INVALID_STATE;
    }
    
    cJSON *cert_pem = cJSON_GetObjectItem(root, "certificate");
    if (!cert_pem || !cJSON_IsString(cert_pem)) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "No certificate in response");
        return ESP_FAIL;
    }
    
    /* The response is untrusted transport data. Its ca_chain is not a trust
     * decision; the material owner verifies against Root's existing bundle. */
    ret = ts_cert_install_issued_certificate_ex(cert_pem->valuestring, strlen(cert_pem->valuestring) + 1, NULL);
    if (ret != ESP_OK) { cJSON_Delete(root); return ret; }

    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "Device certificate installed successfully");
    
    return ESP_OK;
}

static esp_err_t ts_pki_client_enroll_blocking_locked(uint32_t timeout_sec)
{
    if (!s_client.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // 提交 CSR
    esp_err_t ret = ts_pki_client_submit_csr();
    if (ret != ESP_OK) {
        return ret;
    }
    
    uint32_t max_attempts = timeout_sec ? 0 : s_client.config.max_poll_attempts;

    // 轮询等待批准
    uint32_t attempts = 0;
    ts_pki_enroll_status_t status;
    
    while (!max_attempts || attempts < max_attempts) {
        uint32_t wait_ms = (uint32_t)s_client.config.poll_interval_sec * 1000;
        if (s_request_deadline) {
            int64_t remaining = (s_request_deadline - esp_timer_get_time()) / 1000;
            if (remaining <= 0) return ESP_ERR_TIMEOUT;
            if (remaining < wait_ms) wait_ms = (uint32_t)remaining;
        }
        enroll_wait(wait_ms);
        if (s_deinitializing) return ESP_ERR_INVALID_STATE;
        if (!request_current(xTaskGetCurrentTaskHandle())) return ESP_ERR_TIMEOUT;
        
        ret = ts_pki_client_check_status(&status);
        if (ret != ESP_OK) {
            attempts++;
            continue;
        }
        
        switch (status) {
            case TS_PKI_ENROLL_APPROVED:
                ESP_LOGI(TAG, "CSR approved! Installing certificate...");
                return ts_pki_client_install_certificate();
                
            case TS_PKI_ENROLL_REJECTED:
                ESP_LOGE(TAG, "CSR was rejected");
                return ESP_FAIL;
                
            case TS_PKI_ENROLL_PENDING:
                ESP_LOGI(TAG, "Waiting for approval... (%lu/%lu)", 
                         (unsigned long)attempts, (unsigned long)max_attempts);
                break;
                
            default:
                ESP_LOGW(TAG, "Unexpected status: %d", status);
                break;
        }
        
        attempts++;
    }
    
    ESP_LOGE(TAG, "Enrollment timed out");
    return ESP_ERR_TIMEOUT;
}

/*===========================================================================*/
/*                      Background Enrollment Service                         */
/*===========================================================================*/

static esp_err_t ts_pki_client_start_auto_enroll_locked(ts_pki_enroll_callback_t callback,
                                           void *user_data)
{
    if (!s_client.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    portENTER_CRITICAL(&s_operation_init_lock);
    bool already_running = s_client.enroll_running;
    if (!already_running) {
        s_client.stop_requested = false;
        s_client.enroll_task = NULL;
        s_client.enroll_running = true;
    }
    portEXIT_CRITICAL(&s_operation_init_lock);
    if (already_running) return ESP_OK;
    s_client.callback = callback;
    s_client.callback_data = user_data;

    /* MUST use DRAM stack - PKI operations access NVS (SPI Flash).
     * SPI Flash operations disable cache, and PSRAM access requires cache.
     */
    TaskHandle_t task = NULL;
    BaseType_t ret = xTaskCreate(auto_enroll_task, "pki_enroll", 8192,
                                  NULL, 5, &task);
    if (ret != pdPASS) {
        s_client.enroll_running = false;
        ESP_LOGE(TAG, "Failed to create enroll task");
        return ESP_ERR_NO_MEM;
    }
    
    s_client.enroll_task = task;
    ESP_LOGI(TAG, "Auto-enrollment started");
    return ESP_OK;
}

void ts_pki_client_stop_auto_enroll(void)
{
    if (!s_client.enroll_running) return;
    
    s_client.stop_requested = true;
    if (s_client.enroll_task == xTaskGetCurrentTaskHandle()) return;
    
    // 等待任务退出
    for (int i = 0; i < 50; i++) {
        if (!s_client.enroll_running) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (s_client.enroll_running) {
        ESP_LOGW(TAG, "Enrollment cancellation pending; task retains its resources");
        return;
    }
    
    ESP_LOGI(TAG, "Auto-enrollment stopped");
}

bool ts_pki_client_is_enrolling(void)
{
    return s_client.enroll_running;
}

/*===========================================================================*/
/*                          Utility Functions                                 */
/*===========================================================================*/

const char *ts_pki_enroll_status_to_str(ts_pki_enroll_status_t status)
{
    switch (status) {
        case TS_PKI_ENROLL_PENDING:    return "PENDING";
        case TS_PKI_ENROLL_APPROVED:   return "APPROVED";
        case TS_PKI_ENROLL_REJECTED:   return "REJECTED";
        case TS_PKI_ENROLL_NOT_FOUND:  return "NOT_FOUND";
        case TS_PKI_ENROLL_ERROR:      return "ERROR";
        default:                       return "UNKNOWN";
    }
}

static bool ts_pki_client_is_server_reachable_locked(void)
{
    if (!s_client.initialized) return false;
    
    char url[128];
    server_url(url, sizeof(url), "/api/health");
    
    int http_code = 0;
    esp_err_t ret = do_http_request(url, HTTP_METHOD_GET, NULL, &http_code, NULL);
    
    return (ret == ESP_OK && http_code == 200);
}

/*===========================================================================*/
/*                          Private Functions                                 */
/*===========================================================================*/

static esp_err_t get_device_id(void)
{
    // 尝试从证书状态获取设备 ID
    ts_cert_pki_status_t status;
    if (ts_cert_get_status(&status) == ESP_OK && 
        status.cert_info.subject_cn[0] != '\0') {
        strncpy(s_client.device_id, status.cert_info.subject_cn, 
                sizeof(s_client.device_id) - 1);
        return ESP_OK;
    }
    
    // 使用 MAC 地址生成设备 ID
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_client.device_id, sizeof(s_client.device_id),
             "TIANSHAN-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return ESP_OK;
}

static esp_err_t do_http_request(const char *url, esp_http_client_method_t method,
                                  const char *post_data, int *http_code, cJSON **response)
{
    if (!http_code) return ESP_ERR_INVALID_ARG;
    *http_code = 0;
    if (response) *response = NULL;
    unsigned timeout_ms = TS_PKI_HTTP_TIMEOUT_MS;
    if (s_request_deadline) {
        int64_t remaining = (s_request_deadline - esp_timer_get_time()) / 1000;
        if (remaining <= 0) return ESP_ERR_TIMEOUT;
        if (remaining < timeout_ms) timeout_ms = (unsigned)remaining;
    }
    ts_http_fetch_config_t request = {
        .url = url, .method = method, .body = post_data,
        .content_type = post_data ? "application/json" : NULL,
        .timeout_ms = timeout_ms, .maximum_body = HTTP_RESP_BUF_SIZE - 1, .follow_redirects = true,
        .current = request_current, .context = xTaskGetCurrentTaskHandle(),
    };
    ts_http_fetch_result_t reply = {0};
    esp_err_t ret = ts_http_fetch(&request, &reply);
    if (ret == ESP_OK && !request_current(request.context)) ret = ESP_ERR_INVALID_STATE;
    if (ret == ESP_OK) {
        *http_code = reply.status;
        if (response && (reply.status == 200 || reply.status == 201)) {
            const char *end = NULL;
            cJSON *json = reply.body ? cJSON_ParseWithLengthOpts(reply.body, reply.length + 1, &end, true) : NULL;
            if (!cJSON_IsObject(json) || end != reply.body + reply.length) {
                cJSON_Delete(json);
                ret = ESP_ERR_INVALID_RESPONSE;
            } else *response = json;
        }
        ESP_LOGD(TAG, "PKI HTTP status=%d", *http_code);
    } else ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(ret));
    free(reply.body);
    return ret;
}

static void enroll_wait(uint32_t ms)
{
    while (ms && request_current(xTaskGetCurrentTaskHandle())) {
        uint32_t step = ms < 100 ? ms : 100;
        vTaskDelay(pdMS_TO_TICKS(step)); ms -= step;
    }
}

static void auto_enroll_task(void *arg)
{
    ESP_LOGI(TAG, "Auto-enrollment task started");
    
    // 等待网络就绪
    enroll_wait(5000);
    
    if (!request_current(xTaskGetCurrentTaskHandle())) goto done;

    // 检查是否已有有效证书
    ts_cert_pki_status_t cert_status;
    if (ts_cert_get_status(&cert_status) == ESP_OK && 
        (cert_status.has_certificate && cert_status.has_private_key &&
        cert_status.cert_info.validity != TS_CERT_VALIDITY_EXPIRED)) {
        ESP_LOGI(TAG, "Stored certificate retained; checking readiness");
        if (s_client.callback) {
            s_client.callback(ts_cert_prerequisites(&cert_status, true) ? TS_PKI_ENROLL_APPROVED : TS_PKI_ENROLL_PENDING,
                              "Credentials already stored; check time and CA readiness",
                              s_client.callback_data);
        }
        s_client.enroll_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    // 等待 PKI 服务器可达
    int retry = 0;
    while (request_current(xTaskGetCurrentTaskHandle()) && retry < 30) {
        if (ts_pki_client_is_server_reachable()) {
            ESP_LOGI(TAG, "PKI server is reachable");
            break;
        }
        ESP_LOGW(TAG, "PKI server not reachable, retrying...");
        enroll_wait(10000);
        retry++;
    }
    
    if (!request_current(xTaskGetCurrentTaskHandle())) {
        s_client.enroll_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    // 提交 CSR
    esp_err_t ret = ts_pki_client_submit_csr();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to submit CSR: %s", esp_err_to_name(ret));
        if (s_client.callback) {
            s_client.callback(TS_PKI_ENROLL_ERROR, "CSR submission failed",
                              s_client.callback_data);
        }
        // 重试
        enroll_wait(60000);
    }
    
    if (s_client.callback) {
        s_client.callback(TS_PKI_ENROLL_PENDING, "CSR submitted, awaiting approval",
                          s_client.callback_data);
    }
    
    // 轮询等待批准
    uint32_t attempts = 0;
    ts_pki_enroll_status_t status;
    
    while (request_current(xTaskGetCurrentTaskHandle())) {
        if (s_client.config.max_poll_attempts > 0 && 
            attempts >= s_client.config.max_poll_attempts) {
            ESP_LOGE(TAG, "Max poll attempts reached");
            if (s_client.callback) {
                s_client.callback(TS_PKI_ENROLL_ERROR, "Enrollment timed out",
                                  s_client.callback_data);
            }
            break;
        }
        
        enroll_wait(s_client.config.poll_interval_sec * 1000);
        
        if (!request_current(xTaskGetCurrentTaskHandle())) break;
        ret = ts_pki_client_check_status(&status);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to check status, retrying...");
            attempts++;
            continue;
        }
        
        switch (status) {
            case TS_PKI_ENROLL_APPROVED:
                ESP_LOGI(TAG, "CSR approved!");
                ret = ts_pki_client_install_certificate();
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Certificate installed successfully!");
                    if (s_client.callback) {
                        s_client.callback(TS_PKI_ENROLL_APPROVED, 
                                          "Enrollment complete",
                                          s_client.callback_data);
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to install certificate");
                    if (s_client.callback) {
                        s_client.callback(TS_PKI_ENROLL_ERROR,
                                          "Certificate installation failed",
                                          s_client.callback_data);
                    }
                }
                goto done;
                
            case TS_PKI_ENROLL_REJECTED:
                ESP_LOGE(TAG, "CSR was rejected");
                if (s_client.callback) {
                    s_client.callback(TS_PKI_ENROLL_REJECTED, 
                                      "CSR rejected by admin",
                                      s_client.callback_data);
                }
                goto done;
                
            case TS_PKI_ENROLL_PENDING:
                ESP_LOGD(TAG, "Still pending... (%lu attempts)", 
                         (unsigned long)attempts);
                break;
                
            case TS_PKI_ENROLL_NOT_FOUND:
                ESP_LOGW(TAG, "CSR not found, resubmitting...");
                ts_pki_client_submit_csr();
                break;
                
            default:
                break;
        }
        
        attempts++;
    }
    
done:
    ESP_LOGI(TAG, "Auto-enrollment task finished");
    s_client.enroll_running = false;
    vTaskDelete(NULL);
}

/* Public entry points; busy never starts a second request or changes its target. */
esp_err_t ts_pki_client_init_with_config(const ts_pki_client_config_t *config)
{
    if (!operation_enter()) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ts_pki_client_init_with_config_locked(config);
    xSemaphoreGiveRecursive(s_operation);
    return result;
}

esp_err_t ts_pki_client_set_server(const char *host, uint16_t port)
{
    if (!operation_enter()) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ts_pki_client_set_server_locked(host, port);
    xSemaphoreGiveRecursive(s_operation);
    return result;
}

esp_err_t ts_pki_client_get_server(char *host, uint16_t *port)
{
    if (!operation_enter()) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ts_pki_client_get_server_locked(host, port);
    xSemaphoreGiveRecursive(s_operation);
    return result;
}

esp_err_t ts_pki_client_submit_csr(void)
{
    if (!operation_enter()) return ESP_ERR_INVALID_STATE;
    if (!enrollment_owner()) { xSemaphoreGiveRecursive(s_operation); return ESP_ERR_INVALID_STATE; }
    esp_err_t result = ts_pki_client_submit_csr_locked();
    xSemaphoreGiveRecursive(s_operation);
    return result;
}

esp_err_t ts_pki_client_check_status(ts_pki_enroll_status_t *status)
{
    if (status) *status = TS_PKI_ENROLL_ERROR;
    if (!operation_enter()) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ts_pki_client_check_status_locked(status);
    xSemaphoreGiveRecursive(s_operation);
    return result;
}

esp_err_t ts_pki_client_install_certificate(void)
{
    if (!operation_enter()) return ESP_ERR_INVALID_STATE;
    if (!enrollment_owner()) { xSemaphoreGiveRecursive(s_operation); return ESP_ERR_INVALID_STATE; }
    esp_err_t result = ts_pki_client_install_certificate_locked();
    xSemaphoreGiveRecursive(s_operation);
    return result;
}

esp_err_t ts_pki_client_enroll_blocking(uint32_t timeout_sec)
{
    if (!operation_enter()) return ESP_ERR_INVALID_STATE;
    if (!enrollment_owner()) { xSemaphoreGiveRecursive(s_operation); return ESP_ERR_INVALID_STATE; }
    s_request_deadline = timeout_sec ? esp_timer_get_time() + (int64_t)timeout_sec * 1000000 : 0;
    esp_err_t result = ts_pki_client_enroll_blocking_locked(timeout_sec);
    s_request_deadline = 0;
    xSemaphoreGiveRecursive(s_operation);
    return result;
}

esp_err_t ts_pki_client_start_auto_enroll(ts_pki_enroll_callback_t callback,
                                           void *user_data)
{
    if (!operation_enter()) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ts_pki_client_start_auto_enroll_locked(callback, user_data);
    xSemaphoreGiveRecursive(s_operation);
    return result;
}

bool ts_pki_client_is_server_reachable(void)
{
    if (!operation_enter()) return false;
    bool result = ts_pki_client_is_server_reachable_locked();
    xSemaphoreGiveRecursive(s_operation);
    return result;
}
