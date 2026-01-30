/**
 * @file ts_ota_version.c
 * @brief TianShanOS OTA Version Handling
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_app_desc.h"
#include "ts_ota.h"

static const char *TAG = "ts_ota_version";

/**
 * @brief Get running firmware version info
 */
esp_err_t ts_ota_get_running_version(ts_ota_version_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));

    const esp_app_desc_t *desc = esp_app_get_description();
    if (!desc) {
        ESP_LOGE(TAG, "Failed to get app description");
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(info->version, desc->version, sizeof(info->version) - 1);
    strncpy(info->project_name, desc->project_name, sizeof(info->project_name) - 1);
    strncpy(info->compile_time, desc->time, sizeof(info->compile_time) - 1);
    strncpy(info->compile_date, desc->date, sizeof(info->compile_date) - 1);
    strncpy(info->idf_version, desc->idf_ver, sizeof(info->idf_version) - 1);
    info->secure_version = desc->secure_version;

    return ESP_OK;
}

/**
 * @brief Parse version string to numeric components
 *
 * Supports formats: "1.2.3", "1.2.3-rc1", "v1.2.3"
 *
 * @param version Version string
 * @param major Output major version
 * @param minor Output minor version
 * @param patch Output patch version
 * @param prerelease Output prerelease string (optional)
 * @return ESP_OK on success
 */
static esp_err_t parse_version(const char *version, 
                                int *major, int *minor, int *patch,
                                char *prerelease, size_t prerelease_len)
{
    if (!version || !major || !minor || !patch) {
        return ESP_ERR_INVALID_ARG;
    }

    *major = 0;
    *minor = 0;
    *patch = 0;
    if (prerelease && prerelease_len > 0) {
        prerelease[0] = '\0';
    }

    const char *p = version;

    // Skip leading 'v' or 'V'
    if (*p == 'v' || *p == 'V') {
        p++;
    }

    // Parse major
    if (!isdigit((unsigned char)*p)) {
        return ESP_ERR_INVALID_ARG;
    }
    *major = strtol(p, (char **)&p, 10);

    // Parse minor (optional)
    if (*p == '.') {
        p++;
        if (isdigit((unsigned char)*p)) {
            *minor = strtol(p, (char **)&p, 10);
        }
    }

    // Parse patch (optional)
    if (*p == '.') {
        p++;
        if (isdigit((unsigned char)*p)) {
            *patch = strtol(p, (char **)&p, 10);
        }
    }

    // Parse prerelease (optional)
    if (*p == '-' && prerelease && prerelease_len > 0) {
        p++;
        strncpy(prerelease, p, prerelease_len - 1);
        prerelease[prerelease_len - 1] = '\0';
    }

    return ESP_OK;
}

/**
 * @brief Compare two version strings
 *
 * @param v1 First version string
 * @param v2 Second version string
 * @return -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
 */
int ts_ota_compare_versions(const char *v1, const char *v2)
{
    if (!v1 && !v2) return 0;
    if (!v1) return -1;
    if (!v2) return 1;

    int major1, minor1, patch1;
    int major2, minor2, patch2;
    char pre1[32], pre2[32];

    if (parse_version(v1, &major1, &minor1, &patch1, pre1, sizeof(pre1)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse version: %s", v1);
        return strcmp(v1, v2);  // Fallback to string comparison
    }

    if (parse_version(v2, &major2, &minor2, &patch2, pre2, sizeof(pre2)) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse version: %s", v2);
        return strcmp(v1, v2);
    }

    // Compare major
    if (major1 != major2) {
        return (major1 > major2) ? 1 : -1;
    }

    // Compare minor
    if (minor1 != minor2) {
        return (minor1 > minor2) ? 1 : -1;
    }

    // Compare patch
    if (patch1 != patch2) {
        return (patch1 > patch2) ? 1 : -1;
    }

    // Compare prerelease
    // No prerelease > has prerelease (e.g., 1.0.0 > 1.0.0-rc1)
    bool has_pre1 = (pre1[0] != '\0');
    bool has_pre2 = (pre2[0] != '\0');

    if (!has_pre1 && has_pre2) return 1;   // 1.0.0 > 1.0.0-rc1
    if (has_pre1 && !has_pre2) return -1;  // 1.0.0-rc1 < 1.0.0
    if (has_pre1 && has_pre2) {
        return strcmp(pre1, pre2);  // Compare prerelease strings
    }

    return 0;  // Versions are equal
}

/**
 * @brief Check if update is available
 */
esp_err_t ts_ota_check_update(const char *url, bool *available,
                               char *new_version, size_t new_version_len)
{
    if (!url || !available) {
        return ESP_ERR_INVALID_ARG;
    }

    *available = false;

    // Delegate to HTTPS implementation
    extern esp_err_t ts_ota_check_update_https(const char *url, bool *available,
                                                char *new_version, size_t new_version_len);
    
    return ts_ota_check_update_https(url, available, new_version, new_version_len);
}

/**
 * @brief Format version info as string
 */
void ts_ota_format_version_info(const ts_ota_version_info_t *info, 
                                 char *buffer, size_t buffer_len)
{
    if (!info || !buffer || buffer_len == 0) {
        return;
    }

    snprintf(buffer, buffer_len,
             "%s v%s (%s %s, IDF %s)",
             info->project_name,
             info->version,
             info->compile_date,
             info->compile_time,
             info->idf_version);
}

/**
 * @brief Start OTA update (dispatcher)
 */
esp_err_t ts_ota_start(const ts_ota_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting OTA update, source=%d, url=%s",
             config->source, config->url ? config->url : "(null)");

    // Forward declarations
    extern esp_err_t ts_ota_start_https(const ts_ota_config_t *config);
    extern esp_err_t ts_ota_start_sdcard(const ts_ota_config_t *config);
    // ts_ota_download_firmware is declared in ts_ota.h

    switch (config->source) {
        case TS_OTA_SOURCE_HTTPS:
            return ts_ota_start_https(config);

        case TS_OTA_SOURCE_SDCARD:
            return ts_ota_start_sdcard(config);

        case TS_OTA_SOURCE_DOWNLOAD:
            // 统一下载模式：先下载到 /sdcard/recovery/ 然后刷入
            return ts_ota_download_firmware(config->url, config->skip_cert_verify,
                                            config->progress_cb, config->user_data,
                                            config->auto_reboot);

        case TS_OTA_SOURCE_UPLOAD:
            // Upload is handled by ts_ota_upload_* functions
            ESP_LOGE(TAG, "Use ts_ota_upload_begin/write/end for upload source");
            return ESP_ERR_INVALID_ARG;

        default:
            ESP_LOGE(TAG, "Unknown OTA source: %d", config->source);
            return ESP_ERR_INVALID_ARG;
    }
}
