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
#include "ts_variable.h"
#include "ts_config_module.h"
#include "ts_storage.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define TAG "ts_temp_source"

/** NVS 存储键 */
#define NVS_NAMESPACE       "ts_temp"
#define NVS_KEY_PREFERRED   "preferred"
#define NVS_KEY_BOUND_VAR   "bound_var"

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
    ts_temp_source_type_t preferred_source;  /**< 用户首选源（0=自动）*/
    char bound_variable[TS_TEMP_MAX_VARNAME_LEN];  /**< 绑定的变量名 */
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
static esp_err_t load_preferred_source_from_nvs(void);
static esp_err_t save_preferred_source_to_nvs(ts_temp_source_type_t type);
static void export_temp_config_to_sdcard(void);

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
        case TS_TEMP_SOURCE_VARIABLE:     return "variable";
        case TS_TEMP_SOURCE_MANUAL:       return "manual";
        default:                          return "unknown";
    }
}

/*===========================================================================*/
/*                          Core Logic                                        */
/*===========================================================================*/

/**
 * @brief 检查 provider 是否可用（已注册、激活且数据未过期）
 */
static bool is_provider_valid(ts_temp_source_type_t type, uint32_t now)
{
    if (type >= TS_TEMP_SOURCE_MAX) return false;
    
    // VARIABLE 类型特殊处理 - 检查绑定变量是否可读
    if (type == TS_TEMP_SOURCE_VARIABLE) {
        if (s_state.bound_variable[0] == '\0') return false;
        double value = 0;
        return (ts_variable_get_float(s_state.bound_variable, &value) == ESP_OK);
    }
    
    provider_t *p = &s_state.providers[type];
    if (!p->registered || !p->active) return false;
    
    // DEFAULT 源始终有效
    if (type == TS_TEMP_SOURCE_DEFAULT) return true;
    
    // 检查数据是否过期
    uint32_t age = now - p->last_update_ms;
    return (age < TS_TEMP_DATA_TIMEOUT_MS);
}

/**
 * @brief 从绑定的变量读取温度值
 */
static int16_t read_temp_from_variable(void)
{
    if (s_state.bound_variable[0] == '\0') {
        return TS_TEMP_DEFAULT_VALUE;
    }
    
    double value = 0;
    esp_err_t ret = ts_variable_get_float(s_state.bound_variable, &value);
    if (ret != ESP_OK) {
        TS_LOGD(TAG, "Failed to read variable '%s': %s", 
                s_state.bound_variable, esp_err_to_name(ret));
        return TS_TEMP_DEFAULT_VALUE;
    }
    
    // 转换为 0.1°C 单位
    int16_t temp_01c = (int16_t)(value * 10.0);
    
    // 范围检查
    if (temp_01c < TS_TEMP_MIN_VALID || temp_01c > TS_TEMP_MAX_VALID) {
        TS_LOGW(TAG, "Variable '%s' value out of range: %.1f°C", 
                s_state.bound_variable, value);
        return TS_TEMP_DEFAULT_VALUE;
    }
    
    return temp_01c;
}

/**
 * @brief 根据优先级和数据有效性选择活动温度源
 * 
 * 选择逻辑：
 * 1. 手动模式最高优先级（无视 preferred_source）
 * 2. 如果设置了 preferred_source 且该源可用，使用它
 * 3. 否则按默认优先级（VARIABLE > AGX > SENSOR > DEFAULT）
 */
static void evaluate_active_source(void)
{
    uint32_t now = get_current_ms();
    ts_temp_source_type_t best_source = TS_TEMP_SOURCE_DEFAULT;
    int16_t best_temp = TS_TEMP_DEFAULT_VALUE;
    
    // 1. 手动模式最高优先级
    if (s_state.manual_mode) {
        provider_t *p = &s_state.providers[TS_TEMP_SOURCE_MANUAL];
        if (p->registered) {
            best_source = TS_TEMP_SOURCE_MANUAL;
            best_temp = p->value;
            goto done;
        }
    }
    
    // 2. 检查用户首选源
    if (s_state.preferred_source != TS_TEMP_SOURCE_DEFAULT && 
        s_state.preferred_source != TS_TEMP_SOURCE_MANUAL) {
        
        // 变量绑定特殊处理
        if (s_state.preferred_source == TS_TEMP_SOURCE_VARIABLE) {
            if (is_provider_valid(TS_TEMP_SOURCE_VARIABLE, now)) {
                best_source = TS_TEMP_SOURCE_VARIABLE;
                best_temp = read_temp_from_variable();
                goto done;
            }
        } else if (is_provider_valid(s_state.preferred_source, now)) {
            best_source = s_state.preferred_source;
            best_temp = s_state.providers[s_state.preferred_source].value;
            goto done;
        }
        // 首选源不可用，降级到自动选择
        TS_LOGD(TAG, "Preferred source %s unavailable, falling back",
                ts_temp_source_type_to_str(s_state.preferred_source));
    }
    
    // 3. 默认优先级：VARIABLE > AGX > SENSOR > DEFAULT
    if (is_provider_valid(TS_TEMP_SOURCE_VARIABLE, now)) {
        best_source = TS_TEMP_SOURCE_VARIABLE;
        best_temp = read_temp_from_variable();
        goto done;
    }
    
    if (is_provider_valid(TS_TEMP_SOURCE_AGX_AUTO, now)) {
        best_source = TS_TEMP_SOURCE_AGX_AUTO;
        best_temp = s_state.providers[TS_TEMP_SOURCE_AGX_AUTO].value;
        goto done;
    }
    
    if (is_provider_valid(TS_TEMP_SOURCE_SENSOR_LOCAL, now)) {
        best_source = TS_TEMP_SOURCE_SENSOR_LOCAL;
        best_temp = s_state.providers[TS_TEMP_SOURCE_SENSOR_LOCAL].value;
        goto done;
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
    s_state.preferred_source = TS_TEMP_SOURCE_DEFAULT;  // 默认自动选择
    
    // 从 NVS 加载首选温度源
    load_preferred_source_from_nvs();
    
    s_state.initialized = true;
    
    TS_LOGI(TAG, "Temperature source manager initialized (v%s), preferred: %s", 
            TS_TEMP_SOURCE_VERSION, 
            s_state.preferred_source == TS_TEMP_SOURCE_DEFAULT ? "auto" : 
            ts_temp_source_type_to_str(s_state.preferred_source));
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
    
    /* 重新评估活动源并更新温度（确保获取最新值） */
    evaluate_active_source();
    
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
    
    /* 重新评估活动源并更新温度（确保获取最新值） */
    evaluate_active_source();
    
    status->initialized = s_state.initialized;
    status->active_source = s_state.active_source;
    status->preferred_source = s_state.preferred_source;
    status->current_temp = s_state.current_temp;
    status->manual_mode = s_state.manual_mode;
    
    /* 复制绑定的变量名 */
    if (s_state.bound_variable[0] != '\0') {
        strncpy(status->bound_variable, s_state.bound_variable, sizeof(status->bound_variable) - 1);
        status->bound_variable[sizeof(status->bound_variable) - 1] = '\0';
    }
    
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

/*===========================================================================*/
/*                          Public API - Preferred Source                     */
/*===========================================================================*/

esp_err_t ts_temp_set_preferred_source(ts_temp_source_type_t type)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    
    // 不能选择 MANUAL（手动模式通过专用 API）或无效值
    if (type == TS_TEMP_SOURCE_MANUAL || type >= TS_TEMP_SOURCE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    ts_temp_source_type_t old_preferred = s_state.preferred_source;
    s_state.preferred_source = type;
    
    // 切换到非手动源时，自动禁用手动模式
    if (s_state.manual_mode) {
        s_state.manual_mode = false;
        TS_LOGI(TAG, "Manual mode disabled (switching to %s)", ts_temp_source_type_to_str(type));
    }
    
    // 重新评估活动源
    evaluate_active_source();
    
    xSemaphoreGive(s_state.mutex);
    
    // 保存到 NVS + SD 卡
    save_preferred_source_to_nvs(type);
    export_temp_config_to_sdcard();
    
    TS_LOGI(TAG, "Preferred source: %s -> %s",
            old_preferred == TS_TEMP_SOURCE_DEFAULT ? "auto" : ts_temp_source_type_to_str(old_preferred),
            type == TS_TEMP_SOURCE_DEFAULT ? "auto" : ts_temp_source_type_to_str(type));
    
    return ESP_OK;
}

ts_temp_source_type_t ts_temp_get_preferred_source(void)
{
    return s_state.preferred_source;
}

esp_err_t ts_temp_clear_preferred_source(void)
{
    return ts_temp_set_preferred_source(TS_TEMP_SOURCE_DEFAULT);
}

/*===========================================================================*/
/*                          NVS Functions                                     */
/*===========================================================================*/

/* Forward declarations */
static esp_err_t load_temp_config_from_file(const char *filepath);
static esp_err_t save_preferred_source_to_nvs(ts_temp_source_type_t type);
static esp_err_t save_bound_variable_to_nvs(const char *var_name);

/**
 * @brief 加载温度源配置
 * 
 * Priority: SD card file > NVS > defaults (遵循系统配置优先级原则)
 */
static esp_err_t load_preferred_source_from_nvs(void)
{
    esp_err_t ret;
    
    /* 1. 优先从 SD 卡加载 */
    if (ts_storage_sd_mounted()) {
        ret = load_temp_config_from_file("/sdcard/config/temp.json");
        if (ret == ESP_OK) {
            TS_LOGI(TAG, "Loaded temp config from SD card");
            return ESP_OK;  /* SD 卡配置已自动保存到 NVS */
        }
    }
    
    /* 2. SD 卡无配置，从 NVS 加载 */
    nvs_handle_t handle;
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    
    if (ret != ESP_OK) {
        TS_LOGD(TAG, "No saved temp config found, using defaults");
        return ESP_OK;
    }
    
    uint8_t preferred = 0;
    ret = nvs_get_u8(handle, NVS_KEY_PREFERRED, &preferred);
    
    if (ret == ESP_OK && preferred < TS_TEMP_SOURCE_MAX) {
        s_state.preferred_source = (ts_temp_source_type_t)preferred;
        TS_LOGI(TAG, "Loaded preferred source from NVS: %s",
                preferred == 0 ? "auto" : ts_temp_source_type_to_str(s_state.preferred_source));
        
        /* 加载绑定的变量名 */
        size_t len = sizeof(s_state.bound_variable);
        ret = nvs_get_str(handle, NVS_KEY_BOUND_VAR, s_state.bound_variable, &len);
        if (ret == ESP_OK && s_state.bound_variable[0] != '\0') {
            TS_LOGI(TAG, "Loaded bound variable from NVS: %s", s_state.bound_variable);
        } else {
            s_state.bound_variable[0] = '\0';
        }
    } else {
        TS_LOGD(TAG, "No saved temp config found, using defaults");
    }
    
    nvs_close(handle);
    return ESP_OK;
}

/**
 * @brief Load temp config from SD card JSON file
 */
static esp_err_t load_temp_config_from_file(const char *filepath)
{
    if (!filepath) return ESP_ERR_INVALID_ARG;
    
    FILE *f = fopen(filepath, "r");
    if (!f) {
        TS_LOGD(TAG, "Cannot open file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0 || size > 1024) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    
    char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(content, 1, size, f);
    fclose(f);
    content[read_size] = '\0';
    
    cJSON *root = cJSON_Parse(content);
    free(content);
    
    if (!root) {
        TS_LOGW(TAG, "Failed to parse JSON: %s", filepath);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Parse preferred_source */
    cJSON *pref = cJSON_GetObjectItem(root, "preferred_source");
    if (pref && cJSON_IsString(pref)) {
        const char *pref_str = pref->valuestring;
        if (strcmp(pref_str, "variable") == 0) {
            s_state.preferred_source = TS_TEMP_SOURCE_VARIABLE;
        } else if (strcmp(pref_str, "agx") == 0 || strcmp(pref_str, "agx_auto") == 0) {
            s_state.preferred_source = TS_TEMP_SOURCE_AGX_AUTO;
        } else if (strcmp(pref_str, "local") == 0 || strcmp(pref_str, "sensor_local") == 0) {
            s_state.preferred_source = TS_TEMP_SOURCE_SENSOR_LOCAL;
        } else if (strcmp(pref_str, "manual") == 0) {
            s_state.preferred_source = TS_TEMP_SOURCE_MANUAL;
        } else {
            s_state.preferred_source = TS_TEMP_SOURCE_DEFAULT;
        }
    }
    
    /* Parse bound_variable */
    cJSON *bound = cJSON_GetObjectItem(root, "bound_variable");
    if (bound && cJSON_IsString(bound)) {
        strncpy(s_state.bound_variable, bound->valuestring, sizeof(s_state.bound_variable) - 1);
        s_state.bound_variable[sizeof(s_state.bound_variable) - 1] = '\0';
    }
    
    cJSON_Delete(root);
    
    TS_LOGI(TAG, "Loaded temp config from SD card: preferred=%s, bound=%s",
            ts_temp_source_type_to_str(s_state.preferred_source),
            s_state.bound_variable[0] ? s_state.bound_variable : "(none)");
    
    /* Save to NVS for next boot */
    save_preferred_source_to_nvs(s_state.preferred_source);
    if (s_state.bound_variable[0]) {
        save_bound_variable_to_nvs(s_state.bound_variable);
    }
    
    return ESP_OK;
}

static esp_err_t save_preferred_source_to_nvs(ts_temp_source_type_t type)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = nvs_set_u8(handle, NVS_KEY_PREFERRED, (uint8_t)type);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to write preferred source: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }
    
    ret = nvs_commit(handle);
    nvs_close(handle);
    
    if (ret == ESP_OK) {
        TS_LOGD(TAG, "Saved preferred source to NVS: %s",
                type == 0 ? "auto" : ts_temp_source_type_to_str(type));
    }
    
    return ret;
}

static esp_err_t save_bound_variable_to_nvs(const char *var_name)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (var_name && var_name[0] != '\0') {
        ret = nvs_set_str(handle, NVS_KEY_BOUND_VAR, var_name);
    } else {
        ret = nvs_erase_key(handle, NVS_KEY_BOUND_VAR);
        if (ret == ESP_ERR_NVS_NOT_FOUND) ret = ESP_OK;  // 键不存在也是成功
    }
    
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to write bound variable: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }
    
    ret = nvs_commit(handle);
    nvs_close(handle);
    
    if (ret == ESP_OK) {
        TS_LOGD(TAG, "Saved bound variable to NVS: %s", var_name ? var_name : "(none)");
    }
    
    return ret;
}

/**
 * @brief 导出温度配置到 SD 卡
 * 
 * 生成包含 preferred_source 和 bound_variable 的 JSON 并写入 SD 卡
 */
static void export_temp_config_to_sdcard(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        TS_LOGW(TAG, "Failed to create JSON for SD card export");
        return;
    }
    
    /* 导出首选源 */
    const char *preferred_str = s_state.preferred_source == TS_TEMP_SOURCE_DEFAULT 
                                ? "auto" 
                                : ts_temp_source_type_to_str(s_state.preferred_source);
    cJSON_AddStringToObject(root, "preferred_source", preferred_str);
    
    /* 导出绑定变量（如果有） */
    if (s_state.bound_variable[0] != '\0') {
        cJSON_AddStringToObject(root, "bound_variable", s_state.bound_variable);
    }
    
    /* 使用配置模块导出 API */
    esp_err_t ret = ts_config_module_export_custom_json(TS_CONFIG_MODULE_TEMP, root);
    cJSON_Delete(root);
    
    if (ret != ESP_OK && ret != TS_CONFIG_ERR_SD_NOT_MOUNTED) {
        TS_LOGW(TAG, "Failed to export temp config to SD card: %s", esp_err_to_name(ret));
    }
}

/*===========================================================================*/
/*                      Public API - Variable Binding                         */
/*===========================================================================*/

esp_err_t ts_temp_bind_variable(const char *var_name)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    if (!var_name || var_name[0] == '\0') return ESP_ERR_INVALID_ARG;
    
    /* 检查变量名长度 */
    if (strlen(var_name) >= TS_TEMP_MAX_VARNAME_LEN) {
        TS_LOGE(TAG, "Variable name too long: %s", var_name);
        return ESP_ERR_INVALID_SIZE;
    }
    
    /* 验证变量是否存在 */
    if (!ts_variable_exists(var_name)) {
        TS_LOGW(TAG, "Variable does not exist: %s (will bind anyway)", var_name);
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    strncpy(s_state.bound_variable, var_name, sizeof(s_state.bound_variable) - 1);
    s_state.bound_variable[sizeof(s_state.bound_variable) - 1] = '\0';
    
    /* 重新评估活动源 */
    evaluate_active_source();
    
    xSemaphoreGive(s_state.mutex);
    
    /* 保存到 NVS + SD 卡 */
    save_bound_variable_to_nvs(var_name);
    export_temp_config_to_sdcard();
    
    TS_LOGI(TAG, "Temperature bound to variable: %s", var_name);
    
    return ESP_OK;
}

esp_err_t ts_temp_get_bound_variable(char *var_name, size_t len)
{
    if (!var_name || len == 0) return ESP_ERR_INVALID_ARG;
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    if (s_state.bound_variable[0] == '\0') {
        xSemaphoreGive(s_state.mutex);
        var_name[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }
    
    strncpy(var_name, s_state.bound_variable, len - 1);
    var_name[len - 1] = '\0';
    
    xSemaphoreGive(s_state.mutex);
    
    return ESP_OK;
}

esp_err_t ts_temp_unbind_variable(void)
{
    if (!s_state.initialized) return ESP_ERR_INVALID_STATE;
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    
    bool was_bound = (s_state.bound_variable[0] != '\0');
    s_state.bound_variable[0] = '\0';
    
    /* 重新评估活动源 */
    evaluate_active_source();
    
    xSemaphoreGive(s_state.mutex);
    
    if (was_bound) {
        /* 从 NVS + SD 卡清除 */
        save_bound_variable_to_nvs(NULL);
        export_temp_config_to_sdcard();
        TS_LOGI(TAG, "Temperature variable binding removed");
    }
    
    return ESP_OK;
}
