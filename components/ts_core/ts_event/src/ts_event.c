/**
 * @file ts_event.c
 * @brief TianShanOS Event Bus Implementation
 *
 * 事件总线实现
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#include <string.h>
#include <stdlib.h>
#include "ts_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ts_event";

/* ============================================================================
 * 私有类型定义
 * ========================================================================== */

/**
 * @brief 内部事件结构（用于队列传输）
 */
typedef struct {
    ts_event_base_t base;
    ts_event_id_t id;
    ts_event_priority_t priority;
    uint32_t timestamp_ms;
    void *source;
    size_t data_size;
    uint8_t data[TS_EVENT_DATA_MAX_SIZE];
} ts_event_internal_t;

/**
 * @brief 处理器节点
 */
typedef struct ts_event_handler_instance {
    ts_event_base_t base;
    ts_event_id_t id;
    ts_event_priority_t min_priority;
    ts_event_handler_t handler;
    void *user_data;
    struct ts_event_handler_instance *next;
} ts_event_handler_instance_t;

/**
 * @brief 事务节点
 */
typedef struct ts_event_transaction_node {
    ts_event_internal_t event;
    struct ts_event_transaction_node *next;
} ts_event_transaction_node_t;

/**
 * @brief 事务结构
 */
typedef struct ts_event_transaction {
    ts_event_transaction_node_t *events;
    size_t count;
} ts_event_transaction_impl_t;

/**
 * @brief 事件上下文
 */
typedef struct {
    bool initialized;
    SemaphoreHandle_t mutex;
    QueueHandle_t event_queue;
    TaskHandle_t event_task;
    ts_event_handler_instance_t *handlers;
    size_t handler_count;
    ts_event_stats_t stats;
    bool running;
} ts_event_context_t;

/* ============================================================================
 * 私有变量
 * ========================================================================== */

static ts_event_context_t s_event_ctx = {0};

/* ============================================================================
 * 私有函数声明
 * ========================================================================== */

static void event_loop_task(void *arg);
static void dispatch_event(const ts_event_internal_t *internal_event);
static bool handler_matches(const ts_event_handler_instance_t *handler,
                            const ts_event_internal_t *event);

/* ============================================================================
 * 初始化和反初始化
 * ========================================================================== */

esp_err_t ts_event_init(void)
{
    if (s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing TianShanOS Event System...");

    // 创建互斥锁
    s_event_ctx.mutex = xSemaphoreCreateMutex();
    if (s_event_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 创建事件队列
    s_event_ctx.event_queue = xQueueCreate(TS_EVENT_QUEUE_SIZE, sizeof(ts_event_internal_t));
    if (s_event_ctx.event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        vSemaphoreDelete(s_event_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    // 初始化统计
    memset(&s_event_ctx.stats, 0, sizeof(ts_event_stats_t));

    s_event_ctx.handlers = NULL;
    s_event_ctx.handler_count = 0;
    s_event_ctx.running = true;

    // 创建事件循环任务
#ifndef CONFIG_TS_EVENT_LOOP_TASK_PRIORITY
#define CONFIG_TS_EVENT_LOOP_TASK_PRIORITY 5
#endif

#ifndef CONFIG_TS_EVENT_LOOP_TASK_STACK_SIZE
#define CONFIG_TS_EVENT_LOOP_TASK_STACK_SIZE 4096
#endif

    BaseType_t ret = xTaskCreate(
        event_loop_task,
        "ts_event",
        CONFIG_TS_EVENT_LOOP_TASK_STACK_SIZE,
        NULL,
        CONFIG_TS_EVENT_LOOP_TASK_PRIORITY,
        &s_event_ctx.event_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create event loop task");
        vQueueDelete(s_event_ctx.event_queue);
        vSemaphoreDelete(s_event_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    s_event_ctx.initialized = true;
    ESP_LOGI(TAG, "Event system initialized (queue_size=%d)", TS_EVENT_QUEUE_SIZE);

    return ESP_OK;
}

esp_err_t ts_event_deinit(void)
{
    if (!s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing event system...");

    // 停止事件循环
    s_event_ctx.running = false;

    // 发送一个空事件来唤醒任务
    ts_event_internal_t dummy = {0};
    xQueueSend(s_event_ctx.event_queue, &dummy, 0);

    // 等待任务结束
    vTaskDelay(pdMS_TO_TICKS(100));

    // 删除任务
    if (s_event_ctx.event_task != NULL) {
        vTaskDelete(s_event_ctx.event_task);
        s_event_ctx.event_task = NULL;
    }

    xSemaphoreTake(s_event_ctx.mutex, portMAX_DELAY);

    // 释放所有处理器
    ts_event_handler_instance_t *handler = s_event_ctx.handlers;
    while (handler != NULL) {
        ts_event_handler_instance_t *next = handler->next;
        free(handler);
        handler = next;
    }
    s_event_ctx.handlers = NULL;
    s_event_ctx.handler_count = 0;

    xSemaphoreGive(s_event_ctx.mutex);

    // 删除队列
    vQueueDelete(s_event_ctx.event_queue);
    s_event_ctx.event_queue = NULL;

    // 删除互斥锁
    vSemaphoreDelete(s_event_ctx.mutex);
    s_event_ctx.mutex = NULL;

    s_event_ctx.initialized = false;
    ESP_LOGI(TAG, "Event system deinitialized");

    return ESP_OK;
}

bool ts_event_is_initialized(void)
{
    return s_event_ctx.initialized;
}

/* ============================================================================
 * 事件注册
 * ========================================================================== */

esp_err_t ts_event_register(ts_event_base_t event_base,
                             ts_event_id_t event_id,
                             ts_event_handler_t handler,
                             void *user_data,
                             ts_event_handler_handle_t *handle)
{
    return ts_event_register_with_priority(event_base, event_id, 
                                            TS_EVENT_PRIORITY_LOW,
                                            handler, user_data, handle);
}

esp_err_t ts_event_register_with_priority(ts_event_base_t event_base,
                                           ts_event_id_t event_id,
                                           ts_event_priority_t min_priority,
                                           ts_event_handler_t handler,
                                           void *user_data,
                                           ts_event_handler_handle_t *handle)
{
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_event_ctx.handler_count >= TS_EVENT_HANDLERS_MAX) {
        ESP_LOGE(TAG, "Maximum handlers reached");
        return ESP_ERR_NO_MEM;
    }

    ts_event_handler_instance_t *node = calloc(1, sizeof(ts_event_handler_instance_t));
    if (node == NULL) {
        return ESP_ERR_NO_MEM;
    }

    node->base = event_base;
    node->id = event_id;
    node->min_priority = min_priority;
    node->handler = handler;
    node->user_data = user_data;

    xSemaphoreTake(s_event_ctx.mutex, portMAX_DELAY);

    node->next = s_event_ctx.handlers;
    s_event_ctx.handlers = node;
    s_event_ctx.handler_count++;
    s_event_ctx.stats.handlers_registered = s_event_ctx.handler_count;

    xSemaphoreGive(s_event_ctx.mutex);

    if (handle != NULL) {
        *handle = node;
    }

    ESP_LOGD(TAG, "Registered handler for %s:%ld", 
             event_base ? event_base : "*", (long)event_id);

    return ESP_OK;
}

esp_err_t ts_event_unregister(ts_event_handler_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_event_ctx.mutex, portMAX_DELAY);

    ts_event_handler_instance_t *prev = NULL;
    ts_event_handler_instance_t *node = s_event_ctx.handlers;
    ts_event_handler_instance_t *target = (ts_event_handler_instance_t *)handle;

    while (node != NULL) {
        if (node == target) {
            if (prev == NULL) {
                s_event_ctx.handlers = node->next;
            } else {
                prev->next = node->next;
            }
            s_event_ctx.handler_count--;
            s_event_ctx.stats.handlers_registered = s_event_ctx.handler_count;

            xSemaphoreGive(s_event_ctx.mutex);
            free(node);
            return ESP_OK;
        }
        prev = node;
        node = node->next;
    }

    xSemaphoreGive(s_event_ctx.mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ts_event_unregister_all(ts_event_base_t event_base, ts_event_id_t event_id)
{
    if (!s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_event_ctx.mutex, portMAX_DELAY);

    ts_event_handler_instance_t *prev = NULL;
    ts_event_handler_instance_t *node = s_event_ctx.handlers;

    while (node != NULL) {
        bool match = (event_base == NULL || 
                      (node->base != NULL && strcmp(node->base, event_base) == 0));
        if (match && (event_id == TS_EVENT_ANY_ID || node->id == event_id)) {
            ts_event_handler_instance_t *to_delete = node;
            if (prev == NULL) {
                s_event_ctx.handlers = node->next;
                node = s_event_ctx.handlers;
            } else {
                prev->next = node->next;
                node = node->next;
            }
            s_event_ctx.handler_count--;
            free(to_delete);
        } else {
            prev = node;
            node = node->next;
        }
    }

    s_event_ctx.stats.handlers_registered = s_event_ctx.handler_count;
    xSemaphoreGive(s_event_ctx.mutex);

    return ESP_OK;
}

/* ============================================================================
 * 事件发布
 * ========================================================================== */

esp_err_t ts_event_post(ts_event_base_t event_base,
                         ts_event_id_t event_id,
                         const void *data,
                         size_t data_size,
                         uint32_t timeout_ms)
{
    return ts_event_post_with_priority(event_base, event_id, data, data_size,
                                        TS_EVENT_PRIORITY_NORMAL, timeout_ms);
}

esp_err_t ts_event_post_with_priority(ts_event_base_t event_base,
                                       ts_event_id_t event_id,
                                       const void *data,
                                       size_t data_size,
                                       ts_event_priority_t priority,
                                       uint32_t timeout_ms)
{
    if (!s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data_size > TS_EVENT_DATA_MAX_SIZE) {
        ESP_LOGE(TAG, "Event data too large: %zu > %d", data_size, TS_EVENT_DATA_MAX_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    ts_event_internal_t event = {
        .base = event_base,
        .id = event_id,
        .priority = priority,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .source = NULL,
        .data_size = data_size,
    };

    if (data != NULL && data_size > 0) {
        memcpy(event.data, data, data_size);
    }

    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? 
                        portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    if (xQueueSend(s_event_ctx.event_queue, &event, ticks) != pdTRUE) {
        s_event_ctx.stats.events_dropped++;
        ESP_LOGW(TAG, "Event queue full, event dropped");
        return ESP_ERR_TIMEOUT;
    }

    s_event_ctx.stats.events_posted++;

    // 更新高水位
    UBaseType_t queue_count = uxQueueMessagesWaiting(s_event_ctx.event_queue);
    if (queue_count > s_event_ctx.stats.queue_high_watermark) {
        s_event_ctx.stats.queue_high_watermark = queue_count;
    }

#ifdef CONFIG_TS_EVENT_ENABLE_TRACING
    ESP_LOGD(TAG, "Posted event: %s:%ld (priority=%d)", 
             event_base ? event_base : "?", (long)event_id, priority);
#endif

    return ESP_OK;
}

esp_err_t ts_event_post_sync(ts_event_base_t event_base,
                              ts_event_id_t event_id,
                              const void *data,
                              size_t data_size)
{
    if (!s_event_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data_size > TS_EVENT_DATA_MAX_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    ts_event_internal_t event = {
        .base = event_base,
        .id = event_id,
        .priority = TS_EVENT_PRIORITY_NORMAL,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .source = NULL,
        .data_size = data_size,
    };

    if (data != NULL && data_size > 0) {
        memcpy(event.data, data, data_size);
    }

    s_event_ctx.stats.events_posted++;
    dispatch_event(&event);

    return ESP_OK;
}

esp_err_t ts_event_post_from_isr(ts_event_base_t event_base,
                                  ts_event_id_t event_id,
                                  const void *data,
                                  size_t data_size,
                                  BaseType_t *higher_priority_task_woken)
{
    if (!s_event_ctx.initialized || data_size > TS_EVENT_DATA_MAX_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    ts_event_internal_t event = {
        .base = event_base,
        .id = event_id,
        .priority = TS_EVENT_PRIORITY_HIGH,
        .timestamp_ms = 0,  // ISR 中不获取时间戳
        .source = NULL,
        .data_size = data_size,
    };

    if (data != NULL && data_size > 0) {
        memcpy(event.data, data, data_size);
    }

    if (xQueueSendFromISR(s_event_ctx.event_queue, &event, 
                           higher_priority_task_woken) != pdTRUE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* ============================================================================
 * 事务支持
 * ========================================================================== */

esp_err_t ts_event_transaction_begin(ts_event_transaction_t *transaction)
{
    if (transaction == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ts_event_transaction_impl_t *tx = calloc(1, sizeof(ts_event_transaction_impl_t));
    if (tx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    tx->events = NULL;
    tx->count = 0;

    *transaction = tx;
    return ESP_OK;
}

esp_err_t ts_event_transaction_post(ts_event_transaction_t transaction,
                                     ts_event_base_t event_base,
                                     ts_event_id_t event_id,
                                     const void *data,
                                     size_t data_size)
{
    if (transaction == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (data_size > TS_EVENT_DATA_MAX_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    ts_event_transaction_impl_t *tx = (ts_event_transaction_impl_t *)transaction;

    ts_event_transaction_node_t *node = calloc(1, sizeof(ts_event_transaction_node_t));
    if (node == NULL) {
        return ESP_ERR_NO_MEM;
    }

    node->event.base = event_base;
    node->event.id = event_id;
    node->event.priority = TS_EVENT_PRIORITY_NORMAL;
    node->event.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    node->event.data_size = data_size;

    if (data != NULL && data_size > 0) {
        memcpy(node->event.data, data, data_size);
    }

    node->next = tx->events;
    tx->events = node;
    tx->count++;

    return ESP_OK;
}

esp_err_t ts_event_transaction_commit(ts_event_transaction_t transaction)
{
    if (transaction == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ts_event_transaction_impl_t *tx = (ts_event_transaction_impl_t *)transaction;

    // 发布所有事件
    ts_event_transaction_node_t *node = tx->events;
    while (node != NULL) {
        xQueueSend(s_event_ctx.event_queue, &node->event, pdMS_TO_TICKS(100));
        s_event_ctx.stats.events_posted++;
        node = node->next;
    }

    // 释放事务
    node = tx->events;
    while (node != NULL) {
        ts_event_transaction_node_t *next = node->next;
        free(node);
        node = next;
    }
    free(tx);

    return ESP_OK;
}

esp_err_t ts_event_transaction_rollback(ts_event_transaction_t transaction)
{
    if (transaction == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ts_event_transaction_impl_t *tx = (ts_event_transaction_impl_t *)transaction;

    // 释放所有事件节点
    ts_event_transaction_node_t *node = tx->events;
    while (node != NULL) {
        ts_event_transaction_node_t *next = node->next;
        free(node);
        node = next;
    }

    free(tx);
    return ESP_OK;
}

/* ============================================================================
 * 统计和调试
 * ========================================================================== */

esp_err_t ts_event_get_stats(ts_event_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(stats, &s_event_ctx.stats, sizeof(ts_event_stats_t));
    return ESP_OK;
}

void ts_event_reset_stats(void)
{
    memset(&s_event_ctx.stats, 0, sizeof(ts_event_stats_t));
    s_event_ctx.stats.handlers_registered = s_event_ctx.handler_count;
}

void ts_event_dump_stats(void)
{
    ESP_LOGI(TAG, "=== Event Statistics ===");
    ESP_LOGI(TAG, "  Posted: %lu", (unsigned long)s_event_ctx.stats.events_posted);
    ESP_LOGI(TAG, "  Delivered: %lu", (unsigned long)s_event_ctx.stats.events_delivered);
    ESP_LOGI(TAG, "  Dropped: %lu", (unsigned long)s_event_ctx.stats.events_dropped);
    ESP_LOGI(TAG, "  Handlers: %lu", (unsigned long)s_event_ctx.stats.handlers_registered);
    ESP_LOGI(TAG, "  Queue HWM: %lu", (unsigned long)s_event_ctx.stats.queue_high_watermark);
    ESP_LOGI(TAG, "========================");
}

size_t ts_event_get_queue_count(void)
{
    if (s_event_ctx.event_queue == NULL) {
        return 0;
    }
    return uxQueueMessagesWaiting(s_event_ctx.event_queue);
}

/* ============================================================================
 * 私有函数实现
 * ========================================================================== */

static void event_loop_task(void *arg)
{
    (void)arg;
    ts_event_internal_t event;

    ESP_LOGI(TAG, "Event loop task started");

    while (s_event_ctx.running) {
        if (xQueueReceive(s_event_ctx.event_queue, &event, portMAX_DELAY) == pdTRUE) {
            if (!s_event_ctx.running) {
                break;
            }
            dispatch_event(&event);
        }
    }

    ESP_LOGI(TAG, "Event loop task ended");
    vTaskDelete(NULL);
}

static void dispatch_event(const ts_event_internal_t *internal_event)
{
    if (internal_event == NULL) {
        return;
    }

    int64_t start_time = esp_timer_get_time();

    // 构造外部事件结构
    ts_event_t event = {
        .base = internal_event->base,
        .id = internal_event->id,
        .data = (internal_event->data_size > 0) ? (void *)internal_event->data : NULL,
        .data_size = internal_event->data_size,
        .priority = internal_event->priority,
        .timestamp_ms = internal_event->timestamp_ms,
        .source = internal_event->source,
    };

    xSemaphoreTake(s_event_ctx.mutex, portMAX_DELAY);

    ts_event_handler_instance_t *handler = s_event_ctx.handlers;
    while (handler != NULL) {
        if (handler_matches(handler, internal_event)) {
            // 释放锁来调用处理器（避免死锁）
            xSemaphoreGive(s_event_ctx.mutex);

            if (handler->handler != NULL) {
                handler->handler(&event, handler->user_data);
            }

            s_event_ctx.stats.events_delivered++;

            xSemaphoreTake(s_event_ctx.mutex, portMAX_DELAY);
        }
        handler = handler->next;
    }

    xSemaphoreGive(s_event_ctx.mutex);

    // 更新统计
    int64_t elapsed = esp_timer_get_time() - start_time;
    if (elapsed > s_event_ctx.stats.max_delivery_time_us) {
        s_event_ctx.stats.max_delivery_time_us = (uint32_t)elapsed;
    }
}

static bool handler_matches(const ts_event_handler_instance_t *handler,
                            const ts_event_internal_t *event)
{
    // 检查优先级
    if (event->priority < handler->min_priority) {
        return false;
    }

    // 检查事件基础
    if (handler->base != TS_EVENT_ANY_BASE && handler->base != NULL) {
        if (event->base == NULL || strcmp(handler->base, event->base) != 0) {
            return false;
        }
    }

    // 检查事件 ID
    if (handler->id != TS_EVENT_ANY_ID && handler->id != event->id) {
        return false;
    }

    return true;
}
