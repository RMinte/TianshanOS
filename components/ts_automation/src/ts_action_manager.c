/**
 * @file ts_action_manager.c
 * @brief TianShanOS Automation Engine - Action Manager Implementation
 *
 * Implements unified action execution for automation rules:
 * - SSH command execution (sync/async)
 * - LED control (board/touch/matrix)
 * - GPIO control (set level, pulse)
 * - Log, variable set, device control
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_action_manager.h"
#include "ts_variable.h"
#include "ts_event.h"
#include "ts_ssh_client.h"
#include "ts_ssh_commands_config.h"
#include "ts_ssh_hosts_config.h"
#include "ts_keystore.h"
#include "ts_led.h"
#include "ts_led_preset.h"
#include "ts_led_animation.h"
#include "ts_hal.h"
#include "ts_core.h"
#include "ts_console.h"
#include "ts_config_module.h"
#include "ts_storage.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"  /* For xTaskCreateWithCaps */
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_heap_caps.h"           /* For MALLOC_CAP_INTERNAL */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "ts_action_mgr";

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

/** Maximum SSH hosts */
#define MAX_SSH_HOSTS               8

/** Action executor task stack size
 * CRITICAL: Must use DRAM stack (not PSRAM) because this task performs
 * NVS/Flash operations. SPI Flash operations disable cache, and accessing
 * PSRAM with cache disabled causes a crash.
 */
#define ACTION_TASK_STACK_SIZE      8192

/** Action executor task priority */
#define ACTION_TASK_PRIORITY        5

/** NVS namespace for action templates */
#define NVS_NAMESPACE               "action_tpl"

/** NVS key for template count */
#define NVS_KEY_COUNT               "count"

/** NVS key prefix for templates */
#define NVS_KEY_PREFIX              "tpl_"

/*===========================================================================*/
/*                              Internal State                                */
/*===========================================================================*/

typedef struct {
    /* SSH hosts */
    ts_action_ssh_host_t ssh_hosts[MAX_SSH_HOSTS];
    int ssh_host_count;
    SemaphoreHandle_t ssh_hosts_mutex;
    
    /* Action templates */
    ts_action_template_t templates[TS_ACTION_TEMPLATE_MAX];
    int template_count;
    SemaphoreHandle_t templates_mutex;
    
    /* Action queue */
    QueueHandle_t action_queue;
    TaskHandle_t executor_task;
    bool running;
    
    /* Statistics */
    ts_action_stats_t stats;
    SemaphoreHandle_t stats_mutex;
    
    /* Initialization state */
    bool initialized;
} action_manager_ctx_t;

static action_manager_ctx_t *s_ctx = NULL;

/*===========================================================================*/
/*                          Forward Declarations                              */
/*===========================================================================*/

static void action_executor_task(void *arg);
static esp_err_t execute_action_internal(const ts_auto_action_t *action, 
                                          ts_action_result_t *result);

/*===========================================================================*/
/*                          Initialization                                    */
/*===========================================================================*/

esp_err_t ts_action_manager_init(void)
{
    if (s_ctx != NULL) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing action manager");
    
    /* Allocate context (PSRAM preferred) */
    s_ctx = TS_CALLOC_PSRAM(1, sizeof(action_manager_ctx_t));
    if (s_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate context");
        return ESP_ERR_NO_MEM;
    }
    
    /* Create mutexes */
    s_ctx->ssh_hosts_mutex = xSemaphoreCreateMutex();
    s_ctx->stats_mutex = xSemaphoreCreateMutex();
    s_ctx->templates_mutex = xSemaphoreCreateMutex();
    if (!s_ctx->ssh_hosts_mutex || !s_ctx->stats_mutex || !s_ctx->templates_mutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        goto cleanup;
    }
    
    /* Create action queue */
    s_ctx->action_queue = xQueueCreate(TS_ACTION_QUEUE_SIZE, 
                                        sizeof(ts_action_queue_entry_t));
    if (!s_ctx->action_queue) {
        ESP_LOGE(TAG, "Failed to create action queue");
        goto cleanup;
    }
    
    /* Start executor task
     * CRITICAL: Must use DRAM stack (not PSRAM) because this task performs
     * NVS/Flash operations. SPI Flash operations disable cache, and accessing
     * PSRAM (external SPI RAM) with cache disabled causes a crash.
     * Use xTaskCreateWithCaps to explicitly allocate stack in DRAM.
     */
    s_ctx->running = true;
    BaseType_t ret = xTaskCreateWithCaps(action_executor_task,
                                          "action_exec",
                                          ACTION_TASK_STACK_SIZE,
                                          NULL,
                                          ACTION_TASK_PRIORITY,
                                          &s_ctx->executor_task,
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create executor task");
        goto cleanup;
    }
    
    s_ctx->initialized = true;
    
    /* Load saved templates from NVS */
    ts_action_templates_load();
    
    ESP_LOGI(TAG, "Action manager initialized");
    return ESP_OK;
    
cleanup:
    if (s_ctx->action_queue) vQueueDelete(s_ctx->action_queue);
    if (s_ctx->ssh_hosts_mutex) vSemaphoreDelete(s_ctx->ssh_hosts_mutex);
    if (s_ctx->stats_mutex) vSemaphoreDelete(s_ctx->stats_mutex);
    if (s_ctx->templates_mutex) vSemaphoreDelete(s_ctx->templates_mutex);
    free(s_ctx);
    s_ctx = NULL;
    return ESP_ERR_NO_MEM;
}

esp_err_t ts_action_manager_deinit(void)
{
    if (s_ctx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Deinitializing action manager");
    
    /* Stop executor task */
    s_ctx->running = false;
    if (s_ctx->executor_task) {
        /* Send empty entry to wake up task */
        ts_action_queue_entry_t empty = {0};
        xQueueSend(s_ctx->action_queue, &empty, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    /* Cleanup resources */
    if (s_ctx->action_queue) vQueueDelete(s_ctx->action_queue);
    if (s_ctx->ssh_hosts_mutex) vSemaphoreDelete(s_ctx->ssh_hosts_mutex);
    if (s_ctx->stats_mutex) vSemaphoreDelete(s_ctx->stats_mutex);
    
    free(s_ctx);
    s_ctx = NULL;
    
    ESP_LOGI(TAG, "Action manager deinitialized");
    return ESP_OK;
}

bool ts_action_manager_is_initialized(void)
{
    return (s_ctx != NULL && s_ctx->initialized);
}

/*===========================================================================*/
/*                          SSH Host Management                               */
/*===========================================================================*/

esp_err_t ts_action_register_ssh_host(const ts_action_ssh_host_t *host)
{
    if (!s_ctx || !host || !host->id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->ssh_hosts_mutex, portMAX_DELAY);
    
    /* Check if already exists */
    for (int i = 0; i < s_ctx->ssh_host_count; i++) {
        if (strcmp(s_ctx->ssh_hosts[i].id, host->id) == 0) {
            /* Update existing */
            memcpy(&s_ctx->ssh_hosts[i], host, sizeof(ts_action_ssh_host_t));
            xSemaphoreGive(s_ctx->ssh_hosts_mutex);
            ESP_LOGD(TAG, "Updated SSH host: %s", host->id);
            return ESP_OK;
        }
    }
    
    /* Add new */
    if (s_ctx->ssh_host_count >= MAX_SSH_HOSTS) {
        xSemaphoreGive(s_ctx->ssh_hosts_mutex);
        ESP_LOGE(TAG, "SSH host limit reached");
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(&s_ctx->ssh_hosts[s_ctx->ssh_host_count], host, sizeof(ts_action_ssh_host_t));
    s_ctx->ssh_host_count++;
    
    xSemaphoreGive(s_ctx->ssh_hosts_mutex);
    ESP_LOGI(TAG, "Registered SSH host: %s (%s@%s:%d)", 
             host->id, host->username, host->host, host->port);
    return ESP_OK;
}

esp_err_t ts_action_unregister_ssh_host(const char *host_id)
{
    if (!s_ctx || !host_id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->ssh_hosts_mutex, portMAX_DELAY);
    
    for (int i = 0; i < s_ctx->ssh_host_count; i++) {
        if (strcmp(s_ctx->ssh_hosts[i].id, host_id) == 0) {
            /* Move last to this position */
            if (i < s_ctx->ssh_host_count - 1) {
                memcpy(&s_ctx->ssh_hosts[i], 
                       &s_ctx->ssh_hosts[s_ctx->ssh_host_count - 1],
                       sizeof(ts_action_ssh_host_t));
            }
            s_ctx->ssh_host_count--;
            xSemaphoreGive(s_ctx->ssh_hosts_mutex);
            ESP_LOGI(TAG, "Unregistered SSH host: %s", host_id);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(s_ctx->ssh_hosts_mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ts_action_get_ssh_host(const char *host_id, ts_action_ssh_host_t *host_out)
{
    if (!s_ctx || !host_id || !host_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* First, try to find in internal list */
    xSemaphoreTake(s_ctx->ssh_hosts_mutex, portMAX_DELAY);
    
    for (int i = 0; i < s_ctx->ssh_host_count; i++) {
        if (strcmp(s_ctx->ssh_hosts[i].id, host_id) == 0) {
            memcpy(host_out, &s_ctx->ssh_hosts[i], sizeof(ts_action_ssh_host_t));
            xSemaphoreGive(s_ctx->ssh_hosts_mutex);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(s_ctx->ssh_hosts_mutex);
    
    /* Fallback: try to get from SSH hosts config system */
    ts_ssh_host_config_t config;
    esp_err_t ret = ts_ssh_hosts_config_get(host_id, &config);
    if (ret == ESP_OK) {
        /* Convert ts_ssh_host_config_t to ts_action_ssh_host_t */
        memset(host_out, 0, sizeof(ts_action_ssh_host_t));
        strncpy(host_out->id, config.id, sizeof(host_out->id) - 1);
        strncpy(host_out->host, config.host, sizeof(host_out->host) - 1);
        host_out->port = config.port;
        strncpy(host_out->username, config.username, sizeof(host_out->username) - 1);
        host_out->use_key_auth = (config.auth_type == TS_SSH_HOST_AUTH_KEY);
        /* Store keyid in key_path field - will be resolved at connection time */
        if (host_out->use_key_auth && config.keyid[0]) {
            strncpy(host_out->key_path, config.keyid, sizeof(host_out->key_path) - 1);
        }
        ESP_LOGD(TAG, "Got SSH host '%s' from config system", host_id);
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

int ts_action_get_ssh_host_count(void)
{
    if (!s_ctx) return 0;
    
    xSemaphoreTake(s_ctx->ssh_hosts_mutex, portMAX_DELAY);
    int count = s_ctx->ssh_host_count;
    xSemaphoreGive(s_ctx->ssh_hosts_mutex);
    
    return count;
}

esp_err_t ts_action_get_ssh_hosts(ts_action_ssh_host_t *hosts_out, 
                                   size_t max_count, 
                                   size_t *count_out)
{
    if (!s_ctx || !hosts_out || !count_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->ssh_hosts_mutex, portMAX_DELAY);
    
    size_t to_copy = (s_ctx->ssh_host_count < max_count) ? 
                     s_ctx->ssh_host_count : max_count;
    
    for (size_t i = 0; i < to_copy; i++) {
        memcpy(&hosts_out[i], &s_ctx->ssh_hosts[i], sizeof(ts_action_ssh_host_t));
        // 清除敏感信息（密码）
        memset(hosts_out[i].password, 0, sizeof(hosts_out[i].password));
    }
    
    *count_out = to_copy;
    
    xSemaphoreGive(s_ctx->ssh_hosts_mutex);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Action Execution                                  */
/*===========================================================================*/

/**
 * Execute action synchronously via the executor task (DRAM stack).
 * 
 * This function queues the action and waits for completion. This ensures
 * the action executes in the executor task context, which has a DRAM stack
 * and can safely perform NVS/Flash operations.
 */
esp_err_t ts_action_manager_execute(const ts_auto_action_t *action, 
                                     ts_action_result_t *result)
{
    if (!s_ctx || !action) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_ctx->running || !s_ctx->executor_task) {
        ESP_LOGE(TAG, "Executor task not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    ts_action_result_t local_result = {0};
    ts_action_result_t *res = result ? result : &local_result;
    
    /* Create semaphore for sync execution */
    SemaphoreHandle_t done_sem = xSemaphoreCreateBinary();
    if (!done_sem) {
        ESP_LOGE(TAG, "Failed to create sync semaphore");
        return ESP_ERR_NO_MEM;
    }
    
    /* Queue the action with sync fields */
    ts_action_queue_entry_t entry = {
        .action = *action,
        .callback = NULL,
        .user_data = NULL,
        .priority = 0,
        .enqueue_time = esp_timer_get_time() / 1000,
        .done_sem = done_sem,
        .result_ptr = res
    };
    
    if (xQueueSend(s_ctx->action_queue, &entry, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Action queue full");
        vSemaphoreDelete(done_sem);
        return ESP_ERR_NO_MEM;
    }
    
    /* Wait for completion (timeout based on action type) */
    uint32_t timeout_ms = 30000; /* Default 30s */
    if (action->type == TS_AUTO_ACT_SSH_CMD || action->type == TS_AUTO_ACT_SSH_CMD_REF) {
        timeout_ms = 60000; /* SSH commands may take longer */
    }
    
    if (xSemaphoreTake(done_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "Action execution timeout");
        res->status = TS_ACTION_STATUS_TIMEOUT;
        snprintf(res->output, sizeof(res->output), "Execution timeout");
        vSemaphoreDelete(done_sem);
        return ESP_ERR_TIMEOUT;
    }
    
    vSemaphoreDelete(done_sem);
    
    /* Stats are updated by executor task */
    return (res->status == TS_ACTION_STATUS_SUCCESS) ? ESP_OK : ESP_FAIL;
}

esp_err_t ts_action_queue(const ts_auto_action_t *action,
                          ts_action_callback_t callback,
                          void *user_data,
                          uint8_t priority)
{
    if (!s_ctx || !action) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_action_queue_entry_t entry = {
        .action = *action,
        .callback = callback,
        .user_data = user_data,
        .priority = priority,
        .enqueue_time = esp_timer_get_time() / 1000,
        .done_sem = NULL,      /* Async mode: no sync semaphore */
        .result_ptr = NULL     /* Async mode: no result pointer */
    };
    
    if (xQueueSend(s_ctx->action_queue, &entry, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Action queue full");
        return ESP_ERR_NO_MEM;
    }
    
    /* Update high water mark */
    xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
    UBaseType_t waiting = uxQueueMessagesWaiting(s_ctx->action_queue);
    if (waiting > s_ctx->stats.queue_high_water) {
        s_ctx->stats.queue_high_water = waiting;
    }
    xSemaphoreGive(s_ctx->stats_mutex);
    
    return ESP_OK;
}

esp_err_t ts_action_execute_sequence(const ts_auto_action_t *actions,
                                      int count,
                                      bool stop_on_error)
{
    if (!actions || count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    ts_action_result_t result;
    
    for (int i = 0; i < count; i++) {
        ret = ts_action_manager_execute(&actions[i], &result);
        if (ret != ESP_OK || result.status != TS_ACTION_STATUS_SUCCESS) {
            ESP_LOGW(TAG, "Action %d failed: %s", i, result.output);
            if (stop_on_error) {
                return ret != ESP_OK ? ret : ESP_FAIL;
            }
        }
    }
    
    return ESP_OK;
}

esp_err_t ts_action_cancel_all(void)
{
    if (!s_ctx) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Clear queue */
    xQueueReset(s_ctx->action_queue);
    ESP_LOGI(TAG, "Cancelled all pending actions");
    return ESP_OK;
}

/*===========================================================================*/
/*                       Individual Action Executors                          */
/*===========================================================================*/

esp_err_t ts_action_exec_ssh(const ts_auto_action_ssh_t *ssh,
                              ts_action_result_t *result)
{
    if (!ssh || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int64_t start_time = esp_timer_get_time();
    result->status = TS_ACTION_STATUS_RUNNING;
    
    /* Get SSH host config */
    ts_action_ssh_host_t host;
    if (ts_action_get_ssh_host(ssh->host_ref, &host) != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "SSH host '%s' not found", ssh->host_ref);
        result->status = TS_ACTION_STATUS_FAILED;
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Expand variables in command */
    char expanded_cmd[256];
    ts_action_expand_variables(ssh->command, expanded_cmd, sizeof(expanded_cmd));
    
    ESP_LOGI(TAG, "SSH [%s]: %s", ssh->host_ref, expanded_cmd);
    
    /* Create SSH session */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host.host;
    config.port = host.port;
    config.username = host.username;
    config.timeout_ms = ssh->timeout_ms > 0 ? ssh->timeout_ms : TS_ACTION_SSH_TIMEOUT_MS;
    
    /* Load SSH key from keystore if using key auth */
    char *key_data = NULL;
    size_t key_len = 0;
    esp_err_t ret;
    
    if (host.use_key_auth && host.key_path[0]) {
        /* host.key_path actually contains the keyid */
        const char *keyid = host.key_path;
        
        /* Try to load from keystore first */
        ret = ts_keystore_load_private_key(keyid, &key_data, &key_len);
        if (ret == ESP_OK && key_data && key_len > 0) {
            config.auth_method = TS_SSH_AUTH_PUBLICKEY;
            config.auth.key.private_key = (const uint8_t *)key_data;
            config.auth.key.private_key_len = key_len;
            config.auth.key.private_key_path = NULL;
            ESP_LOGI(TAG, "Loaded SSH key '%s' from keystore (%zu bytes)", keyid, key_len);
        } else {
            /* Fallback: try as file path */
            char full_path[128];
            if (keyid[0] == '/') {
                strncpy(full_path, keyid, sizeof(full_path) - 1);
            } else {
                snprintf(full_path, sizeof(full_path), "/sdcard/ssh/%s", keyid);
            }
            config.auth_method = TS_SSH_AUTH_PUBLICKEY;
            config.auth.key.private_key_path = full_path;
            ESP_LOGI(TAG, "Using SSH key file: %s", full_path);
        }
    } else {
        config.auth_method = TS_SSH_AUTH_PASSWORD;
        config.auth.password = host.password;
    }
    
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "SSH session create failed: %s", esp_err_to_name(ret));
        result->status = TS_ACTION_STATUS_FAILED;
        return ret;
    }
    
    /* Connect */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "SSH connect failed: %s", esp_err_to_name(ret));
        result->status = TS_ACTION_STATUS_FAILED;
        ts_ssh_session_destroy(session);
        return ret;
    }
    
    /* Execute command */
    ts_ssh_exec_result_t exec_result = {0};
    ret = ts_ssh_exec(session, expanded_cmd, &exec_result);
    
    if (ret == ESP_OK) {
        result->exit_code = exec_result.exit_code;
        if (exec_result.stdout_data && exec_result.stdout_len > 0) {
            size_t copy_len = exec_result.stdout_len < sizeof(result->output) - 1 
                            ? exec_result.stdout_len 
                            : sizeof(result->output) - 1;
            memcpy(result->output, exec_result.stdout_data, copy_len);
            result->output[copy_len] = '\0';
        } else if (exec_result.stderr_data && exec_result.stderr_len > 0) {
            size_t copy_len = exec_result.stderr_len < sizeof(result->output) - 1 
                            ? exec_result.stderr_len 
                            : sizeof(result->output) - 1;
            memcpy(result->output, exec_result.stderr_data, copy_len);
            result->output[copy_len] = '\0';
        }
        
        result->status = (exec_result.exit_code == 0) 
                       ? TS_ACTION_STATUS_SUCCESS 
                       : TS_ACTION_STATUS_FAILED;
        
        /* Free result strings */
        if (exec_result.stdout_data) free(exec_result.stdout_data);
        if (exec_result.stderr_data) free(exec_result.stderr_data);
    } else {
        snprintf(result->output, sizeof(result->output), 
                 "SSH exec failed: %s", esp_err_to_name(ret));
        result->status = (ret == ESP_ERR_TIMEOUT) 
                       ? TS_ACTION_STATUS_TIMEOUT 
                       : TS_ACTION_STATUS_FAILED;
    }
    
    /* Cleanup */
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    
    /* Free key data if loaded from keystore */
    if (key_data) {
        free(key_data);
    }
    
    result->duration_ms = (esp_timer_get_time() - start_time) / 1000;
    result->timestamp = esp_timer_get_time() / 1000;
    
    /* Update statistics */
    xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
    s_ctx->stats.ssh_commands++;
    xSemaphoreGive(s_ctx->stats_mutex);
    
    ESP_LOGD(TAG, "SSH result: exit=%d, duration=%lu ms", 
             result->exit_code, result->duration_ms);
    
    return ret;
}

/**
 * @brief 辅助函数：解析设备名称短名到完整名称
 */
static const char *action_resolve_led_device_name(const char *name)
{
    if (!name) return NULL;
    if (strcmp(name, "touch") == 0) return "led_touch";
    if (strcmp(name, "board") == 0) return "led_board";
    if (strcmp(name, "matrix") == 0) return "led_matrix";
    return name;  // 返回原名（可能已经是完整名称）
}

esp_err_t ts_action_exec_led(const ts_auto_action_led_t *led,
                              ts_action_result_t *result)
{
    if (!led || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 创建本地副本以支持变量展开 */
    ts_auto_action_led_t led_expanded;
    memcpy(&led_expanded, led, sizeof(ts_auto_action_led_t));
    
    /* 展开字符串字段中的变量 */
    char expanded_buf[128];
    
    if (led->text[0]) {
        ts_action_expand_variables(led->text, expanded_buf, sizeof(expanded_buf));
        strncpy(led_expanded.text, expanded_buf, sizeof(led_expanded.text) - 1);
    }
    if (led->image_path[0]) {
        ts_action_expand_variables(led->image_path, expanded_buf, sizeof(expanded_buf));
        strncpy(led_expanded.image_path, expanded_buf, sizeof(led_expanded.image_path) - 1);
    }
    if (led->qr_text[0]) {
        ts_action_expand_variables(led->qr_text, expanded_buf, sizeof(expanded_buf));
        strncpy(led_expanded.qr_text, expanded_buf, sizeof(led_expanded.qr_text) - 1);
    }
    if (led->filter[0]) {
        ts_action_expand_variables(led->filter, expanded_buf, sizeof(expanded_buf));
        strncpy(led_expanded.filter, expanded_buf, sizeof(led_expanded.filter) - 1);
    }
    if (led->effect[0]) {
        ts_action_expand_variables(led->effect, expanded_buf, sizeof(expanded_buf));
        strncpy(led_expanded.effect, expanded_buf, sizeof(led_expanded.effect) - 1);
    }
    
    /* 使用展开后的结构 */
    const ts_auto_action_led_t *led_final = &led_expanded;
    
    result->status = TS_ACTION_STATUS_RUNNING;
    int64_t start_time = esp_timer_get_time();
    esp_err_t ret = ESP_OK;
    
    /* 解析设备名称（支持短名和完整名） */
    const char *device_name = action_resolve_led_device_name(led_final->device);
    
    ESP_LOGI(TAG, "LED action: device=%s, ctrl_type=%d", device_name, led_final->ctrl_type);
    
    /* 获取设备句柄 */
    ts_led_device_t device = ts_led_device_get(device_name);
    if (!device) {
        ESP_LOGW(TAG, "LED device '%s' not found", device_name);
        ret = ESP_ERR_NOT_FOUND;
        goto done;
    }
    
    /* 获取默认 layer */
    ts_led_layer_t layer = ts_led_layer_get(device, 0);
    if (!layer) {
        ESP_LOGW(TAG, "LED layer not found for device '%s'", device_name);
        ret = ESP_ERR_NOT_FOUND;
        goto done;
    }
    
    /* 根据控制类型执行对应操作 */
    switch (led_final->ctrl_type) {
        case TS_LED_CTRL_OFF:
            /* 关闭 LED */
            ts_led_animation_stop(layer);
            ret = ts_led_fill(layer, TS_LED_RGB(0, 0, 0));
            snprintf(result->output, sizeof(result->output), "LED %s turned off", led->device);
            break;
            
        case TS_LED_CTRL_BRIGHTNESS:
            /* 仅调节亮度 */
            ret = ts_led_device_set_brightness(device, led_final->brightness);
            snprintf(result->output, sizeof(result->output), "LED %s brightness=%d", led->device, led_final->brightness);
            break;
            
        case TS_LED_CTRL_EFFECT:
            /* 启动效果动画 */
            if (led_final->effect[0]) {
                const ts_led_animation_def_t *anim = ts_led_animation_get_builtin(led_final->effect);
                if (anim) {
                    ESP_LOGI(TAG, "Starting effect '%s' on device '%s'", led_final->effect, device_name);
                    ret = ts_led_animation_start(layer, anim);
                    snprintf(result->output, sizeof(result->output), "LED %s effect=%.32s started", led->device, led_final->effect);
                } else {
                    ESP_LOGW(TAG, "Effect '%s' not found", led_final->effect);
                    ret = ESP_ERR_NOT_FOUND;
                    snprintf(result->output, sizeof(result->output), "Effect '%.32s' not found", led_final->effect);
                }
            } else {
                ret = ESP_ERR_INVALID_ARG;
                snprintf(result->output, sizeof(result->output), "No effect specified");
            }
            break;
            
        case TS_LED_CTRL_FILL:
        default:
            /* 纯色填充（默认行为，兼容旧版本） */
            /* 如果有效果名（旧版兼容），优先启动效果 */
            if (led_final->effect[0]) {
                const ts_led_animation_def_t *anim = ts_led_animation_get_builtin(led_final->effect);
                if (anim) {
                    ret = ts_led_animation_start(layer, anim);
                    snprintf(result->output, sizeof(result->output), "LED %s effect=%.32s started", led->device, led_final->effect);
                    break;
                }
            }
            
            ts_led_rgb_t color = TS_LED_RGB(led_final->r, led_final->g, led_final->b);
            if (led_final->index == 0xFF) {
                ret = ts_led_fill(layer, color);
            } else {
                ret = ts_led_device_set_pixel(device, (uint16_t)led_final->index, color);
            }
            snprintf(result->output, sizeof(result->output), "LED %s filled with color", led->device);
            break;
            
        case TS_LED_CTRL_TEXT:
            /* 显示文本（仅 Matrix）- 通过 CLI 执行 */
            if (strcmp(device_name, "led_matrix") != 0) {
                ret = ESP_ERR_NOT_SUPPORTED;
                snprintf(result->output, sizeof(result->output), "Text display only supported on matrix");
                break;
            }
            if (led_final->text[0]) {
                /* 构建 CLI 命令 */
                char cmd[512];
                int len = snprintf(cmd, sizeof(cmd), "led --draw-text --device matrix --text \"%s\"", led_final->text);
                if (led_final->font[0]) {
                    len += snprintf(cmd + len, sizeof(cmd) - len, " --font %s", led_final->font);
                } else {
                    len += snprintf(cmd + len, sizeof(cmd) - len, " --font pixel9x9");
                }
                if (led_final->r || led_final->g || led_final->b) {
                    len += snprintf(cmd + len, sizeof(cmd) - len, " --color #%02X%02X%02X", led_final->r, led_final->g, led_final->b);
                }
                if (led_final->scroll[0] && strcmp(led_final->scroll, "none") != 0) {
                    len += snprintf(cmd + len, sizeof(cmd) - len, " --scroll %s", led_final->scroll);
                    if (led_final->loop) {
                        len += snprintf(cmd + len, sizeof(cmd) - len, " --loop");
                    }
                }
                if (led_final->speed > 0) {
                    len += snprintf(cmd + len, sizeof(cmd) - len, " --speed %d", led_final->speed);
                }
                
                ESP_LOGI(TAG, "Executing LED text CLI: %s", cmd);
                ret = ts_console_exec(cmd, NULL);
                snprintf(result->output, sizeof(result->output), "LED text: %.200s%s", led_final->text, strlen(led_final->text) > 200 ? "..." : "");
            } else {
                ret = ESP_ERR_INVALID_ARG;
                snprintf(result->output, sizeof(result->output), "No text specified");
            }
            break;
            
        case TS_LED_CTRL_IMAGE:
            /* 显示图像（仅 Matrix）- 通过 CLI 执行 */
            if (strcmp(device_name, "led_matrix") != 0) {
                ret = ESP_ERR_NOT_SUPPORTED;
                snprintf(result->output, sizeof(result->output), "Image display only supported on matrix");
                break;
            }
            if (led_final->image_path[0]) {
                char cmd[512];
                snprintf(cmd, sizeof(cmd), "led --image --device matrix --file %.256s%s", 
                         led_final->image_path, led_final->center ? " --center content" : "");
                
                ESP_LOGI(TAG, "Executing LED image CLI: %s", cmd);
                ret = ts_console_exec(cmd, NULL);
                snprintf(result->output, sizeof(result->output), "LED image: %.200s%s", led_final->image_path, strlen(led_final->image_path) > 200 ? "..." : "");
            } else {
                ret = ESP_ERR_INVALID_ARG;
                snprintf(result->output, sizeof(result->output), "No image path specified");
            }
            break;
            
        case TS_LED_CTRL_QRCODE:
            /* 显示 QR 码（仅 Matrix）- 通过 CLI 执行 */
            if (strcmp(device_name, "led_matrix") != 0) {
                ret = ESP_ERR_NOT_SUPPORTED;
                snprintf(result->output, sizeof(result->output), "QR code only supported on matrix");
                break;
            }
            if (led_final->qr_text[0]) {
                char cmd[512];
                int len = snprintf(cmd, sizeof(cmd), "led --qrcode --device matrix --text \"%s\"", led_final->qr_text);
                if (led_final->qr_ecc) {
                    len += snprintf(cmd + len, sizeof(cmd) - len, " --ecc %c", led_final->qr_ecc);
                }
                if (led_final->r || led_final->g || led_final->b) {
                    len += snprintf(cmd + len, sizeof(cmd) - len, " --color #%02X%02X%02X", led_final->r, led_final->g, led_final->b);
                }
                
                ESP_LOGI(TAG, "Executing LED QR CLI: %s", cmd);
                ret = ts_console_exec(cmd, NULL);
                snprintf(result->output, sizeof(result->output), "LED QR: %.200s%s", 
                         led_final->qr_text, strlen(led_final->qr_text) > 200 ? "..." : "");
            } else {
                ret = ESP_ERR_INVALID_ARG;
                snprintf(result->output, sizeof(result->output), "No QR text specified");
            }
            break;
            
        case TS_LED_CTRL_FILTER:
            /* 应用后处理滤镜（仅 Matrix）- 通过 CLI 执行 */
            if (strcmp(device_name, "led_matrix") != 0) {
                ret = ESP_ERR_NOT_SUPPORTED;
                snprintf(result->output, sizeof(result->output), "Filter only supported on matrix");
                break;
            }
            if (led_final->filter[0]) {
                char cmd[128];
                if (strcmp(led_final->filter, "none") == 0 || strcmp(led_final->filter, "stop") == 0) {
                    snprintf(cmd, sizeof(cmd), "led --stop-filter --device matrix");
                } else {
                    snprintf(cmd, sizeof(cmd), "led --filter --device matrix --filter-name %s", led_final->filter);
                }
                
                ESP_LOGI(TAG, "Executing LED filter CLI: %s", cmd);
                ret = ts_console_exec(cmd, NULL);
                snprintf(result->output, sizeof(result->output), "LED filter: %s", led_final->filter);
            } else {
                ret = ESP_ERR_INVALID_ARG;
                snprintf(result->output, sizeof(result->output), "No filter specified");
            }
            break;
            
        case TS_LED_CTRL_FILTER_STOP:
            /* 停止滤镜（仅 Matrix）*/
            if (strcmp(device_name, "led_matrix") != 0) {
                ret = ESP_ERR_NOT_SUPPORTED;
                snprintf(result->output, sizeof(result->output), "Filter stop only supported on matrix");
                break;
            }
            {
                const char *cmd = "led --stop-filter --device matrix";
                ESP_LOGI(TAG, "Executing LED filter stop CLI: %s", cmd);
                ret = ts_console_exec(cmd, NULL);
                snprintf(result->output, sizeof(result->output), "LED filter stopped");
            }
            break;
            
        case TS_LED_CTRL_TEXT_STOP:
            /* 停止文本覆盖层（仅 Matrix）*/
            if (strcmp(device_name, "led_matrix") != 0) {
                ret = ESP_ERR_NOT_SUPPORTED;
                snprintf(result->output, sizeof(result->output), "Text stop only supported on matrix");
                break;
            }
            {
                const char *cmd = "led --stop-text --device matrix";
                ESP_LOGI(TAG, "Executing LED text stop CLI: %s", cmd);
                ret = ts_console_exec(cmd, NULL);
                snprintf(result->output, sizeof(result->output), "LED text stopped");
            }
            break;
    }

done:
    result->duration_ms = (esp_timer_get_time() - start_time) / 1000;
    result->timestamp = esp_timer_get_time() / 1000;
    
    if (ret == ESP_OK) {
        result->status = TS_ACTION_STATUS_SUCCESS;
    } else {
        result->status = TS_ACTION_STATUS_FAILED;
        if (result->output[0] == '\0') {
            snprintf(result->output, sizeof(result->output), 
                     "LED failed: %s", esp_err_to_name(ret));
        }
    }
    
    /* Update statistics */
    xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
    s_ctx->stats.led_actions++;
    xSemaphoreGive(s_ctx->stats_mutex);
    
    return ret;
}

esp_err_t ts_action_exec_gpio(const ts_auto_action_gpio_t *gpio_action,
                               ts_action_result_t *result)
{
    if (!gpio_action || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    result->status = TS_ACTION_STATUS_RUNNING;
    int64_t start_time = esp_timer_get_time();
    
    uint8_t pin = gpio_action->pin;
    bool level = gpio_action->level;
    uint32_t pulse_ms = gpio_action->pulse_ms;
    
    ESP_LOGD(TAG, "GPIO action: pin=%d, level=%d, pulse_ms=%lu", 
             pin, level, pulse_ms);
    
    /* Configure GPIO as output if not already */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "GPIO config failed: %s", esp_err_to_name(ret));
        result->status = TS_ACTION_STATUS_FAILED;
        return ret;
    }
    
    /* Set level */
    ret = gpio_set_level(pin, level ? 1 : 0);
    if (ret != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "GPIO set failed: %s", esp_err_to_name(ret));
        result->status = TS_ACTION_STATUS_FAILED;
        return ret;
    }
    
    /* If pulse mode, wait and toggle back */
    if (pulse_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(pulse_ms));
        ret = gpio_set_level(pin, level ? 0 : 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "GPIO pulse restore failed: %s", esp_err_to_name(ret));
        }
        snprintf(result->output, sizeof(result->output), 
                 "GPIO %d pulse %lu ms", pin, pulse_ms);
    } else {
        snprintf(result->output, sizeof(result->output), 
                 "GPIO %d set to %d", pin, level);
    }
    
    result->duration_ms = (esp_timer_get_time() - start_time) / 1000;
    result->timestamp = esp_timer_get_time() / 1000;
    result->status = TS_ACTION_STATUS_SUCCESS;
    
    /* Update statistics */
    xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
    s_ctx->stats.gpio_actions++;
    xSemaphoreGive(s_ctx->stats_mutex);
    
    return ESP_OK;
}

esp_err_t ts_action_exec_log(const ts_auto_action_log_t *log_action,
                              ts_action_result_t *result)
{
    if (!log_action || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    result->status = TS_ACTION_STATUS_RUNNING;
    
    /* Expand variables in message */
    char expanded_msg[256];
    ts_action_expand_variables(log_action->message, expanded_msg, sizeof(expanded_msg));
    
    /* Log at appropriate level */
    switch (log_action->level) {
        case ESP_LOG_ERROR:
            ESP_LOGE("AUTOMATION", "%s", expanded_msg);
            break;
        case ESP_LOG_WARN:
            ESP_LOGW("AUTOMATION", "%s", expanded_msg);
            break;
        case ESP_LOG_INFO:
            ESP_LOGI("AUTOMATION", "%s", expanded_msg);
            break;
        case ESP_LOG_DEBUG:
            ESP_LOGD("AUTOMATION", "%s", expanded_msg);
            break;
        default:
            ESP_LOGI("AUTOMATION", "%s", expanded_msg);
            break;
    }
    
    result->status = TS_ACTION_STATUS_SUCCESS;
    strncpy(result->output, expanded_msg, sizeof(result->output) - 1);
    result->timestamp = esp_timer_get_time() / 1000;
    
    return ESP_OK;
}

esp_err_t ts_action_exec_set_var(const ts_auto_action_set_var_t *set_var,
                                  ts_action_result_t *result)
{
    if (!set_var || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    result->status = TS_ACTION_STATUS_RUNNING;
    
    esp_err_t ret = ts_variable_set(set_var->variable, &set_var->value);
    
    if (ret == ESP_OK) {
        result->status = TS_ACTION_STATUS_SUCCESS;
        snprintf(result->output, sizeof(result->output), 
                 "Variable '%s' set", set_var->variable);
    } else {
        result->status = TS_ACTION_STATUS_FAILED;
        snprintf(result->output, sizeof(result->output), 
                 "Set variable failed: %s", esp_err_to_name(ret));
    }
    
    result->timestamp = esp_timer_get_time() / 1000;
    return ret;
}

esp_err_t ts_action_exec_device(const ts_auto_action_device_t *device,
                                 ts_action_result_t *result)
{
    if (!device || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    result->status = TS_ACTION_STATUS_RUNNING;
    
    ESP_LOGI(TAG, "Device control: %s -> %s", device->device, device->action);
    
    /* TODO: Integrate with ts_device_ctrl for AGX/LPMU control */
    /* For now, just log the intent */
    
    snprintf(result->output, sizeof(result->output), 
             "Device control: %s.%s (not implemented)", 
             device->device, device->action);
    result->status = TS_ACTION_STATUS_SUCCESS;
    result->timestamp = esp_timer_get_time() / 1000;
    
    return ESP_OK;
}

esp_err_t ts_action_exec_ssh_ref(const ts_auto_action_ssh_ref_t *ssh_ref,
                                  ts_action_result_t *result)
{
    if (!ssh_ref || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int64_t start_time = esp_timer_get_time();
    result->status = TS_ACTION_STATUS_RUNNING;
    
    /* Look up the SSH command configuration */
    ts_ssh_command_config_t cmd_config;
    esp_err_t ret = ts_ssh_commands_config_get(ssh_ref->cmd_id, &cmd_config);
    if (ret != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "SSH command '%s' not found", ssh_ref->cmd_id);
        result->status = TS_ACTION_STATUS_FAILED;
        ESP_LOGW(TAG, "SSH command ref not found: %s", ssh_ref->cmd_id);
        return ESP_ERR_NOT_FOUND;
    }
    
    if (!cmd_config.enabled) {
        snprintf(result->output, sizeof(result->output), 
                 "SSH command '%s' is disabled", ssh_ref->cmd_id);
        result->status = TS_ACTION_STATUS_FAILED;
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "SSH ref [%s]: host=%s, cmd=%s", 
             ssh_ref->cmd_id, cmd_config.host_id, cmd_config.command);
    
    /* Get SSH host config */
    ts_action_ssh_host_t host;
    if (ts_action_get_ssh_host(cmd_config.host_id, &host) != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "SSH host '%s' not found for command '%s'", 
                 cmd_config.host_id, ssh_ref->cmd_id);
        result->status = TS_ACTION_STATUS_FAILED;
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Expand variables in command */
    char expanded_cmd[256];
    ts_action_expand_variables(cmd_config.command, expanded_cmd, sizeof(expanded_cmd));
    
    /* Handle nohup mode: wrap command for background execution */
    char nohup_cmd[512];
    if (cmd_config.nohup) {
        /* Generate safe name from command name for log file */
        char safe_name[32] = {0};
        const char *src = cmd_config.name;
        int j = 0;
        for (int i = 0; src[i] && j < 20; i++) {
            if ((src[i] >= 'a' && src[i] <= 'z') || 
                (src[i] >= 'A' && src[i] <= 'Z') || 
                (src[i] >= '0' && src[i] <= '9')) {
                safe_name[j++] = src[i];
            }
        }
        if (j == 0) {
            strcpy(safe_name, "cmd");
        }
        
        snprintf(nohup_cmd, sizeof(nohup_cmd),
                 "nohup %s > /tmp/ts_nohup_%s.log 2>&1 &",
                 expanded_cmd, safe_name);
        ESP_LOGI(TAG, "SSH nohup mode: %s", nohup_cmd);
    }
    
    /* Create SSH session */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host.host;
    config.port = host.port;
    config.username = host.username;
    config.timeout_ms = cmd_config.timeout_sec > 0 ? cmd_config.timeout_sec * 1000 : TS_ACTION_SSH_TIMEOUT_MS;
    
    /* Load SSH key from keystore if using key auth */
    char *key_data = NULL;
    size_t key_len = 0;
    
    if (host.use_key_auth && host.key_path[0]) {
        /* host.key_path actually contains the keyid */
        const char *keyid = host.key_path;
        
        /* Try to load from keystore first */
        ret = ts_keystore_load_private_key(keyid, &key_data, &key_len);
        if (ret == ESP_OK && key_data && key_len > 0) {
            config.auth_method = TS_SSH_AUTH_PUBLICKEY;
            config.auth.key.private_key = (const uint8_t *)key_data;
            config.auth.key.private_key_len = key_len;
            config.auth.key.private_key_path = NULL;
            ESP_LOGI(TAG, "Loaded SSH key '%s' from keystore (%zu bytes)", keyid, key_len);
        } else {
            /* Fallback: try as file path */
            char full_path[128];
            if (keyid[0] == '/') {
                strncpy(full_path, keyid, sizeof(full_path) - 1);
            } else {
                snprintf(full_path, sizeof(full_path), "/sdcard/ssh/%s", keyid);
            }
            config.auth_method = TS_SSH_AUTH_PUBLICKEY;
            config.auth.key.private_key_path = full_path;
            ESP_LOGI(TAG, "Using SSH key file: %s", full_path);
        }
    } else {
        config.auth_method = TS_SSH_AUTH_PASSWORD;
        config.auth.password = host.password;
    }
    
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "SSH session create failed: %s", esp_err_to_name(ret));
        result->status = TS_ACTION_STATUS_FAILED;
        return ret;
    }
    
    /* Connect */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        snprintf(result->output, sizeof(result->output), 
                 "SSH connect failed: %s", esp_err_to_name(ret));
        result->status = TS_ACTION_STATUS_FAILED;
        ts_ssh_session_destroy(session);
        return ret;
    }
    
    /* Execute command */
    ts_ssh_exec_result_t exec_result = {0};
    const char *cmd_to_exec = cmd_config.nohup ? nohup_cmd : expanded_cmd;
    ret = ts_ssh_exec(session, cmd_to_exec, &exec_result);
    
    if (ret == ESP_OK) {
        result->exit_code = exec_result.exit_code;
        if (exec_result.stdout_data && exec_result.stdout_len > 0) {
            size_t copy_len = exec_result.stdout_len < sizeof(result->output) - 1 
                            ? exec_result.stdout_len 
                            : sizeof(result->output) - 1;
            memcpy(result->output, exec_result.stdout_data, copy_len);
            result->output[copy_len] = '\0';
        } else if (exec_result.stderr_data && exec_result.stderr_len > 0) {
            size_t copy_len = exec_result.stderr_len < sizeof(result->output) - 1 
                            ? exec_result.stderr_len 
                            : sizeof(result->output) - 1;
            memcpy(result->output, exec_result.stderr_data, copy_len);
            result->output[copy_len] = '\0';
        }
        
        result->status = (exec_result.exit_code == 0) 
                       ? TS_ACTION_STATUS_SUCCESS 
                       : TS_ACTION_STATUS_FAILED;
        
        /* Update variables if var_name is configured */
        if (cmd_config.var_name[0]) {
            char var_full[64];
            
            /* Set exit_code variable */
            snprintf(var_full, sizeof(var_full), "%s.exit_code", cmd_config.var_name);
            ts_variable_set_int(var_full, exec_result.exit_code);
            
            /* Set status variable */
            snprintf(var_full, sizeof(var_full), "%s.status", cmd_config.var_name);
            ts_variable_set_string(var_full, 
                exec_result.exit_code == 0 ? "success" : "failed");
            
            /* Set timestamp variable */
            snprintf(var_full, sizeof(var_full), "%s.timestamp", cmd_config.var_name);
            ts_variable_set_int(var_full, (int32_t)(esp_timer_get_time() / 1000000));
        }
        
        /* Free result strings */
        if (exec_result.stdout_data) free(exec_result.stdout_data);
        if (exec_result.stderr_data) free(exec_result.stderr_data);
    } else {
        snprintf(result->output, sizeof(result->output), 
                 "SSH exec failed: %s", esp_err_to_name(ret));
        result->status = (ret == ESP_ERR_TIMEOUT) 
                       ? TS_ACTION_STATUS_TIMEOUT 
                       : TS_ACTION_STATUS_FAILED;
    }
    
    /* Cleanup */
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    
    /* Free key data if loaded from keystore */
    if (key_data) {
        free(key_data);
    }
    
    result->duration_ms = (esp_timer_get_time() - start_time) / 1000;
    result->timestamp = esp_timer_get_time() / 1000;
    
    /* Update command execution time */
    ts_ssh_commands_config_update_exec_time(ssh_ref->cmd_id);
    
    /* Update statistics */
    xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
    s_ctx->stats.ssh_commands++;
    xSemaphoreGive(s_ctx->stats_mutex);
    
    ESP_LOGD(TAG, "SSH ref result: cmd=%s, exit=%d, duration=%lu ms", 
             ssh_ref->cmd_id, result->exit_code, result->duration_ms);
    
    return ret;
}

/**
 * @brief Execute CLI command action
 */
esp_err_t ts_action_exec_cli(const ts_auto_action_cli_t *cli,
                              ts_action_result_t *result)
{
    if (!cli || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(result, 0, sizeof(*result));
    result->status = TS_ACTION_STATUS_RUNNING;
    
    int64_t start_time = esp_timer_get_time();
    
    if (!cli->command[0]) {
        snprintf(result->output, sizeof(result->output), "Empty CLI command");
        result->status = TS_ACTION_STATUS_FAILED;
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Executing CLI command: %s", cli->command);
    
    /* Execute CLI command using ts_console_exec */
    ts_cmd_result_t cmd_result = {0};
    esp_err_t ret = ts_console_exec(cli->command, &cmd_result);
    
    result->duration_ms = (esp_timer_get_time() - start_time) / 1000;
    result->timestamp = esp_timer_get_time() / 1000;
    result->exit_code = cmd_result.code;
    
    if (ret == ESP_OK) {
        result->status = (cmd_result.code == 0) 
                       ? TS_ACTION_STATUS_SUCCESS 
                       : TS_ACTION_STATUS_FAILED;
        
        /* Copy message to output */
        if (cmd_result.message) {
            strncpy(result->output, cmd_result.message, sizeof(result->output) - 1);
            free(cmd_result.message);
        } else {
            snprintf(result->output, sizeof(result->output), 
                     "CLI command completed (code=%d)", cmd_result.code);
        }
        
        /* Store to variable if configured */
        if (cli->var_name[0]) {
            char var_full[80];  /* var_name (max 64) + suffix (max 11) + null */
            
            /* Set exit_code variable */
            snprintf(var_full, sizeof(var_full), "%.63s.exit_code", cli->var_name);
            ts_variable_set_int(var_full, cmd_result.code);
            
            /* Set status variable */
            snprintf(var_full, sizeof(var_full), "%.63s.status", cli->var_name);
            ts_variable_set_string(var_full, 
                cmd_result.code == 0 ? "success" : "failed");
            
            /* Set output variable */
            snprintf(var_full, sizeof(var_full), "%.63s.output", cli->var_name);
            ts_variable_set_string(var_full, result->output);
        }
    } else {
        snprintf(result->output, sizeof(result->output), 
                 "CLI exec failed: %s", esp_err_to_name(ret));
        result->status = TS_ACTION_STATUS_FAILED;
    }
    
    /* Free any allocated data */
    if (cmd_result.data) {
        free(cmd_result.data);
    }
    
    ESP_LOGD(TAG, "CLI result: cmd=%s, exit=%d, duration=%lu ms", 
             cli->command, result->exit_code, result->duration_ms);
    
    return ret;
}

/*===========================================================================*/
/*                          Internal Execute                                  */
/*===========================================================================*/

static esp_err_t execute_action_internal(const ts_auto_action_t *action, 
                                          ts_action_result_t *result)
{
    switch (action->type) {
        case TS_AUTO_ACT_SSH_CMD:
            return ts_action_exec_ssh(&action->ssh, result);
            
        case TS_AUTO_ACT_SSH_CMD_REF:
            return ts_action_exec_ssh_ref(&action->ssh_ref, result);
            
        case TS_AUTO_ACT_CLI:
            return ts_action_exec_cli(&action->cli, result);
            
        case TS_AUTO_ACT_LED:
            return ts_action_exec_led(&action->led, result);
            
        case TS_AUTO_ACT_GPIO:
            return ts_action_exec_gpio(&action->gpio, result);
            
        case TS_AUTO_ACT_LOG:
            return ts_action_exec_log(&action->log, result);
            
        case TS_AUTO_ACT_SET_VAR:
            return ts_action_exec_set_var(&action->set_var, result);
            
        case TS_AUTO_ACT_DEVICE_CTRL:
            return ts_action_exec_device(&action->device, result);
            
        case TS_AUTO_ACT_WEBHOOK:
            /* TODO: implement webhook */
            result->status = TS_ACTION_STATUS_FAILED;
            snprintf(result->output, sizeof(result->output), "Webhook not implemented");
            return ESP_ERR_NOT_SUPPORTED;
            
        default:
            result->status = TS_ACTION_STATUS_FAILED;
            snprintf(result->output, sizeof(result->output), 
                     "Unknown action type: %d", action->type);
            return ESP_ERR_INVALID_ARG;
    }
}

/*===========================================================================*/
/*                          Executor Task                                     */
/*===========================================================================*/

static void action_executor_task(void *arg)
{
    ESP_LOGI(TAG, "Action executor task started (DRAM stack)");
    
    ts_action_queue_entry_t entry;
    
    while (s_ctx->running) {
        if (xQueueReceive(s_ctx->action_queue, &entry, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!s_ctx->running) break;
            
            ts_action_result_t local_result = {0};
            ts_action_result_t *result = entry.result_ptr ? entry.result_ptr : &local_result;
            
            /* Handle delay */
            if (entry.action.delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(entry.action.delay_ms));
            }
            
            /* Execute action (safe: this task has DRAM stack) */
            execute_action_internal(&entry.action, result);
            
            /* Callback (async mode) */
            if (entry.callback) {
                entry.callback(&entry.action, result, entry.user_data);
            }
            
            /* Signal completion (sync mode) */
            if (entry.done_sem) {
                xSemaphoreGive(entry.done_sem);
            }
            
            /* Update stats */
            xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
            s_ctx->stats.total_executed++;
            if (result->status == TS_ACTION_STATUS_SUCCESS) {
                s_ctx->stats.total_success++;
            } else if (result->status == TS_ACTION_STATUS_TIMEOUT) {
                s_ctx->stats.total_timeout++;
            } else {
                s_ctx->stats.total_failed++;
            }
            xSemaphoreGive(s_ctx->stats_mutex);
        }
    }
    
    ESP_LOGI(TAG, "Action executor task exiting");
    s_ctx->executor_task = NULL;
    vTaskDelete(NULL);
}

/*===========================================================================*/
/*                          Status & Statistics                               */
/*===========================================================================*/

esp_err_t ts_action_get_queue_status(int *pending_out, int *running_out)
{
    if (!s_ctx) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (pending_out) {
        *pending_out = uxQueueMessagesWaiting(s_ctx->action_queue);
    }
    if (running_out) {
        *running_out = 0; /* TODO: track running count */
    }
    
    return ESP_OK;
}

esp_err_t ts_action_get_stats(ts_action_stats_t *stats_out)
{
    if (!s_ctx || !stats_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
    memcpy(stats_out, &s_ctx->stats, sizeof(ts_action_stats_t));
    xSemaphoreGive(s_ctx->stats_mutex);
    
    return ESP_OK;
}

void ts_action_reset_stats(void)
{
    if (!s_ctx) return;
    
    xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
    memset(&s_ctx->stats, 0, sizeof(ts_action_stats_t));
    xSemaphoreGive(s_ctx->stats_mutex);
}

/*===========================================================================*/
/*                          Utility Functions                                 */
/*===========================================================================*/

int ts_action_expand_variables(const char *input, char *output, size_t output_size)
{
    if (!input || !output || output_size == 0) {
        return -1;
    }
    
    const char *p = input;
    char *out = output;
    char *out_end = output + output_size - 1;
    
    while (*p && out < out_end) {
        if (*p == '$' && *(p + 1) == '{') {
            /* Find closing brace */
            const char *end = strchr(p + 2, '}');
            if (end) {
                /* Extract variable name */
                size_t name_len = end - (p + 2);
                char var_name[64];
                if (name_len < sizeof(var_name)) {
                    memcpy(var_name, p + 2, name_len);
                    var_name[name_len] = '\0';
                    
                    /* Get variable value */
                    ts_auto_value_t value;
                    if (ts_variable_get(var_name, &value) == ESP_OK) {
                        /* Format value based on type */
                        char val_str[64];
                        switch (value.type) {
                            case TS_AUTO_VAL_BOOL:
                                snprintf(val_str, sizeof(val_str), "%s", 
                                         value.bool_val ? "true" : "false");
                                break;
                            case TS_AUTO_VAL_INT:
                                snprintf(val_str, sizeof(val_str), "%ld", 
                                         (long)value.int_val);
                                break;
                            case TS_AUTO_VAL_FLOAT:
                                snprintf(val_str, sizeof(val_str), "%.2f", 
                                         value.float_val);
                                break;
                            case TS_AUTO_VAL_STRING:
                                strncpy(val_str, value.str_val, sizeof(val_str) - 1);
                                break;
                            default:
                                val_str[0] = '\0';
                                break;
                        }
                        
                        /* Copy value to output */
                        size_t val_len = strlen(val_str);
                        if (out + val_len < out_end) {
                            memcpy(out, val_str, val_len);
                            out += val_len;
                        }
                    } else {
                        /* Variable not found, keep original */
                        size_t orig_len = (end + 1) - p;
                        if (out + orig_len < out_end) {
                            memcpy(out, p, orig_len);
                            out += orig_len;
                        }
                    }
                }
                p = end + 1;
                continue;
            }
        }
        
        *out++ = *p++;
    }
    
    *out = '\0';
    return out - output;
}

esp_err_t ts_action_parse_color(const char *color_str, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!color_str || !r || !g || !b) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Skip whitespace */
    while (isspace((unsigned char)*color_str)) color_str++;
    
    /* Hex format: #RRGGBB */
    if (color_str[0] == '#') {
        unsigned int rgb;
        if (sscanf(color_str + 1, "%6x", &rgb) == 1) {
            *r = (rgb >> 16) & 0xFF;
            *g = (rgb >> 8) & 0xFF;
            *b = rgb & 0xFF;
            return ESP_OK;
        }
    }
    
    /* RGB format: rgb(r,g,b) */
    if (strncasecmp(color_str, "rgb(", 4) == 0) {
        int rv, gv, bv;
        if (sscanf(color_str + 4, "%d,%d,%d", &rv, &gv, &bv) == 3) {
            *r = (uint8_t)rv;
            *g = (uint8_t)gv;
            *b = (uint8_t)bv;
            return ESP_OK;
        }
    }
    
    /* Named colors */
    static const struct {
        const char *name;
        uint8_t r, g, b;
    } colors[] = {
        {"red", 255, 0, 0},
        {"green", 0, 255, 0},
        {"blue", 0, 0, 255},
        {"white", 255, 255, 255},
        {"black", 0, 0, 0},
        {"yellow", 255, 255, 0},
        {"cyan", 0, 255, 255},
        {"magenta", 255, 0, 255},
        {"orange", 255, 165, 0},
        {"purple", 128, 0, 128},
        {"pink", 255, 192, 203},
        {NULL, 0, 0, 0}
    };
    
    for (int i = 0; colors[i].name; i++) {
        if (strcasecmp(color_str, colors[i].name) == 0) {
            *r = colors[i].r;
            *g = colors[i].g;
            *b = colors[i].b;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_INVALID_ARG;
}

const char *ts_action_type_name(ts_auto_action_type_t type)
{
    switch (type) {
        case TS_AUTO_ACT_LED:         return "LED";
        case TS_AUTO_ACT_SSH_CMD:     return "SSH";
        case TS_AUTO_ACT_SSH_CMD_REF: return "SSH-Ref";
        case TS_AUTO_ACT_CLI:         return "CLI";
        case TS_AUTO_ACT_GPIO:        return "GPIO";
        case TS_AUTO_ACT_WEBHOOK:     return "Webhook";
        case TS_AUTO_ACT_LOG:         return "Log";
        case TS_AUTO_ACT_SET_VAR:     return "SetVar";
        case TS_AUTO_ACT_DEVICE_CTRL: return "Device";
        default:                       return "Unknown";
    }
}

const char *ts_action_status_name(ts_action_status_t status)
{
    switch (status) {
        case TS_ACTION_STATUS_PENDING:   return "Pending";
        case TS_ACTION_STATUS_RUNNING:   return "Running";
        case TS_ACTION_STATUS_SUCCESS:   return "Success";
        case TS_ACTION_STATUS_FAILED:    return "Failed";
        case TS_ACTION_STATUS_TIMEOUT:   return "Timeout";
        case TS_ACTION_STATUS_CANCELLED: return "Cancelled";
        case TS_ACTION_STATUS_QUEUED:    return "Queued";
        default:                         return "Unknown";
    }
}

/*===========================================================================*/
/*                       Action Template Management                           */
/*===========================================================================*/

esp_err_t ts_action_template_add(const ts_action_template_t *tpl)
{
    if (!s_ctx || !tpl || !tpl->id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    
    /* Check if ID already exists */
    for (int i = 0; i < s_ctx->template_count; i++) {
        if (strcmp(s_ctx->templates[i].id, tpl->id) == 0) {
            xSemaphoreGive(s_ctx->templates_mutex);
            return ESP_ERR_INVALID_STATE; /* Already exists */
        }
    }
    
    /* Check capacity */
    if (s_ctx->template_count >= TS_ACTION_TEMPLATE_MAX) {
        xSemaphoreGive(s_ctx->templates_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    /* Add template */
    memcpy(&s_ctx->templates[s_ctx->template_count], tpl, sizeof(ts_action_template_t));
    s_ctx->templates[s_ctx->template_count].created_at = esp_timer_get_time() / 1000;
    s_ctx->templates[s_ctx->template_count].use_count = 0;
    s_ctx->template_count++;
    
    xSemaphoreGive(s_ctx->templates_mutex);
    
    /* Save to NVS */
    ts_action_templates_save();
    
    ESP_LOGI(TAG, "Added action template: %s (%s)", tpl->id, tpl->name);
    return ESP_OK;
}

esp_err_t ts_action_template_remove(const char *id)
{
    if (!s_ctx || !id || !id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    
    for (int i = 0; i < s_ctx->template_count; i++) {
        if (strcmp(s_ctx->templates[i].id, id) == 0) {
            /* Shift remaining templates */
            if (i < s_ctx->template_count - 1) {
                memmove(&s_ctx->templates[i], 
                        &s_ctx->templates[i + 1],
                        (s_ctx->template_count - i - 1) * sizeof(ts_action_template_t));
            }
            s_ctx->template_count--;
            xSemaphoreGive(s_ctx->templates_mutex);
            
            /* Save to NVS */
            ts_action_templates_save();
            
            ESP_LOGI(TAG, "Removed action template: %s", id);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(s_ctx->templates_mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ts_action_template_get(const char *id, ts_action_template_t *tpl_out)
{
    if (!s_ctx || !id || !tpl_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    
    for (int i = 0; i < s_ctx->template_count; i++) {
        if (strcmp(s_ctx->templates[i].id, id) == 0) {
            memcpy(tpl_out, &s_ctx->templates[i], sizeof(ts_action_template_t));
            xSemaphoreGive(s_ctx->templates_mutex);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(s_ctx->templates_mutex);
    return ESP_ERR_NOT_FOUND;
}

int ts_action_template_count(void)
{
    if (!s_ctx) return 0;
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    int count = s_ctx->template_count;
    xSemaphoreGive(s_ctx->templates_mutex);
    
    return count;
}

esp_err_t ts_action_template_list(ts_action_template_t *tpls_out,
                                   size_t max_count,
                                   size_t *count_out)
{
    if (!s_ctx || !tpls_out || !count_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    
    size_t copy_count = (max_count < (size_t)s_ctx->template_count) ? 
                        max_count : (size_t)s_ctx->template_count;
    
    if (copy_count > 0) {
        memcpy(tpls_out, s_ctx->templates, copy_count * sizeof(ts_action_template_t));
    }
    *count_out = copy_count;
    
    xSemaphoreGive(s_ctx->templates_mutex);
    return ESP_OK;
}

esp_err_t ts_action_template_execute(const char *id, ts_action_result_t *result)
{
    if (!s_ctx || !id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_action_template_t tpl;
    esp_err_t ret = ts_action_template_get(id, &tpl);
    if (ret != ESP_OK) {
        if (result) {
            result->status = TS_ACTION_STATUS_FAILED;
            snprintf(result->output, sizeof(result->output), "Template not found: %s", id);
        }
        return ret;
    }
    
    if (!tpl.enabled) {
        if (result) {
            result->status = TS_ACTION_STATUS_FAILED;
            snprintf(result->output, sizeof(result->output), "Template disabled: %s", id);
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Check async mode from template or action */
    bool is_async = tpl.async || tpl.action.async;
    
    if (is_async) {
        /* Async execution: queue and return immediately */
        ret = ts_action_queue(&tpl.action, NULL, NULL, 0);
        if (result) {
            if (ret == ESP_OK) {
                result->status = TS_ACTION_STATUS_QUEUED;
                snprintf(result->output, sizeof(result->output), "Action queued for async execution");
            } else {
                result->status = TS_ACTION_STATUS_FAILED;
                snprintf(result->output, sizeof(result->output), "Failed to queue action: %s", esp_err_to_name(ret));
            }
        }
    } else {
        /* Sync execution: wait for completion */
        ret = ts_action_manager_execute(&tpl.action, result);
    }
    
    /* Update usage stats */
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    for (int i = 0; i < s_ctx->template_count; i++) {
        if (strcmp(s_ctx->templates[i].id, id) == 0) {
            s_ctx->templates[i].last_used_at = esp_timer_get_time() / 1000;
            s_ctx->templates[i].use_count++;
            break;
        }
    }
    xSemaphoreGive(s_ctx->templates_mutex);
    
    return ret;
}

esp_err_t ts_action_template_update(const char *id, const ts_action_template_t *tpl)
{
    if (!s_ctx || !id || !tpl) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    
    for (int i = 0; i < s_ctx->template_count; i++) {
        if (strcmp(s_ctx->templates[i].id, id) == 0) {
            /* Preserve some fields */
            int64_t created_at = s_ctx->templates[i].created_at;
            uint32_t use_count = s_ctx->templates[i].use_count;
            int64_t last_used = s_ctx->templates[i].last_used_at;
            
            /* Update template */
            memcpy(&s_ctx->templates[i], tpl, sizeof(ts_action_template_t));
            
            /* Restore preserved fields */
            s_ctx->templates[i].created_at = created_at;
            s_ctx->templates[i].use_count = use_count;
            s_ctx->templates[i].last_used_at = last_used;
            
            xSemaphoreGive(s_ctx->templates_mutex);
            
            /* Save to NVS */
            ts_action_templates_save();
            
            ESP_LOGI(TAG, "Updated action template: %s", id);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(s_ctx->templates_mutex);
    return ESP_ERR_NOT_FOUND;
}

/*===========================================================================*/
/*                       Action Template Persistence                          */
/*===========================================================================*/

/**
 * @brief Convert action type enum to string for storage
 */
static const char *action_type_to_str(ts_auto_action_type_t type)
{
    switch (type) {
        case TS_AUTO_ACT_CLI: return "cli";
        case TS_AUTO_ACT_LED: return "led";
        case TS_AUTO_ACT_SSH_CMD: return "ssh_cmd";
        case TS_AUTO_ACT_SSH_CMD_REF: return "ssh_cmd_ref";
        case TS_AUTO_ACT_GPIO: return "gpio";
        case TS_AUTO_ACT_WEBHOOK: return "webhook";
        case TS_AUTO_ACT_LOG: return "log";
        case TS_AUTO_ACT_SET_VAR: return "set_var";
        case TS_AUTO_ACT_DEVICE_CTRL: return "device_ctrl";
        default: return "unknown";
    }
}

/**
 * @brief Convert string to action type enum
 */
static ts_auto_action_type_t str_to_action_type(const char *str)
{
    if (!str) return TS_AUTO_ACT_LOG;
    if (strcmp(str, "cli") == 0) return TS_AUTO_ACT_CLI;
    if (strcmp(str, "led") == 0) return TS_AUTO_ACT_LED;
    if (strcmp(str, "ssh_cmd") == 0) return TS_AUTO_ACT_SSH_CMD;
    if (strcmp(str, "ssh_cmd_ref") == 0) return TS_AUTO_ACT_SSH_CMD_REF;
    if (strcmp(str, "gpio") == 0) return TS_AUTO_ACT_GPIO;
    if (strcmp(str, "webhook") == 0) return TS_AUTO_ACT_WEBHOOK;
    if (strcmp(str, "log") == 0) return TS_AUTO_ACT_LOG;
    if (strcmp(str, "set_var") == 0) return TS_AUTO_ACT_SET_VAR;
    if (strcmp(str, "device_ctrl") == 0) return TS_AUTO_ACT_DEVICE_CTRL;
    return TS_AUTO_ACT_LOG;
}

/**
 * @brief Serialize action template to JSON string
 */
static char *template_to_json(const ts_action_template_t *tpl)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    
    cJSON_AddStringToObject(root, "id", tpl->id);
    cJSON_AddStringToObject(root, "name", tpl->name);
    cJSON_AddStringToObject(root, "description", tpl->description);
    cJSON_AddBoolToObject(root, "enabled", tpl->enabled);
    cJSON_AddStringToObject(root, "type", action_type_to_str(tpl->action.type));
    cJSON_AddNumberToObject(root, "delay_ms", tpl->action.delay_ms);
    cJSON_AddNumberToObject(root, "created_at", tpl->created_at);
    cJSON_AddNumberToObject(root, "use_count", tpl->use_count);
    
    /* Serialize type-specific data */
    switch (tpl->action.type) {
        case TS_AUTO_ACT_CLI: {
            cJSON *cli = cJSON_AddObjectToObject(root, "cli");
            cJSON_AddStringToObject(cli, "command", tpl->action.cli.command);
            cJSON_AddStringToObject(cli, "var_name", tpl->action.cli.var_name);
            cJSON_AddNumberToObject(cli, "timeout_ms", tpl->action.cli.timeout_ms);
            break;
        }
        case TS_AUTO_ACT_SSH_CMD_REF: {
            cJSON *ssh_ref = cJSON_AddObjectToObject(root, "ssh_ref");
            cJSON_AddStringToObject(ssh_ref, "cmd_id", tpl->action.ssh_ref.cmd_id);
            break;
        }
        case TS_AUTO_ACT_LED: {
            cJSON *led = cJSON_AddObjectToObject(root, "led");
            cJSON_AddStringToObject(led, "device", tpl->action.led.device);
            /* 控制类型 */
            const char *ctrl_type_str = "fill";
            switch (tpl->action.led.ctrl_type) {
                case TS_LED_CTRL_EFFECT: ctrl_type_str = "effect"; break;
                case TS_LED_CTRL_BRIGHTNESS: ctrl_type_str = "brightness"; break;
                case TS_LED_CTRL_OFF: ctrl_type_str = "off"; break;
                case TS_LED_CTRL_TEXT: ctrl_type_str = "text"; break;
                case TS_LED_CTRL_IMAGE: ctrl_type_str = "image"; break;
                case TS_LED_CTRL_QRCODE: ctrl_type_str = "qrcode"; break;
                case TS_LED_CTRL_FILTER: ctrl_type_str = "filter"; break;
                case TS_LED_CTRL_FILTER_STOP: ctrl_type_str = "filter_stop"; break;
                case TS_LED_CTRL_TEXT_STOP: ctrl_type_str = "text_stop"; break;
                default: ctrl_type_str = "fill"; break;
            }
            cJSON_AddStringToObject(led, "ctrl_type", ctrl_type_str);
            cJSON_AddNumberToObject(led, "index", tpl->action.led.index);
            /* 颜色输出为 hex */
            char color_hex[8];
            snprintf(color_hex, sizeof(color_hex), "#%02X%02X%02X", 
                     tpl->action.led.r, tpl->action.led.g, tpl->action.led.b);
            cJSON_AddStringToObject(led, "color", color_hex);
            cJSON_AddNumberToObject(led, "brightness", tpl->action.led.brightness);
            if (tpl->action.led.effect[0]) {
                cJSON_AddStringToObject(led, "effect", tpl->action.led.effect);
            }
            cJSON_AddNumberToObject(led, "speed", tpl->action.led.speed);
            cJSON_AddNumberToObject(led, "duration_ms", tpl->action.led.duration_ms);
            /* Matrix 高级功能 */
            if (tpl->action.led.text[0]) {
                cJSON_AddStringToObject(led, "text", tpl->action.led.text);
            }
            if (tpl->action.led.font[0]) {
                cJSON_AddStringToObject(led, "font", tpl->action.led.font);
            }
            if (tpl->action.led.image_path[0]) {
                cJSON_AddStringToObject(led, "image_path", tpl->action.led.image_path);
            }
            if (tpl->action.led.qr_text[0]) {
                cJSON_AddStringToObject(led, "qr_text", tpl->action.led.qr_text);
            }
            if (tpl->action.led.qr_ecc) {
                char ecc_str[2] = { tpl->action.led.qr_ecc, '\0' };
                cJSON_AddStringToObject(led, "qr_ecc", ecc_str);
            }
            if (tpl->action.led.filter[0]) {
                cJSON_AddStringToObject(led, "filter", tpl->action.led.filter);
            }
            cJSON_AddBoolToObject(led, "center", tpl->action.led.center);
            cJSON_AddBoolToObject(led, "loop", tpl->action.led.loop);
            if (tpl->action.led.scroll[0]) {
                cJSON_AddStringToObject(led, "scroll", tpl->action.led.scroll);
            }
            if (tpl->action.led.align[0]) {
                cJSON_AddStringToObject(led, "align", tpl->action.led.align);
            }
            cJSON_AddNumberToObject(led, "x", tpl->action.led.x);
            cJSON_AddNumberToObject(led, "y", tpl->action.led.y);
            break;
        }
        case TS_AUTO_ACT_LOG: {
            cJSON *log_obj = cJSON_AddObjectToObject(root, "log");
            cJSON_AddNumberToObject(log_obj, "level", tpl->action.log.level);
            cJSON_AddStringToObject(log_obj, "message", tpl->action.log.message);
            break;
        }
        case TS_AUTO_ACT_SET_VAR: {
            cJSON *set_var = cJSON_AddObjectToObject(root, "set_var");
            cJSON_AddStringToObject(set_var, "variable", tpl->action.set_var.variable);
            /* Store value as string for simplicity */
            if (tpl->action.set_var.value.type == TS_AUTO_VAL_STRING) {
                cJSON_AddStringToObject(set_var, "value", tpl->action.set_var.value.str_val);
            }
            break;
        }
        case TS_AUTO_ACT_WEBHOOK: {
            cJSON *webhook = cJSON_AddObjectToObject(root, "webhook");
            cJSON_AddStringToObject(webhook, "url", tpl->action.webhook.url);
            cJSON_AddStringToObject(webhook, "method", tpl->action.webhook.method);
            cJSON_AddStringToObject(webhook, "body_template", tpl->action.webhook.body_template);
            break;
        }
        default:
            break;
    }
    
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/**
 * @brief Parse JSON string to action template
 */
static esp_err_t json_to_template(const char *json, ts_action_template_t *tpl)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return ESP_ERR_INVALID_ARG;
    
    memset(tpl, 0, sizeof(ts_action_template_t));
    
    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "id")) && cJSON_IsString(item)) {
        strncpy(tpl->id, item->valuestring, sizeof(tpl->id) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "name")) && cJSON_IsString(item)) {
        strncpy(tpl->name, item->valuestring, sizeof(tpl->name) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "description")) && cJSON_IsString(item)) {
        strncpy(tpl->description, item->valuestring, sizeof(tpl->description) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "enabled"))) {
        tpl->enabled = cJSON_IsTrue(item);
    } else {
        tpl->enabled = true;
    }
    if ((item = cJSON_GetObjectItem(root, "type")) && cJSON_IsString(item)) {
        tpl->action.type = str_to_action_type(item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(root, "delay_ms")) && cJSON_IsNumber(item)) {
        tpl->action.delay_ms = (uint16_t)item->valueint;
    }
    if ((item = cJSON_GetObjectItem(root, "created_at")) && cJSON_IsNumber(item)) {
        tpl->created_at = (int64_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(root, "use_count")) && cJSON_IsNumber(item)) {
        tpl->use_count = (uint32_t)item->valueint;
    }
    
    /* Parse type-specific data */
    switch (tpl->action.type) {
        case TS_AUTO_ACT_CLI: {
            cJSON *cli = cJSON_GetObjectItem(root, "cli");
            if (cli) {
                if ((item = cJSON_GetObjectItem(cli, "command")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.cli.command, item->valuestring, 
                            sizeof(tpl->action.cli.command) - 1);
                }
                if ((item = cJSON_GetObjectItem(cli, "var_name")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.cli.var_name, item->valuestring, 
                            sizeof(tpl->action.cli.var_name) - 1);
                }
                if ((item = cJSON_GetObjectItem(cli, "timeout_ms")) && cJSON_IsNumber(item)) {
                    tpl->action.cli.timeout_ms = (uint32_t)item->valueint;
                }
            }
            break;
        }
        case TS_AUTO_ACT_SSH_CMD_REF: {
            cJSON *ssh_ref = cJSON_GetObjectItem(root, "ssh_ref");
            if (ssh_ref) {
                if ((item = cJSON_GetObjectItem(ssh_ref, "cmd_id")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.ssh_ref.cmd_id, item->valuestring, 
                            sizeof(tpl->action.ssh_ref.cmd_id) - 1);
                }
            }
            break;
        }
        case TS_AUTO_ACT_LED: {
            cJSON *led = cJSON_GetObjectItem(root, "led");
            if (led) {
                if ((item = cJSON_GetObjectItem(led, "device")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.device, item->valuestring, 
                            sizeof(tpl->action.led.device) - 1);
                }
                /* 解析控制类型 */
                if ((item = cJSON_GetObjectItem(led, "ctrl_type")) && cJSON_IsString(item)) {
                    const char *ct = item->valuestring;
                    if (strcmp(ct, "fill") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_FILL;
                    else if (strcmp(ct, "effect") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_EFFECT;
                    else if (strcmp(ct, "brightness") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_BRIGHTNESS;
                    else if (strcmp(ct, "off") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_OFF;
                    else if (strcmp(ct, "text") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_TEXT;
                    else if (strcmp(ct, "image") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_IMAGE;
                    else if (strcmp(ct, "qrcode") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_QRCODE;
                    else if (strcmp(ct, "filter") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_FILTER;
                    else if (strcmp(ct, "filter_stop") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_FILTER_STOP;
                    else if (strcmp(ct, "text_stop") == 0) tpl->action.led.ctrl_type = TS_LED_CTRL_TEXT_STOP;
                }
                if ((item = cJSON_GetObjectItem(led, "index")) && cJSON_IsNumber(item)) {
                    tpl->action.led.index = (uint8_t)item->valueint;
                } else {
                    tpl->action.led.index = 0xFF;  /* Default: fill all */
                }
                /* 解析颜色（支持 color 字符串或 r/g/b 数值） */
                if ((item = cJSON_GetObjectItem(led, "color")) && cJSON_IsString(item)) {
                    /* Parse hex color like "#FF0000" */
                    const char *hex = item->valuestring;
                    if (hex[0] == '#') hex++;
                    uint32_t color_val = strtoul(hex, NULL, 16);
                    tpl->action.led.r = (color_val >> 16) & 0xFF;
                    tpl->action.led.g = (color_val >> 8) & 0xFF;
                    tpl->action.led.b = color_val & 0xFF;
                } else {
                    if ((item = cJSON_GetObjectItem(led, "r")) && cJSON_IsNumber(item)) {
                        tpl->action.led.r = (uint8_t)item->valueint;
                    }
                    if ((item = cJSON_GetObjectItem(led, "g")) && cJSON_IsNumber(item)) {
                        tpl->action.led.g = (uint8_t)item->valueint;
                    }
                    if ((item = cJSON_GetObjectItem(led, "b")) && cJSON_IsNumber(item)) {
                        tpl->action.led.b = (uint8_t)item->valueint;
                    }
                }
                if ((item = cJSON_GetObjectItem(led, "brightness")) && cJSON_IsNumber(item)) {
                    tpl->action.led.brightness = (uint8_t)item->valueint;
                }
                if ((item = cJSON_GetObjectItem(led, "effect")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.effect, item->valuestring, 
                            sizeof(tpl->action.led.effect) - 1);
                }
                if ((item = cJSON_GetObjectItem(led, "speed")) && cJSON_IsNumber(item)) {
                    tpl->action.led.speed = (uint8_t)item->valueint;
                }
                if ((item = cJSON_GetObjectItem(led, "duration_ms")) && cJSON_IsNumber(item)) {
                    tpl->action.led.duration_ms = (uint16_t)item->valueint;
                }
                /* Matrix 高级功能参数 */
                if ((item = cJSON_GetObjectItem(led, "text")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.text, item->valuestring, 
                            sizeof(tpl->action.led.text) - 1);
                }
                if ((item = cJSON_GetObjectItem(led, "font")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.font, item->valuestring, 
                            sizeof(tpl->action.led.font) - 1);
                }
                if ((item = cJSON_GetObjectItem(led, "image_path")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.image_path, item->valuestring, 
                            sizeof(tpl->action.led.image_path) - 1);
                }
                if ((item = cJSON_GetObjectItem(led, "qr_text")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.qr_text, item->valuestring, 
                            sizeof(tpl->action.led.qr_text) - 1);
                }
                if ((item = cJSON_GetObjectItem(led, "qr_ecc")) && cJSON_IsString(item)) {
                    tpl->action.led.qr_ecc = item->valuestring[0];  /* L/M/Q/H */
                }
                if ((item = cJSON_GetObjectItem(led, "filter")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.filter, item->valuestring, 
                            sizeof(tpl->action.led.filter) - 1);
                }
                if ((item = cJSON_GetObjectItem(led, "center")) && cJSON_IsBool(item)) {
                    tpl->action.led.center = cJSON_IsTrue(item);
                }
                if ((item = cJSON_GetObjectItem(led, "loop")) && cJSON_IsBool(item)) {
                    tpl->action.led.loop = cJSON_IsTrue(item);
                }
                if ((item = cJSON_GetObjectItem(led, "scroll")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.scroll, item->valuestring, 
                            sizeof(tpl->action.led.scroll) - 1);
                }
                if ((item = cJSON_GetObjectItem(led, "align")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.led.align, item->valuestring, 
                            sizeof(tpl->action.led.align) - 1);
                }
                if ((item = cJSON_GetObjectItem(led, "x")) && cJSON_IsNumber(item)) {
                    tpl->action.led.x = (int16_t)item->valueint;
                }
                if ((item = cJSON_GetObjectItem(led, "y")) && cJSON_IsNumber(item)) {
                    tpl->action.led.y = (int16_t)item->valueint;
                }
            }
            break;
        }
        case TS_AUTO_ACT_LOG: {
            cJSON *log_obj = cJSON_GetObjectItem(root, "log");
            if (log_obj) {
                if ((item = cJSON_GetObjectItem(log_obj, "level")) && cJSON_IsNumber(item)) {
                    tpl->action.log.level = (uint8_t)item->valueint;
                }
                if ((item = cJSON_GetObjectItem(log_obj, "message")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.log.message, item->valuestring, 
                            sizeof(tpl->action.log.message) - 1);
                }
            }
            break;
        }
        case TS_AUTO_ACT_SET_VAR: {
            cJSON *set_var = cJSON_GetObjectItem(root, "set_var");
            if (set_var) {
                if ((item = cJSON_GetObjectItem(set_var, "variable")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.set_var.variable, item->valuestring, 
                            sizeof(tpl->action.set_var.variable) - 1);
                }
                if ((item = cJSON_GetObjectItem(set_var, "value")) && cJSON_IsString(item)) {
                    tpl->action.set_var.value.type = TS_AUTO_VAL_STRING;
                    strncpy(tpl->action.set_var.value.str_val, item->valuestring, 
                            sizeof(tpl->action.set_var.value.str_val) - 1);
                }
            }
            break;
        }
        case TS_AUTO_ACT_WEBHOOK: {
            cJSON *webhook = cJSON_GetObjectItem(root, "webhook");
            if (webhook) {
                if ((item = cJSON_GetObjectItem(webhook, "url")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.webhook.url, item->valuestring, 
                            sizeof(tpl->action.webhook.url) - 1);
                }
                if ((item = cJSON_GetObjectItem(webhook, "method")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.webhook.method, item->valuestring, 
                            sizeof(tpl->action.webhook.method) - 1);
                }
                if ((item = cJSON_GetObjectItem(webhook, "body_template")) && cJSON_IsString(item)) {
                    strncpy(tpl->action.webhook.body_template, item->valuestring, 
                            sizeof(tpl->action.webhook.body_template) - 1);
                }
            }
            break;
        }
        default:
            break;
    }
    
    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * @brief Save all action templates to NVS
 */
esp_err_t ts_action_templates_save(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;
    
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Erase old data first */
    nvs_erase_all(handle);
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    
    /* Save count */
    ret = nvs_set_u8(handle, NVS_KEY_COUNT, (uint8_t)s_ctx->template_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save template count: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_ctx->templates_mutex);
        nvs_close(handle);
        return ret;
    }
    
    /* 为 SD 卡导出准备 JSON 数组 */
    cJSON *export_json = cJSON_CreateObject();
    cJSON *templates_array = cJSON_CreateArray();
    
    /* Save each template */
    for (int i = 0; i < s_ctx->template_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);
        
        char *json = template_to_json(&s_ctx->templates[i]);
        if (!json) {
            ESP_LOGW(TAG, "Failed to serialize template %d", i);
            continue;
        }
        
        /* 保存到 NVS */
        ret = nvs_set_str(handle, key, json);
        
        /* 同时添加到 SD 卡 JSON 数组 */
        cJSON *tpl_obj = cJSON_Parse(json);
        if (tpl_obj) {
            cJSON_AddItemToArray(templates_array, tpl_obj);
        }
        
        free(json);
        
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save template %d: %s", i, esp_err_to_name(ret));
        }
    }
    
    xSemaphoreGive(s_ctx->templates_mutex);
    
    ret = nvs_commit(handle);
    nvs_close(handle);
    
    /* 构建完整 JSON 并导出到 SD 卡 */
    cJSON_AddItemToObject(export_json, "templates", templates_array);
    cJSON_AddNumberToObject(export_json, "count", s_ctx->template_count);
    
    /* 使用新的 API 导出到 SD 卡 */
    esp_err_t sd_ret = ts_config_module_export_custom_json(TS_CONFIG_MODULE_ACTIONS, export_json);
    cJSON_Delete(export_json);
    
    if (sd_ret != ESP_OK && sd_ret != TS_CONFIG_ERR_SD_NOT_MOUNTED) {
        ESP_LOGW(TAG, "Failed to export action templates to SD card: %s", esp_err_to_name(sd_ret));
    }
    
    ESP_LOGI(TAG, "Saved %d action templates to NVS", s_ctx->template_count);
    return ret;
}

/**
 * @brief Load all action templates
 * 
 * Priority: SD card file > NVS > empty (遵循系统配置优先级原则)
 */
esp_err_t ts_action_templates_load(void)
{
    if (!s_ctx) return ESP_ERR_INVALID_STATE;
    
    esp_err_t ret;
    
    /* 1. 优先从 SD 卡加载 */
    if (ts_storage_sd_mounted()) {
        ret = ts_action_templates_load_from_file("/sdcard/config/actions.json");
        if (ret == ESP_OK && s_ctx->template_count > 0) {
            ESP_LOGI(TAG, "Loaded %d action templates from SD card", s_ctx->template_count);
            return ESP_OK;  /* SD 卡配置已自动保存到 NVS */
        }
    }
    
    /* 2. SD 卡无配置，从 NVS 加载 */
    uint8_t count = 0;
    nvs_handle_t handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No saved action templates found");
        return ESP_OK;
    }
    
    ret = nvs_get_u8(handle, NVS_KEY_COUNT, &count);
    if (ret != ESP_OK || count == 0) {
        nvs_close(handle);
        ESP_LOGI(TAG, "No saved action templates found");
        return ESP_OK;
    }
    
    /* Load from NVS */
    ESP_LOGI(TAG, "Loading %d action templates from NVS", count);
    
    /* 使用堆分配避免栈溢出 - ts_action_template_t 结构体很大（>1KB）*/
    ts_action_template_t *tpl = heap_caps_malloc(sizeof(ts_action_template_t), MALLOC_CAP_SPIRAM);
    if (!tpl) {
        tpl = malloc(sizeof(ts_action_template_t));
    }
    if (!tpl) {
        nvs_close(handle);
        ESP_LOGE(TAG, "Failed to allocate template buffer");
        return ESP_ERR_NO_MEM;
    }
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    
    for (int i = 0; i < count && s_ctx->template_count < TS_ACTION_TEMPLATE_MAX; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);
        
        size_t len = 0;
        ret = nvs_get_str(handle, key, NULL, &len);
        if (ret != ESP_OK || len == 0) continue;
        
        char *json = malloc(len);
        if (!json) {
            ESP_LOGW(TAG, "Failed to allocate memory for template %d", i);
            continue;
        }
        
        ret = nvs_get_str(handle, key, json, &len);
        if (ret == ESP_OK) {
            if (json_to_template(json, tpl) == ESP_OK) {
                memcpy(&s_ctx->templates[s_ctx->template_count], tpl, sizeof(ts_action_template_t));
                s_ctx->template_count++;
                ESP_LOGD(TAG, "Loaded template: %s", tpl->id);
            }
        }
        free(json);
    }
    
    xSemaphoreGive(s_ctx->templates_mutex);
    free(tpl);
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Loaded %d action templates from NVS", s_ctx->template_count);
    return ESP_OK;
}

/**
 * @brief Load action templates from SD card JSON file
 */
esp_err_t ts_action_templates_load_from_file(const char *filepath)
{
    if (!s_ctx || !filepath) return ESP_ERR_INVALID_ARG;
    
    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGD(TAG, "Cannot open file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0 || size > 32768) {  /* Max 32KB */
        fclose(f);
        ESP_LOGW(TAG, "File too large or empty: %s (%ld bytes)", filepath, size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    char *content = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
    if (!content) {
        content = malloc(size + 1);
    }
    if (!content) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read = fread(content, 1, size, f);
    fclose(f);
    content[read] = '\0';
    
    /* Parse JSON */
    cJSON *root = cJSON_Parse(content);
    free(content);
    
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse JSON: %s", filepath);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Get templates array */
    cJSON *templates = cJSON_GetObjectItem(root, "templates");
    if (!templates || !cJSON_IsArray(templates)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "No 'templates' array in file");
        return ESP_ERR_INVALID_ARG;
    }
    
    int loaded = 0;
    
    /* 使用堆分配避免栈溢出 - ts_action_template_t 结构体很大（>1KB）*/
    ts_action_template_t *tpl = heap_caps_malloc(sizeof(ts_action_template_t), MALLOC_CAP_SPIRAM);
    if (!tpl) {
        tpl = malloc(sizeof(ts_action_template_t));
    }
    if (!tpl) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to allocate template buffer");
        return ESP_ERR_NO_MEM;
    }
    
    xSemaphoreTake(s_ctx->templates_mutex, portMAX_DELAY);
    
    cJSON *item;
    cJSON_ArrayForEach(item, templates) {
        if (s_ctx->template_count >= TS_ACTION_TEMPLATE_MAX) break;
        
        char *json_str = cJSON_PrintUnformatted(item);
        if (!json_str) continue;
        
        if (json_to_template(json_str, tpl) == ESP_OK) {
            memcpy(&s_ctx->templates[s_ctx->template_count], tpl, sizeof(ts_action_template_t));
            s_ctx->template_count++;
            loaded++;
        }
        cJSON_free(json_str);
    }
    
    xSemaphoreGive(s_ctx->templates_mutex);
    free(tpl);
    cJSON_Delete(root);
    
    if (loaded > 0) {
        ESP_LOGI(TAG, "Loaded %d action templates from SD card: %s", loaded, filepath);
        /* Save to NVS for next boot */
        ts_action_templates_save();
    }
    
    return ESP_OK;
}
