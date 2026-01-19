/**
 * @file ts_api_storage.c
 * @brief Storage API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_api.h"
#include "ts_storage.h"
#include "ts_log.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define TAG "api_storage"

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief storage.status - Get storage status
 */
static esp_err_t api_storage_status(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    bool spiffs_mounted = ts_storage_spiffs_mounted();
    bool sd_mounted = ts_storage_sd_mounted();
    
    /* SPIFFS info */
    cJSON *spiffs = cJSON_AddObjectToObject(data, "spiffs");
    cJSON_AddBoolToObject(spiffs, "mounted", spiffs_mounted);
    cJSON_AddStringToObject(spiffs, "path", "/spiffs");
    
    /* SD card info */
    cJSON *sd = cJSON_AddObjectToObject(data, "sd");
    cJSON_AddBoolToObject(sd, "mounted", sd_mounted);
    cJSON_AddStringToObject(sd, "path", "/sdcard");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief storage.mount - Mount SD card
 */
static esp_err_t api_storage_mount(const cJSON *params, ts_api_result_t *result)
{
    if (ts_storage_sd_mounted()) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "SD card already mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ts_storage_mount_sd(NULL);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to mount SD card");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status", "mounted");
    cJSON_AddStringToObject(data, "path", "/sdcard");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief storage.unmount - Unmount SD card
 */
static esp_err_t api_storage_unmount(const cJSON *params, ts_api_result_t *result)
{
    if (!ts_storage_sd_mounted()) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = ts_storage_unmount_sd();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to unmount SD card");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status", "unmounted");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief storage.list - List directory contents
 * 
 * @param params { "path": "/sdcard", "recursive": false }
 */
static esp_err_t api_storage_list(const cJSON *params, ts_api_result_t *result)
{
    const char *path = "/sdcard";
    bool recursive = false;
    
    if (params) {
        const cJSON *path_param = cJSON_GetObjectItem(params, "path");
        if (path_param && cJSON_IsString(path_param)) {
            path = path_param->valuestring;
        }
        
        const cJSON *recursive_param = cJSON_GetObjectItem(params, "recursive");
        if (recursive_param && cJSON_IsBool(recursive_param)) {
            recursive = cJSON_IsTrue(recursive_param);
        }
    }
    
    DIR *dir = opendir(path);
    if (!dir) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Directory not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        closedir(dir);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(data, "path", path);
    cJSON *entries = cJSON_AddArrayToObject(data, "entries");
    
    struct dirent *entry;
    struct stat st;
    char fullpath[512];
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(fullpath, sizeof(fullpath), "%.255s/%.255s", path, entry->d_name);
        
        if (stat(fullpath, &st) != 0) {
            continue;
        }
        
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", entry->d_name);
        cJSON_AddStringToObject(item, "type", S_ISDIR(st.st_mode) ? "dir" : "file");
        if (!S_ISDIR(st.st_mode)) {
            cJSON_AddNumberToObject(item, "size", st.st_size);
        }
        cJSON_AddItemToArray(entries, item);
    }
    
    closedir(dir);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t s_storage_endpoints[] = {
    {
        .name = "storage.status",
        .description = "Get storage status",
        .category = TS_API_CAT_STORAGE,
        .handler = api_storage_status,
        .requires_auth = false
    },
    {
        .name = "storage.mount",
        .description = "Mount SD card",
        .category = TS_API_CAT_STORAGE,
        .handler = api_storage_mount,
        .requires_auth = false
    },
    {
        .name = "storage.unmount",
        .description = "Unmount SD card",
        .category = TS_API_CAT_STORAGE,
        .handler = api_storage_unmount,
        .requires_auth = false
    },
    {
        .name = "storage.list",
        .description = "List directory contents",
        .category = TS_API_CAT_STORAGE,
        .handler = api_storage_list,
        .requires_auth = false
    }
};

esp_err_t ts_api_storage_register(void)
{
    TS_LOGI(TAG, "Registering storage APIs...");
    
    esp_err_t ret = ts_api_register_multiple(
        s_storage_endpoints, 
        sizeof(s_storage_endpoints) / sizeof(s_storage_endpoints[0])
    );
    
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Storage APIs registered (%zu endpoints)", 
            sizeof(s_storage_endpoints) / sizeof(s_storage_endpoints[0]));
    }
    
    return ret;
}
