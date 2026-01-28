/**
 * @file ts_ota_www.c
 * @brief TianShanOS WWW (WebUI) Partition OTA Implementation
 * 
 * Downloads and writes www.bin to the SPIFFS www partition.
 * This is the second step of two-step OTA upgrade.
 * Supports both HTTP download and SD card file sources.
 */

#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"

/* PSRAM-first allocation for OTA buffers */
#define OTA_MALLOC(size) ({ void *p = heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); p ? p : malloc(size); })
#include "esp_crt_bundle.h"
#include "esp_partition.h"
#include "ts_ota.h"
#include "ts_event.h"

static const char *TAG = "ts_ota_www";

// WWW OTA task handle
static TaskHandle_t s_www_ota_task_handle = NULL;
static bool s_www_ota_running = false;
static bool s_www_ota_abort_requested = false;

// Configuration for current www OTA
typedef struct {
    char url[256];           // URL or file path
    bool skip_cert_verify;
    bool from_sdcard;        // true = SD card file, false = HTTP URL
    ts_ota_progress_cb_t progress_cb;
    void *user_data;
} www_ota_config_t;

static www_ota_config_t s_www_config;

// WWW OTA progress tracking
static ts_ota_progress_t s_www_progress = {0};
static SemaphoreHandle_t s_www_progress_mutex = NULL;

// Forward declarations
static void www_ota_task(void *arg);
static void www_ota_sdcard_task(void *arg);

/**
 * @brief Initialize www OTA module
 */
esp_err_t ts_ota_www_init(void)
{
    if (!s_www_progress_mutex) {
        s_www_progress_mutex = xSemaphoreCreateMutex();
        if (!s_www_progress_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

/**
 * @brief Update www OTA progress
 */
static void www_ota_update_progress(ts_ota_state_t state, size_t received, size_t total, const char *msg)
{
    if (xSemaphoreTake(s_www_progress_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_www_progress.state = state;
        s_www_progress.received_size = received;
        s_www_progress.total_size = total;
        if (total > 0) {
            s_www_progress.progress_percent = (received * 100) / total;
        } else {
            s_www_progress.progress_percent = 0;
        }
        s_www_progress.status_msg = msg;
        xSemaphoreGive(s_www_progress_mutex);
    }
    
    // Call user callback if set
    if (s_www_config.progress_cb) {
        ts_ota_progress_t progress = {
            .state = state,
            .received_size = received,
            .total_size = total,
            .progress_percent = (total > 0) ? (received * 100 / total) : 0,
            .status_msg = msg
        };
        s_www_config.progress_cb(&progress, s_www_config.user_data);
    }
}

/**
 * @brief Get www OTA progress
 */
esp_err_t ts_ota_www_get_progress(ts_ota_progress_t *progress)
{
    if (!progress || !s_www_progress_mutex) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_www_progress_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(progress, &s_www_progress, sizeof(ts_ota_progress_t));
        xSemaphoreGive(s_www_progress_mutex);
        return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief Start WWW partition OTA from URL
 */
esp_err_t ts_ota_www_start(const char *url, bool skip_cert_verify,
                            ts_ota_progress_cb_t progress_cb, void *user_data)
{
    if (!url || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_www_ota_running) {
        ESP_LOGE(TAG, "WWW OTA already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Initialize if not done
    if (!s_www_progress_mutex) {
        esp_err_t ret = ts_ota_www_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // Reset abort flag
    s_www_ota_abort_requested = false;
    
    // Store config
    strncpy(s_www_config.url, url, sizeof(s_www_config.url) - 1);
    s_www_config.url[sizeof(s_www_config.url) - 1] = '\0';
    s_www_config.skip_cert_verify = skip_cert_verify;
    s_www_config.progress_cb = progress_cb;
    s_www_config.user_data = user_data;
    
    /* Create task - MUST use DRAM stack because OTA writes to SPI Flash.
     * SPI Flash operations disable cache, and PSRAM access requires cache.
     */
    BaseType_t ret = xTaskCreate(
        www_ota_task,
        "ota_www",
        8192,
        NULL,
        5,
        &s_www_ota_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WWW OTA task");
        return ESP_ERR_NO_MEM;
    }
    
    s_www_ota_running = true;
    return ESP_OK;
}

/**
 * @brief Abort WWW OTA
 */
esp_err_t ts_ota_www_abort(void)
{
    if (!s_www_ota_running) {
        return ESP_OK;
    }
    
    s_www_ota_abort_requested = true;
    ESP_LOGI(TAG, "WWW OTA abort requested");
    return ESP_OK;
}

/**
 * @brief Check if WWW OTA is running
 */
bool ts_ota_www_is_running(void)
{
    return s_www_ota_running;
}

/**
 * @brief HTTP event handler for download
 */
static esp_err_t www_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief WWW OTA download and write task
 */
static void www_ota_task(void *arg)
{
    esp_err_t ret;
    esp_http_client_handle_t client = NULL;
    char *buffer = NULL;
    const size_t buffer_size = 4096;
    
    ESP_LOGI(TAG, "Starting WWW OTA from: %s", s_www_config.url);
    
    // Update progress - connecting
    www_ota_update_progress(TS_OTA_STATE_DOWNLOADING, 0, 0, "正在连接服务器...");
    
    // Find www partition
    const esp_partition_t *www_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    
    if (!www_partition) {
        ESP_LOGE(TAG, "WWW partition not found");
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "找不到 www 分区");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "WWW partition: addr=0x%lx, size=%lu", 
             www_partition->address, www_partition->size);
    
    // Allocate download buffer (PSRAM first)
    buffer = OTA_MALLOC(buffer_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "内存不足");
        goto cleanup;
    }
    
    // Configure HTTP client
    bool is_http = (strncmp(s_www_config.url, "http://", 7) == 0);
    
    esp_http_client_config_t http_config = {
        .url = s_www_config.url,
        .event_handler = www_http_event_handler,
        .timeout_ms = 30000,
        .buffer_size = buffer_size,
        .skip_cert_common_name_check = is_http ? true : s_www_config.skip_cert_verify,
    };
    
    if (is_http) {
        http_config.transport_type = HTTP_TRANSPORT_OVER_TCP;
        ESP_LOGW(TAG, "Using plain HTTP - NOT RECOMMENDED for production!");
    } else if (!s_www_config.skip_cert_verify) {
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
    }
    
    client = esp_http_client_init(&http_config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "HTTP 初始化失败");
        goto cleanup;
    }
    
    // Open connection
    ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(ret));
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "连接服务器失败");
        goto cleanup;
    }
    
    // Get content length
    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    
    // Also try to get content length from header directly
    if (content_length <= 0) {
        content_length = esp_http_client_get_content_length(client);
    }
    
    ESP_LOGI(TAG, "HTTP status: %d, content length: %d", status_code, content_length);
    
    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "服务器返回错误");
        goto cleanup;
    }
    
    // Handle chunked transfer or unknown content length
    bool chunked_mode = (content_length <= 0);
    size_t max_size = www_partition->size;
    
    if (chunked_mode) {
        ESP_LOGW(TAG, "Content-Length not provided, using chunked mode (max %lu bytes)", max_size);
        content_length = 0;  // Will be updated as we download
    } else if (content_length > (int)max_size) {
        ESP_LOGE(TAG, "File too large: %d > %lu", content_length, max_size);
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "文件太大，超出分区容量");
        goto cleanup;
    }
    
    www_ota_update_progress(TS_OTA_STATE_DOWNLOADING, 0, content_length, "正在下载 WebUI...");
    
    // Erase partition
    ESP_LOGI(TAG, "Erasing www partition...");
    www_ota_update_progress(TS_OTA_STATE_WRITING, 0, content_length, "正在擦除分区...");
    
    ret = esp_partition_erase_range(www_partition, 0, www_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(ret));
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "擦除分区失败");
        goto cleanup;
    }
    
    // Download and write
    size_t received = 0;
    size_t write_offset = 0;
    bool download_complete = false;
    
    while (!download_complete) {
        // Check abort
        if (s_www_ota_abort_requested) {
            ESP_LOGI(TAG, "WWW OTA aborted");
            www_ota_update_progress(TS_OTA_STATE_IDLE, 0, 0, "已中止");
            goto cleanup;
        }
        
        // Read data
        int read_len = esp_http_client_read(client, buffer, buffer_size);
        if (read_len < 0) {
            ESP_LOGE(TAG, "Read error");
            www_ota_update_progress(TS_OTA_STATE_ERROR, received, content_length, "下载失败");
            goto cleanup;
        }
        if (read_len == 0) {
            // Connection closed - download complete
            download_complete = true;
            break;
        }
        
        // Check if we exceed partition size
        if (received + read_len > max_size) {
            ESP_LOGE(TAG, "Download exceeds partition size");
            www_ota_update_progress(TS_OTA_STATE_ERROR, received, max_size, "文件太大，超出分区容量");
            goto cleanup;
        }
        
        // Write to partition
        ret = esp_partition_write(www_partition, write_offset, buffer, read_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Write error at offset %zu: %s", write_offset, esp_err_to_name(ret));
            www_ota_update_progress(TS_OTA_STATE_ERROR, received, content_length, "写入失败");
            goto cleanup;
        }
        
        received += read_len;
        write_offset += read_len;
        
        // Update progress
        int percent = 0;
        if (content_length > 0) {
            percent = (received * 100) / content_length;
        }
        ESP_LOGI(TAG, "Downloaded: %zu bytes%s", received, 
                 content_length > 0 ? "" : " (chunked)");
        www_ota_update_progress(TS_OTA_STATE_DOWNLOADING, received, 
                                content_length > 0 ? content_length : received, 
                                "正在下载 WebUI...");
        
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // For non-chunked mode, check if complete
        if (!chunked_mode && received >= (size_t)content_length) {
            download_complete = true;
        }
    }
    
    // Verify download
    if (!chunked_mode && received != (size_t)content_length) {
        ESP_LOGE(TAG, "Incomplete download: %zu / %d", received, content_length);
        www_ota_update_progress(TS_OTA_STATE_ERROR, received, content_length, "下载不完整");
        goto cleanup;
    }
    
    if (received == 0) {
        ESP_LOGE(TAG, "No data received");
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "未收到数据");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "WWW OTA completed successfully! Total: %zu bytes", received);
    www_ota_update_progress(TS_OTA_STATE_PENDING_REBOOT, received, received, "WebUI 升级完成");
    
    // Post event
    ts_event_post(TS_EVENT_BASE_OTA, TS_EVENT_OTA_COMPLETED, NULL, 0, 0);
    
cleanup:
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (buffer) {
        free(buffer);
    }
    
    s_www_ota_running = false;
    s_www_ota_abort_requested = false;
    s_www_ota_task_handle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
//                         SD Card WWW OTA Functions
// ============================================================================

/**
 * @brief Start WWW partition OTA from SD card file
 */
esp_err_t ts_ota_www_start_sdcard(const char *filepath, 
                                   ts_ota_progress_cb_t progress_cb, 
                                   void *user_data)
{
    if (!filepath || strlen(filepath) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_www_ota_running) {
        ESP_LOGE(TAG, "WWW OTA already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if file exists
    struct stat st;
    if (stat(filepath, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "WWW file: %s, size: %ld bytes", filepath, st.st_size);
    
    // Initialize if not done
    if (!s_www_progress_mutex) {
        esp_err_t ret = ts_ota_www_init();
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    // Reset abort flag
    s_www_ota_abort_requested = false;
    
    // Store config
    strncpy(s_www_config.url, filepath, sizeof(s_www_config.url) - 1);
    s_www_config.url[sizeof(s_www_config.url) - 1] = '\0';
    s_www_config.from_sdcard = true;
    s_www_config.progress_cb = progress_cb;
    s_www_config.user_data = user_data;
    
    /* Create task - MUST use DRAM stack because OTA writes to SPI Flash.
     * SPI Flash operations disable cache, and PSRAM access requires cache.
     */
    BaseType_t ret = xTaskCreate(
        www_ota_sdcard_task,
        "ota_www_sd",
        8192,
        NULL,
        5,
        &s_www_ota_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WWW OTA task");
        return ESP_ERR_NO_MEM;
    }
    
    s_www_ota_running = true;
    return ESP_OK;
}

/**
 * @brief SD Card WWW OTA task
 */
static void www_ota_sdcard_task(void *arg)
{
    esp_err_t ret;
    FILE *f = NULL;
    char *buffer = NULL;
    const size_t buffer_size = 4096;
    
    ESP_LOGI(TAG, "Starting WWW OTA from SD card: %s", s_www_config.url);
    
    // Update progress
    www_ota_update_progress(TS_OTA_STATE_DOWNLOADING, 0, 0, "正在读取文件...");
    
    // Find www partition
    const esp_partition_t *www_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    
    if (!www_partition) {
        ESP_LOGE(TAG, "WWW partition not found");
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "找不到 www 分区");
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "WWW partition: addr=0x%lx, size=%lu", 
             www_partition->address, www_partition->size);
    
    // Open file
    f = fopen(s_www_config.url, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", s_www_config.url);
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "打开文件失败");
        goto cleanup;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    ESP_LOGI(TAG, "File size: %zu bytes", file_size);
    
    if (file_size > www_partition->size) {
        ESP_LOGE(TAG, "File too large: %zu > %lu", file_size, www_partition->size);
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "文件太大，超出分区容量");
        goto cleanup;
    }
    
    // Allocate buffer (PSRAM first)
    buffer = OTA_MALLOC(buffer_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "内存不足");
        goto cleanup;
    }
    
    // Erase partition
    ESP_LOGI(TAG, "Erasing www partition...");
    www_ota_update_progress(TS_OTA_STATE_WRITING, 0, file_size, "正在擦除分区...");
    
    ret = esp_partition_erase_range(www_partition, 0, www_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(ret));
        www_ota_update_progress(TS_OTA_STATE_ERROR, 0, 0, "擦除分区失败");
        goto cleanup;
    }
    
    // Read and write
    size_t written = 0;
    size_t write_offset = 0;
    
    while (written < file_size) {
        // Check abort
        if (s_www_ota_abort_requested) {
            ESP_LOGI(TAG, "WWW OTA aborted");
            www_ota_update_progress(TS_OTA_STATE_IDLE, 0, 0, "已中止");
            goto cleanup;
        }
        
        // Read from file
        size_t read_len = fread(buffer, 1, buffer_size, f);
        if (read_len == 0) {
            if (feof(f)) {
                break;
            }
            ESP_LOGE(TAG, "Read error");
            www_ota_update_progress(TS_OTA_STATE_ERROR, written, file_size, "读取失败");
            goto cleanup;
        }
        
        // Write to partition
        ret = esp_partition_write(www_partition, write_offset, buffer, read_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Write error at offset %zu: %s", write_offset, esp_err_to_name(ret));
            www_ota_update_progress(TS_OTA_STATE_ERROR, written, file_size, "写入失败");
            goto cleanup;
        }
        
        written += read_len;
        write_offset += read_len;
        
        // Update progress
        int percent = (written * 100) / file_size;
        ESP_LOGI(TAG, "Written: %zu / %zu bytes (%d%%)", written, file_size, percent);
        www_ota_update_progress(TS_OTA_STATE_WRITING, written, file_size, "正在写入 WebUI...");
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "WWW OTA from SD card completed! Total: %zu bytes", written);
    www_ota_update_progress(TS_OTA_STATE_PENDING_REBOOT, written, written, "WebUI 升级完成");
    
    // Post event
    ts_event_post(TS_EVENT_BASE_OTA, TS_EVENT_OTA_COMPLETED, NULL, 0, 0);
    
cleanup:
    if (f) {
        fclose(f);
    }
    if (buffer) {
        free(buffer);
    }
    
    s_www_ota_running = false;
    s_www_ota_abort_requested = false;
    s_www_ota_task_handle = NULL;
    vTaskDelete(NULL);
}
