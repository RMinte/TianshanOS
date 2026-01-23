/**
 * @file ts_ws_subscriptions.c
 * @brief WebSocket Subscription Manager Implementation
 */

#include "ts_ws_subscriptions.h"
#include "ts_webui.h"
#include "ts_log.h"
#include "ts_event.h"
#include "ts_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "ws_subs"

#define MAX_SUBSCRIPTIONS 32
#define MAX_TOPIC_LEN 32

typedef struct {
    int fd;                          // 客户端文件描述符
    char topic[MAX_TOPIC_LEN];       // 订阅的主题
    uint32_t min_interval_ms;        // 最小推送间隔（去抖动）
    int64_t last_broadcast_time;     // 上次广播时间戳（微秒）
    bool active;                     // 订阅是否激活
} subscription_t;

typedef struct {
    const char *topic;                  // 主题名称
    ts_event_base_t event_base;         // 事件基础
    int32_t event_id;                   // 事件 ID
    uint32_t default_min_interval_ms;   // 默认最小间隔
} topic_mapping_t;

/* 订阅数组 */
static subscription_t s_subscriptions[MAX_SUBSCRIPTIONS];
static SemaphoreHandle_t s_subs_mutex = NULL;

/* 事件处理器句柄 */
static ts_event_handler_handle_t s_system_event_handle = NULL;
static ts_event_handler_handle_t s_device_event_handle = NULL;
static ts_event_handler_handle_t s_ota_event_handle = NULL;

/* 定时器句柄 */
static esp_timer_handle_t s_system_info_timer = NULL;  /* 5s 间隔 */
static esp_timer_handle_t s_cpu_stats_timer = NULL;     /* 1s 间隔 */
static esp_timer_handle_t s_dashboard_timer = NULL;    /* 1s 间隔 - 聚合订阅 */

/* 主题映射表 */
static const topic_mapping_t s_topic_map[] = {
    {"system.dashboard", TS_EVENT_BASE_SYSTEM,     TS_EVENT_SYSTEM_INFO_CHANGED, 1000},  /* 聚合主题 */
    {"system.info",      TS_EVENT_BASE_SYSTEM,     TS_EVENT_SYSTEM_INFO_CHANGED, 5000},
    {"system.memory",    TS_EVENT_BASE_SYSTEM,     TS_EVENT_SYSTEM_INFO_CHANGED, 5000},
    {"system.cpu",       TS_EVENT_BASE_SYSTEM,     TS_EVENT_SYSTEM_INFO_CHANGED, 1000},
    {"network.status",   TS_EVENT_BASE_SYSTEM,     TS_EVENT_SYSTEM_INFO_CHANGED, 5000},
    {"power.status",     TS_EVENT_BASE_SYSTEM,     TS_EVENT_SYSTEM_INFO_CHANGED, 5000},
    {"fan.status",       TS_EVENT_BASE_SYSTEM,     TS_EVENT_SYSTEM_INFO_CHANGED, 5000},
    {"service.list",     TS_EVENT_BASE_SYSTEM,     TS_EVENT_SYSTEM_INFO_CHANGED, 5000},
    {"device.status",    TS_EVENT_BASE_DEVICE_MON, TS_EVENT_DEVICE_STATUS_CHANGED, 2000},
    {"ota.progress",     TS_EVENT_BASE_OTA,        TS_EVENT_OTA_PROGRESS_UPDATE, 1000},
};
#define TOPIC_MAP_SIZE (sizeof(s_topic_map) / sizeof(s_topic_map[0]))

/* 前向声明 */
static void system_event_handler(const ts_event_t *event, void *user_data);
static void device_event_handler(const ts_event_t *event, void *user_data);
static void ota_event_handler(const ts_event_t *event, void *user_data);

/*===========================================================================*/
/*                          辅助函数                                          */
/*===========================================================================*/

/* 查找主题映射 */
static const topic_mapping_t *find_topic_mapping(const char *topic)
{
    for (int i = 0; i < TOPIC_MAP_SIZE; i++) {
        if (strcmp(s_topic_map[i].topic, topic) == 0) {
            return &s_topic_map[i];
        }
    }
    return NULL;
}

/* 检查是否需要广播（去抖动） */
static bool should_broadcast(subscription_t *sub)
{
    int64_t now = esp_timer_get_time();
    int64_t elapsed_ms = (now - sub->last_broadcast_time) / 1000;
    return (elapsed_ms >= sub->min_interval_ms);
}

/* 查找或分配订阅槽位 */
static subscription_t *find_or_alloc_subscription(int fd, const char *topic)
{
    subscription_t *free_slot = NULL;
    
    // 查找已存在的订阅
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].active && 
            s_subscriptions[i].fd == fd &&
            strcmp(s_subscriptions[i].topic, topic) == 0) {
            return &s_subscriptions[i];
        }
        if (!s_subscriptions[i].active && !free_slot) {
            free_slot = &s_subscriptions[i];
        }
    }
    
    return free_slot;
}

/* 统计某主题的活跃订阅数 */
static int count_active_subscriptions(const char *topic)
{
    int count = 0;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].active && strcmp(s_subscriptions[i].topic, topic) == 0) {
            count++;
        }
    }
    return count;
}

/*===========================================================================*/
/*                          事件处理器                                        */
/*===========================================================================*/

static void system_event_handler(const ts_event_t *event, void *user_data)
{
    // 调用 API 获取系统信息（只推送动态数据）
    ts_api_result_t result = {0};
    esp_err_t ret;
    
    // 获取系统基本信息（包含 uptime）
    ret = ts_api_call("system.info", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        TS_LOGD(TAG, "Broadcasting system.info");
        ts_ws_broadcast_to_topic("system.info", result.data);
    }
    
    // 获取内存信息（动态）
    ret = ts_api_call("system.memory", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        TS_LOGD(TAG, "Broadcasting system.memory");
        ts_ws_broadcast_to_topic("system.memory", result.data);
    }
    
    // 获取 CPU 统计（高频动态）
    ret = ts_api_call("system.cpu", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        TS_LOGD(TAG, "Broadcasting system.cpu");
        ts_ws_broadcast_to_topic("system.cpu", result.data);
    }
    
    // 获取网络状态（动态）
    ret = ts_api_call("network.status", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        TS_LOGD(TAG, "Broadcasting network.status");
        ts_ws_broadcast_to_topic("network.status", result.data);
    }
    
    // 获取电源状态（动态）
    ret = ts_api_call("power.status", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        TS_LOGD(TAG, "Broadcasting power.status");
        ts_ws_broadcast_to_topic("power.status", result.data);
    }
    
    // 获取风扇状态（动态）
    ret = ts_api_call("fan.status", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        TS_LOGD(TAG, "Broadcasting fan.status");
        ts_ws_broadcast_to_topic("fan.status", result.data);
    }
    
    // 获取服务列表（动态）
    ret = ts_api_call("service.list", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        TS_LOGD(TAG, "Broadcasting service.list");
        ts_ws_broadcast_to_topic("service.list", result.data);
    }
}

static void device_event_handler(const ts_event_t *event, void *user_data)
{
    if (!event->data || event->data_size == 0) {
        return;
    }
    
    // 解析 JSON 字符串
    cJSON *data = cJSON_Parse((const char *)event->data);
    if (data) {
        ts_ws_broadcast_to_topic("device.status", data);
        cJSON_Delete(data);
    }
}

static void ota_event_handler(const ts_event_t *event, void *user_data)
{
    if (!event->data || event->data_size == 0) {
        return;
    }
    
    // 解析 JSON 字符串
    cJSON *data = cJSON_Parse((const char *)event->data);
    if (data) {
        ts_ws_broadcast_to_topic("ota.progress", data);
        cJSON_Delete(data);
    }
}

/* 定时器回调：定期发送系统信息 */
static void system_info_timer_callback(void *arg)
{
    // 发送系统信息事件
    ts_event_post(TS_EVENT_BASE_SYSTEM, TS_EVENT_SYSTEM_INFO_CHANGED, NULL, 0, 0);
}

/* 定时器回调：CPU 统计 */
static void cpu_stats_timer_callback(void *arg)
{
    // 直接调用 API 并广播
    ts_api_result_t result = {0};
    esp_err_t ret = ts_api_call("system.cpu", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        ts_ws_broadcast_to_topic("system.cpu", result.data);
    }
}

/* 定时器回调：Dashboard 聚合数据 */
static void dashboard_timer_callback(void *arg)
{
    // 创建聚合 JSON 对象
    cJSON *dashboard = cJSON_CreateObject();
    if (!dashboard) {
        TS_LOGE(TAG, "Failed to create dashboard JSON");
        return;
    }
    
    ts_api_result_t result = {0};
    esp_err_t ret;
    
    // 1. CPU 统计（高频数据）
    ret = ts_api_call("system.cpu", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        cJSON_AddItemToObject(dashboard, "cpu", cJSON_Duplicate(result.data, 1));
    }
    
    // 2. 内存信息
    ret = ts_api_call("system.memory", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        cJSON_AddItemToObject(dashboard, "memory", cJSON_Duplicate(result.data, 1));
    }
    
    // 3. 系统信息（uptime 等）
    ret = ts_api_call("system.info", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        cJSON_AddItemToObject(dashboard, "info", cJSON_Duplicate(result.data, 1));
    }
    
    // 4. 网络状态
    ret = ts_api_call("network.status", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        cJSON_AddItemToObject(dashboard, "network", cJSON_Duplicate(result.data, 1));
    }
    
    // 6. 电源状态
    ret = ts_api_call("power.status", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        cJSON_AddItemToObject(dashboard, "power", cJSON_Duplicate(result.data, 1));
    }
    
    // 6. 风扇状态
    ret = ts_api_call("fan.status", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        cJSON_AddItemToObject(dashboard, "fan", cJSON_Duplicate(result.data, 1));
    }
    
    // 7. 服务列表
    ret = ts_api_call("service.list", NULL, &result);
    if (ret == ESP_OK && result.code == 0 && result.data) {
        cJSON_AddItemToObject(dashboard, "services", cJSON_Duplicate(result.data, 1));
    }
    
    // 广播聚合数据
    ts_ws_broadcast_to_topic("system.dashboard", dashboard);
    cJSON_Delete(dashboard);
}

/*===========================================================================*/
/*                          公共 API                                          */
/*===========================================================================*/

esp_err_t ts_ws_subscriptions_init(void)
{
    if (s_subs_mutex) {
        return ESP_OK;  // 已初始化
    }
    
    // 创建互斥锁
    s_subs_mutex = xSemaphoreCreateMutex();
    if (!s_subs_mutex) {
        TS_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 清空订阅数组
    memset(s_subscriptions, 0, sizeof(s_subscriptions));
    
    // 创建定时器（5秒间隔推送系统信息）
    esp_timer_create_args_t timer_args = {
        .callback = system_info_timer_callback,
        .name = "system_info_timer"
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_system_info_timer);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to create system info timer: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_subs_mutex);
        s_subs_mutex = NULL;
        return ret;
    }
    
    // 创建 CPU 统计定时器（1秒间隔）
    esp_timer_create_args_t cpu_timer_args = {
        .callback = cpu_stats_timer_callback,
        .name = "cpu_stats_timer"
    };
    ret = esp_timer_create(&cpu_timer_args, &s_cpu_stats_timer);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to create CPU stats timer: %s", esp_err_to_name(ret));
        esp_timer_delete(s_system_info_timer);
        vSemaphoreDelete(s_subs_mutex);
        s_subs_mutex = NULL;
        return ret;
    }
    
    // 创建 Dashboard 聚合定时器（1秒间隔）
    esp_timer_create_args_t dashboard_timer_args = {
        .callback = dashboard_timer_callback,
        .name = "dashboard_timer"
    };
    ret = esp_timer_create(&dashboard_timer_args, &s_dashboard_timer);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to create dashboard timer: %s", esp_err_to_name(ret));
        esp_timer_delete(s_cpu_stats_timer);
        esp_timer_delete(s_system_info_timer);
        vSemaphoreDelete(s_subs_mutex);
        s_subs_mutex = NULL;
        return ret;
    }
    
    TS_LOGI(TAG, "Subscription manager initialized");
    return ESP_OK;
}

void ts_ws_subscriptions_deinit(void)
{
    if (!s_subs_mutex) return;
    
    // 停止并删除定时器
    if (s_system_info_timer) {
        esp_timer_stop(s_system_info_timer);
        esp_timer_delete(s_system_info_timer);
        s_system_info_timer = NULL;
    }
    
    if (s_cpu_stats_timer) {
        esp_timer_stop(s_cpu_stats_timer);
        esp_timer_delete(s_cpu_stats_timer);
        s_cpu_stats_timer = NULL;
    }
    
    // 取消所有事件处理器
    if (s_system_event_handle) {
        ts_event_unregister(s_system_event_handle);
        s_system_event_handle = NULL;
    }
    if (s_device_event_handle) {
        ts_event_unregister(s_device_event_handle);
        s_device_event_handle = NULL;
    }
    if (s_ota_event_handle) {
        ts_event_unregister(s_ota_event_handle);
        s_ota_event_handle = NULL;
    }
    
    vSemaphoreDelete(s_subs_mutex);
    s_subs_mutex = NULL;
    
    TS_LOGI(TAG, "Subscription manager deinitialized");
}

esp_err_t ts_ws_subscribe(int fd, const char *topic, cJSON *params)
{
    if (!topic || !s_subs_mutex) return ESP_ERR_INVALID_ARG;
    
    const topic_mapping_t *mapping = find_topic_mapping(topic);
    if (!mapping) {
        TS_LOGW(TAG, "Unknown topic: %s", topic);
        return ESP_ERR_NOT_FOUND;
    }
    
    xSemaphoreTake(s_subs_mutex, portMAX_DELAY);
    
    // 查找或分配订阅槽位
    subscription_t *sub = find_or_alloc_subscription(fd, topic);
    if (!sub) {
        xSemaphoreGive(s_subs_mutex);
        TS_LOGE(TAG, "No free subscription slots");
        return ESP_ERR_NO_MEM;
    }
    
    // 配置订阅
    sub->fd = fd;
    strncpy(sub->topic, topic, MAX_TOPIC_LEN - 1);
    sub->topic[MAX_TOPIC_LEN - 1] = '\0';
    sub->min_interval_ms = mapping->default_min_interval_ms;
    sub->last_broadcast_time = 0;
    sub->active = true;
    
    // 从参数中提取自定义间隔
    if (params) {
        cJSON *interval = cJSON_GetObjectItem(params, "interval");
        if (cJSON_IsNumber(interval) && interval->valueint > 0) {
            sub->min_interval_ms = interval->valueint;
        }
    }
    
    // 检查是否需要注册事件处理器（首个订阅该主题的客户端）
    int count = count_active_subscriptions(topic);
    bool first_subscriber = (count == 1);
    
    xSemaphoreGive(s_subs_mutex);
    
    if (first_subscriber) {
        // 注册事件处理器
        ts_event_handler_handle_t *handle = NULL;
        void (*handler)(const ts_event_t *, void *) = NULL;
        
        if (strcmp(topic, "system.memory") == 0 ||
            strcmp(topic, "network.status") == 0 ||
            strcmp(topic, "power.status") == 0 ||
            strcmp(topic, "fan.status") == 0 ||
            strcmp(topic, "service.list") == 0) {
            handle = &s_system_event_handle;
            handler = system_event_handler;
            
            // 启动定时器（5秒间隔）
            if (s_system_info_timer && !esp_timer_is_active(s_system_info_timer)) {
                esp_timer_start_periodic(s_system_info_timer, 5000000);  // 5秒
                TS_LOGI(TAG, "Started system data timer (5s interval)");
            }
        } else if (strcmp(topic, "system.cpu") == 0) {
            // CPU 统计使用独立的 1秒定时器
            if (s_cpu_stats_timer && !esp_timer_is_active(s_cpu_stats_timer)) {
                esp_timer_start_periodic(s_cpu_stats_timer, 1000000);  // 1秒
                TS_LOGI(TAG, "Started CPU stats timer (1s interval)");
            }
        } else if (strcmp(topic, "system.dashboard") == 0) {
            // Dashboard 聚合订阅使用独立的 1秒定时器
            if (s_dashboard_timer && !esp_timer_is_active(s_dashboard_timer)) {
                esp_timer_start_periodic(s_dashboard_timer, 1000000);  // 1秒
                TS_LOGI(TAG, "Started dashboard timer (1s interval)");
            }
        } else if (strcmp(topic, "device.status") == 0) {
            handle = &s_device_event_handle;
            handler = device_event_handler;
        } else if (strcmp(topic, "ota.progress") == 0) {
            handle = &s_ota_event_handle;
            handler = ota_event_handler;
        }
        
        if (handle && handler) {
            esp_err_t ret = ts_event_register(
                mapping->event_base,
                mapping->event_id,
                handler,
                NULL,
                handle
            );
            if (ret == ESP_OK) {
                TS_LOGI(TAG, "Registered event handler for topic: %s", topic);
            } else {
                TS_LOGW(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
            }
        }
    }
    
    TS_LOGI(TAG, "Client %d subscribed to '%s' (interval: %lu ms)", 
            fd, topic, (unsigned long)sub->min_interval_ms);
    return ESP_OK;
}

esp_err_t ts_ws_unsubscribe(int fd, const char *topic)
{
    if (!topic || !s_subs_mutex) return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(s_subs_mutex, portMAX_DELAY);
    
    // 查找并移除订阅
    bool found = false;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].active &&
            s_subscriptions[i].fd == fd &&
            strcmp(s_subscriptions[i].topic, topic) == 0) {
            s_subscriptions[i].active = false;
            found = true;
            break;
        }
    }
    
    // 检查是否需要取消注册事件处理器（最后一个订阅者离开）
    int count = count_active_subscriptions(topic);
    bool last_subscriber = (count == 0);
    
    xSemaphoreGive(s_subs_mutex);
    
    if (last_subscriber) {
        // 取消注册事件处理器
        ts_event_handler_handle_t *handle = NULL;
        if (strcmp(topic, "system.memory") == 0 ||
            strcmp(topic, "network.status") == 0 ||
            strcmp(topic, "power.status") == 0 ||
            strcmp(topic, "fan.status") == 0 ||
            strcmp(topic, "service.list") == 0) {
            handle = &s_system_event_handle;
            
            // 检查是否还有其他 system.* 订阅
            int system_subs = count_active_subscriptions("system.memory") +
                            count_active_subscriptions("network.status") +
                            count_active_subscriptions("power.status") +
                            count_active_subscriptions("fan.status") +
                            count_active_subscriptions("service.list");
            
            // 如果所有 system.* topic 都没有订阅了，停止定时器
            if (system_subs == 0 && s_system_info_timer) {
                esp_timer_stop(s_system_info_timer);
                TS_LOGI(TAG, "Stopped system data timer");
            }
        } else if (strcmp(topic, "system.cpu") == 0) {
            // 停止 CPU 统计定时器
            if (s_cpu_stats_timer && count_active_subscriptions("system.cpu") == 0) {
                esp_timer_stop(s_cpu_stats_timer);
                TS_LOGI(TAG, "Stopped CPU stats timer");
            }
        } else if (strcmp(topic, "device.status") == 0) {
            handle = &s_device_event_handle;
        } else if (strcmp(topic, "ota.progress") == 0) {
            handle = &s_ota_event_handle;
        }
        
        if (handle && *handle) {
            ts_event_unregister(*handle);
            *handle = NULL;
            TS_LOGI(TAG, "Unregistered event handler for topic: %s", topic);
        }
    }
    
    if (found) {
        TS_LOGI(TAG, "Client %d unsubscribed from '%s'", fd, topic);
    }
    
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void ts_ws_client_disconnected(int fd)
{
    if (!s_subs_mutex) return;
    
    xSemaphoreTake(s_subs_mutex, portMAX_DELAY);
    
    // 移除该客户端的所有订阅
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (s_subscriptions[i].active && s_subscriptions[i].fd == fd) {
            char topic[MAX_TOPIC_LEN];
            strncpy(topic, s_subscriptions[i].topic, MAX_TOPIC_LEN);
            s_subscriptions[i].active = false;
            
            xSemaphoreGive(s_subs_mutex);
            ts_ws_unsubscribe(fd, topic);  // 递归调用，处理事件取消注册
            xSemaphoreTake(s_subs_mutex, portMAX_DELAY);
        }
    }
    
    xSemaphoreGive(s_subs_mutex);
    TS_LOGI(TAG, "Cleaned up subscriptions for client %d", fd);
}

void ts_ws_broadcast_to_topic(const char *topic, cJSON *data)
{
    if (!topic || !data || !s_subs_mutex) return;
    
    xSemaphoreTake(s_subs_mutex, portMAX_DELAY);
    
    // 构造 WebSocket 消息
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "data");
    cJSON_AddStringToObject(msg, "topic", topic);
    cJSON_AddItemToObject(msg, "data", cJSON_Duplicate(data, true));
    cJSON_AddNumberToObject(msg, "timestamp", esp_timer_get_time() / 1000000);
    
    char *json_str = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    
    if (!json_str) {
        xSemaphoreGive(s_subs_mutex);
        return;
    }
    
    // 遍历订阅，发送给符合条件的客户端
    int sent_count = 0;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        subscription_t *sub = &s_subscriptions[i];
        if (!sub->active || strcmp(sub->topic, topic) != 0) {
            continue;
        }
        
        // 检查去抖动
        if (!should_broadcast(sub)) {
            continue;
        }
        
        // 更新时间戳
        sub->last_broadcast_time = esp_timer_get_time();
        
        // 发送消息（异步）
        // 注意：这里需要从 ts_webui_ws.c 导出发送函数，或使用 broadcast
        // 暂时使用 broadcast API
        xSemaphoreGive(s_subs_mutex);
        ts_webui_broadcast(json_str);  // TODO: 优化为只发送给订阅者
        xSemaphoreTake(s_subs_mutex, portMAX_DELAY);
        
        sent_count++;
    }
    
    xSemaphoreGive(s_subs_mutex);
    
    if (sent_count > 0) {
        TS_LOGD(TAG, "Broadcasted to %d subscribers of '%s'", sent_count, topic);
    }
    
    free(json_str);
}
