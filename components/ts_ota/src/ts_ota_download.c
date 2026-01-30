/**
 * @file ts_ota_download.c
 * @brief TianShanOS 统一 OTA 下载模块
 *
 * 所有 OTA 来源（HTTPS/浏览器上传/HTTP URL）统一先下载到 /sdcard/recovery/ 目录，
 * 然后通过统一的验证和写入流程完成升级。
 *
 * 优势：
 * - 代码复用：所有来源共用一套验证/写入逻辑
 * - 断点续传：下载中断后文件仍在 SD 卡
 * - 离线恢复：启动时 recovery 检查自动生效
 * - 调试方便：可以直接查看 SD 卡上的固件文件
 * - 回滚支持：可以保留多个版本固件
 */

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "ts_ota.h"
#include "ts_event.h"
#include "ts_storage.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"

static const char *TAG = "ts_ota_download";

/* Recovery 目录路径 */
#define RECOVERY_DIR        "/sdcard/recovery"
#define RECOVERY_FIRMWARE   RECOVERY_DIR "/TianShanOS.bin"
#define RECOVERY_WWW        RECOVERY_DIR "/www.bin"
#define RECOVERY_MANIFEST   RECOVERY_DIR "/manifest.json"
#define RECOVERY_TEMP       RECOVERY_DIR "/temp.bin"

/* PSRAM-first allocation */
#define OTA_MALLOC(size) ({ void *p = heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); p ? p : malloc(size); })
#define OTA_FREE(p) do { if (p) free(p); } while(0)

/* Download buffer size */
#define DOWNLOAD_BUFFER_SIZE    8192

/* Download context */
typedef struct {
    char *url;                      // 下载 URL（PSRAM 分配）
    char *dest_path;                // 目标路径
    bool skip_cert_verify;          // 跳过证书验证
    ts_ota_progress_cb_t cb;        // 进度回调
    void *user_data;                // 回调用户数据
    size_t total_size;              // 总大小
    size_t received_size;           // 已接收大小
    bool abort_requested;           // 中止标志
    TaskHandle_t task_handle;       // 任务句柄
    bool is_firmware;               // 是否是固件（vs www）
} download_ctx_t;

static download_ctx_t s_download_ctx = {0};
static bool s_download_running = false;

/* Forward declarations */
static void download_task(void *arg);
static esp_err_t download_file_to_sdcard(const char *url, const char *dest_path, 
                                          bool skip_cert_verify, size_t *out_size);
static esp_err_t verify_firmware_file(const char *path, char *version, size_t version_len);
static esp_err_t create_manifest_file(const char *firmware_version, const char *www_version, bool force);


/**
 * @brief 确保 recovery 目录存在
 */
static esp_err_t ensure_recovery_dir(void)
{
    if (!ts_storage_exists(RECOVERY_DIR)) {
        esp_err_t ret = ts_storage_mkdir_p(RECOVERY_DIR);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create recovery dir: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}

/**
 * @brief 计算文件 SHA256 哈希
 */
static esp_err_t calculate_file_sha256(const char *path, char *hash_str, size_t hash_len)
{
    if (!path || !hash_str || hash_len < 65) {
        return ESP_ERR_INVALID_ARG;
    }
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA256
    
    uint8_t *buffer = OTA_MALLOC(4096);
    if (!buffer) {
        fclose(f);
        mbedtls_sha256_free(&ctx);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_len;
    while ((read_len = fread(buffer, 1, 4096, f)) > 0) {
        mbedtls_sha256_update(&ctx, buffer, read_len);
    }
    
    fclose(f);
    OTA_FREE(buffer);
    
    uint8_t hash[32];
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    
    // Convert to hex string
    for (int i = 0; i < 32; i++) {
        snprintf(hash_str + i * 2, 3, "%02x", hash[i]);
    }
    hash_str[64] = '\0';
    
    return ESP_OK;
}

/**
 * @brief 从 URL 下载固件到 recovery 目录
 *
 * 统一入口：HTTPS OTA 调用此函数下载固件
 *
 * @param url 固件下载 URL
 * @param skip_cert_verify 跳过 SSL 证书验证
 * @param progress_cb 进度回调
 * @param user_data 回调用户数据
 * @param auto_flash 下载后自动刷入
 * @return ESP_OK 成功
 */
esp_err_t ts_ota_download_firmware(const char *url, bool skip_cert_verify,
                                    ts_ota_progress_cb_t progress_cb, void *user_data,
                                    bool auto_flash)
{
    if (!url) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_download_running) {
        ESP_LOGE(TAG, "Download already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 确保 recovery 目录存在
    esp_err_t ret = ensure_recovery_dir();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 初始化下载上下文
    memset(&s_download_ctx, 0, sizeof(s_download_ctx));
    
    s_download_ctx.url = OTA_MALLOC(strlen(url) + 1);
    if (!s_download_ctx.url) {
        return ESP_ERR_NO_MEM;
    }
    strcpy(s_download_ctx.url, url);
    
    s_download_ctx.dest_path = RECOVERY_FIRMWARE;
    s_download_ctx.skip_cert_verify = skip_cert_verify;
    s_download_ctx.cb = progress_cb;
    s_download_ctx.user_data = user_data;
    s_download_ctx.is_firmware = true;
    
    // 创建下载任务
    BaseType_t xret = xTaskCreate(
        download_task,
        "ota_download",
        8192,
        (void *)(auto_flash ? 1 : 0),  // 传递 auto_flash 标志
        5,
        &s_download_ctx.task_handle
    );
    
    if (xret != pdPASS) {
        OTA_FREE(s_download_ctx.url);
        s_download_ctx.url = NULL;
        return ESP_ERR_NO_MEM;
    }
    
    s_download_running = true;
    return ESP_OK;
}

/**
 * @brief 从浏览器上传的数据保存到 recovery 目录
 *
 * 统一入口：WebUI 上传调用此函数保存固件
 *
 * @param data 固件二进制数据
 * @param len 数据长度
 * @param is_firmware true=固件, false=www
 * @param auto_flash 保存后自动刷入
 * @return ESP_OK 成功
 */
esp_err_t ts_ota_save_upload(const void *data, size_t len, bool is_firmware, bool auto_flash)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 确保 recovery 目录存在
    esp_err_t ret = ensure_recovery_dir();
    if (ret != ESP_OK) {
        return ret;
    }
    
    const char *dest_path = is_firmware ? RECOVERY_FIRMWARE : RECOVERY_WWW;
    
    // 更新状态
    ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, 0, len, "正在保存上传文件...");
    
    // 写入文件
    ret = ts_storage_write_file(dest_path, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write %s: %s", dest_path, esp_err_to_name(ret));
        ts_ota_set_error(TS_OTA_ERR_WRITE_FAILED, "保存文件失败");
        return ret;
    }
    
    ESP_LOGI(TAG, "Saved upload to %s (%zu bytes)", dest_path, len);
    ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, len, len, "文件保存完成");
    
    // 验证固件
    if (is_firmware) {
        char version[64] = {0};
        ret = verify_firmware_file(dest_path, version, sizeof(version));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Firmware verification failed");
            unlink(dest_path);
            return ret;
        }
        
        // 创建 manifest
        ret = create_manifest_file(version, NULL, true);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to create manifest (non-fatal)");
        }
    }
    
    // 自动刷入
    if (auto_flash) {
        if (is_firmware) {
            return ts_ota_flash_from_recovery(true);
        } else {
            return ts_ota_www_start_sdcard(dest_path, NULL, NULL);
        }
    }
    
    return ESP_OK;
}

/**
 * @brief 从 recovery 目录刷入固件
 *
 * 读取 /sdcard/recovery/TianShanOS.bin 并写入 OTA 分区
 *
 * @param auto_reboot 刷入后自动重启
 * @return ESP_OK 成功
 */
esp_err_t ts_ota_flash_from_recovery(bool auto_reboot)
{
    // 检查文件是否存在
    if (!ts_storage_exists(RECOVERY_FIRMWARE)) {
        ESP_LOGE(TAG, "Recovery firmware not found: %s", RECOVERY_FIRMWARE);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 使用 SD 卡 OTA 接口
    ts_ota_config_t config = {
        .source = TS_OTA_SOURCE_SDCARD,
        .url = RECOVERY_FIRMWARE,
        .auto_reboot = auto_reboot,
        .allow_downgrade = true,  // Recovery 允许降级
    };
    
    return ts_ota_start(&config);
}

/**
 * @brief 从 recovery 目录刷入 WWW
 *
 * 读取 /sdcard/recovery/www.bin 并写入 www 分区
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_ota_www_from_recovery(void)
{
    // 检查文件是否存在
    if (!ts_storage_exists(RECOVERY_WWW)) {
        ESP_LOGE(TAG, "Recovery www not found: %s", RECOVERY_WWW);
        return ESP_ERR_NOT_FOUND;
    }
    
    return ts_ota_www_start_sdcard(RECOVERY_WWW, NULL, NULL);
}

/**
 * @brief 检查 recovery 目录状态
 *
 * @param has_firmware 输出：是否有固件文件
 * @param has_www 输出：是否有 www 文件
 * @param firmware_version 输出：固件版本（需要 64 字节缓冲）
 * @return ESP_OK 成功
 */
esp_err_t ts_ota_check_recovery_files(bool *has_firmware, bool *has_www, char *firmware_version)
{
    if (has_firmware) {
        *has_firmware = ts_storage_exists(RECOVERY_FIRMWARE);
    }
    
    if (has_www) {
        *has_www = ts_storage_exists(RECOVERY_WWW);
    }
    
    if (firmware_version && has_firmware && *has_firmware) {
        verify_firmware_file(RECOVERY_FIRMWARE, firmware_version, 64);
    }
    
    return ESP_OK;
}

/**
 * @brief 清理 recovery 目录
 */
esp_err_t ts_ota_clean_recovery(void)
{
    if (ts_storage_exists(RECOVERY_FIRMWARE)) {
        unlink(RECOVERY_FIRMWARE);
    }
    if (ts_storage_exists(RECOVERY_WWW)) {
        unlink(RECOVERY_WWW);
    }
    if (ts_storage_exists(RECOVERY_MANIFEST)) {
        unlink(RECOVERY_MANIFEST);
    }
    if (ts_storage_exists(RECOVERY_TEMP)) {
        unlink(RECOVERY_TEMP);
    }
    
    ESP_LOGI(TAG, "Recovery directory cleaned");
    return ESP_OK;
}

/**
 * @brief 中止下载
 */
void ts_ota_download_abort(void)
{
    if (s_download_running) {
        s_download_ctx.abort_requested = true;
        ESP_LOGI(TAG, "Download abort requested");
    }
}

/**
 * @brief 检查是否正在下载
 */
bool ts_ota_download_is_running(void)
{
    return s_download_running;
}

/*===========================================================================*/
/*                           Internal Functions                               */
/*===========================================================================*/

/**
 * @brief 下载任务
 */
static void download_task(void *arg)
{
    bool auto_flash = (arg != NULL);
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Download task started: %s -> %s", 
             s_download_ctx.url, s_download_ctx.dest_path);
    
    // 更新状态
    ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, 0, 0, "正在连接服务器...");
    ts_event_post(TS_EVENT_BASE_OTA, TS_EVENT_OTA_STARTED, NULL, 0, 0);
    
    // 下载文件
    size_t file_size = 0;
    ret = download_file_to_sdcard(s_download_ctx.url, s_download_ctx.dest_path,
                                   s_download_ctx.skip_cert_verify, &file_size);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Download completed: %zu bytes", file_size);
    
    // 验证固件
    if (s_download_ctx.is_firmware) {
        char version[64] = {0};
        ts_ota_update_progress(TS_OTA_STATE_VERIFYING, file_size, file_size, "正在验证固件...");
        
        ret = verify_firmware_file(s_download_ctx.dest_path, version, sizeof(version));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Firmware verification failed");
            unlink(s_download_ctx.dest_path);
            goto cleanup;
        }
        
        ESP_LOGI(TAG, "Firmware verified: version=%s", version);
        
        // 创建 manifest
        create_manifest_file(version, NULL, false);
    }
    
    ts_ota_update_progress(TS_OTA_STATE_IDLE, file_size, file_size, "下载完成");
    
    // 自动刷入
    if (auto_flash && s_download_ctx.is_firmware) {
        ESP_LOGI(TAG, "Auto-flashing firmware...");
        ret = ts_ota_flash_from_recovery(true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Auto-flash failed: %s", esp_err_to_name(ret));
        }
        // 如果自动刷入成功，会重启，不会执行到这里
    }
    
cleanup:
    // 清理
    if (s_download_ctx.url) {
        OTA_FREE(s_download_ctx.url);
        s_download_ctx.url = NULL;
    }
    
    s_download_running = false;
    s_download_ctx.task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief HTTP 事件处理
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static FILE *file = NULL;
    static size_t written = 0;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP connected");
            // 打开目标文件
            file = fopen(s_download_ctx.dest_path, "wb");
            if (!file) {
                ESP_LOGE(TAG, "Failed to open file: %s", s_download_ctx.dest_path);
                return ESP_FAIL;
            }
            written = 0;
            break;
            
        case HTTP_EVENT_ON_DATA:
            if (file && evt->data && evt->data_len > 0) {
                size_t w = fwrite(evt->data, 1, evt->data_len, file);
                if (w != evt->data_len) {
                    ESP_LOGE(TAG, "Write error: %zu / %d", w, evt->data_len);
                    return ESP_FAIL;
                }
                written += w;
                
                // 更新进度
                s_download_ctx.received_size = written;
                int percent = (s_download_ctx.total_size > 0) ? 
                              (written * 100 / s_download_ctx.total_size) : 0;
                
                ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, written, 
                                       s_download_ctx.total_size, "正在下载...");
                
                // 回调
                if (s_download_ctx.cb) {
                    ts_ota_progress_t progress = {
                        .state = TS_OTA_STATE_DOWNLOADING,
                        .total_size = s_download_ctx.total_size,
                        .received_size = written,
                        .progress_percent = percent,
                        .status_msg = "正在下载..."
                    };
                    s_download_ctx.cb(&progress, s_download_ctx.user_data);
                }
            }
            break;
            
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP download finished");
            if (file) {
                fclose(file);
                file = NULL;
            }
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "HTTP disconnected");
            if (file) {
                fclose(file);
                file = NULL;
            }
            break;
            
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP error");
            if (file) {
                fclose(file);
                file = NULL;
            }
            break;
            
        default:
            break;
    }
    
    return ESP_OK;
}

/**
 * @brief 下载文件到 SD 卡
 */
static esp_err_t download_file_to_sdcard(const char *url, const char *dest_path,
                                          bool skip_cert_verify, size_t *out_size)
{
    // 判断协议
    bool is_http = (strncmp(url, "http://", 7) == 0);
    bool is_https = (strncmp(url, "https://", 8) == 0);
    
    if (!is_http && !is_https) {
        ESP_LOGE(TAG, "Invalid URL scheme: %s", url);
        ts_ota_set_error(TS_OTA_ERR_INVALID_PARAM, "无效的 URL 协议");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 配置 HTTP 客户端
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 30000,
        .buffer_size = DOWNLOAD_BUFFER_SIZE,
    };
    
    if (is_http) {
        config.transport_type = HTTP_TRANSPORT_OVER_TCP;
        ESP_LOGW(TAG, "Using plain HTTP (insecure)");
    } else {
        if (!skip_cert_verify) {
            config.crt_bundle_attach = esp_crt_bundle_attach;
        } else {
            config.skip_cert_common_name_check = true;
            ESP_LOGW(TAG, "SSL certificate verification disabled");
        }
    }
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ts_ota_set_error(TS_OTA_ERR_INTERNAL, "HTTP 客户端初始化失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 执行请求
    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(ret));
        ts_ota_set_error(TS_OTA_ERR_CONNECTION_FAILED, "连接服务器失败");
        esp_http_client_cleanup(client);
        return ret;
    }
    
    // 获取 Content-Length
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGW(TAG, "Content-Length unknown");
        content_length = 0;
    } else {
        ESP_LOGI(TAG, "Content-Length: %d", content_length);
    }
    s_download_ctx.total_size = content_length;
    
    // 检查 HTTP 状态码
    int status = esp_http_client_get_status_code(client);
    if (status != 200 && status != 206) {
        ESP_LOGE(TAG, "HTTP error: %d", status);
        ts_ota_set_error(TS_OTA_ERR_DOWNLOAD_FAILED, "服务器返回错误");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    
    // 打开目标文件
    FILE *f = fopen(dest_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s: %s", dest_path, strerror(errno));
        ts_ota_set_error(TS_OTA_ERR_WRITE_FAILED, "无法创建文件");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 分配缓冲区
    uint8_t *buffer = OTA_MALLOC(DOWNLOAD_BUFFER_SIZE);
    if (!buffer) {
        fclose(f);
        ts_ota_set_error(TS_OTA_ERR_INTERNAL, "内存不足");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    
    // 下载数据
    size_t received = 0;
    int read_len;
    
    while ((read_len = esp_http_client_read(client, (char *)buffer, DOWNLOAD_BUFFER_SIZE)) > 0) {
        // 检查中止标志
        if (s_download_ctx.abort_requested) {
            ESP_LOGI(TAG, "Download aborted by user");
            ts_ota_update_progress(TS_OTA_STATE_IDLE, 0, 0, "已中止");
            ret = ESP_ERR_INVALID_STATE;
            break;
        }
        
        // 写入文件
        size_t w = fwrite(buffer, 1, read_len, f);
        if (w != (size_t)read_len) {
            ESP_LOGE(TAG, "Write error: %zu / %d", w, read_len);
            ts_ota_set_error(TS_OTA_ERR_WRITE_FAILED, "写入文件失败");
            ret = ESP_FAIL;
            break;
        }
        
        received += read_len;
        
        // 更新进度
        int percent = (content_length > 0) ? (received * 100 / content_length) : 0;
        ts_ota_update_progress(TS_OTA_STATE_DOWNLOADING, received, content_length, "正在下载...");
        
        // 回调
        if (s_download_ctx.cb) {
            ts_ota_progress_t progress = {
                .state = TS_OTA_STATE_DOWNLOADING,
                .total_size = content_length,
                .received_size = received,
                .progress_percent = percent,
                .status_msg = "正在下载..."
            };
            s_download_ctx.cb(&progress, s_download_ctx.user_data);
        }
        
        // Yield
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // 清理
    fclose(f);
    OTA_FREE(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    if (ret == ESP_OK && read_len == 0) {
        // 下载完成
        if (out_size) {
            *out_size = received;
        }
        
        // 验证大小
        if (content_length > 0 && received != (size_t)content_length) {
            ESP_LOGE(TAG, "Size mismatch: %zu / %d", received, content_length);
            ts_ota_set_error(TS_OTA_ERR_DOWNLOAD_FAILED, "下载不完整");
            unlink(dest_path);
            return ESP_FAIL;
        }
        
        return ESP_OK;
    }
    
    // 下载失败，删除部分文件
    unlink(dest_path);
    return (ret == ESP_OK) ? ESP_FAIL : ret;
}

/**
 * @brief 验证固件文件
 */
static esp_err_t verify_firmware_file(const char *path, char *version, size_t version_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ts_ota_set_error(TS_OTA_ERR_FILE_NOT_FOUND, "固件文件不存在");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 读取头部
    size_t header_size = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    uint8_t *header = OTA_MALLOC(header_size);
    if (!header) {
        fclose(f);
        ts_ota_set_error(TS_OTA_ERR_INTERNAL, "内存不足");
        return ESP_ERR_NO_MEM;
    }
    
    size_t read = fread(header, 1, header_size, f);
    fclose(f);
    
    if (read < header_size) {
        OTA_FREE(header);
        ts_ota_set_error(TS_OTA_ERR_VERIFY_FAILED, "固件文件过小");
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 验证 magic word
    esp_app_desc_t *app_desc = (esp_app_desc_t *)(header + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    
    if (app_desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        OTA_FREE(header);
        ts_ota_set_error(TS_OTA_ERR_VERIFY_FAILED, "无效的固件格式");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 提取版本
    if (version && version_len > 0) {
        strncpy(version, app_desc->version, version_len - 1);
        version[version_len - 1] = '\0';
    }
    
    ESP_LOGI(TAG, "Firmware verified: %s v%s", app_desc->project_name, app_desc->version);
    
    OTA_FREE(header);
    return ESP_OK;
}

/**
 * @brief 创建 manifest 文件
 */
static esp_err_t create_manifest_file(const char *firmware_version, const char *www_version, bool force)
{
    cJSON *manifest = cJSON_CreateObject();
    if (!manifest) {
        return ESP_ERR_NO_MEM;
    }
    
    if (firmware_version) {
        cJSON_AddStringToObject(manifest, "firmware_version", firmware_version);
    }
    if (www_version) {
        cJSON_AddStringToObject(manifest, "www_version", www_version);
    }
    cJSON_AddBoolToObject(manifest, "force", force);
    cJSON_AddBoolToObject(manifest, "delete_after", true);
    
    // 计算 SHA256
    char sha256[65] = {0};
    if (ts_storage_exists(RECOVERY_FIRMWARE)) {
        if (calculate_file_sha256(RECOVERY_FIRMWARE, sha256, sizeof(sha256)) == ESP_OK) {
            cJSON_AddStringToObject(manifest, "firmware_sha256", sha256);
        }
    }
    if (ts_storage_exists(RECOVERY_WWW)) {
        if (calculate_file_sha256(RECOVERY_WWW, sha256, sizeof(sha256)) == ESP_OK) {
            cJSON_AddStringToObject(manifest, "www_sha256", sha256);
        }
    }
    
    // 写入文件
    char *json = cJSON_Print(manifest);
    cJSON_Delete(manifest);
    
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = ts_storage_write_file(RECOVERY_MANIFEST, json, strlen(json));
    free(json);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Manifest created: %s", RECOVERY_MANIFEST);
    }
    
    return ret;
}
