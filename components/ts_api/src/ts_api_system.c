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
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "api_system"

/*===========================================================================*/
/*                          Delayed Reboot Task                               */
/*===========================================================================*/

static void reboot_task(void *arg)
{
    int delay_ms = (int)(intptr_t)arg;
    
    /* Wait for response to be sent */
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    
    TS_LOGI(TAG, "Rebooting system...");
    esp_restart();
    
    /* Should not reach here */
    vTaskDelete(NULL);
}

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
#if configTASKLIST_INCLUDE_COREID
        cJSON_AddNumberToObject(task, "core", task_array[i].xCoreID);
#else
        cJSON_AddNumberToObject(task, "core", -1);
#endif
        
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
 * @brief system.cpu - Get CPU core statistics
 */
static esp_err_t api_system_cpu(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    /* Get chip info for core count */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint8_t num_cores = chip_info.cores;
    
#if configUSE_TRACE_FACILITY && configGENERATE_RUN_TIME_STATS
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    TaskStatus_t *task_array = malloc(task_count * sizeof(TaskStatus_t));
    
    if (task_array == NULL) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    uint32_t total_runtime;
    UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
    
    /* Calculate per-core CPU usage */
    uint32_t core_runtime[2] = {0, 0};  /* ESP32-S3 has max 2 cores */
    uint32_t idle_runtime[2] = {0, 0};
    
    /* Sum up runtime for each core and track idle tasks */
    for (UBaseType_t i = 0; i < actual_count; i++) {
        BaseType_t core_id = task_array[i].xCoreID;
        uint32_t task_runtime = task_array[i].ulRunTimeCounter;
        
        if (core_id >= 0 && core_id < num_cores) {
            core_runtime[core_id] += task_runtime;
            
            /* Check if this is an IDLE task */
            if (strncmp(task_array[i].pcTaskName, "IDLE", 4) == 0) {
                idle_runtime[core_id] = task_runtime;
            }
        }
    }
    
    cJSON *cores = cJSON_AddArrayToObject(data, "cores");
    uint32_t total_usage_sum = 0;
    
    for (int i = 0; i < num_cores; i++) {
        cJSON *core = cJSON_CreateObject();
        cJSON_AddNumberToObject(core, "id", i);
        
        /* Calculate CPU usage percentage (100% - idle%) */
        uint32_t cpu_percent = 0;
        if (core_runtime[i] > 0) {
            /* Usage = (total - idle) / total * 100 */
            uint32_t busy_time = core_runtime[i] - idle_runtime[i];
            cpu_percent = (busy_time * 100) / core_runtime[i];
        }
        
        cJSON_AddNumberToObject(core, "usage", cpu_percent);
        cJSON_AddNumberToObject(core, "runtime", core_runtime[i]);
        cJSON_AddNumberToObject(core, "idle_runtime", idle_runtime[i]);
        
        cJSON_AddItemToArray(cores, core);
        total_usage_sum += cpu_percent;
    }
    
    /* Overall CPU usage (average across cores) */
    cJSON_AddNumberToObject(data, "total_usage", num_cores > 0 ? total_usage_sum / num_cores : 0);
    cJSON_AddNumberToObject(data, "total_runtime", total_runtime);
    cJSON_AddNumberToObject(data, "task_count", actual_count);
    
    free(task_array);
#else
    /* Fallback when runtime stats not available */
    cJSON *cores = cJSON_AddArrayToObject(data, "cores");
    for (int i = 0; i < num_cores; i++) {
        cJSON *core = cJSON_CreateObject();
        cJSON_AddNumberToObject(core, "id", i);
        cJSON_AddNumberToObject(core, "usage", 0);
        cJSON_AddStringToObject(core, "error", "Runtime stats not enabled");
        cJSON_AddItemToArray(cores, core);
    }
    cJSON_AddNumberToObject(data, "total_usage", 0);
    cJSON_AddStringToObject(data, "error", "CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS not enabled");
#endif
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief system.reboot - Reboot the system
 */
static esp_err_t api_system_reboot(const cJSON *params, ts_api_result_t *result)
{
    int delay_ms = 500;  /* Default 500ms to allow response to be sent */
    
    if (params) {
        cJSON *delay = cJSON_GetObjectItem(params, "delay");
        if (delay && cJSON_IsNumber(delay)) {
            delay_ms = delay->valueint;
            if (delay_ms < 100) delay_ms = 100;  /* Minimum 100ms to send response */
            if (delay_ms > 10000) delay_ms = 10000;
        }
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status", "rebooting");
    cJSON_AddNumberToObject(data, "delay_ms", delay_ms);
    
    ts_api_result_ok(result, data);
    
    /* Schedule reboot in a separate task to allow HTTP response to be sent first */
    xTaskCreate(reboot_task, "reboot", 2048, (void *)(intptr_t)delay_ms, 1, NULL);
    
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
            if (lvl >= TS_LOG_NONE && lvl <= TS_LOG_VERBOSE) {
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

esp_err_t ts_api_system_register(void)
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
            .name = "system.cpu",
            .description = "Get CPU core statistics",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_system_cpu,
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
            .requires_auth = false,  /* TODO: Enable auth in production */
            .permission = NULL
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
