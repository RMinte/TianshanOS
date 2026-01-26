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
#include "ts_led.h"
#include "ts_led_preset.h"
#include "ts_hal.h"
#include "ts_core.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

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

/** Action executor task stack size */
#define ACTION_TASK_STACK_SIZE      8192

/** Action executor task priority */
#define ACTION_TASK_PRIORITY        5

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
    
    /* Start executor task */
    s_ctx->running = true;
    BaseType_t ret = xTaskCreate(action_executor_task,
                                  "action_exec",
                                  ACTION_TASK_STACK_SIZE,
                                  NULL,
                                  ACTION_TASK_PRIORITY,
                                  &s_ctx->executor_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create executor task");
        goto cleanup;
    }
    
    s_ctx->initialized = true;
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
    
    xSemaphoreTake(s_ctx->ssh_hosts_mutex, portMAX_DELAY);
    
    for (int i = 0; i < s_ctx->ssh_host_count; i++) {
        if (strcmp(s_ctx->ssh_hosts[i].id, host_id) == 0) {
            memcpy(host_out, &s_ctx->ssh_hosts[i], sizeof(ts_action_ssh_host_t));
            xSemaphoreGive(s_ctx->ssh_hosts_mutex);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(s_ctx->ssh_hosts_mutex);
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

esp_err_t ts_action_manager_execute(const ts_auto_action_t *action, 
                                     ts_action_result_t *result)
{
    if (!s_ctx || !action) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_action_result_t local_result = {0};
    ts_action_result_t *res = result ? result : &local_result;
    
    /* Handle delay before execution */
    if (action->delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(action->delay_ms));
    }
    
    esp_err_t ret = execute_action_internal(action, res);
    
    /* Update statistics */
    xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
    s_ctx->stats.total_executed++;
    if (res->status == TS_ACTION_STATUS_SUCCESS) {
        s_ctx->stats.total_success++;
    } else if (res->status == TS_ACTION_STATUS_TIMEOUT) {
        s_ctx->stats.total_timeout++;
    } else {
        s_ctx->stats.total_failed++;
    }
    xSemaphoreGive(s_ctx->stats_mutex);
    
    return ret;
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
        .enqueue_time = esp_timer_get_time() / 1000
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
    
    if (host.use_key_auth && host.key_path[0]) {
        config.auth_method = TS_SSH_AUTH_PUBLICKEY;
        config.auth.key.private_key_path = host.key_path;
    } else {
        config.auth_method = TS_SSH_AUTH_PASSWORD;
        config.auth.password = host.password;
    }
    
    ts_ssh_session_t session = NULL;
    esp_err_t ret = ts_ssh_session_create(&config, &session);
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

esp_err_t ts_action_exec_led(const ts_auto_action_led_t *led,
                              ts_action_result_t *result)
{
    if (!led || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    result->status = TS_ACTION_STATUS_RUNNING;
    int64_t start_time = esp_timer_get_time();
    esp_err_t ret = ESP_OK;
    
    ESP_LOGD(TAG, "LED action: device=%s, color=(%d,%d,%d), effect=%s",
             led->device, led->r, led->g, led->b, led->effect);
    
    ts_led_rgb_t color = TS_LED_RGB(led->r, led->g, led->b);
    
    /* Determine device and execute */
    if (strcmp(led->device, "board") == 0) {
        /* Board LED - use preset API or direct control */
        ts_led_device_t device = ts_led_board_get();
        if (device) {
            if (led->effect[0]) {
                /* Effect-based control - get layer and apply effect */
                ts_led_layer_t layer = ts_led_layer_get(device, 0);
                if (layer) {
                    if (strcmp(led->effect, "blink") == 0) {
                        /* TODO: implement blink through layer effect */
                    } else if (strcmp(led->effect, "pulse") == 0) {
                        /* TODO: implement pulse */
                    }
                }
            } else {
                /* Direct color set */
                ret = ts_led_device_fill(device, color);
            }
        } else {
            ret = ESP_ERR_NOT_FOUND;
        }
    } else if (strcmp(led->device, "touch") == 0) {
        /* Touch screen LED */
        ts_led_device_t device = ts_led_touch_get();
        if (device) {
            if (led->index == 0xFF) {
                ret = ts_led_device_fill(device, color);
            } else {
                ret = ts_led_device_set_pixel(device, led->index, color);
            }
        } else {
            ret = ESP_ERR_NOT_FOUND;
        }
    } else if (strcmp(led->device, "matrix") == 0) {
        /* Matrix display */
        ts_led_device_t device = ts_led_matrix_get();
        if (device) {
            if (led->effect[0]) {
                /* Start named effect/animation */
                /* TODO: lookup and start animation by name */
                ESP_LOGW(TAG, "Matrix effect '%s' not yet implemented", led->effect);
            } else {
                ret = ts_led_device_fill(device, color);
            }
        } else {
            ret = ESP_ERR_NOT_FOUND;
        }
    } else {
        ESP_LOGW(TAG, "Unknown LED device: %s", led->device);
        ret = ESP_ERR_NOT_FOUND;
    }
    
    result->duration_ms = (esp_timer_get_time() - start_time) / 1000;
    result->timestamp = esp_timer_get_time() / 1000;
    
    if (ret == ESP_OK) {
        result->status = TS_ACTION_STATUS_SUCCESS;
        snprintf(result->output, sizeof(result->output), "LED %s updated", led->device);
    } else {
        result->status = TS_ACTION_STATUS_FAILED;
        snprintf(result->output, sizeof(result->output), 
                 "LED failed: %s", esp_err_to_name(ret));
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

/*===========================================================================*/
/*                          Internal Execute                                  */
/*===========================================================================*/

static esp_err_t execute_action_internal(const ts_auto_action_t *action, 
                                          ts_action_result_t *result)
{
    switch (action->type) {
        case TS_AUTO_ACT_SSH_CMD:
            return ts_action_exec_ssh(&action->ssh, result);
            
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
    ESP_LOGI(TAG, "Action executor task started");
    
    ts_action_queue_entry_t entry;
    
    while (s_ctx->running) {
        if (xQueueReceive(s_ctx->action_queue, &entry, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (!s_ctx->running) break;
            
            ts_action_result_t result = {0};
            
            /* Handle delay */
            if (entry.action.delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(entry.action.delay_ms));
            }
            
            /* Execute */
            execute_action_internal(&entry.action, &result);
            
            /* Callback */
            if (entry.callback) {
                entry.callback(&entry.action, &result, entry.user_data);
            }
            
            /* Update stats */
            xSemaphoreTake(s_ctx->stats_mutex, portMAX_DELAY);
            s_ctx->stats.total_executed++;
            if (result.status == TS_ACTION_STATUS_SUCCESS) {
                s_ctx->stats.total_success++;
            } else if (result.status == TS_ACTION_STATUS_TIMEOUT) {
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
    
    /* Execute the action */
    ret = ts_action_manager_execute(&tpl.action, result);
    
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
            
            ESP_LOGI(TAG, "Updated action template: %s", id);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(s_ctx->templates_mutex);
    return ESP_ERR_NOT_FOUND;
}
