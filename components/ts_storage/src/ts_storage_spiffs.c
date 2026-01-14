/**
 * @file ts_storage_spiffs.c
 * @brief SPIFFS Storage Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_storage.h"
#include "ts_log.h"
#include "esp_spiffs.h"
#include <string.h>

#define TAG "storage_spiffs"

/*===========================================================================*/
/*                          External Functions                                */
/*===========================================================================*/

extern void ts_storage_set_spiffs_mounted(bool mounted, const char *mount_point);

/*===========================================================================*/
/*                          Private Data                                      */
/*===========================================================================*/

static char s_mount_point[32] = "";
static char s_partition[32] = "";

/*===========================================================================*/
/*                          SPIFFS Operations                                 */
/*===========================================================================*/

esp_err_t ts_storage_mount_spiffs(const ts_spiffs_config_t *config)
{
    ts_spiffs_config_t cfg;
    
    if (config) {
        cfg = *config;
    } else {
        /* Use defaults from Kconfig */
#ifdef CONFIG_TS_STORAGE_SPIFFS_MOUNT_POINT
        cfg.mount_point = CONFIG_TS_STORAGE_SPIFFS_MOUNT_POINT;
#else
        cfg.mount_point = "/spiffs";
#endif

#ifdef CONFIG_TS_STORAGE_SPIFFS_PARTITION
        cfg.partition_label = CONFIG_TS_STORAGE_SPIFFS_PARTITION;
#else
        cfg.partition_label = "storage";
#endif

#ifdef CONFIG_TS_STORAGE_SPIFFS_MAX_FILES
        cfg.max_files = CONFIG_TS_STORAGE_SPIFFS_MAX_FILES;
#else
        cfg.max_files = 5;
#endif
        cfg.format_if_mount_failed = true;
    }
    
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = cfg.mount_point,
        .partition_label = cfg.partition_label,
        .max_files = cfg.max_files,
        .format_if_mount_failed = cfg.format_if_mount_failed
    };
    
    TS_LOGI(TAG, "Mounting SPIFFS at %s (partition: %s)", 
            cfg.mount_point, cfg.partition_label);
    
    esp_err_t ret = esp_vfs_spiffs_register(&spiffs_conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            TS_LOGE(TAG, "Failed to mount or format SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            TS_LOGE(TAG, "SPIFFS partition not found");
        } else {
            TS_LOGE(TAG, "Failed to initialize SPIFFS: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    /* Store mount info */
    strncpy(s_mount_point, cfg.mount_point, sizeof(s_mount_point) - 1);
    strncpy(s_partition, cfg.partition_label, sizeof(s_partition) - 1);
    
    ts_storage_set_spiffs_mounted(true, s_mount_point);
    
    /* Log stats */
    ts_storage_stats_t stats;
    if (ts_storage_spiffs_stats(&stats) == ESP_OK) {
        TS_LOGI(TAG, "SPIFFS mounted: %llu total, %llu used, %llu free",
                stats.total_bytes, stats.used_bytes, stats.free_bytes);
    }
    
    return ESP_OK;
}

esp_err_t ts_storage_unmount_spiffs(void)
{
    if (!ts_storage_spiffs_mounted()) {
        return ESP_OK;
    }
    
    esp_err_t ret = esp_vfs_spiffs_unregister(s_partition[0] ? s_partition : NULL);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to unmount SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ts_storage_set_spiffs_mounted(false, NULL);
    
    TS_LOGI(TAG, "SPIFFS unmounted");
    
    return ESP_OK;
}

bool ts_storage_spiffs_mounted(void)
{
    return esp_spiffs_mounted(s_partition[0] ? s_partition : NULL);
}

esp_err_t ts_storage_spiffs_stats(ts_storage_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!ts_storage_spiffs_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    size_t total, used;
    esp_err_t ret = esp_spiffs_info(s_partition[0] ? s_partition : NULL, &total, &used);
    if (ret != ESP_OK) {
        return ret;
    }
    
    stats->total_bytes = total;
    stats->used_bytes = used;
    stats->free_bytes = total - used;
    
    return ESP_OK;
}

esp_err_t ts_storage_format_spiffs(void)
{
    TS_LOGW(TAG, "Formatting SPIFFS...");
    
    esp_err_t ret = esp_spiffs_format(s_partition[0] ? s_partition : NULL);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to format SPIFFS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    TS_LOGI(TAG, "SPIFFS formatted");
    
    return ESP_OK;
}
