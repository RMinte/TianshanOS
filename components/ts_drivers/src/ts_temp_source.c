/**
 * @file ts_temp_source.c
 * @brief Temperature Source Management Implementation
 * 
 * 温度源管理服务实现
 * - 多 provider 支持（AGX/本地传感器/手动）
 * - 优先级选择
 * - 事件发布
 */

#include "ts_temp_source.h"
#include "ts_event.h"
#include "ts_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "ts_temp_source"

/*===========================================================================*/
/*                          Internal Types                                    */
/*===========================================================================*/

typedef struct {
    ts_temp_source_type_t type;
    const char *name;
    int16_t value;
    uint32_t last_update_ms;
    uint32_t update_count;
    bool registered;
    bool active;
} provider_t;

typedef struct {
    bool initialized;
    bool manual_mode;
    int16_t manual_temp;
    int16_t current_temp;
    ts_temp_source_type_t active_source;
    provider_t providers[TS_TEMP_SOURCE_MAX];
    SemaphoreHandle_t mutex;
} temp_source_state_t;

/*===========================================================================*/
/*                          Static Variables                                  */
/*===========================================================================*/

static temp_source_state_t s_state = {0};

/*===========================================================================*/
/*                          Forward Declarations                              */
/*===========================================================================*/

static uint32_t get_current_ms(void);
static void evaluate_active_source(void);
static void publish_temp_event(int16_t new_temp, ts_temp_source_type_t new_source,
                               int16_t prev_temp, ts_temp_source_type_t prev_source);

/*===========================================================================*/
/*                          Utility Functions                                 */
/*===========================================================================*/

static uint32_t get_current_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

const char *ts_temp_source_type_to_str(ts_temp_source_type_t type)
{
    switch (type) {
        case TS_TEMP_SOURCE_DEFAULT:      return "default";
        case TS_TEMP_SOURCE_SENSOR_LOCAL: return "sensor";
        case TS_TEMP_SOURCE_AGX_AUTO:     return "agx";
        case TS_TEMP_SOURCE_MANUAL:       return "manual";
        default:                          return "unknown";
    }
}

/*===========================================================================*/
/*                          Core Logic                                        */
/*===========================================================================*/

/**
 * @brief 根据优先级和数据有效性选择活动温度源
 */
static void evaluate_active_source(void)
{
    uint32_t now = get_current_ms();
    ts_temp_source_type_t best_source = TS_TEMP_SOURCE_DEFAULT;
    int16_t best_temp = TS_TEMP_DEFAULT_VALUE;
    
    // 优先级：MANUAL > AGX_AUTO > SENSOR_LOCAL > DEFAULT
    // 从高优先级到低优先级检查
    
    // 1. 手动模式最高优先级
    if (s_state.manual_mode) {
        provider_t *p = &s_state.providers[TS_TEMP_SOURCE_MANUAL];
        if (p->registered) {
            best_source = TS_TEMP_SOURCE_MANUAL;
            best_temp = p->value;
            goto done;
        }
    }
    
    // 2. AGX 自动
    {
        provider_t *p = &s_state.providers[TS_TEMP_SOURCE_AGX_AUTO];
        if (p->registered && p->active) {
            uint32_t age = now - p->last_update_ms;
            if (age < TS_TEMP_DATA_TIMEOUT_MS) {
                best_source = TS_TEMP_SOURCE_AGX_AUTO;
                best_temp = p->value;
                goto done;
            }
        }
    }
    
    // 3. 本地传感器
    {
        provider_t *p = &s_state.providers[TS_TEMP_SOURCE_SENSOR_LOCAL];
        if (p->registered && p->active) {
            uint32_t age = now - p->last_update_ms;
            if (age < TS_TEMP_DATA_TIMEOUT_MS) {
                best_source = TS_TEMP_SOURCE_SENSOR_LOCAL;
                best_temp = p->value;
                goto done;
            }
        }
    }
    
    // 4. 默认值
    best_source = TS_TEMP_SOURCE_DEFAULT;
    best_temp = TS_TEMP_DEFAULT_VALUE;

done:
    // 检查是否需要发布事件
    if (best_temp != s_state.current_temp || best_source != s_state.active_source) {
        int16_t prev_temp = s_state.current_temp;
        ts_temp_source_type_t prev_source = s_state.active_source;
        
        s_state.current_temp = best_temp;
        s_state.active_source = best_source;
        
        publish_temp_event(best_temp, best_source, prev_temp, prev_source);
    }
}

/**
 * @brief 发布温度更新事件
 */
static void publish_temp_event(int16_t new_temp, ts_temp_source_type_t new_source,
                               int16_t prev_temp, ts_temp_source_type_t prev_source)
{
    ts_temp_event_data_t evt_data = {
        .temp = new_temp,
        .source = new_source,
        .prev_temp = prev_temp,
        .prev_source = prev_source,
    };
    
    ts_event_post(TS_EVENT_BASE_TEMP, TS_EVT_TEMP_UPDATED,
                  &evt_data, sizeof(evt_data), 0);
    
    TS_LOGD(TAG, "Temp: %.1f°C (%s) -> %.1f°C (%s)",
            prev_temp / 10.0f, ts_temp_source_type_to_str(prev_source),
            new_temp / 10.0f, ts_temp_source_type_to_str(new_source));
}

/*===========================================================================*/
/*                          Public API - Init                                 */
/*===========================================================================*/

esp_err_t ts_temp_source_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }
    
    memset(&s_state, 0, sizeof(s_state));
    
    s_state.mutex = xSemaphoreCreateMutex();
    if (!s_state.mutex) {
        TS_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化默认 provider
    s_state.providers[TS_TEMP_SOURCE_DEFAULT].type = TS_TEMP_SOURCE_DEFAULT;
    s_state.providers[TS_TEMP_SOURCE_DEFAULT].name = "default";
    s_state.providers[TS_TEMP_SOURCE_DEFAULT].value = TS_TEMP_DEFAULT_VALUE;
    s_state.providers[TS_TEMP_SOURCE_DEFAULT].registered = true;
    s_state.providers[TS_TEMP_SOURCE_DEFAULT].active = true;
    
    s_state.current_temp = TS_TEMP_DEFAULT_VALUE;
    s_state.active_source = TS_TEMP_SOURCE_DEFAULT;
    s_state.manual_temp = TS_TEMP_DEFAULT_VALUE;
    
    s_state.initialized = true;
    
    TS_LOGI(TAG, "Temperature source manager initialized (v%s)", TS_TEMP_SOURCE_VERSION);
    return ESP_OK;
}

esp_err_t ts_temp_source_deinit(void)
{
    if (!s_state.initialized) {
        return ESP_OK;
    }
    
    if (s_state.mutex) {
        vSemaphoreDelete(s_state.mutex);
        s_state.mutex = NULL;
    }
    
    s_state.initialized = false;
    return ESP_OK;
}

bool ts_temp_source_is_initialized(void)
{
    return s_state.initialized;
}

/*===========================================================================*/
/*                          Public API - Provider                             */
/*===========================================================================*/

esp_err_t ts_temp_provider_register(ts_temp_source_type_t type, const char *name)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    if (type >= TS_TEMP_SOURCE_MAX) return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    provider_t *p = &s_state.providers[type];
    p->type = type;
    p->name = name ? name : ts_temp_source_type_to_str(type);
    p->value = TS_TEMP_DEFAULT_VALUE;
    p->last_update_ms = 0;
    p->update_count = 0;
    p->registered = true;
    p->active = false;
    
    xSemaphoreGive(s_state.mutex);
    
    TS_LOGI(TAG, "Provider registered: %s (%s)", p->name, ts_temp_source_type_to_str(type));
    return ESP_OK;
}

esp_err_t ts_temp_provider_unregister(ts_temp_source_type_t type)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    if (type >= TS_TEMP_SOURCE_MAX) return ESP_ERR_INVALID_ARG;
    if (type == TS_TEMP_SOURCE_DEFAULT) return ESP_ERR_NOT_SUPPORTED;  // 不能注销默认
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    s_state.providers[type].registered = false;
    s_state.providers[type].active = false;
    
    evaluate_active_source();
    
    xSemaphoreGive(s_state.mutex);
    
    TS_LOGI(TAG, "Provider unregistered: %s", ts_temp_source_type_to_str(type));
    return ESP_OK;
}

esp_err_t ts_temp_provider_update(ts_temp_source_type_t type, int16_t temp_01c)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    if (type >= TS_TEMP_SOURCE_MAX) return ESP_ERR_INVALID_ARG;
    
    // 温度范围检查
    if (temp_01c < TS_TEMP_MIN_VALID || temp_01c > TS_TEMP_MAX_VALID) {
        TS_LOGW(TAG, "Invalid temp from %s: %d", ts_temp_source_type_to_str(type), temp_01c);
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    provider_t *p = &s_state.providers[type];
    if (!p->registered) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_INVALID_STATE;
    }
    
    p->value = temp_01c;
    p->last_update_ms = get_current_ms();
    p->update_count++;
    p->active = true;
    
    evaluate_active_source();
    
    xSemaphoreGive(s_state.mutex);
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API - Consumer                             */
/*===========================================================================*/

int16_t ts_temp_get_effective(ts_temp_data_t *data)
{
    if (!s_state.initialized) {
        if (data) {
            data->value = TS_TEMP_DEFAULT_VALUE;
            data->source = TS_TEMP_SOURCE_DEFAULT;
            data->timestamp_ms = 0;
            data->valid = false;
        }
        return TS_TEMP_DEFAULT_VALUE;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    int16_t temp = s_state.current_temp;
    ts_temp_source_type_t source = s_state.active_source;
    
    if (data) {
        data->value = temp;
        data->source = source;
        data->timestamp_ms = s_state.providers[source].last_update_ms;
        data->valid = true;
    }
    
    xSemaphoreGive(s_state.mutex);
    
    return temp;
}

esp_err_t ts_temp_get_by_source(ts_temp_source_type_t type, ts_temp_data_t *data)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    if (type >= TS_TEMP_SOURCE_MAX || !data) return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    provider_t *p = &s_state.providers[type];
    if (!p->registered) {
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    data->value = p->value;
    data->source = p->type;
    data->timestamp_ms = p->last_update_ms;
    data->valid = p->active;
    
    xSemaphoreGive(s_state.mutex);
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API - Manual Mode                          */
/*===========================================================================*/

esp_err_t ts_temp_set_manual(int16_t temp_01c)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    
    // 注册手动 provider（如果尚未注册）
    if (!s_state.providers[TS_TEMP_SOURCE_MANUAL].registered) {
        ts_temp_provider_register(TS_TEMP_SOURCE_MANUAL, "manual");
    }
    
    // 启用手动模式
    s_state.manual_mode = true;
    s_state.manual_temp = temp_01c;
    
    // 更新 provider
    return ts_temp_provider_update(TS_TEMP_SOURCE_MANUAL, temp_01c);
}

esp_err_t ts_temp_set_manual_mode(bool enable)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    s_state.manual_mode = enable;
    
    if (enable && !s_state.providers[TS_TEMP_SOURCE_MANUAL].registered) {
        // 注册手动 provider
        provider_t *p = &s_state.providers[TS_TEMP_SOURCE_MANUAL];
        p->type = TS_TEMP_SOURCE_MANUAL;
        p->name = "manual";
        p->value = s_state.manual_temp;
        p->last_update_ms = get_current_ms();
        p->registered = true;
        p->active = true;
    }
    
    evaluate_active_source();
    
    xSemaphoreGive(s_state.mutex);
    
    TS_LOGI(TAG, "Manual mode %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}

bool ts_temp_is_manual_mode(void)
{
    return s_state.manual_mode;
}

/*===========================================================================*/
/*                          Public API - Status                               */
/*===========================================================================*/

esp_err_t ts_temp_get_status(ts_temp_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    
    memset(status, 0, sizeof(ts_temp_status_t));
    
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    status->initialized = s_state.initialized;
    status->active_source = s_state.active_source;
    status->current_temp = s_state.current_temp;
    status->manual_mode = s_state.manual_mode;
    
    uint32_t count = 0;
    for (int i = 0; i < TS_TEMP_SOURCE_MAX; i++) {
        provider_t *p = &s_state.providers[i];
        if (p->registered) {
            ts_temp_provider_info_t *info = &status->providers[count];
            info->type = p->type;
            info->name = p->name;
            info->last_value = p->value;
            info->last_update_ms = p->last_update_ms;
            info->update_count = p->update_count;
            info->active = p->active;
            count++;
        }
    }
    status->provider_count = count;
    
    xSemaphoreGive(s_state.mutex);
    
    return ESP_OK;
}

ts_temp_source_type_t ts_temp_get_active_source(void)
{
    return s_state.active_source;
}
