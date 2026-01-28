/**
 * @file ts_source_manager.c
 * @brief TianShanOS 自动化引擎 - 数据源管理器实现
 *
 * 管理多类数据源：
 * 1. WebSocket - 外部服务器实时数据
 * 2. Socket.IO - Socket.IO v4 协议（如 AGX Monitor）
 * 3. REST - HTTP API 轮询（支持本地 API 和外部 API）
 *
 * 数据提取使用 JSONPath 表达式，支持动态映射到变量
 *
 * @author TianShanOS Team
 * @version 1.1.0
 */

#include "ts_source_manager.h"
#include "ts_variable.h"
#include "ts_jsonpath.h"
// ts_var.h 已废弃，统一使用 ts_variable.h（ts_automation 变量系统）

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// HAL 组件（用于 builtin 源）
#include "ts_hal.h"

// HTTP 客户端（用于 REST 源）
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "cJSON.h"

// API 层（用于本地 API 调用）
#include "ts_api.h"

// 网络状态检查
#include "ts_net_manager.h"

// NVS 持久化
#include "nvs_flash.h"
#include "nvs.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "ts_source_mgr";

// NVS 命名空间和键
#define NVS_NAMESPACE       "ts_auto_src"
#define NVS_KEY_COUNT       "count"
#define NVS_KEY_PREFIX      "src_"

/*===========================================================================*/
/*                          Socket.IO 协议常量                                */
/*===========================================================================*/

#define SIO_PROBE_MSG           "2probe"
#define SIO_UPGRADE_MSG         "5"
#define SIO_PING_MSG            "2"
#define SIO_PONG_MSG            "3"
#define SIO_EVENT_PREFIX        "42"
#define SIO_SID_MAX_LEN         64
#define SIO_MAX_CONNECTIONS     4

/*===========================================================================*/
/*                              配置常量                                      */
/*===========================================================================*/

#ifndef CONFIG_TS_AUTOMATION_MAX_SOURCES
#define CONFIG_TS_AUTOMATION_MAX_SOURCES  16
#endif

/*===========================================================================*/
/*                              内部状态                                      */
/*===========================================================================*/

typedef struct {
    ts_auto_source_t *sources;           // 源数组
    int count;                           // 当前源数量
    int capacity;                        // 最大容量
    SemaphoreHandle_t mutex;             // 访问互斥锁
    bool initialized;                    // 初始化标志
    bool running;                        // 运行标志
    ts_source_manager_stats_t stats;     // 统计信息
} ts_source_manager_ctx_t;

static ts_source_manager_ctx_t s_src_ctx = {
    .sources = NULL,
    .count = 0,
    .capacity = 0,
    .mutex = NULL,
    .initialized = false,
    .running = false,
};

/*===========================================================================*/
/*                          Socket.IO 连接上下文                              */
/*===========================================================================*/

/** Socket.IO 消息缓冲区大小 */
#define SIO_MSG_BUF_SIZE    8192

/** Socket.IO 连接状态 */
typedef struct {
    char source_id[TS_AUTO_NAME_MAX_LEN];   // 关联的数据源 ID
    esp_websocket_client_handle_t client;    // WebSocket 客户端
    char session_id[SIO_SID_MAX_LEN];        // Socket.IO session ID
    bool connected;                          // WebSocket 已连接
    bool upgraded;                           // Socket.IO 升级完成
    int64_t last_message_ms;                 // 最后消息时间
    TaskHandle_t task_handle;                // 连接任务句柄
    bool should_stop;                        // 停止标志
    // 消息分片缓冲
    char *msg_buf;                           // 消息缓冲区（动态分配）
    size_t msg_buf_len;                      // 当前缓冲长度
    // 待处理数据（延迟到主任务中处理，避免回调栈溢出）
    char *pending_json;                      // 待处理的 JSON 字符串
    SemaphoreHandle_t pending_mutex;         // 保护 pending_json
    bool auto_discovered;                    // 已完成自动发现（防止重复创建）
} sio_connection_t;

/** Socket.IO 连接管理 */
static struct {
    sio_connection_t connections[SIO_MAX_CONNECTIONS];
    int count;
    SemaphoreHandle_t mutex;
} s_sio_ctx = {0};

/*===========================================================================*/
/*                              辅助函数                                      */
/*===========================================================================*/

/**
 * 查找源索引
 */
static int find_source_index(const char *id)
{
    for (int i = 0; i < s_src_ctx.count; i++) {
        if (strcmp(s_src_ctx.sources[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/*===========================================================================*/
/*                              NVS 持久化                                    */
/*===========================================================================*/

/**
 * 将数据源序列化为 JSON
 */
static char *source_to_json(const ts_auto_source_t *src)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "id", src->id);
    cJSON_AddStringToObject(root, "label", src->label);
    cJSON_AddNumberToObject(root, "type", src->type);
    cJSON_AddBoolToObject(root, "enabled", src->enabled);
    cJSON_AddBoolToObject(root, "auto_discover", src->auto_discover);
    cJSON_AddNumberToObject(root, "poll_interval_ms", src->poll_interval_ms);

    switch (src->type) {
        case TS_AUTO_SRC_WEBSOCKET:
            cJSON_AddStringToObject(root, "ws_uri", src->websocket.uri);
            cJSON_AddStringToObject(root, "ws_path", src->websocket.path);
            cJSON_AddNumberToObject(root, "reconnect_ms", src->websocket.reconnect_ms);
            break;
        case TS_AUTO_SRC_SOCKETIO:
            cJSON_AddStringToObject(root, "sio_url", src->socketio.url);
            cJSON_AddStringToObject(root, "sio_event", src->socketio.event);
            cJSON_AddNumberToObject(root, "reconnect_ms", src->socketio.reconnect_ms);
            break;
        case TS_AUTO_SRC_REST:
            cJSON_AddStringToObject(root, "rest_url", src->rest.url);
            cJSON_AddStringToObject(root, "rest_path", src->rest.path);
            cJSON_AddStringToObject(root, "rest_method", src->rest.method);
            cJSON_AddStringToObject(root, "rest_auth", src->rest.auth_header);
            break;
        case TS_AUTO_SRC_VARIABLE:
            cJSON_AddStringToObject(root, "var_name", src->variable.var_name);
            cJSON_AddStringToObject(root, "var_prefix", src->variable.var_prefix);
            cJSON_AddBoolToObject(root, "var_watch_all", src->variable.watch_all);
            // SSH 命令模式相关字段
            if (src->variable.ssh_host_id[0] != '\0') {
                cJSON_AddStringToObject(root, "ssh_host_id", src->variable.ssh_host_id);
            }
            if (src->variable.ssh_command[0] != '\0') {
                cJSON_AddStringToObject(root, "ssh_command", src->variable.ssh_command);
            }
            if (src->variable.expect_pattern[0] != '\0') {
                cJSON_AddStringToObject(root, "expect_pattern", src->variable.expect_pattern);
            }
            if (src->variable.fail_pattern[0] != '\0') {
                cJSON_AddStringToObject(root, "fail_pattern", src->variable.fail_pattern);
            }
            if (src->variable.extract_pattern[0] != '\0') {
                cJSON_AddStringToObject(root, "extract_pattern", src->variable.extract_pattern);
            }
            if (src->variable.timeout_sec > 0) {
                cJSON_AddNumberToObject(root, "timeout_sec", src->variable.timeout_sec);
            }
            break;
        default:
            break;
    }

    // 序列化映射
    if (src->mapping_count > 0) {
        cJSON *mappings = cJSON_CreateArray();
        for (uint8_t i = 0; i < src->mapping_count && i < TS_AUTO_MAX_MAPPINGS; i++) {
            cJSON *mapping = cJSON_CreateObject();
            cJSON_AddStringToObject(mapping, "path", src->mappings[i].json_path);
            cJSON_AddStringToObject(mapping, "var", src->mappings[i].var_name);
            if (src->mappings[i].transform[0] != '\0') {
                cJSON_AddStringToObject(mapping, "transform", src->mappings[i].transform);
            }
            cJSON_AddItemToArray(mappings, mapping);
        }
        cJSON_AddItemToObject(root, "mappings", mappings);
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

/**
 * 从 JSON 反序列化数据源
 */
static esp_err_t json_to_source(const char *json_str, ts_auto_source_t *src)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return ESP_ERR_INVALID_ARG;

    memset(src, 0, sizeof(ts_auto_source_t));

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "id")) && cJSON_IsString(item)) {
        strncpy(src->id, item->valuestring, sizeof(src->id) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "label")) && cJSON_IsString(item)) {
        strncpy(src->label, item->valuestring, sizeof(src->label) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "type")) && cJSON_IsNumber(item)) {
        src->type = (ts_auto_source_type_t)item->valueint;
    }
    if ((item = cJSON_GetObjectItem(root, "enabled"))) {
        src->enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "auto_discover"))) {
        src->auto_discover = cJSON_IsTrue(item);
    } else {
        src->auto_discover = true;  // 默认启用自动发现（兼容旧配置）
    }
    if ((item = cJSON_GetObjectItem(root, "poll_interval_ms")) && cJSON_IsNumber(item)) {
        src->poll_interval_ms = (uint32_t)item->valueint;
    }

    switch (src->type) {
        case TS_AUTO_SRC_WEBSOCKET:
            if ((item = cJSON_GetObjectItem(root, "ws_uri")) && cJSON_IsString(item)) {
                strncpy(src->websocket.uri, item->valuestring, sizeof(src->websocket.uri) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "ws_path")) && cJSON_IsString(item)) {
                strncpy(src->websocket.path, item->valuestring, sizeof(src->websocket.path) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "reconnect_ms")) && cJSON_IsNumber(item)) {
                src->websocket.reconnect_ms = (uint16_t)item->valueint;
            }
            break;
        case TS_AUTO_SRC_SOCKETIO:
            if ((item = cJSON_GetObjectItem(root, "sio_url")) && cJSON_IsString(item)) {
                strncpy(src->socketio.url, item->valuestring, sizeof(src->socketio.url) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "sio_event")) && cJSON_IsString(item)) {
                strncpy(src->socketio.event, item->valuestring, sizeof(src->socketio.event) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "reconnect_ms")) && cJSON_IsNumber(item)) {
                src->socketio.reconnect_ms = (uint16_t)item->valueint;
            }
            break;
        case TS_AUTO_SRC_REST:
            if ((item = cJSON_GetObjectItem(root, "rest_url")) && cJSON_IsString(item)) {
                strncpy(src->rest.url, item->valuestring, sizeof(src->rest.url) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "rest_path")) && cJSON_IsString(item)) {
                strncpy(src->rest.path, item->valuestring, sizeof(src->rest.path) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "rest_method")) && cJSON_IsString(item)) {
                strncpy(src->rest.method, item->valuestring, sizeof(src->rest.method) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "rest_auth")) && cJSON_IsString(item)) {
                strncpy(src->rest.auth_header, item->valuestring, sizeof(src->rest.auth_header) - 1);
            }
            break;
        case TS_AUTO_SRC_VARIABLE:
            if ((item = cJSON_GetObjectItem(root, "var_name")) && cJSON_IsString(item)) {
                strncpy(src->variable.var_name, item->valuestring, sizeof(src->variable.var_name) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "var_prefix")) && cJSON_IsString(item)) {
                strncpy(src->variable.var_prefix, item->valuestring, sizeof(src->variable.var_prefix) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "var_watch_all"))) {
                src->variable.watch_all = cJSON_IsTrue(item);
            }
            // SSH 命令模式相关字段
            if ((item = cJSON_GetObjectItem(root, "ssh_host_id")) && cJSON_IsString(item)) {
                strncpy(src->variable.ssh_host_id, item->valuestring, sizeof(src->variable.ssh_host_id) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "ssh_command")) && cJSON_IsString(item)) {
                strncpy(src->variable.ssh_command, item->valuestring, sizeof(src->variable.ssh_command) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "expect_pattern")) && cJSON_IsString(item)) {
                strncpy(src->variable.expect_pattern, item->valuestring, sizeof(src->variable.expect_pattern) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "fail_pattern")) && cJSON_IsString(item)) {
                strncpy(src->variable.fail_pattern, item->valuestring, sizeof(src->variable.fail_pattern) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "extract_pattern")) && cJSON_IsString(item)) {
                strncpy(src->variable.extract_pattern, item->valuestring, sizeof(src->variable.extract_pattern) - 1);
            }
            if ((item = cJSON_GetObjectItem(root, "timeout_sec")) && cJSON_IsNumber(item)) {
                src->variable.timeout_sec = (uint16_t)item->valueint;
            }
            break;
        default:
            break;
    }

    // 反序列化映射
    cJSON *mappings = cJSON_GetObjectItem(root, "mappings");
    if (mappings && cJSON_IsArray(mappings)) {
        int count = cJSON_GetArraySize(mappings);
        src->mapping_count = (count > TS_AUTO_MAX_MAPPINGS) ? TS_AUTO_MAX_MAPPINGS : count;

        for (int i = 0; i < src->mapping_count; i++) {
            cJSON *mapping = cJSON_GetArrayItem(mappings, i);
            if (!mapping) continue;

            cJSON *path_item = cJSON_GetObjectItem(mapping, "path");
            cJSON *var_item = cJSON_GetObjectItem(mapping, "var");
            cJSON *transform_item = cJSON_GetObjectItem(mapping, "transform");

            if (path_item && cJSON_IsString(path_item)) {
                strncpy(src->mappings[i].json_path, path_item->valuestring, 
                        sizeof(src->mappings[i].json_path) - 1);
            }
            if (var_item && cJSON_IsString(var_item)) {
                strncpy(src->mappings[i].var_name, var_item->valuestring, 
                        sizeof(src->mappings[i].var_name) - 1);
            }
            if (transform_item && cJSON_IsString(transform_item)) {
                strncpy(src->mappings[i].transform, transform_item->valuestring, 
                        sizeof(src->mappings[i].transform) - 1);
            }
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/**
 * 保存所有数据源到 NVS
 */
static esp_err_t save_sources_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // 先清除所有旧数据
    nvs_erase_all(handle);

    // 保存数量
    ret = nvs_set_u8(handle, NVS_KEY_COUNT, (uint8_t)s_src_ctx.count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save count: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    // 保存每个数据源
    for (int i = 0; i < s_src_ctx.count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);

        char *json = source_to_json(&s_src_ctx.sources[i]);
        if (!json) {
            ESP_LOGW(TAG, "Failed to serialize source %d", i);
            continue;
        }

        ret = nvs_set_str(handle, key, json);
        free(json);

        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to save source %d: %s", i, esp_err_to_name(ret));
        }
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Saved %d sources to NVS", s_src_ctx.count);
    return ret;
}

/**
 * 从 NVS 加载数据源
 */
static esp_err_t load_sources_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved sources found");
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // 读取数量
    uint8_t count = 0;
    ret = nvs_get_u8(handle, NVS_KEY_COUNT, &count);
    if (ret != ESP_OK || count == 0) {
        nvs_close(handle);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Loading %d sources from NVS", count);

    // 加载每个数据源
    for (int i = 0; i < count && s_src_ctx.count < s_src_ctx.capacity; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);

        // 先获取字符串长度
        size_t len = 0;
        ret = nvs_get_str(handle, key, NULL, &len);
        if (ret != ESP_OK || len == 0) {
            continue;
        }

        // 分配并读取（优先使用 PSRAM）
        char *json = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
        if (!json) json = malloc(len);
        if (!json) continue;

        ret = nvs_get_str(handle, key, json, &len);
        if (ret == ESP_OK) {
            // 使用堆分配避免栈溢出（ts_auto_source_t ~1KB+）
            ts_auto_source_t *src = heap_caps_malloc(sizeof(ts_auto_source_t), MALLOC_CAP_SPIRAM);
            if (!src) {
                src = malloc(sizeof(ts_auto_source_t));  // Fallback to DRAM
            }
            if (src) {
                memset(src, 0, sizeof(ts_auto_source_t));
                if (json_to_source(json, src) == ESP_OK && src->id[0]) {
                    memcpy(&s_src_ctx.sources[s_src_ctx.count], src, sizeof(ts_auto_source_t));
                    s_src_ctx.count++;
                    ESP_LOGD(TAG, "Loaded source: %s", src->id);

    // 记录需要自动连接的 Socket.IO 源（不在此处连接，等待 start_all 时连接）
                    // Socket.IO 源在 ts_source_start_all() 中连接，确保网络就绪
                }
                free(src);
            }
        }
        free(json);
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded %d sources from NVS", s_src_ctx.count);

    return ESP_OK;
}

/*===========================================================================*/
/*                              辅助函数                                      */
/*===========================================================================*/

/**
 * 更新源关联的变量
 */
static void update_source_variable(const ts_auto_source_t *src)
{
    // 查找关联此源的变量并更新
    // 这里简化处理，假设变量名格式为 "category.source_id_suffix"
    // 实际应该通过变量的 source_id 字段关联

    // 暂时不实现，留给配置加载器处理映射
}

/**
 * 从 JSON 中按路径提取值
 * @param json JSON 对象
 * @param path 路径字符串，支持多种格式：
 *   - 点分隔: "data.cpu.usage"
 *   - 数组索引: "items[0].name" 或 "items.0.name"
 *   - 混合: "data.servers[2].metrics.cpu"
 * @param value 输出值
 * @return ESP_OK 成功
 */
static esp_err_t extract_json_value(cJSON *json, const char *path, ts_auto_value_t *value)
{
    if (!json || !path || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    // 特殊情况：空路径或 "$" 表示根对象
    if (path[0] == '\0' || (path[0] == '$' && path[1] == '\0')) {
        // 返回整个 JSON 作为字符串
        char *str = cJSON_PrintUnformatted(json);
        if (str) {
            value->type = TS_AUTO_VAL_STRING;
            strncpy(value->str_val, str, sizeof(value->str_val) - 1);
            value->str_val[sizeof(value->str_val) - 1] = '\0';
            free(str);
            return ESP_OK;
        }
        return ESP_ERR_NO_MEM;
    }

    // 复制路径用于处理
    char path_copy[TS_AUTO_NAME_MAX_LEN];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // 跳过开头的 "$." (JSONPath 根符号)
    char *p = path_copy;
    if (p[0] == '$' && p[1] == '.') {
        p += 2;
    }

    cJSON *current = json;
    char token[64];
    size_t token_idx = 0;

    while (*p && current) {
        // 跳过分隔符
        if (*p == '.' || *p == '/') {
            p++;
            continue;
        }

        // 处理数组索引 [n]
        if (*p == '[') {
            p++;  // 跳过 '['
            int index = 0;
            while (*p >= '0' && *p <= '9') {
                index = index * 10 + (*p - '0');
                p++;
            }
            if (*p == ']') p++;  // 跳过 ']'
            
            if (cJSON_IsArray(current)) {
                current = cJSON_GetArrayItem(current, index);
            } else {
                ESP_LOGD(TAG, "Path '%s': expected array at index %d", path, index);
                return ESP_ERR_NOT_FOUND;
            }
            continue;
        }

        // 提取字段名
        token_idx = 0;
        while (*p && *p != '.' && *p != '[' && *p != '/' && token_idx < sizeof(token) - 1) {
            token[token_idx++] = *p++;
        }
        token[token_idx] = '\0';

        if (token_idx == 0) continue;

        // 检查是否是纯数字（数组索引简写：items.0.name）
        bool is_numeric = true;
        for (size_t i = 0; i < token_idx; i++) {
            if (token[i] < '0' || token[i] > '9') {
                is_numeric = false;
                break;
            }
        }

        if (is_numeric && cJSON_IsArray(current)) {
            int index = atoi(token);
            current = cJSON_GetArrayItem(current, index);
        } else {
            current = cJSON_GetObjectItem(current, token);
        }
    }

    if (!current) {
        return ESP_ERR_NOT_FOUND;
    }

    // 根据 JSON 类型设置值
    if (cJSON_IsNumber(current)) {
        double num = current->valuedouble;
        if (num == (int32_t)num) {
            value->type = TS_AUTO_VAL_INT;
            value->int_val = (int32_t)num;
        } else {
            value->type = TS_AUTO_VAL_FLOAT;
            value->float_val = (float)num;
        }
    } else if (cJSON_IsBool(current)) {
        value->type = TS_AUTO_VAL_BOOL;
        value->bool_val = cJSON_IsTrue(current);
    } else if (cJSON_IsString(current)) {
        value->type = TS_AUTO_VAL_STRING;
        strncpy(value->str_val, current->valuestring, sizeof(value->str_val) - 1);
        value->str_val[sizeof(value->str_val) - 1] = '\0';
    } else {
        // 对象或数组，转为字符串
        char *str = cJSON_PrintUnformatted(current);
        if (str) {
            value->type = TS_AUTO_VAL_STRING;
            strncpy(value->str_val, str, sizeof(value->str_val) - 1);
            value->str_val[sizeof(value->str_val) - 1] = '\0';
            free(str);
        } else {
            return ESP_ERR_NO_MEM;
        }
    }

    return ESP_OK;
}

/**
 * 将 cJSON 值转换为 ts_auto_value_t
 */
static void cjson_to_value(cJSON *json, ts_auto_value_t *value)
{
    if (!json || !value) return;

    if (cJSON_IsNumber(json)) {
        double num = json->valuedouble;
        if (num == (int32_t)num && num >= INT32_MIN && num <= INT32_MAX) {
            value->type = TS_AUTO_VAL_INT;
            value->int_val = (int32_t)num;
        } else {
            value->type = TS_AUTO_VAL_FLOAT;
            value->float_val = (float)num;
        }
    } else if (cJSON_IsBool(json)) {
        value->type = TS_AUTO_VAL_BOOL;
        value->bool_val = cJSON_IsTrue(json);
    } else if (cJSON_IsString(json)) {
        value->type = TS_AUTO_VAL_STRING;
        strncpy(value->str_val, json->valuestring, sizeof(value->str_val) - 1);
        value->str_val[sizeof(value->str_val) - 1] = '\0';
    } else if (cJSON_IsNull(json)) {
        value->type = TS_AUTO_VAL_NULL;
    } else {
        // Object/Array -> JSON string
        char *str = cJSON_PrintUnformatted(json);
        if (str) {
            value->type = TS_AUTO_VAL_STRING;
            strncpy(value->str_val, str, sizeof(value->str_val) - 1);
            value->str_val[sizeof(value->str_val) - 1] = '\0';
            free(str);
        }
    }
}

/**
 * 处理数据源的所有映射
 *
 * 使用 JSONPath 从 JSON 数据中提取值，并更新对应的变量
 *
 * @param src 数据源
 * @param json_data JSON 数据
 * @return 成功处理的映射数量
 */
static int process_source_mappings(ts_auto_source_t *src, cJSON *json_data);

/**
 * 自动发现 JSON 中的叶子节点并创建/更新变量
 * 用于 Socket.IO 数据源没有配置 mappings 时的自动处理
 *
 * @param src 数据源
 * @param json_data JSON 数据
 * @param prefix 变量名前缀（通常是 source id）
 * @param max_depth 最大递归深度
 * @param create_new 是否创建新变量（false 时只更新已存在的变量）
 * @return 成功创建/更新的变量数量
 */
static int auto_discover_json_fields(ts_auto_source_t *src, cJSON *json_data, 
                                     const char *prefix, int max_depth, bool create_new)
{
    if (!src || !json_data || max_depth <= 0) {
        return 0;
    }

    int count = 0;
    char var_name[96];  /* 增大缓冲区以容纳完整变量名 */
    
    if (cJSON_IsObject(json_data)) {
        cJSON *item;
        cJSON_ArrayForEach(item, json_data) {
            // 构建变量名
            if (prefix && prefix[0]) {
                snprintf(var_name, sizeof(var_name), "%s.%s", prefix, item->string);
            } else {
                snprintf(var_name, sizeof(var_name), "%s.%s", src->id, item->string);
            }
            
            // 只处理叶子节点（数值、布尔、字符串）
            if (cJSON_IsBool(item) || cJSON_IsNumber(item) || cJSON_IsString(item)) {
                ts_auto_value_t value = {0};
                cjson_to_value(item, &value);
                
                // 尝试更新变量
                esp_err_t ret = ts_variable_set(var_name, &value);
                if (ret == ESP_ERR_NOT_FOUND && create_new) {
                    // 变量不存在且允许创建
                    ts_auto_variable_t new_var = {0};
                    strncpy(new_var.name, var_name, sizeof(new_var.name) - 1);
                    strncpy(new_var.source_id, src->id, sizeof(new_var.source_id) - 1);
                    new_var.value = value;
                    new_var.flags = 0;
                    
                    ret = ts_variable_register(&new_var);
                    if (ret == ESP_OK) {
                        ESP_LOGD(TAG, "Auto-discovered variable: %s", var_name);
                        count++;
                    }
                    // 存储满时不再尝试创建，继续更新已有变量
                } else if (ret == ESP_OK) {
                    count++;
                }
            } else if (cJSON_IsObject(item) || cJSON_IsArray(item)) {
                // 递归处理子对象
                count += auto_discover_json_fields(src, item, var_name, max_depth - 1, create_new);
            }
        }
    } else if (cJSON_IsArray(json_data)) {
        // 对数组，只处理前几个元素
        int arr_size = cJSON_GetArraySize(json_data);
        int max_elements = (arr_size > 4) ? 4 : arr_size;  // 最多处理4个元素
        
        for (int i = 0; i < max_elements; i++) {
            cJSON *item = cJSON_GetArrayItem(json_data, i);
            char arr_prefix[64];
            snprintf(arr_prefix, sizeof(arr_prefix), "%s[%d]", prefix, i);
            
            if (cJSON_IsBool(item) || cJSON_IsNumber(item) || cJSON_IsString(item)) {
                ts_auto_value_t value = {0};
                cjson_to_value(item, &value);
                
                esp_err_t ret = ts_variable_set(arr_prefix, &value);
                if (ret == ESP_ERR_NOT_FOUND && create_new) {
                    ts_auto_variable_t new_var = {0};
                    strncpy(new_var.name, arr_prefix, sizeof(new_var.name) - 1);
                    strncpy(new_var.source_id, src->id, sizeof(new_var.source_id) - 1);
                    new_var.value = value;
                    new_var.flags = 0;
                    
                    ret = ts_variable_register(&new_var);
                    if (ret == ESP_OK) {
                        count++;
                    }
                } else if (ret == ESP_OK) {
                    count++;
                }
            } else if (cJSON_IsObject(item) || cJSON_IsArray(item)) {
                count += auto_discover_json_fields(src, item, arr_prefix, max_depth - 1, create_new);
            }
        }
    }
    
    return count;
}

/**
 * 处理数据源映射，从 JSON 中提取值并更新变量
 *
 * @param src 数据源
 * @param json_data JSON 数据
 * @return 成功处理的映射数量
 */
static int process_source_mappings(ts_auto_source_t *src, cJSON *json_data)
{
    if (!src || !json_data) {
        return 0;
    }

    int processed = 0;

    // 遍历所有映射
    for (uint8_t i = 0; i < src->mapping_count && i < TS_AUTO_MAX_MAPPINGS; i++) {
        ts_auto_mapping_t *mapping = &src->mappings[i];

        // 跳过空映射
        if (mapping->json_path[0] == '\0' || mapping->var_name[0] == '\0') {
            continue;
        }

        // 使用 JSONPath 提取值
        cJSON *result = ts_jsonpath_get(json_data, mapping->json_path);
        if (!result) {
            ESP_LOGD(TAG, "Mapping %d: path '%s' not found", i, mapping->json_path);
            continue;
        }

        // 如果结果是数组或对象，递归展开为多个变量
        if (cJSON_IsArray(result) || cJSON_IsObject(result)) {
            // 使用 auto_discover 函数来展开，变量名前缀为 mapping->var_name
            int expanded = auto_discover_json_fields(src, result, mapping->var_name, 3, true);
            ESP_LOGD(TAG, "Mapping %d: expanded '%s' into %d variables", 
                     i, mapping->json_path, expanded);
            processed += expanded;
            cJSON_Delete(result);
            continue;
        }

        // 转换为 ts_auto_value_t（仅处理叶子节点）
        ts_auto_value_t value = {0};
        cjson_to_value(result, &value);
        cJSON_Delete(result);  // ts_jsonpath_get 返回副本，需要释放

        // 尝试更新变量
        esp_err_t ret = ts_variable_set(mapping->var_name, &value);
        
        // 如果变量不存在，自动创建
        if (ret == ESP_ERR_NOT_FOUND) {
            ts_auto_variable_t new_var = {0};
            strncpy(new_var.name, mapping->var_name, sizeof(new_var.name) - 1);
            strncpy(new_var.source_id, src->id, sizeof(new_var.source_id) - 1);
            new_var.value = value;
            new_var.flags = 0;  // 可读写
            
            ret = ts_variable_register(&new_var);
            if (ret == ESP_OK) {
                ESP_LOGD(TAG, "Auto-created variable '%s' from mapping", mapping->var_name);
                processed++;
            } else {
                ESP_LOGW(TAG, "Failed to create variable '%s': %s", 
                         mapping->var_name, esp_err_to_name(ret));
            }
        } else if (ret == ESP_OK) {
            processed++;
            ESP_LOGD(TAG, "Mapping %d: %s -> %s (type=%d)", 
                     i, mapping->json_path, mapping->var_name, value.type);
        } else {
            ESP_LOGW(TAG, "Failed to set variable '%s': %s", 
                     mapping->var_name, esp_err_to_name(ret));
        }
    }

    // 更新时间戳
    src->last_update_ms = esp_timer_get_time() / 1000;

    return processed;
}

/**
 * 批量处理多个 JSONPath 并返回结果数组
 *
 * @param json_data JSON 数据
 * @param paths JSONPath 数组
 * @param count 路径数量
 * @param results 输出值数组
 * @return 成功提取的数量
 */
static int batch_extract_json(cJSON *json_data, const char **paths, int count, ts_auto_value_t *results)
{
    if (!json_data || !paths || !results || count <= 0) {
        return 0;
    }

    int success_count = 0;

    for (int i = 0; i < count; i++) {
        results[i].type = TS_AUTO_VAL_NULL;

        if (!paths[i] || paths[i][0] == '\0') {
            continue;
        }

        cJSON *result = ts_jsonpath_get(json_data, paths[i]);
        if (result) {
            cjson_to_value(result, &results[i]);
            cJSON_Delete(result);
            success_count++;
        }
    }

    return success_count;
}

/**
 * 读取 REST 数据源
 * 支持：
 * 1. 本地 API (127.0.0.1 或 localhost) - 直接调用 ts_api_call
 * 2. 外部 API - 通过 HTTP 客户端请求
 */
static esp_err_t read_rest_source(ts_auto_source_t *src, ts_auto_value_t *value)
{
    const char *url = src->rest.url;
    const char *path = src->rest.path;

    ESP_LOGD(TAG, "Reading REST source: %s, path: %s", url, path);

    // 检测是否是本地 API 调用
    bool is_local = (strstr(url, "://127.0.0.1") != NULL || 
                     strstr(url, "://localhost") != NULL);

    cJSON *response_json = NULL;
    esp_err_t ret = ESP_OK;

    if (is_local) {
        // 本地 API 调用 - 解析路径并直接调用
        const char *api_path = strstr(url, "/api/v1/");
        if (!api_path) {
            ESP_LOGW(TAG, "Invalid local API URL: %s", url);
            return ESP_ERR_INVALID_ARG;
        }
        api_path += 8;  // 跳过 "/api/v1/"

        // 将路径转换为 API 名称: system/memory -> system.memory
        char api_name[64] = {0};
        size_t i = 0, j = 0;
        while (api_path[i] && j < sizeof(api_name) - 1) {
            if (api_path[i] == '/') {
                api_name[j++] = '.';
            } else if (api_path[i] == '?') {
                break;
            } else {
                api_name[j++] = api_path[i];
            }
            i++;
        }
        api_name[j] = '\0';

        ESP_LOGD(TAG, "Local API call: %s", api_name);

        // 调用本地 API
        ts_api_result_t api_result = {0};
        cJSON *api_params = cJSON_CreateObject();

        ret = ts_api_call(api_name, api_params, &api_result);
        cJSON_Delete(api_params);

        if (ret == ESP_OK && api_result.code == TS_API_OK && api_result.data) {
            response_json = cJSON_Duplicate(api_result.data, true);
        } else {
            ESP_LOGW(TAG, "Local API call failed: %s, code=%d", 
                     api_result.message ? api_result.message : "unknown", api_result.code);
            ret = ESP_FAIL;
        }

        // 清理
        if (api_result.message) free(api_result.message);
        if (api_result.data) cJSON_Delete(api_result.data);

    } else {
        // 外部 HTTP 请求
        char *response_buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!response_buf) {
            response_buf = malloc(4096);
        }
        if (!response_buf) {
            return ESP_ERR_NO_MEM;
        }

        int response_len = 0;

        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 10000,
            .buffer_size = 2048,
            .skip_cert_common_name_check = true,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (!client) {
            free(response_buf);
            return ESP_ERR_NO_MEM;
        }

        // 添加认证头
        if (src->rest.auth_header[0] != '\0') {
            esp_http_client_set_header(client, "Authorization", src->rest.auth_header);
        }

        ret = esp_http_client_open(client, 0);
        if (ret == ESP_OK) {
            int content_length = esp_http_client_fetch_headers(client);
            int status_code = esp_http_client_get_status_code(client);

            if (status_code == 200) {
                response_len = esp_http_client_read(client, response_buf, 4095);
                if (response_len > 0) {
                    response_buf[response_len] = '\0';
                    response_json = cJSON_Parse(response_buf);
                    if (!response_json) {
                        ESP_LOGW(TAG, "Failed to parse JSON response");
                        ret = ESP_ERR_INVALID_RESPONSE;
                    }
                } else {
                    ret = ESP_ERR_INVALID_RESPONSE;
                }
            } else {
                ESP_LOGW(TAG, "HTTP request failed with status: %d, URL: %s", status_code, url);
                ret = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "HTTP connection failed: %s", esp_err_to_name(ret));
        }

        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(response_buf);
    }

    // 处理 JSON 响应
    if (ret == ESP_OK && response_json) {
        // 方式 1：使用映射（推荐）- 与 Socket.IO 一致
        if (src->mapping_count > 0) {
            int mapped = process_source_mappings(src, response_json);
            ESP_LOGD(TAG, "REST source '%s': processed %d/%d mappings", 
                     src->id, mapped, src->mapping_count);
        }

        // 方式 2：自动发现 - 与 Socket.IO 一致
        if (src->auto_discover) {
            if (!src->auto_discovered) {
                // 首次：自动发现并创建变量
                int discovered = auto_discover_json_fields(src, response_json, NULL, 3, true);
                src->auto_discovered = true;
                ESP_LOGD(TAG, "REST source '%s': auto-discovered %d variables (first time)",
                         src->id, discovered);
            } else {
                // 后续：只更新变量值（不尝试创建新变量）
                int updated = auto_discover_json_fields(src, response_json, NULL, 3, false);
                ESP_LOGD(TAG, "REST source '%s': updated %d variables", src->id, updated);
            }
        }

        // 方式 3：使用单一路径（向后兼容，仅当无 mapping 和无 auto_discover 时）
        if (path[0] != '\0' && value && src->mapping_count == 0 && !src->auto_discover) {
            const char *actual_path = path;
            
            // 对于本地 API，response_json 已经是 data 对象
            if (is_local && strncmp(path, "data.", 5) == 0) {
                actual_path = path + 5;
                ESP_LOGD(TAG, "Local API: stripped 'data.' prefix, using path: %s", actual_path);
            }
            
            // 使用 JSONPath 提取
            cJSON *result = ts_jsonpath_get(response_json, actual_path);
            if (result) {
                cjson_to_value(result, value);
                cJSON_Delete(result);
            } else {
                // Fallback 到旧方法
                ret = extract_json_value(response_json, actual_path, value);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to extract path '%s' from response", actual_path);
                }
            }
        } else if (value && src->mapping_count == 0 && !src->auto_discover) {
            // 无路径也无映射也无自动发现，返回整个 JSON 字符串
            char *str = cJSON_PrintUnformatted(response_json);
            if (str) {
                value->type = TS_AUTO_VAL_STRING;
                strncpy(value->str_val, str, sizeof(value->str_val) - 1);
                value->str_val[sizeof(value->str_val) - 1] = '\0';
                free(str);
            }
        }
        cJSON_Delete(response_json);
    }

    return ret;
}

/**
 * 读取变量数据源
 * 
 * 统一使用 ts_automation 变量系统（ts_variable API）
 * 
 * 两种模式：
 * 1. 监视指令变量：基于前缀读取 SSH 命令执行结果
 * 2. 读取单个变量：直接获取指定变量的值
 */
static esp_err_t read_variable_source(ts_auto_source_t *src, ts_auto_value_t *value)
{
    const char *var_prefix = src->variable.var_prefix;
    const char *var_name = src->variable.var_name;
    bool watch_all = src->variable.watch_all;

    // 模式 1：监视指令变量（基于前缀）
    // 数据源只读取已有变量，不执行 SSH 命令
    // SSH 指令由其他机制触发执行（手动、定时任务等）
    if (var_prefix[0] != '\0') {
        int count = 0;
        
        // 从 var_prefix 提取命令别名（去掉末尾的 "."）
        char alias[TS_AUTO_NAME_MAX_LEN];
        strncpy(alias, var_prefix, sizeof(alias) - 1);
        alias[sizeof(alias) - 1] = '\0';
        size_t len = strlen(alias);
        if (len > 0 && alias[len - 1] == '.') {
            alias[len - 1] = '\0';
        }
        
        // 读取 SSH 命令执行后产生的标准变量（使用 ts_automation 系统）
        const char *suffixes[] = {"status", "exit_code", "extracted", "expect_matched", 
                                   "fail_matched", "host", "timestamp", "output"};
        for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
            char full_name[96];
            snprintf(full_name, sizeof(full_name), "%s.%s", alias, suffixes[i]);
            
            // 直接从 ts_automation 变量系统读取
            ts_auto_value_t var_value = {0};
            if (ts_variable_get(full_name, &var_value) == ESP_OK) {
                // 如果数据源 ID 与变量前缀不同，则复制到数据源命名空间
                if (strcmp(src->id, alias) != 0) {
                    char auto_var_name[128];
                    snprintf(auto_var_name, sizeof(auto_var_name), "%s.%s", src->id, suffixes[i]);
                    ts_variable_set(auto_var_name, &var_value);
                }
                count++;
            }
        }
        
        ESP_LOGD(TAG, "Variable source '%s': read %d variables from prefix '%s'",
                 src->id, count, alias);
        
        if (count > 0) {
            // 返回主要状态变量
            char status_var[96];
            snprintf(status_var, sizeof(status_var), "%s.status", alias);
            ts_auto_value_t status_value = {0};
            if (ts_variable_get(status_var, &status_value) == ESP_OK) {
                *value = status_value;
            } else {
                value->type = TS_AUTO_VAL_INT;
                value->int_val = count;
            }
            return ESP_OK;
        }
        
        // 变量不存在（指令可能尚未执行）
        ESP_LOGD(TAG, "Variable source '%s': no variables found for prefix '%s' (command may not have been executed yet)",
                 src->id, alias);
        return ESP_ERR_NOT_FOUND;
    }

    // 模式 2：读取单个变量（直接使用 ts_automation 系统）
    if (var_name[0] != '\0') {
        ts_auto_value_t var_value = {0};
        esp_err_t ret = ts_variable_get(var_name, &var_value);
        
        if (ret == ESP_OK) {
            *value = var_value;
            
            // 同时更新自动化变量（如果有映射）
            if (src->mapping_count > 0) {
                ts_variable_set(src->mappings[0].var_name, &var_value);
            }
            
            return ESP_OK;
        }
        return ret;
    }

    // 模式 3：监视前缀下的所有变量（使用 ts_automation 枚举）
    if (watch_all && var_prefix[0] != '\0') {
        int count = 0;
        
        // 使用 ts_variable_enumerate 遍历所有匹配前缀的变量
        ts_variable_iterate_ctx_t ctx = {0};
        ts_auto_variable_t var;
        
        while (ts_variable_iterate(&ctx, &var) == ESP_OK) {
            // 检查变量名是否以 var_prefix 开头
            if (strncmp(var.name, var_prefix, strlen(var_prefix)) == 0) {
                // 如果数据源 ID 与前缀不同，复制到数据源命名空间
                if (strncmp(src->id, var_prefix, strlen(src->id)) != 0) {
                    char auto_var_name[128];
                    const char *suffix = var.name + strlen(var_prefix);
                    snprintf(auto_var_name, sizeof(auto_var_name), "%s%s", src->id, suffix);
                    ts_variable_set(auto_var_name, &var.value);
                }
                count++;
            }
        }
        
        ESP_LOGD(TAG, "Variable source '%s': synced %d variables from prefix '%s'",
                 src->id, count, var_prefix);
        
        if (count > 0) {
            value->type = TS_AUTO_VAL_INT;
            value->int_val = count;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/*===========================================================================*/
/*                              初始化                                        */
/*===========================================================================*/

esp_err_t ts_source_manager_init(void)
{
    if (s_src_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing source manager (max %d)", 
             CONFIG_TS_AUTOMATION_MAX_SOURCES);

    // 分配源数组（使用 PSRAM）
    s_src_ctx.capacity = CONFIG_TS_AUTOMATION_MAX_SOURCES;
    size_t alloc_size = s_src_ctx.capacity * sizeof(ts_auto_source_t);

    s_src_ctx.sources = heap_caps_malloc(alloc_size, MALLOC_CAP_SPIRAM);
    if (!s_src_ctx.sources) {
        s_src_ctx.sources = malloc(alloc_size);
        if (!s_src_ctx.sources) {
            ESP_LOGE(TAG, "Failed to allocate source storage");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGW(TAG, "Using DRAM for source storage");
    }

    memset(s_src_ctx.sources, 0, alloc_size);

    // 创建互斥锁
    s_src_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_src_ctx.mutex) {
        free(s_src_ctx.sources);
        s_src_ctx.sources = NULL;
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_src_ctx.stats, 0, sizeof(s_src_ctx.stats));
    s_src_ctx.count = 0;
    s_src_ctx.running = false;
    s_src_ctx.initialized = true;

    // 从 NVS 加载已保存的数据源
    load_sources_from_nvs();

    ESP_LOGI(TAG, "Source manager initialized with %d sources", s_src_ctx.count);
    return ESP_OK;
}

esp_err_t ts_source_manager_deinit(void)
{
    if (!s_src_ctx.initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing source manager");

    // 先停止
    ts_source_stop_all();

    if (s_src_ctx.mutex) {
        vSemaphoreDelete(s_src_ctx.mutex);
        s_src_ctx.mutex = NULL;
    }

    if (s_src_ctx.sources) {
        if (heap_caps_get_allocated_size(s_src_ctx.sources) > 0) {
            heap_caps_free(s_src_ctx.sources);
        } else {
            free(s_src_ctx.sources);
        }
        s_src_ctx.sources = NULL;
    }

    s_src_ctx.count = 0;
    s_src_ctx.capacity = 0;
    s_src_ctx.initialized = false;

    return ESP_OK;
}

/*===========================================================================*/
/*                              源管理                                        */
/*===========================================================================*/

esp_err_t ts_source_register(const ts_auto_source_t *source)
{
    if (!source || !source->id[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_src_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    // 检查是否已存在
    int idx = find_source_index(source->id);
    if (idx >= 0) {
        // 更新现有源，但保留运行时状态
        bool prev_auto_discovered = s_src_ctx.sources[idx].auto_discovered;
        memcpy(&s_src_ctx.sources[idx], source, sizeof(ts_auto_source_t));
        s_src_ctx.sources[idx].auto_discovered = prev_auto_discovered;  // 保留发现状态
        xSemaphoreGive(s_src_ctx.mutex);
        ESP_LOGD(TAG, "Updated source: %s", source->id);
        // 保存到 NVS
        save_sources_to_nvs();
        return ESP_OK;
    }

    // 检查容量
    if (s_src_ctx.count >= s_src_ctx.capacity) {
        xSemaphoreGive(s_src_ctx.mutex);
        ESP_LOGE(TAG, "Source storage full");
        return ESP_ERR_NO_MEM;
    }

    // 添加新源
    memcpy(&s_src_ctx.sources[s_src_ctx.count], source, sizeof(ts_auto_source_t));
    s_src_ctx.sources[s_src_ctx.count].auto_discovered = false;  // 新源始终从未发现开始
    s_src_ctx.count++;

    xSemaphoreGive(s_src_ctx.mutex);

    // 保存到 NVS
    save_sources_to_nvs();

    ESP_LOGI(TAG, "Registered source: %s (%s, interval %"PRIu32"ms)",
             source->id, source->label, source->poll_interval_ms);

    // 如果是 Socket.IO 源且已启用，自动连接
    if (source->type == TS_AUTO_SRC_SOCKETIO && source->enabled) {
        ESP_LOGI(TAG, "Auto-connecting Socket.IO source: %s", source->id);
        ts_source_sio_connect(source->id);
    }

    return ESP_OK;
}

esp_err_t ts_source_unregister(const char *id)
{
    if (!id) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_src_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    int idx = find_source_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_src_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // 获取类型，用于后续断开连接
    ts_auto_source_type_t type = s_src_ctx.sources[idx].type;

    // 移动后续元素
    if (idx < s_src_ctx.count - 1) {
        memmove(&s_src_ctx.sources[idx],
                &s_src_ctx.sources[idx + 1],
                (s_src_ctx.count - idx - 1) * sizeof(ts_auto_source_t));
    }
    s_src_ctx.count--;

    xSemaphoreGive(s_src_ctx.mutex);

    // 如果是 Socket.IO 源，断开连接
    if (type == TS_AUTO_SRC_SOCKETIO) {
        ESP_LOGI(TAG, "Disconnecting Socket.IO source before unregister: %s", id);
        ts_source_sio_disconnect(id);
    }

    // 删除该数据源关联的所有变量
    int removed = ts_variable_unregister_by_source(id);
    ESP_LOGI(TAG, "Removed %d variables associated with source: %s", removed, id);

    // 保存到 NVS
    save_sources_to_nvs();

    ESP_LOGI(TAG, "Unregistered source: %s", id);
    return ESP_OK;
}

esp_err_t ts_source_enable(const char *id)
{
    if (!id || !s_src_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    int idx = find_source_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_src_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    s_src_ctx.sources[idx].enabled = true;
    ts_auto_source_type_t type = s_src_ctx.sources[idx].type;

    xSemaphoreGive(s_src_ctx.mutex);

    // 如果是 Socket.IO 源，自动连接
    if (type == TS_AUTO_SRC_SOCKETIO) {
        ESP_LOGI(TAG, "Auto-connecting Socket.IO source: %s", id);
        ts_source_sio_connect(id);
    }

    return ESP_OK;
}

esp_err_t ts_source_disable(const char *id)
{
    if (!id || !s_src_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    int idx = find_source_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_src_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    s_src_ctx.sources[idx].enabled = false;
    ts_auto_source_type_t type = s_src_ctx.sources[idx].type;

    xSemaphoreGive(s_src_ctx.mutex);

    // 如果是 Socket.IO 源，断开连接
    if (type == TS_AUTO_SRC_SOCKETIO) {
        ESP_LOGI(TAG, "Disconnecting Socket.IO source: %s", id);
        ts_source_sio_disconnect(id);
    }

    return ESP_OK;
}

const ts_auto_source_t *ts_source_get(const char *id)
{
    if (!id || !s_src_ctx.initialized) {
        return NULL;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    int idx = find_source_index(id);
    const ts_auto_source_t *src = (idx >= 0) ? &s_src_ctx.sources[idx] : NULL;

    xSemaphoreGive(s_src_ctx.mutex);
    return src;
}

ts_auto_source_t *ts_source_get_mutable(const char *id)
{
    if (!id || !s_src_ctx.initialized) {
        return NULL;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    int idx = find_source_index(id);
    ts_auto_source_t *src = (idx >= 0) ? &s_src_ctx.sources[idx] : NULL;

    xSemaphoreGive(s_src_ctx.mutex);
    return src;
}

int ts_source_count(void)
{
    if (!s_src_ctx.initialized) {
        return 0;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
    int count = s_src_ctx.count;
    xSemaphoreGive(s_src_ctx.mutex);

    return count;
}

const ts_auto_source_t *ts_source_get_by_index(int index)
{
    if (!s_src_ctx.initialized) {
        return NULL;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    const ts_auto_source_t *src = NULL;
    if (index >= 0 && index < s_src_ctx.count) {
        src = &s_src_ctx.sources[index];
    }

    xSemaphoreGive(s_src_ctx.mutex);
    return src;
}

esp_err_t ts_source_get_by_index_copy(int index, ts_auto_source_t *out_source)
{
    if (!out_source) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_src_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (index >= 0 && index < s_src_ctx.count) {
        memcpy(out_source, &s_src_ctx.sources[index], sizeof(ts_auto_source_t));
        ret = ESP_OK;
    }

    xSemaphoreGive(s_src_ctx.mutex);
    return ret;
}

/*===========================================================================*/
/*                              数据采集                                      */
/*===========================================================================*/

esp_err_t ts_source_start_all(void)
{
    if (!s_src_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting all data sources");

    // 等待网络就绪（最多等待 10 秒）
    int wait_count = 0;
    const int max_wait = 100;  // 100 * 100ms = 10s
    while (wait_count < max_wait) {
        if (ts_net_manager_is_ready(TS_NET_IF_ETH) || 
            ts_net_manager_is_ready(TS_NET_IF_WIFI_STA)) {
            ESP_LOGI(TAG, "Network ready, starting external sources");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
        if (wait_count % 10 == 0) {
            ESP_LOGD(TAG, "Waiting for network... (%d/%d)", wait_count, max_wait);
        }
    }
    
    if (wait_count >= max_wait) {
        ESP_LOGW(TAG, "Network not ready after %d seconds, starting sources anyway", max_wait / 10);
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    // 收集需要启动的 Socket.IO 源（在锁内收集，锁外连接）
    char sio_sources[CONFIG_TS_AUTOMATION_MAX_SOURCES][64];
    int sio_count = 0;

    // 初始化所有数据源连接
    for (int i = 0; i < s_src_ctx.count; i++) {
        if (!s_src_ctx.sources[i].enabled) {
            continue;
        }
        
        if (s_src_ctx.sources[i].type == TS_AUTO_SRC_WEBSOCKET) {
            // TODO: 启动 WebSocket 连接
            ESP_LOGD(TAG, "Starting WS source: %s", s_src_ctx.sources[i].id);
        } else if (s_src_ctx.sources[i].type == TS_AUTO_SRC_SOCKETIO) {
            // 记录 Socket.IO 源 ID，稍后连接
            if (sio_count < CONFIG_TS_AUTOMATION_MAX_SOURCES) {
                strncpy(sio_sources[sio_count], s_src_ctx.sources[i].id, sizeof(sio_sources[0]) - 1);
                sio_sources[sio_count][sizeof(sio_sources[0]) - 1] = '\0';
                sio_count++;
            }
        }
    }

    s_src_ctx.running = true;

    xSemaphoreGive(s_src_ctx.mutex);

    // 在锁外启动 Socket.IO 连接（避免死锁）
    for (int i = 0; i < sio_count; i++) {
        ESP_LOGI(TAG, "Starting Socket.IO source: %s", sio_sources[i]);
        ts_source_sio_connect(sio_sources[i]);
    }

    return ESP_OK;
}

esp_err_t ts_source_stop_all(void)
{
    if (!s_src_ctx.initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping all data sources");

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    s_src_ctx.running = false;

    // 断开所有 WebSocket 连接
    for (int i = 0; i < s_src_ctx.count; i++) {
        if (s_src_ctx.sources[i].type == TS_AUTO_SRC_WEBSOCKET &&
            s_src_ctx.sources[i].connected) {
            // TODO: 断开 WebSocket
            s_src_ctx.sources[i].connected = false;
        }
    }

    xSemaphoreGive(s_src_ctx.mutex);

    return ESP_OK;
}

esp_err_t ts_source_poll(const char *id)
{
    if (!id || !s_src_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    int idx = find_source_index(id);
    if (idx < 0) {
        xSemaphoreGive(s_src_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }

    ts_auto_source_t *src = &s_src_ctx.sources[idx];
    ts_auto_value_t value = {0};
    esp_err_t ret = ESP_OK;

    s_src_ctx.stats.total_polls++;

    switch (src->type) {
        case TS_AUTO_SRC_WEBSOCKET:
        case TS_AUTO_SRC_SOCKETIO:
            // WebSocket 和 Socket.IO 是推送模式，不需要轮询
            ret = ESP_OK;
            break;

        case TS_AUTO_SRC_REST:
            // REST API 轮询
            xSemaphoreGive(s_src_ctx.mutex);  // 释放锁，HTTP 请求可能耗时
            ret = read_rest_source(src, &value);
            xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);  // 重新获取锁
            s_src_ctx.stats.rest_requests++;
            break;

        case TS_AUTO_SRC_VARIABLE:
            // 变量系统轮询
            ret = read_variable_source(src, &value);
            break;

        default:
            ret = ESP_ERR_NOT_SUPPORTED;
    }

    if (ret == ESP_OK) {
        src->last_value = value;
        src->last_update_ms = esp_timer_get_time() / 1000;
        s_src_ctx.stats.successful_polls++;

        // 更新关联变量
        update_source_variable(src);
    } else {
        s_src_ctx.stats.failed_polls++;
    }

    xSemaphoreGive(s_src_ctx.mutex);

    return ret;
}

int ts_source_poll_all(void)
{
    if (!s_src_ctx.initialized || !s_src_ctx.running) {
        return 0;
    }

    int polled = 0;
    int64_t now_ms = esp_timer_get_time() / 1000;

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    for (int i = 0; i < s_src_ctx.count; i++) {
        ts_auto_source_t *src = &s_src_ctx.sources[i];

        if (!src->enabled) {
            continue;
        }

        // WebSocket 和 Socket.IO 是推送模式，跳过轮询
        if (src->type == TS_AUTO_SRC_WEBSOCKET || src->type == TS_AUTO_SRC_SOCKETIO) {
            continue;
        }

        // 检查是否到达轮询时间
        if (src->poll_interval_ms > 0) {
            int64_t elapsed = now_ms - src->last_update_ms;
            if (elapsed < src->poll_interval_ms) {
                continue;
            }
        }

        // 释放锁后轮询（避免长时间持有锁）
        xSemaphoreGive(s_src_ctx.mutex);

        esp_err_t ret = ts_source_poll(src->id);
        if (ret == ESP_OK) {
            polled++;
        }

        xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
    }

    xSemaphoreGive(s_src_ctx.mutex);

    return polled;
}

/*===========================================================================*/
/*                              WebSocket 源                                  */
/*===========================================================================*/

esp_err_t ts_source_ws_connect(const char *id)
{
    // TODO: 实现 WebSocket 连接
    ESP_LOGW(TAG, "WebSocket connect not implemented: %s", id);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ts_source_ws_disconnect(const char *id)
{
    // TODO: 实现 WebSocket 断开
    return ESP_ERR_NOT_SUPPORTED;
}

bool ts_source_ws_is_connected(const char *id)
{
    if (!id || !s_src_ctx.initialized) {
        return false;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    int idx = find_source_index(id);
    bool connected = false;
    if (idx >= 0 && s_src_ctx.sources[idx].type == TS_AUTO_SRC_WEBSOCKET) {
        connected = s_src_ctx.sources[idx].connected;
    }

    xSemaphoreGive(s_src_ctx.mutex);
    return connected;
}

/*===========================================================================*/
/*                              Socket.IO 源                                  */
/*===========================================================================*/

/**
 * 查找 Socket.IO 连接
 */
static sio_connection_t *sio_find_connection(const char *source_id)
{
    for (int i = 0; i < s_sio_ctx.count; i++) {
        if (strcmp(s_sio_ctx.connections[i].source_id, source_id) == 0) {
            return &s_sio_ctx.connections[i];
        }
    }
    return NULL;
}

/**
 * 释放 Socket.IO 连接槽
 */
static void sio_release_connection(const char *source_id)
{
    if (!s_sio_ctx.mutex) return;

    xSemaphoreTake(s_sio_ctx.mutex, portMAX_DELAY);

    for (int i = 0; i < s_sio_ctx.count; i++) {
        if (strcmp(s_sio_ctx.connections[i].source_id, source_id) == 0) {
            // 移动后面的连接填补空位
            for (int j = i; j < s_sio_ctx.count - 1; j++) {
                s_sio_ctx.connections[j] = s_sio_ctx.connections[j + 1];
            }
            s_sio_ctx.count--;
            ESP_LOGD(TAG, "SIO [%s] connection slot released", source_id);
            break;
        }
    }

    xSemaphoreGive(s_sio_ctx.mutex);
}

/**
 * 处理 Socket.IO 事件消息
 * 格式：42["event_name", {...data...}]
 */
static void sio_handle_event_message(sio_connection_t *conn, const char *msg)
{
    if (!msg || strncmp(msg, SIO_EVENT_PREFIX, 2) != 0) {
        return;
    }

    const char *json_start = strchr(msg, '[');
    if (!json_start) return;

    cJSON *array = cJSON_Parse(json_start);
    if (!array || !cJSON_IsArray(array)) {
        if (array) cJSON_Delete(array);
        return;
    }

    cJSON *event_name = cJSON_GetArrayItem(array, 0);
    cJSON *event_data = cJSON_GetArrayItem(array, 1);

    if (!event_name || !cJSON_IsString(event_name)) {
        cJSON_Delete(array);
        return;
    }

    ESP_LOGD(TAG, "SIO event: %s from source %s", event_name->valuestring, conn->source_id);

    // 查找关联的数据源
    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
    int idx = find_source_index(conn->source_id);
    if (idx >= 0) {
        ts_auto_source_t *src = &s_src_ctx.sources[idx];

        // 检查是否是我们订阅的事件
        if (strcmp(event_name->valuestring, src->socketio.event) == 0 && event_data) {
            ESP_LOGD(TAG, "Matched subscribed event '%s', updating source value...", event_name->valuestring);

            // 更新源的 last_value
            // 由于 str_val 只有 64 字节，存储摘要而非完整 JSON
            char *json_str = cJSON_PrintUnformatted(event_data);
            if (json_str) {
                size_t json_len = strlen(json_str);
                src->last_value.type = TS_AUTO_VAL_STRING;
                
                // 存储格式：前50字符... (总大小)
                if (json_len > 50) {
                    snprintf(src->last_value.str_val, sizeof(src->last_value.str_val),
                             "%.45s...(%zu)", json_str, json_len);
                } else {
                    strncpy(src->last_value.str_val, json_str, sizeof(src->last_value.str_val) - 1);
                    src->last_value.str_val[sizeof(src->last_value.str_val) - 1] = '\0';
                }
                ESP_LOGD(TAG, "Source '%s' last_value: %s", src->id, src->last_value.str_val);

                // 处理数据到变量 - 所有处理都在主任务中执行，避免栈溢出
                // （mapping 可能需要递归展开数组/对象，auto_discover 也是递归的）
                if (src->mapping_count > 0 || src->auto_discover) {
                    if (xSemaphoreTake(conn->pending_mutex, 0) == pdTRUE) {
                        if (conn->pending_json) {
                            free(conn->pending_json);  // 释放旧数据
                        }
                        conn->pending_json = json_str;  // 转移所有权
                        json_str = NULL;
                        xSemaphoreGive(conn->pending_mutex);
                        ESP_LOGD(TAG, "SIO source '%s': queued JSON for processing", src->id);
                    } else {
                        ESP_LOGW(TAG, "SIO source '%s': pending mutex busy, dropping data", src->id);
                    }
                }
                
                if (json_str) free(json_str);
            }

            // 更新源状态
            src->last_update_ms = esp_timer_get_time() / 1000;
            src->connected = true;
        }
    }
    xSemaphoreGive(s_src_ctx.mutex);

    cJSON_Delete(array);
}

/**
 * Socket.IO WebSocket 事件处理器
 */
static void sio_websocket_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    sio_connection_t *conn = (sio_connection_t *)arg;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGD(TAG, "SIO [%s] WebSocket connected, sending probe", conn->source_id);
            conn->connected = true;
            conn->last_message_ms = esp_timer_get_time() / 1000;
            esp_websocket_client_send_text(conn->client, SIO_PROBE_MSG, strlen(SIO_PROBE_MSG), portMAX_DELAY);
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "SIO [%s] WebSocket disconnected", conn->source_id);
            conn->connected = false;
            conn->upgraded = false;
            // 更新源状态
            xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
            int idx = find_source_index(conn->source_id);
            if (idx >= 0) {
                s_src_ctx.sources[idx].connected = false;
            }
            xSemaphoreGive(s_src_ctx.mutex);
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->data_len > 0 && data->data_ptr) {
                conn->last_message_ms = esp_timer_get_time() / 1000;

                // 检查是否是分片消息
                bool is_complete = (data->payload_len == 0) || 
                                   (data->payload_offset + data->data_len >= data->payload_len);

                // 如果是分片消息，需要缓冲
                if (!is_complete || conn->msg_buf_len > 0) {
                    // 分配或扩展缓冲区
                    if (!conn->msg_buf) {
                        conn->msg_buf = heap_caps_malloc(SIO_MSG_BUF_SIZE, MALLOC_CAP_SPIRAM);
                        if (!conn->msg_buf) conn->msg_buf = malloc(SIO_MSG_BUF_SIZE);
                        if (!conn->msg_buf) {
                            ESP_LOGE(TAG, "SIO [%s] failed to alloc msg buffer", conn->source_id);
                            break;
                        }
                        conn->msg_buf_len = 0;
                    }

                    // 追加数据到缓冲区
                    size_t copy_len = data->data_len;
                    if (conn->msg_buf_len + copy_len >= SIO_MSG_BUF_SIZE) {
                        copy_len = SIO_MSG_BUF_SIZE - conn->msg_buf_len - 1;
                        ESP_LOGW(TAG, "SIO [%s] msg buffer overflow, truncating", conn->source_id);
                    }
                    memcpy(conn->msg_buf + conn->msg_buf_len, data->data_ptr, copy_len);
                    conn->msg_buf_len += copy_len;
                    conn->msg_buf[conn->msg_buf_len] = '\0';

                    ESP_LOGD(TAG, "SIO [%s] buffered %d bytes, total %zu", 
                             conn->source_id, data->data_len, conn->msg_buf_len);

                    if (!is_complete) {
                        // 还有更多分片，等待
                        break;
                    }

                    // 消息完整，处理缓冲区中的消息
                    ESP_LOGD(TAG, "SIO [%s] complete msg: %zu bytes", conn->source_id, conn->msg_buf_len);
                    
                    // 处理事件消息
                    if (strncmp(conn->msg_buf, SIO_EVENT_PREFIX, 2) == 0) {
                        sio_handle_event_message(conn, conn->msg_buf);
                    }

                    // 清空缓冲区
                    conn->msg_buf_len = 0;
                } else {
                    // 完整的单帧消息
                    char *msg = strndup(data->data_ptr, data->data_len);
                    if (msg) {
                        ESP_LOGD(TAG, "SIO [%s] recv: %.100s%s", conn->source_id, msg,
                                 strlen(msg) > 100 ? "..." : "");

                        // 处理 Socket.IO 协议消息
                        if (strcmp(msg, "3probe") == 0) {
                            ESP_LOGD(TAG, "SIO [%s] probe response, sending upgrade", conn->source_id);
                            esp_websocket_client_send_text(conn->client, SIO_UPGRADE_MSG,
                                                           strlen(SIO_UPGRADE_MSG), portMAX_DELAY);
                        }
                        else if (strcmp(msg, "6") == 0) {
                            // 收到 NOOP (6) 表示升级完成，发送 CONNECT 到默认命名空间
                            ESP_LOGD(TAG, "SIO [%s] upgrade ack, sending CONNECT", conn->source_id);
                            esp_websocket_client_send_text(conn->client, "40", 2, portMAX_DELAY);
                        }
                        else if (strncmp(msg, "40{", 3) == 0 || strcmp(msg, "40") == 0) {
                            // 收到 CONNECT 响应，连接完成
                            conn->upgraded = true;
                            xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
                            int i = find_source_index(conn->source_id);
                            if (i >= 0) {
                                s_src_ctx.sources[i].connected = true;
                            }
                            xSemaphoreGive(s_src_ctx.mutex);
                            ESP_LOGD(TAG, "SIO [%s] connected to namespace", conn->source_id);
                        }
                        else if (strcmp(msg, SIO_PING_MSG) == 0) {
                            ESP_LOGD(TAG, "SIO [%s] ping, sending pong", conn->source_id);
                            esp_websocket_client_send_text(conn->client, SIO_PONG_MSG,
                                                           strlen(SIO_PONG_MSG), portMAX_DELAY);
                        }
                        else if (strncmp(msg, SIO_EVENT_PREFIX, 2) == 0) {
                            sio_handle_event_message(conn, msg);
                        }

                        free(msg);
                    }
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "SIO [%s] WebSocket error", conn->source_id);
            break;
    }
}

/**
 * Socket.IO HTTP 握手获取 session ID
 */
static esp_err_t sio_http_handshake(const char *base_url, char *sid_out, size_t sid_len)
{
    char handshake_url[256];
    snprintf(handshake_url, sizeof(handshake_url),
             "%s/socket.io/?EIO=4&transport=polling", base_url);

    char *response_buf = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (!response_buf) response_buf = malloc(1024);
    if (!response_buf) return ESP_ERR_NO_MEM;

    esp_http_client_config_t config = {
        .url = handshake_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t ret = ESP_FAIL;

    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int len = esp_http_client_read(client, response_buf, 1023);
        if (len > 0) {
            response_buf[len] = '\0';
            ESP_LOGD(TAG, "SIO handshake response: %s", response_buf);

            // 响应格式：0{"sid":"xxx","upgrades":["websocket"],...}
            char *json_start = strchr(response_buf, '{');
            if (json_start) {
                cJSON *root = cJSON_Parse(json_start);
                if (root) {
                    cJSON *sid = cJSON_GetObjectItem(root, "sid");
                    if (sid && cJSON_IsString(sid)) {
                        strncpy(sid_out, sid->valuestring, sid_len - 1);
                        ret = ESP_OK;
                        ESP_LOGD(TAG, "SIO got session ID: %s", sid_out);
                    }
                    cJSON_Delete(root);
                }
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(response_buf);

    return ret;
}

/**
 * Socket.IO 连接任务（带自动重连）
 * 
 * 连接流程：
 * 1. 等待网络就绪
 * 2. 尝试 HTTP 握手，失败则延迟重试（指数退避）
 * 3. 建立 WebSocket 连接
 * 4. 保持连接，处理数据
 * 5. 断开后自动重连
 */
static void sio_connection_task(void *arg)
{
    sio_connection_t *conn = (sio_connection_t *)arg;

    // 保存 source_id 副本用于清理
    char source_id_copy[TS_AUTO_NAME_MAX_LEN];
    strncpy(source_id_copy, conn->source_id, sizeof(source_id_copy) - 1);
    source_id_copy[sizeof(source_id_copy) - 1] = '\0';

    // 重连参数
    const uint32_t INITIAL_RETRY_DELAY_MS = 5000;   // 初始重试延迟 5秒
    const uint32_t MAX_RETRY_DELAY_MS = 60000;      // 最大重试延迟 60秒
    uint32_t retry_delay_ms = INITIAL_RETRY_DELAY_MS;
    int retry_count = 0;
    int idx = -1;  // 源索引，在函数级别声明

connection_retry:
    while (!conn->should_stop) {
        // 检查源是否仍然存在
        xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
        idx = find_source_index(conn->source_id);
        if (idx < 0) {
            xSemaphoreGive(s_src_ctx.mutex);
            ESP_LOGE(TAG, "SIO source not found: %s", conn->source_id);
            goto task_exit;
        }
        char base_url[TS_AUTO_PATH_MAX_LEN];
        strncpy(base_url, s_src_ctx.sources[idx].socketio.url, sizeof(base_url) - 1);
        
        // 获取配置的重连间隔（如果有）
        uint32_t configured_reconnect_ms = s_src_ctx.sources[idx].socketio.reconnect_ms;
        if (configured_reconnect_ms > 0) {
            // 使用配置值作为初始延迟
            if (retry_count == 0) {
                retry_delay_ms = configured_reconnect_ms;
            }
        }
        xSemaphoreGive(s_src_ctx.mutex);

        if (retry_count > 0) {
            ESP_LOGD(TAG, "SIO [%s] retry #%d, waiting %lu ms before reconnect...", 
                     conn->source_id, retry_count, (unsigned long)retry_delay_ms);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
            
            // 指数退避，最大不超过 MAX_RETRY_DELAY_MS
            retry_delay_ms = (retry_delay_ms * 2 > MAX_RETRY_DELAY_MS) 
                           ? MAX_RETRY_DELAY_MS 
                           : retry_delay_ms * 2;
        }

        ESP_LOGD(TAG, "SIO [%s] attempting connection to %s", conn->source_id, base_url);

        // 1. HTTP 握手获取 session ID
        if (sio_http_handshake(base_url, conn->session_id, sizeof(conn->session_id)) != ESP_OK) {
            ESP_LOGW(TAG, "SIO [%s] handshake failed (host unreachable?), will retry", conn->source_id);
            retry_count++;
            continue;  // 重试
        }
        
        // 握手成功，重置重试计数
        retry_count = 0;
        retry_delay_ms = INITIAL_RETRY_DELAY_MS;
        
        // 跳出重试循环，继续建立 WebSocket 连接
        break;
    }
    
    if (conn->should_stop) {
        goto task_exit;
    }

    // 确保 pending_mutex 存在（首次连接或重连）
    if (!conn->pending_mutex) {
        conn->pending_mutex = xSemaphoreCreateMutex();
        if (!conn->pending_mutex) {
            ESP_LOGE(TAG, "SIO [%s] failed to create pending mutex", conn->source_id);
            retry_count++;
            goto connection_retry;
        }
    }

    // 重新获取 base_url（需要新的变量名避免作用域冲突）
    char ws_base_url[TS_AUTO_PATH_MAX_LEN];
    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
    idx = find_source_index(conn->source_id);
    if (idx < 0) {
        xSemaphoreGive(s_src_ctx.mutex);
        goto task_exit;
    }
    strncpy(ws_base_url, s_src_ctx.sources[idx].socketio.url, sizeof(ws_base_url) - 1);
    xSemaphoreGive(s_src_ctx.mutex);

    // 2. 构建 WebSocket URL
    char ws_url[256];
    // 将 http:// 替换为 ws://
    const char *url_path = strstr(ws_base_url, "://");
    if (url_path) {
        url_path += 3;  // 跳过 "://"
        snprintf(ws_url, sizeof(ws_url),
                 "ws://%s/socket.io/?EIO=4&transport=websocket&sid=%s",
                 url_path, conn->session_id);
    } else {
        snprintf(ws_url, sizeof(ws_url),
                 "ws://%s/socket.io/?EIO=4&transport=websocket&sid=%s",
                 ws_base_url, conn->session_id);
    }

    ESP_LOGD(TAG, "SIO [%s] WebSocket URL: %s", conn->source_id, ws_url);

    // 3. 创建 WebSocket 连接
    esp_websocket_client_config_t ws_cfg = {
        .uri = ws_url,
        .buffer_size = 2048,
    };

    conn->client = esp_websocket_client_init(&ws_cfg);
    if (!conn->client) {
        ESP_LOGE(TAG, "SIO [%s] failed to init WebSocket client", conn->source_id);
        sio_release_connection(source_id_copy);
        vTaskDelete(NULL);
        return;
    }

    esp_websocket_register_events(conn->client, WEBSOCKET_EVENT_ANY,
                                  sio_websocket_event_handler, conn);

    if (esp_websocket_client_start(conn->client) != ESP_OK) {
        ESP_LOGE(TAG, "SIO [%s] failed to start WebSocket client", conn->source_id);
        esp_websocket_client_destroy(conn->client);
        conn->client = NULL;
        sio_release_connection(source_id_copy);
        vTaskDelete(NULL);
        return;
    }

    // 4. 主循环：保持连接，处理心跳超时和待处理数据
    while (!conn->should_stop) {
        vTaskDelay(pdMS_TO_TICKS(200));  // 200ms 检查周期

        // 处理待处理的自动发现数据（在此任务中执行，有足够栈空间）
        if (xSemaphoreTake(conn->pending_mutex, 0) == pdTRUE) {
            if (conn->pending_json) {
                char *json_to_process = conn->pending_json;
                conn->pending_json = NULL;
                xSemaphoreGive(conn->pending_mutex);

                // 解析 JSON
                cJSON *parsed = cJSON_Parse(json_to_process);
                if (parsed) {
                    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
                    int idx = find_source_index(conn->source_id);
                    if (idx >= 0) {
                        ts_auto_source_t *src = &s_src_ctx.sources[idx];
                        
                        // 先处理手动配置的映射（在主任务中执行，有足够栈空间）
                        if (src->mapping_count > 0) {
                            int mapped = process_source_mappings(src, parsed);
                            ESP_LOGD(TAG, "SIO source '%s': processed %d/%d mappings", 
                                     src->id, mapped, src->mapping_count);
                        }
                        
                        // 如果启用了自动发现
                        if (src->auto_discover) {
                            if (!conn->auto_discovered) {
                                // 首次：自动发现并创建变量
                                int discovered = auto_discover_json_fields(src, parsed, NULL, 3, true);
                                conn->auto_discovered = true;
                                ESP_LOGD(TAG, "SIO source '%s': auto-discovered %d variables (first time)",
                                         src->id, discovered);
                            } else {
                                // 后续：只更新变量值（不尝试创建新变量）
                                int updated = auto_discover_json_fields(src, parsed, NULL, 3, false);
                                ESP_LOGD(TAG, "SIO source '%s': updated %d variables", src->id, updated);
                            }
                        }
                    }
                    xSemaphoreGive(s_src_ctx.mutex);
                    cJSON_Delete(parsed);
                }
                free(json_to_process);
            } else {
                xSemaphoreGive(conn->pending_mutex);
            }
        }

        // 检查心跳超时（30秒无消息则重连）
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (conn->upgraded && (now_ms - conn->last_message_ms > 30000)) {
            ESP_LOGW(TAG, "SIO [%s] heartbeat timeout, reconnecting...", conn->source_id);
            break;
        }
    }

    // 清理 WebSocket 资源（但保留连接槽以便重连）
    if (conn->client) {
        esp_websocket_client_stop(conn->client);
        esp_websocket_client_destroy(conn->client);
        conn->client = NULL;
    }
    if (conn->msg_buf) {
        free(conn->msg_buf);
        conn->msg_buf = NULL;
        conn->msg_buf_len = 0;
    }
    if (conn->pending_json) {
        free(conn->pending_json);
        conn->pending_json = NULL;
    }
    conn->connected = false;
    conn->upgraded = false;

    // 检查是否应该重连
    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
    idx = find_source_index(conn->source_id);
    bool should_reconnect = (idx >= 0);  // 源仍然存在则重连
    xSemaphoreGive(s_src_ctx.mutex);

    if (should_reconnect && !conn->should_stop) {
        ESP_LOGD(TAG, "SIO [%s] will reconnect...", source_id_copy);
        retry_count++;  // 增加重试计数，触发延迟
        goto connection_retry;
    }

task_exit:
    // 最终清理
    if (conn->pending_mutex) {
        vSemaphoreDelete(conn->pending_mutex);
        conn->pending_mutex = NULL;
    }

    ESP_LOGI(TAG, "SIO [%s] connection task ended", source_id_copy);
    sio_release_connection(source_id_copy);
    vTaskDelete(NULL);
}

/**
 * @brief 连接 Socket.IO 数据源
 */
esp_err_t ts_source_sio_connect(const char *id)
{
    if (!id || !s_src_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查源是否存在且是 Socket.IO 类型
    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
    int idx = find_source_index(id);
    if (idx < 0 || s_src_ctx.sources[idx].type != TS_AUTO_SRC_SOCKETIO) {
        xSemaphoreGive(s_src_ctx.mutex);
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreGive(s_src_ctx.mutex);

    // 检查是否已连接
    sio_connection_t *conn = sio_find_connection(id);
    if (conn) {
        if (conn->task_handle) {
            // 任务仍在运行
            ESP_LOGW(TAG, "SIO [%s] already connected", id);
            return ESP_OK;
        } else {
            // 连接槽存在但任务已结束，先释放旧槽
            ESP_LOGI(TAG, "SIO [%s] releasing stale connection slot", id);
            sio_release_connection(id);
        }
    }

    // 分配连接槽
    if (!s_sio_ctx.mutex) {
        s_sio_ctx.mutex = xSemaphoreCreateMutex();
    }

    xSemaphoreTake(s_sio_ctx.mutex, portMAX_DELAY);

    if (s_sio_ctx.count >= SIO_MAX_CONNECTIONS) {
        xSemaphoreGive(s_sio_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    conn = &s_sio_ctx.connections[s_sio_ctx.count];
    memset(conn, 0, sizeof(sio_connection_t));
    strncpy(conn->source_id, id, sizeof(conn->source_id) - 1);
    s_sio_ctx.count++;

    xSemaphoreGive(s_sio_ctx.mutex);

    // 初始化 pending mutex
    conn->pending_mutex = xSemaphoreCreateMutex();
    if (!conn->pending_mutex) {
        ESP_LOGE(TAG, "Failed to create pending mutex for %s", id);
        return ESP_FAIL;
    }
    conn->pending_json = NULL;

    // 启动连接任务（使用 PSRAM 栈，6KB 用于 WebSocket + JSON 解析）
    char task_name[32];
    snprintf(task_name, sizeof(task_name), "sio_%s", id);

    if (xTaskCreateWithCaps(sio_connection_task, task_name, 6144, conn, 5, 
                            &conn->task_handle, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create SIO task for %s", id);
        vSemaphoreDelete(conn->pending_mutex);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SIO [%s] connection started", id);
    return ESP_OK;
}

/**
 * @brief 断开 Socket.IO 数据源
 */
esp_err_t ts_source_sio_disconnect(const char *id)
{
    if (!id) return ESP_ERR_INVALID_ARG;

    sio_connection_t *conn = sio_find_connection(id);
    if (!conn) {
        return ESP_ERR_NOT_FOUND;
    }

    conn->should_stop = true;

    // 等待任务结束
    int timeout = 50;  // 5秒
    while (conn->task_handle && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "SIO [%s] disconnected", id);
    return ESP_OK;
}

/**
 * @brief 检查 Socket.IO 是否已连接
 */
bool ts_source_sio_is_connected(const char *id)
{
    sio_connection_t *conn = sio_find_connection(id);
    return conn && conn->upgraded;
}

/*===========================================================================*/
/*                              REST 源                                       */
/*===========================================================================*/

esp_err_t ts_source_rest_fetch(const char *id)
{
    // TODO: 实现 REST 请求
    ESP_LOGW(TAG, "REST fetch not implemented: %s", id);
    return ESP_ERR_NOT_SUPPORTED;
}

/*===========================================================================*/
/*                              枚举                                          */
/*===========================================================================*/

int ts_source_enumerate(int type_filter, ts_source_enum_cb_t callback, void *user_data)
{
    if (!callback || !s_src_ctx.initialized) {
        return 0;
    }

    int count = 0;

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);

    for (int i = 0; i < s_src_ctx.count; i++) {
        // 类型过滤
        if (type_filter >= 0 && s_src_ctx.sources[i].type != type_filter) {
            continue;
        }

        count++;

        if (!callback(&s_src_ctx.sources[i], user_data)) {
            break;
        }
    }

    xSemaphoreGive(s_src_ctx.mutex);
    return count;
}

/*===========================================================================*/
/*                              统计                                          */
/*===========================================================================*/

esp_err_t ts_source_manager_get_stats(ts_source_manager_stats_t *stats)
{
    if (!stats || !s_src_ctx.initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_src_ctx.mutex, portMAX_DELAY);
    *stats = s_src_ctx.stats;
    xSemaphoreGive(s_src_ctx.mutex);

    return ESP_OK;
}
