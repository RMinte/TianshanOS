/**
 * @file ts_ota_recovery.c
 * @brief SD Card Recovery OTA - 启动时自动检查并恢复固件
 *
 * 功能：
 * 1. 启动时检查 /sdcard/recovery/ 目录
 * 2. 读取 manifest.json 获取版本信息
 * 3. 如果版本不同或强制标志，自动烧录固件
 * 4. 支持固件 + WebUI 同时恢复
 *
 * 目录结构：
 * /sdcard/recovery/
 * ├── TianShanOS.bin   # 固件文件
 * ├── www.bin          # WebUI 文件（可选）
 * └── manifest.json    # 版本清单
 *
 * manifest.json 格式：
 * {
 *   "version": "0.3.0",
 *   "force": false,
 *   "firmware": "TianShanOS.bin",
 *   "www": "www.bin"
 * }
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

// 内部头文件
#include "ts_ota.h"

static const char *TAG = "ts_recovery";

// Recovery 目录和文件路径
#define RECOVERY_DIR            "/sdcard/recovery"
#define RECOVERY_MANIFEST       RECOVERY_DIR "/manifest.json"
#define RECOVERY_FIRMWARE       RECOVERY_DIR "/TianShanOS.bin"
#define RECOVERY_WWW            RECOVERY_DIR "/www.bin"
#define RECOVERY_BUFFER_SIZE    4096

// 使用 PSRAM 如果可用
#if CONFIG_SPIRAM
#define RECOVERY_MALLOC(size)   heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define RECOVERY_MALLOC(size)   malloc(size)
#endif

/**
 * @brief Recovery manifest 结构
 */
typedef struct {
    char version[32];       // 目标版本
    char firmware[64];      // 固件文件名
    char www[64];           // WebUI 文件名
    bool force;             // 强制恢复（忽略版本比对）
    bool delete_after;      // 成功后删除 recovery 文件
} recovery_manifest_t;

/**
 * @brief 读取 manifest.json
 */
static esp_err_t read_manifest(recovery_manifest_t *manifest)
{
    FILE *f = fopen(RECOVERY_MANIFEST, "r");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size > 4096 || size <= 0) {
        fclose(f);
        ESP_LOGE(TAG, "Invalid manifest size: %ld", size);
        return ESP_ERR_INVALID_SIZE;
    }

    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(buffer, 1, size, f);
    fclose(f);
    buffer[read] = '\0';

    // 解析 JSON
    cJSON *root = cJSON_Parse(buffer);
    free(buffer);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse manifest JSON");
        return ESP_ERR_INVALID_ARG;
    }

    // 初始化默认值
    memset(manifest, 0, sizeof(*manifest));
    strcpy(manifest->firmware, "TianShanOS.bin");
    strcpy(manifest->www, "www.bin");
    manifest->delete_after = true;

    // 读取字段
    cJSON *version = cJSON_GetObjectItem(root, "version");
    if (version && cJSON_IsString(version)) {
        strncpy(manifest->version, version->valuestring, sizeof(manifest->version) - 1);
    }

    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (firmware && cJSON_IsString(firmware)) {
        strncpy(manifest->firmware, firmware->valuestring, sizeof(manifest->firmware) - 1);
    }

    cJSON *www = cJSON_GetObjectItem(root, "www");
    if (www && cJSON_IsString(www)) {
        strncpy(manifest->www, www->valuestring, sizeof(manifest->www) - 1);
    }

    cJSON *force = cJSON_GetObjectItem(root, "force");
    if (force && cJSON_IsBool(force)) {
        manifest->force = cJSON_IsTrue(force);
    }

    cJSON *delete_after = cJSON_GetObjectItem(root, "delete_after");
    if (delete_after && cJSON_IsBool(delete_after)) {
        manifest->delete_after = cJSON_IsTrue(delete_after);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief 检查版本是否需要更新
 */
static bool need_recovery(const recovery_manifest_t *manifest)
{
    if (manifest->force) {
        ESP_LOGW(TAG, "Force recovery enabled");
        return true;
    }

    // 获取当前版本
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (!app_desc) {
        ESP_LOGE(TAG, "Failed to get current app description");
        return false;
    }

    ESP_LOGI(TAG, "Current version: %s, Recovery version: %s",
             app_desc->version, manifest->version);

    // 比较版本
    if (strcmp(app_desc->version, manifest->version) != 0) {
        return true;
    }

    ESP_LOGI(TAG, "Version match, no recovery needed");
    return false;
}

/**
 * @brief 执行固件恢复
 */
static esp_err_t do_firmware_recovery(const char *firmware_path)
{
    esp_err_t ret;
    FILE *f = NULL;
    uint8_t *buffer = NULL;
    esp_ota_handle_t ota_handle = 0;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting firmware recovery from: %s", firmware_path);

    // 打开固件文件
    f = fopen(firmware_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open firmware file");
        return ESP_ERR_NOT_FOUND;
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "Firmware size: %zu bytes", file_size);

    // 分配缓冲区
    buffer = RECOVERY_MALLOC(RECOVERY_BUFFER_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }

    // 读取并验证固件头
    size_t read_len = fread(buffer, 1, sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t), f);
    if (read_len < sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
        ESP_LOGE(TAG, "Failed to read firmware header");
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    // 验证魔数
    esp_app_desc_t *app_desc = (esp_app_desc_t *)(buffer + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
    if (app_desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "Invalid firmware magic word");
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    ESP_LOGI(TAG, "Recovery firmware: %s v%s", app_desc->project_name, app_desc->version);

    // 获取 OTA 分区
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        ret = ESP_ERR_NOT_FOUND;
        goto cleanup;
    }

    ESP_LOGI(TAG, "Writing to partition: %s (offset=0x%lx, size=%lu)",
             update_partition->label, update_partition->address, update_partition->size);

    // 开始 OTA
    ret = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // 写入已读取的头部
    fseek(f, 0, SEEK_SET);
    
    // 写入整个固件
    size_t written = 0;
    while (written < file_size) {
        read_len = fread(buffer, 1, RECOVERY_BUFFER_SIZE, f);
        if (read_len == 0) {
            break;
        }

        ret = esp_ota_write(ota_handle, buffer, read_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
            esp_ota_abort(ota_handle);
            goto cleanup;
        }

        written += read_len;

        // 每 10% 打印进度
        int percent = (written * 100) / file_size;
        static int last_percent = -1;
        if (percent / 10 != last_percent / 10) {
            ESP_LOGI(TAG, "Recovery progress: %d%% (%zu / %zu)", percent, written, file_size);
            last_percent = percent;
        }
    }

    // 完成 OTA
    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // 设置启动分区
    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_LOGI(TAG, "✅ Firmware recovery completed successfully!");
    ret = ESP_OK;

cleanup:
    if (f) fclose(f);
    if (buffer) free(buffer);
    return ret;
}

/**
 * @brief 执行 WebUI (www) 恢复
 */
static esp_err_t do_www_recovery(const char *www_path)
{
    esp_err_t ret;
    FILE *f = NULL;
    uint8_t *buffer = NULL;
    const esp_partition_t *www_partition = NULL;

    ESP_LOGI(TAG, "Starting WebUI recovery from: %s", www_path);

    // 查找 www 分区
    www_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "www");
    if (!www_partition) {
        ESP_LOGW(TAG, "No www partition found, skipping WebUI recovery");
        return ESP_OK;  // 不是错误，只是没有 www 分区
    }

    // 打开文件
    f = fopen(www_path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "WebUI file not found: %s", www_path);
        return ESP_OK;  // 可选文件，不存在不是错误
    }

    // 获取文件大小
    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size > www_partition->size) {
        ESP_LOGE(TAG, "WebUI file too large: %zu > %lu", file_size, www_partition->size);
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "WebUI size: %zu bytes, partition size: %lu", file_size, www_partition->size);

    // 分配缓冲区
    buffer = RECOVERY_MALLOC(RECOVERY_BUFFER_SIZE);
    if (!buffer) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    // 擦除分区
    ESP_LOGI(TAG, "Erasing www partition...");
    ret = esp_partition_erase_range(www_partition, 0, www_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase www partition: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // 写入数据
    size_t written = 0;
    while (written < file_size) {
        size_t read_len = fread(buffer, 1, RECOVERY_BUFFER_SIZE, f);
        if (read_len == 0) break;

        ret = esp_partition_write(www_partition, written, buffer, read_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write www partition: %s", esp_err_to_name(ret));
            goto cleanup;
        }

        written += read_len;

        // 每 10% 打印进度
        int percent = (written * 100) / file_size;
        static int last_percent_www = -1;
        if (percent / 10 != last_percent_www / 10) {
            ESP_LOGI(TAG, "WebUI recovery progress: %d%%", percent);
            last_percent_www = percent;
        }
    }

    ESP_LOGI(TAG, "✅ WebUI recovery completed successfully!");
    ret = ESP_OK;

cleanup:
    if (f) fclose(f);
    if (buffer) free(buffer);
    return ret;
}

/**
 * @brief 删除 recovery 文件
 */
static void cleanup_recovery_files(const recovery_manifest_t *manifest)
{
    char path[128];

    // 删除固件
    snprintf(path, sizeof(path), RECOVERY_DIR "/%s", manifest->firmware);
    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Deleted: %s", path);
    }

    // 删除 www
    snprintf(path, sizeof(path), RECOVERY_DIR "/%s", manifest->www);
    if (remove(path) == 0) {
        ESP_LOGI(TAG, "Deleted: %s", path);
    }

    // 删除 manifest
    if (remove(RECOVERY_MANIFEST) == 0) {
        ESP_LOGI(TAG, "Deleted: %s", RECOVERY_MANIFEST);
    }
}

/**
 * @brief 检查并执行 SD 卡 Recovery
 *
 * 此函数应在 SD 卡挂载后立即调用
 *
 * @return
 *   - ESP_OK: 无需恢复或恢复成功
 *   - ESP_ERR_NOT_FOUND: 未找到 recovery 目录
 *   - 其他错误码: 恢复失败
 */
esp_err_t ts_ota_check_recovery(void)
{
    struct stat st;
    recovery_manifest_t manifest;
    esp_err_t ret;
    bool firmware_done = false;
    bool www_done = false;

    ESP_LOGI(TAG, "Checking for SD card recovery...");

    // 检查 recovery 目录是否存在
    if (stat(RECOVERY_DIR, &st) != 0) {
        ESP_LOGI(TAG, "No recovery directory found");
        return ESP_ERR_NOT_FOUND;
    }

    // 读取 manifest
    ret = read_manifest(&manifest);
    if (ret != ESP_OK) {
        // 没有 manifest，检查是否有固件文件（向后兼容）
        if (stat(RECOVERY_FIRMWARE, &st) == 0) {
            ESP_LOGW(TAG, "No manifest.json, using default settings");
            memset(&manifest, 0, sizeof(manifest));
            strcpy(manifest.firmware, "TianShanOS.bin");
            strcpy(manifest.www, "www.bin");
            manifest.force = true;  // 无 manifest 时强制恢复
            manifest.delete_after = true;
        } else {
            ESP_LOGI(TAG, "No recovery files found");
            return ESP_ERR_NOT_FOUND;
        }
    }

    ESP_LOGI(TAG, "Recovery manifest: version=%s, force=%d, firmware=%s, www=%s",
             manifest.version, manifest.force, manifest.firmware, manifest.www);

    // 检查是否需要恢复
    if (!need_recovery(&manifest)) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "  SD CARD RECOVERY INITIATED");
    ESP_LOGW(TAG, "===========================================");

    // 构建完整路径
    char firmware_path[128];
    char www_path[128];
    snprintf(firmware_path, sizeof(firmware_path), RECOVERY_DIR "/%s", manifest.firmware);
    snprintf(www_path, sizeof(www_path), RECOVERY_DIR "/%s", manifest.www);

    // 检查固件文件
    if (stat(firmware_path, &st) != 0) {
        ESP_LOGE(TAG, "Firmware file not found: %s", firmware_path);
        return ESP_ERR_NOT_FOUND;
    }

    // 执行固件恢复
    ret = do_firmware_recovery(firmware_path);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ Firmware recovery failed: %s", esp_err_to_name(ret));
        return ret;
    }
    firmware_done = true;

    // 执行 WebUI 恢复（可选）
    if (stat(www_path, &st) == 0) {
        ret = do_www_recovery(www_path);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WebUI recovery failed (non-fatal): %s", esp_err_to_name(ret));
        } else {
            www_done = true;
        }
    }

    // 清理 recovery 文件
    if (manifest.delete_after && firmware_done) {
        ESP_LOGI(TAG, "Cleaning up recovery files...");
        cleanup_recovery_files(&manifest);
    }

    // 重启
    ESP_LOGW(TAG, "===========================================");
    ESP_LOGW(TAG, "  RECOVERY COMPLETE - REBOOTING IN 3s");
    ESP_LOGW(TAG, "  Firmware: %s, WebUI: %s",
             firmware_done ? "✅" : "❌",
             www_done ? "✅" : "⏭️");
    ESP_LOGW(TAG, "===========================================");

    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;  // 不会执行到这里
}

/**
 * @brief 创建 recovery 清单文件
 *
 * 用于准备 recovery 更新
 *
 * @param version 目标版本
 * @param force 是否强制更新
 * @return esp_err_t
 */
esp_err_t ts_ota_create_recovery_manifest(const char *version, bool force)
{
    // 创建 recovery 目录
    mkdir(RECOVERY_DIR, 0755);

    // 创建 JSON
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "version", version ? version : "");
    cJSON_AddBoolToObject(root, "force", force);
    cJSON_AddStringToObject(root, "firmware", "TianShanOS.bin");
    cJSON_AddStringToObject(root, "www", "www.bin");
    cJSON_AddBoolToObject(root, "delete_after", true);

    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (!json_str) {
        return ESP_ERR_NO_MEM;
    }

    // 写入文件
    FILE *f = fopen(RECOVERY_MANIFEST, "w");
    if (!f) {
        free(json_str);
        return ESP_ERR_NOT_FOUND;
    }

    fputs(json_str, f);
    fclose(f);
    free(json_str);

    ESP_LOGI(TAG, "Created recovery manifest: %s", RECOVERY_MANIFEST);
    return ESP_OK;
}
