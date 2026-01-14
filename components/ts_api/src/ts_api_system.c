/**
 * @file ts_api_system.c
 * @brief System API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_api.h"
#include "ts_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "api_system"

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief system.info - Get system information
 */
static esp_err_t api_system_info(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    /* App info */
    const esp_app_desc_t *app_desc = esp_app_get_description();
    cJSON *app = cJSON_AddObjectToObject(data, "app");
    cJSON_AddStringToObject(app, "name", app_desc->project_name);
    cJSON_AddStringToObject(app, "version", app_desc->version);
    cJSON_AddStringToObject(app, "idf_version", app_desc->idf_ver);
    cJSON_AddStringToObject(app, "compile_time", app_desc->time);
    cJSON_AddStringToObject(app, "compile_date", app_desc->date);
    
    /* Chip info */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    cJSON *chip = cJSON_AddObjectToObject(data, "chip");
    
    const char *model_name = "unknown";
    switch (chip_info.model) {
        case CHIP_ESP32:   model_name = "ESP32"; break;
        case CHIP_ESP32S2: model_name = "ESP32-S2"; break;
        case CHIP_ESP32S3: model_name = "ESP32-S3"; break;
        case CHIP_ESP32C3: model_name = "ESP32-C3"; break;
        case CHIP_ESP32C2: model_name = "ESP32-C2"; break;
        case CHIP_ESP32C6: model_name = "ESP32-C6"; break;
        case CHIP_ESP32H2: model_name = "ESP32-H2"; break;
        default: break;
    }
    cJSON_AddStringToObject(chip, "model", model_name);
    cJSON_AddNumberToObject(chip, "cores", chip_info.cores);
    cJSON_AddNumberToObject(chip, "revision", chip_info.revision);
    
    cJSON *features = cJSON_AddArrayToObject(chip, "features");
    if (chip_info.features & CHIP_FEATURE_WIFI_BGN) 
        cJSON_AddItemToArray(features, cJSON_CreateString("WiFi"));
    if (chip_info.features & CHIP_FEATURE_BT) 
        cJSON_AddItemToArray(features, cJSON_CreateString("BT"));
    if (chip_info.features & CHIP_FEATURE_BLE) 
        cJSON_AddItemToArray(features, cJSON_CreateString("BLE"));
    if (chip_info.features & CHIP_FEATURE_EMB_FLASH) 
        cJSON_AddItemToArray(features, cJSON_CreateString("Embedded Flash"));
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM) 
        cJSON_AddItemToArray(features, cJSON_CreateString("Embedded PSRAM"));
    
    /* Flash */
    uint32_t flash_size;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        cJSON_AddNumberToObject(data, "flash_size", flash_size);
    }
    
    /* Uptime */
    int64_t uptime_us = esp_timer_get_time();
    cJSON_AddNumberToObject(data, "uptime_ms", uptime_us / 1000);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief system.memory - Get memory information
 */
static esp_err_t api_system_memory(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    /* Heap info */
    cJSON_AddNumberToObject(data, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(data, "min_free_heap", esp_get_minimum_free_heap_size());
    
    /* Internal memory */
    multi_heap_info_t heap_info;
    heap_caps_get_info(&heap_info, MALLOC_CAP_INTERNAL);
    
    cJSON *internal = cJSON_AddObjectToObject(data, "internal");
    cJSON_AddNumberToObject(internal, "total", 
        heap_info.total_free_bytes + heap_info.total_allocated_bytes);
    cJSON_AddNumberToObject(internal, "free", heap_info.total_free_bytes);
    cJSON_AddNumberToObject(internal, "allocated", heap_info.total_allocated_bytes);
    cJSON_AddNumberToObject(internal, "largest_block", heap_info.largest_free_block);
    
    /* PSRAM */
    heap_caps_get_info(&heap_info, MALLOC_CAP_SPIRAM);
    if (heap_info.total_free_bytes > 0 || heap_info.total_allocated_bytes > 0) {
        cJSON *psram = cJSON_AddObjectToObject(data, "psram");
        cJSON_AddNumberToObject(psram, "total", 
            heap_info.total_free_bytes + heap_info.total_allocated_bytes);
        cJSON_AddNumberToObject(psram, "free", heap_info.total_free_bytes);
        cJSON_AddNumberToObject(psram, "allocated", heap_info.total_allocated_bytes);
        cJSON_AddNumberToObject(psram, "largest_block", heap_info.largest_free_block);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief system.tasks - Get task list
 */
static esp_err_t api_system_tasks(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
#if configUSE_TRACE_FACILITY
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = malloc(task_count * sizeof(TaskStatus_t));
    
    if (task_array == NULL) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    uint32_t total_runtime;
    task_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
    
    cJSON *tasks = cJSON_AddArrayToObject(data, "tasks");
    
    for (int i = 0; i < task_count; i++) {
        cJSON *task = cJSON_CreateObject();
        cJSON_AddStringToObject(task, "name", task_array[i].pcTaskName);
        cJSON_AddNumberToObject(task, "priority", task_array[i].uxCurrentPriority);
        cJSON_AddNumberToObject(task, "stack_hwm", task_array[i].usStackHighWaterMark);
        cJSON_AddNumberToObject(task, "core", task_array[i].xCoreID);
        
        const char *state = "unknown";
        switch (task_array[i].eCurrentState) {
            case eRunning:   state = "running"; break;
            case eReady:     state = "ready"; break;
            case eBlocked:   state = "blocked"; break;
            case eSuspended: state = "suspended"; break;
            case eDeleted:   state = "deleted"; break;
            default: break;
        }
        cJSON_AddStringToObject(task, "state", state);
        
        cJSON_AddItemToArray(tasks, task);
    }
    
    free(task_array);
    cJSON_AddNumberToObject(data, "count", task_count);
#else
    cJSON_AddStringToObject(data, "error", "Task trace not enabled");
#endif
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief system.reboot - Reboot the system
 */
static esp_err_t api_system_reboot(const cJSON *params, ts_api_result_t *result)
{
    int delay_ms = 100;
    
    if (params) {
        cJSON *delay = cJSON_GetObjectItem(params, "delay");
        if (delay && cJSON_IsNumber(delay)) {
            delay_ms = delay->valueint;
            if (delay_ms < 0) delay_ms = 0;
            if (delay_ms > 10000) delay_ms = 10000;
        }
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status", "rebooting");
    cJSON_AddNumberToObject(data, "delay_ms", delay_ms);
    
    ts_api_result_ok(result, data);
    
    /* Schedule reboot */
    if (delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    esp_restart();
    
    return ESP_OK;
}

/**
 * @brief system.log.level - Get/set log level
 */
static esp_err_t api_system_log_level(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    
    if (params) {
        cJSON *level = cJSON_GetObjectItem(params, "level");
        cJSON *tag = cJSON_GetObjectItem(params, "tag");
        
        if (level && cJSON_IsNumber(level)) {
            int lvl = level->valueint;
            if (lvl >= TS_LOG_LEVEL_NONE && lvl <= TS_LOG_LEVEL_VERBOSE) {
                if (tag && cJSON_IsString(tag)) {
                    ts_log_set_tag_level(tag->valuestring, lvl);
                    cJSON_AddStringToObject(data, "tag", tag->valuestring);
                } else {
                    ts_log_set_level(lvl);
                }
                cJSON_AddNumberToObject(data, "level", lvl);
                cJSON_AddStringToObject(data, "status", "set");
            } else {
                ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid log level");
                cJSON_Delete(data);
                return ESP_ERR_INVALID_ARG;
            }
        }
    } else {
        cJSON_AddNumberToObject(data, "level", ts_log_get_level());
        cJSON_AddStringToObject(data, "status", "get");
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                      Register System APIs                                  */
/*===========================================================================*/

esp_err_t ts_api_register_system_apis(void)
{
    static const ts_api_endpoint_t system_apis[] = {
        {
            .name = "system.info",
            .description = "Get system information",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_system_info,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "system.memory",
            .description = "Get memory information",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_system_memory,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "system.tasks",
            .description = "Get task list",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_system_tasks,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "system.reboot",
            .description = "Reboot the system",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_system_reboot,
            .requires_auth = true,
            .permission = "system.admin"
        },
        {
            .name = "system.log.level",
            .description = "Get/set log level",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_system_log_level,
            .requires_auth = true,
            .permission = "system.config"
        }
    };
    
    return ts_api_register_multiple(system_apis, 
                                     sizeof(system_apis) / sizeof(system_apis[0]));
}
