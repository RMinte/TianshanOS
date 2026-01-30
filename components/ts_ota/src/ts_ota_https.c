/**
 * @file ts_ota_https.c
 * @brief TianShanOS OTA HTTPS Download Implementation
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "ts_ota.h"
#include "ts_event.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

/* PSRAM-first allocation */
#define OTA_STRDUP(s) ({ const char *_s = (s); size_t _len = _s ? strlen(_s) + 1 : 0; char *_p = _len ? heap_caps_malloc(_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL; if (_p) memcpy(_p, _s, _len); else if (_len) { _p = strdup(_s); } _p; })

static const char *TAG = "ts_ota_https";

// OTA task handle
static TaskHandle_t s_ota_task_handle = NULL;
static ts_ota_config_t s_ota_config;
static char *s_ota_url = NULL;  // Deep copy of URL
static bool s_ota_running = false;
static bool s_ota_abort_requested = false;  // Abort flag

// Forward declarations
static void https_ota_task(void *arg);
static esp_err_t http_event_handler(esp_http_client_event_t *evt);

/**
 * @brief Start HTTPS OTA update
 */
esp_err_t ts_ota_start_https(const ts_ota_config_t *config)
{
    if (!config || !config->url) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ota_running) {
        ESP_LOGE(TAG, "OTA already running");
        return ESP_ERR_INVALID_STATE;
    }

    // Reset abort flag
    s_ota_abort_requested = false;

    // Deep copy URL (caller's buffer may be on stack)
    if (s_ota_url) {
        free(s_ota_url);
    }
    s_ota_url = OTA_STRDUP(config->url);
    if (!s_ota_url) {
        ESP_LOGE(TAG, "Failed to allocate URL buffer");
        return ESP_ERR_NO_MEM;
    }

    // Copy config and update URL pointer
    memcpy(&s_ota_config, config, sizeof(ts_ota_config_t));
    s_ota_config.url = s_ota_url;
    
    // Create OTA task
    BaseType_t ret = xTaskCreate(
        https_ota_task,
        "ota_https",
        CONFIG_TS_OTA_TASK_STACK_SIZE,
        NULL,
        CONFIG_TS_OTA_TASK_PRIORITY,
        &s_ota_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_ERR_NO_MEM;
    }

    s_ota_running = true;
    return ESP_OK;
}

/**
 * @brief HTTP event handler for progress tracking
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADERS_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER: %s: %s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief HTTPS OTA download task
 */
static void https_ota_task(void *arg)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Starting HTTPS OTA from: %s", s_ota_config.url);
    
    // Update global state - connecting
    ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, 0, 0, "正在连接服务器...");

    // 预先检查网络连通性 - 解析 URL 中的主机名
    // 提取主机名进行 DNS 查询
    const char *url = s_ota_config.url;
    const char *host_start = strstr(url, "://");
    if (host_start) {
        host_start += 3;  // 跳过 "://"
        char host[128] = {0};
        const char *host_end = strchr(host_start, ':');
        if (!host_end) host_end = strchr(host_start, '/');
        if (!host_end) host_end = host_start + strlen(host_start);
        
        size_t host_len = host_end - host_start;
        if (host_len < sizeof(host)) {
            strncpy(host, host_start, host_len);
            ESP_LOGI(TAG, "OTA target host: %s", host);
            
            // 尝试 DNS 解析
            struct addrinfo hints = {
                .ai_family = AF_INET,
                .ai_socktype = SOCK_STREAM,
            };
            struct addrinfo *res = NULL;
            int dns_err = getaddrinfo(host, "80", &hints, &res);
            if (dns_err != 0 || res == NULL) {
                ESP_LOGE(TAG, "DNS lookup failed for %s: %d", host, dns_err);
            } else {
                struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
                ESP_LOGI(TAG, "DNS resolved %s -> %s", host, inet_ntoa(addr->sin_addr));
                freeaddrinfo(res);
            }
        }
    }

    // Check if URL is HTTP or HTTPS
    bool is_http = (strncmp(s_ota_config.url, "http://", 7) == 0);
    bool is_https = (strncmp(s_ota_config.url, "https://", 8) == 0);
    
    // Configure HTTP client - initialize all fields explicitly
    esp_http_client_config_t http_config = {
        .url = s_ota_config.url,
        .event_handler = http_event_handler,
        .keep_alive_enable = true,
        .timeout_ms = 30000,
        .buffer_size = CONFIG_TS_OTA_BUFFER_SIZE,           // Receive buffer (default 512 is too small)
        .buffer_size_tx = 1024,                              // Transmit buffer
        // For HTTP: must set skip_cert_common_name_check to pass esp_https_ota validation
        // For HTTPS: will be configured below based on certificate settings
        .skip_cert_common_name_check = is_http ? true : s_ota_config.skip_cert_verify,
    };
    
    // For plain HTTP, bypass SSL entirely
    if (is_http) {
        http_config.transport_type = HTTP_TRANSPORT_OVER_TCP;
        ESP_LOGW(TAG, "Using plain HTTP (no TLS) - NOT RECOMMENDED for production!");
    } else if (is_https) {
        // Certificate configuration (only for HTTPS)
        if (s_ota_config.cert_pem) {
            http_config.cert_pem = s_ota_config.cert_pem;
        } else if (!s_ota_config.skip_cert_verify) {
            // Use certificate bundle for verification
            http_config.crt_bundle_attach = esp_crt_bundle_attach;
        }

        if (s_ota_config.skip_cert_verify) {
            ESP_LOGW(TAG, "Certificate verification disabled - NOT RECOMMENDED for production!");
        }
    } else {
        ESP_LOGE(TAG, "Invalid URL scheme, must be http:// or https://");
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    // Configure HTTPS OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    
    ESP_LOGI(TAG, "Calling esp_https_ota_begin with URL: %s", s_ota_config.url);
    ESP_LOGI(TAG, "HTTP config: timeout=%d, buffer_size=%d, skip_cert=%d",
             (int)http_config.timeout_ms, (int)http_config.buffer_size,
             (int)http_config.skip_cert_common_name_check);
    
    ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s (0x%x)", esp_err_to_name(ret), ret);
        // 更详细的错误信息
        const char *error_detail;
        switch (ret) {
            case ESP_ERR_INVALID_ARG:
                error_detail = "参数无效";
                break;
            case ESP_ERR_NO_MEM:
                error_detail = "内存不足";
                break;
            case ESP_ERR_HTTP_CONNECT:
                error_detail = "HTTP连接失败";
                break;
            case ESP_ERR_HTTP_FETCH_HEADER:
                error_detail = "获取响应头失败";
                break;
            case ESP_ERR_INVALID_STATE:
                error_detail = "状态无效";
                break;
            case ESP_ERR_HTTP_INVALID_TRANSPORT:
                error_detail = "传输协议无效";
                break;
            default:
                error_detail = "连接服务器失败";
                break;
        }
        ts_ota_set_error(TS_OTA_ERR_CONNECTION_FAILED, error_detail);
        goto cleanup;
    }
    
    // Update state - connected, getting image info
    ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, 0, 0, "正在获取固件信息...");

    // Get image info
    esp_app_desc_t app_desc;
    ret = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed: %s", esp_err_to_name(ret));
        ts_ota_set_error(TS_OTA_ERR_DOWNLOAD_FAILED, "获取固件信息失败");
        esp_https_ota_abort(https_ota_handle);
        goto cleanup;
    }

    ESP_LOGI(TAG, "New firmware: %s, version: %s", app_desc.project_name, app_desc.version);

    // Version check (if enabled)
    #if CONFIG_TS_OTA_VERSION_CHECK
    if (!s_ota_config.allow_downgrade) {
        const esp_app_desc_t *running_app = esp_app_get_description();
        int cmp = ts_ota_compare_versions(app_desc.version, running_app->version);
        if (cmp <= 0) {
            ESP_LOGW(TAG, "New version (%s) is not newer than current (%s)",
                     app_desc.version, running_app->version);
            if (cmp < 0) {
                ESP_LOGE(TAG, "Downgrade not allowed");
                esp_https_ota_abort(https_ota_handle);
                ret = ESP_ERR_INVALID_VERSION;
                goto cleanup;
            }
        }
    }
    #endif

    // Get total size
    int image_size = esp_https_ota_get_image_size(https_ota_handle);
    ESP_LOGI(TAG, "Image size: %d bytes", image_size);
    
    // Update global state - start downloading
    ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, 0, image_size, "正在下载固件...");

    // Download and write firmware
    size_t received = 0;
    while (1) {
        // Check abort flag
        if (s_ota_abort_requested) {
            ESP_LOGI(TAG, "OTA abort requested, stopping download");
            esp_https_ota_abort(https_ota_handle);
            ts_ota_update_progress(TS_OTA_STATE_IDLE, 0, 0, "已中止");
            ret = ESP_ERR_INVALID_STATE;
            goto cleanup_aborted;
        }
        
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Update progress
        received = esp_https_ota_get_image_len_read(https_ota_handle);
        
        if (image_size > 0) {
            int percent = (received * 100) / image_size;
            ESP_LOGI(TAG, "Downloaded: %zu / %d bytes (%d%%)", received, image_size, percent);
            
            // Update global state with progress
            ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, received, image_size, "正在下载固件...");
        } else {
            ESP_LOGI(TAG, "Downloaded: %zu bytes", received);
            ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, received, 0, "正在下载固件...");
        }

        // Call progress callback (optional user callback)
        if (s_ota_config.progress_cb) {
            ts_ota_progress_t progress = {
                .state = TS_OTA_STATE_DOWNLOADING,
                .error = TS_OTA_ERR_NONE,
                .total_size = image_size,
                .received_size = received,
                .progress_percent = (image_size > 0) ? (received * 100 / image_size) : 0,
                .status_msg = "正在下载..."
            };
            s_ota_config.progress_cb(&progress, s_ota_config.user_data);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(https_ota_handle);
        ts_ota_set_error(TS_OTA_ERR_DOWNLOAD_FAILED, "下载失败");
        goto cleanup;
    }

    // Verify and finish
    ts_ota_update_progress(TS_OTA_STATE_VERIFYING, received, image_size, "正在验证固件...");
    
    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "Complete data was not received");
        esp_https_ota_abort(https_ota_handle);
        ts_ota_set_error(TS_OTA_ERR_DOWNLOAD_FAILED, "固件数据不完整");
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    // Writing to flash
    ts_ota_update_progress(TS_OTA_STATE_WRITING, received, image_size, "正在写入闪存...");
    
    ret = esp_https_ota_finish(https_ota_handle);
    if (ret != ESP_OK) {
        // ESP_ERR_OTA_VALIDATE_FAILED is 0x1509
        if (ret == (esp_err_t)0x1509) {
            ESP_LOGE(TAG, "Image validation failed");
            ts_ota_set_error(TS_OTA_ERR_VERIFY_FAILED, "固件验证失败");
        } else {
            ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(ret));
            ts_ota_set_error(TS_OTA_ERR_WRITE_FAILED, "写入闪存失败");
        }
        goto cleanup;
    }

    ESP_LOGI(TAG, "OTA update successful!");

    // Update global state - completed
    ts_ota_set_completed("升级完成，等待重启");

    // Post completion event
    ts_event_post(TS_EVENT_BASE_OTA, TS_EVENT_OTA_COMPLETED, NULL, 0, 0);

    // Notify callback
    if (s_ota_config.progress_cb) {
        ts_ota_progress_t progress = {
            .state = TS_OTA_STATE_PENDING_REBOOT,
            .error = TS_OTA_ERR_NONE,
            .total_size = image_size,
            .received_size = received,
            .progress_percent = 100,
            .status_msg = "升级完成，等待重启"
        };
        s_ota_config.progress_cb(&progress, s_ota_config.user_data);
    }

    // Auto reboot if requested
    if (s_ota_config.auto_reboot) {
        ts_ota_update_progress(TS_OTA_STATE_PENDING_REBOOT, received, image_size, "即将重启...");
        ESP_LOGI(TAG, "Rebooting in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
    goto task_exit;

cleanup:
    if (ret != ESP_OK) {
        // Error state already set by ts_ota_set_error calls above
        // Only set generic error if not already set
        ts_ota_progress_t progress;
        ts_ota_get_progress(&progress);
        if (progress.state != TS_OTA_STATE_ERROR) {
            ts_ota_set_error(TS_OTA_ERR_DOWNLOAD_FAILED, "下载失败");
        }

        if (s_ota_config.progress_cb) {
            ts_ota_progress_t cb_progress = {
                .state = TS_OTA_STATE_ERROR,
                .error = TS_OTA_ERR_DOWNLOAD_FAILED,
                .status_msg = "下载失败"
            };
            s_ota_config.progress_cb(&cb_progress, s_ota_config.user_data);
        }
    }
    goto task_exit;

cleanup_aborted:
    // Handle abort case - don't set error, just clean up
    ESP_LOGI(TAG, "OTA aborted by user");
    if (s_ota_config.progress_cb) {
        ts_ota_progress_t cb_progress = {
            .state = TS_OTA_STATE_IDLE,
            .error = TS_OTA_ERR_ABORTED,
            .status_msg = "已中止"
        };
        s_ota_config.progress_cb(&cb_progress, s_ota_config.user_data);
    }

task_exit:
    s_ota_running = false;
    s_ota_abort_requested = false;  // Reset abort flag
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Check for updates from URL
 */
esp_err_t ts_ota_check_update_https(const char *url, bool *available,
                                     char *new_version, size_t new_version_len)
{
    if (!url || !available) {
        return ESP_ERR_INVALID_ARG;
    }

    *available = false;

    // This would typically check a version manifest or firmware header
    // For now, we'll just try to get the firmware header

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    // Set range header to get only the app description
    esp_http_client_set_header(client, "Range", "bytes=0-288");
    
    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 || status == 206) {
            // In a real implementation, we'd parse the firmware header
            // and compare versions
            *available = true;
            if (new_version && new_version_len > 0) {
                strncpy(new_version, "检测到新版本", new_version_len - 1);
            }
        }
    }

    esp_http_client_cleanup(client);
    return ret;
}

/**
 * @brief Abort HTTPS OTA download
 */
void ts_ota_abort_https(void)
{
    s_ota_abort_requested = true;
    ESP_LOGI(TAG, "HTTPS OTA abort requested");
}

/**
 * @brief Check if HTTPS OTA is running
 */
bool ts_ota_https_is_running(void)
{
    return s_ota_running;
}
