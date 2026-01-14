/**
 * @file ts_log.c
 * @brief TianShanOS Logging System Implementation
 *
 * 日志系统实现
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ts_log.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "ts_log";

/* ============================================================================
 * ANSI 颜色代码
 * ========================================================================== */

#define LOG_COLOR_BLACK   "30"
#define LOG_COLOR_RED     "31"
#define LOG_COLOR_GREEN   "32"
#define LOG_COLOR_BROWN   "33"
#define LOG_COLOR_BLUE    "34"
#define LOG_COLOR_PURPLE  "35"
#define LOG_COLOR_CYAN    "36"
#define LOG_COLOR_WHITE   "37"

#define LOG_COLOR(COLOR)  "\033[0;" COLOR "m"
#define LOG_BOLD(COLOR)   "\033[1;" COLOR "m"
#define LOG_RESET_COLOR   "\033[0m"

#define LOG_COLOR_E       LOG_BOLD(LOG_COLOR_RED)
#define LOG_COLOR_W       LOG_BOLD(LOG_COLOR_BROWN)
#define LOG_COLOR_I       LOG_COLOR(LOG_COLOR_GREEN)
#define LOG_COLOR_D       LOG_COLOR(LOG_COLOR_CYAN)
#define LOG_COLOR_V       LOG_COLOR(LOG_COLOR_WHITE)

/* ============================================================================
 * 私有类型定义
 * ========================================================================== */

/**
 * @brief 标签级别配置
 */
typedef struct ts_log_tag_level {
    char tag[TS_LOG_TAG_MAX_LEN];
    ts_log_level_t level;
    struct ts_log_tag_level *next;
} ts_log_tag_level_t;

/**
 * @brief 回调节点
 */
typedef struct ts_log_callback_node {
    ts_log_callback_t callback;
    ts_log_level_t min_level;
    void *user_data;
    struct ts_log_callback_node *next;
} ts_log_callback_node_t;

/**
 * @brief 日志环形缓冲区
 */
typedef struct {
    ts_log_entry_t *entries;
    size_t capacity;
    size_t head;
    size_t count;
} ts_log_ring_buffer_t;

/**
 * @brief 日志上下文
 */
typedef struct {
    bool initialized;
    SemaphoreHandle_t mutex;
    ts_log_level_t global_level;
    uint32_t output_mask;
    bool colors_enabled;
    bool timestamp_enabled;
    bool task_name_enabled;
    ts_log_tag_level_t *tag_levels;
    ts_log_callback_node_t *callbacks;
    ts_log_ring_buffer_t buffer;
    char file_path[128];
    FILE *log_file;
    size_t file_size;
    int file_index;
} ts_log_context_t;

/* ============================================================================
 * 私有变量
 * ========================================================================== */

static ts_log_context_t s_log_ctx = {0};

/* ============================================================================
 * 私有函数声明
 * ========================================================================== */

static void log_output_console(const ts_log_entry_t *entry);
static void log_output_file(const ts_log_entry_t *entry);
static void log_output_buffer(const ts_log_entry_t *entry);
static void notify_callbacks(const ts_log_entry_t *entry);
static ts_log_level_t get_effective_level(const char *tag);
static void rotate_log_file(void);

/* ============================================================================
 * 初始化和反初始化
 * ========================================================================== */

esp_err_t ts_log_init(void)
{
    if (s_log_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing TianShanOS Logging System...");

    // 创建互斥锁
    s_log_ctx.mutex = xSemaphoreCreateMutex();
    if (s_log_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 初始化默认配置
    s_log_ctx.global_level = (ts_log_level_t)CONFIG_TS_LOG_DEFAULT_LEVEL;
    s_log_ctx.output_mask = 0;

#ifdef CONFIG_TS_LOG_OUTPUT_CONSOLE
    s_log_ctx.output_mask |= TS_LOG_OUTPUT_CONSOLE;
#endif

#ifdef CONFIG_TS_LOG_OUTPUT_FILE
    s_log_ctx.output_mask |= TS_LOG_OUTPUT_FILE;
#endif

#ifdef CONFIG_TS_LOG_OUTPUT_BUFFER
    s_log_ctx.output_mask |= TS_LOG_OUTPUT_BUFFER;
#endif

#ifdef CONFIG_TS_LOG_COLORS
    s_log_ctx.colors_enabled = true;
#else
    s_log_ctx.colors_enabled = false;
#endif

#ifdef CONFIG_TS_LOG_TIMESTAMP
    s_log_ctx.timestamp_enabled = true;
#else
    s_log_ctx.timestamp_enabled = false;
#endif

#ifdef CONFIG_TS_LOG_TASK_NAME
    s_log_ctx.task_name_enabled = true;
#else
    s_log_ctx.task_name_enabled = false;
#endif

    // 初始化日志缓冲区
    if (s_log_ctx.output_mask & TS_LOG_OUTPUT_BUFFER) {
        s_log_ctx.buffer.capacity = TS_LOG_BUFFER_SIZE;
        s_log_ctx.buffer.entries = calloc(TS_LOG_BUFFER_SIZE, sizeof(ts_log_entry_t));
        if (s_log_ctx.buffer.entries == NULL) {
            ESP_LOGW(TAG, "Failed to allocate log buffer");
            s_log_ctx.output_mask &= ~TS_LOG_OUTPUT_BUFFER;
        }
        s_log_ctx.buffer.head = 0;
        s_log_ctx.buffer.count = 0;
    }

    // 初始化文件路径
#ifdef CONFIG_TS_LOG_FILE_PATH
    strncpy(s_log_ctx.file_path, CONFIG_TS_LOG_FILE_PATH, sizeof(s_log_ctx.file_path) - 1);
#else
    strcpy(s_log_ctx.file_path, "/sdcard/logs");
#endif
    s_log_ctx.log_file = NULL;
    s_log_ctx.file_size = 0;
    s_log_ctx.file_index = 0;

    s_log_ctx.tag_levels = NULL;
    s_log_ctx.callbacks = NULL;

    s_log_ctx.initialized = true;
    ESP_LOGI(TAG, "Logging system initialized (level=%d, outputs=0x%02lx)",
             s_log_ctx.global_level, (unsigned long)s_log_ctx.output_mask);

    return ESP_OK;
}

esp_err_t ts_log_deinit(void)
{
    if (!s_log_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    // 关闭日志文件
    if (s_log_ctx.log_file != NULL) {
        fclose(s_log_ctx.log_file);
        s_log_ctx.log_file = NULL;
    }

    // 释放缓冲区
    if (s_log_ctx.buffer.entries != NULL) {
        free(s_log_ctx.buffer.entries);
        s_log_ctx.buffer.entries = NULL;
    }

    // 释放标签级别列表
    ts_log_tag_level_t *tag = s_log_ctx.tag_levels;
    while (tag != NULL) {
        ts_log_tag_level_t *next = tag->next;
        free(tag);
        tag = next;
    }
    s_log_ctx.tag_levels = NULL;

    // 释放回调列表
    ts_log_callback_node_t *cb = s_log_ctx.callbacks;
    while (cb != NULL) {
        ts_log_callback_node_t *next = cb->next;
        free(cb);
        cb = next;
    }
    s_log_ctx.callbacks = NULL;

    xSemaphoreGive(s_log_ctx.mutex);
    vSemaphoreDelete(s_log_ctx.mutex);
    s_log_ctx.mutex = NULL;

    s_log_ctx.initialized = false;
    return ESP_OK;
}

bool ts_log_is_initialized(void)
{
    return s_log_ctx.initialized;
}

/* ============================================================================
 * 日志输出 API
 * ========================================================================== */

void ts_log(ts_log_level_t level, const char *tag, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    ts_log_v(level, tag, format, args);
    va_end(args);
}

void ts_log_v(ts_log_level_t level, const char *tag, const char *format, va_list args)
{
    if (!s_log_ctx.initialized) {
        // 未初始化时回退到 ESP-IDF 日志
        esp_log_level_t esp_level;
        switch (level) {
            case TS_LOG_ERROR:   esp_level = ESP_LOG_ERROR; break;
            case TS_LOG_WARN:    esp_level = ESP_LOG_WARN; break;
            case TS_LOG_INFO:    esp_level = ESP_LOG_INFO; break;
            case TS_LOG_DEBUG:   esp_level = ESP_LOG_DEBUG; break;
            case TS_LOG_VERBOSE: esp_level = ESP_LOG_VERBOSE; break;
            default: return;
        }
        esp_log_writev(esp_level, tag, format, args);
        return;
    }

    // 检查级别过滤
    ts_log_level_t effective_level = get_effective_level(tag);
    if (level > effective_level) {
        return;
    }

    // 构建日志条目
    ts_log_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    entry.level = level;

    if (tag != NULL) {
        strncpy(entry.tag, tag, TS_LOG_TAG_MAX_LEN - 1);
    }

    if (s_log_ctx.task_name_enabled) {
        TaskHandle_t task = xTaskGetCurrentTaskHandle();
        if (task != NULL) {
            strncpy(entry.task_name, pcTaskGetName(task), sizeof(entry.task_name) - 1);
        }
    }

    vsnprintf(entry.message, TS_LOG_MSG_MAX_LEN, format, args);

    // 输出到各目标
    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    if (s_log_ctx.output_mask & TS_LOG_OUTPUT_CONSOLE) {
        log_output_console(&entry);
    }

    if (s_log_ctx.output_mask & TS_LOG_OUTPUT_FILE) {
        log_output_file(&entry);
    }

    if (s_log_ctx.output_mask & TS_LOG_OUTPUT_BUFFER) {
        log_output_buffer(&entry);
    }

    xSemaphoreGive(s_log_ctx.mutex);

    // 通知回调（在锁外调用）
    notify_callbacks(&entry);
}

void ts_log_hex(ts_log_level_t level, const char *tag, const void *data, size_t length)
{
    if (!s_log_ctx.initialized || data == NULL || length == 0) {
        return;
    }

    // 检查级别过滤
    ts_log_level_t effective_level = get_effective_level(tag);
    if (level > effective_level) {
        return;
    }

    const uint8_t *bytes = (const uint8_t *)data;
    char line[80];
    size_t offset = 0;

    while (offset < length) {
        int pos = snprintf(line, sizeof(line), "%04zx: ", offset);

        // 十六进制部分
        for (size_t i = 0; i < 16 && offset + i < length; i++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02x ", bytes[offset + i]);
        }

        // 填充
        for (size_t i = length - offset; i < 16 && length > offset; i++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "   ");
        }

        // ASCII 部分
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (size_t i = 0; i < 16 && offset + i < length; i++) {
            char c = bytes[offset + i];
            if (c >= 32 && c < 127) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%c", c);
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, ".");
            }
        }
        snprintf(line + pos, sizeof(line) - pos, "|");

        ts_log(level, tag, "%s", line);
        offset += 16;
    }
}

/* ============================================================================
 * 日志级别控制
 * ========================================================================== */

void ts_log_set_level(ts_log_level_t level)
{
    s_log_ctx.global_level = level;
    ESP_LOGI(TAG, "Global log level set to %s", ts_log_level_to_string(level));
}

ts_log_level_t ts_log_get_level(void)
{
    return s_log_ctx.global_level;
}

esp_err_t ts_log_set_tag_level(const char *tag, ts_log_level_t level)
{
    if (tag == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    // 查找是否已存在
    ts_log_tag_level_t *node = s_log_ctx.tag_levels;
    while (node != NULL) {
        if (strcmp(node->tag, tag) == 0) {
            node->level = level;
            xSemaphoreGive(s_log_ctx.mutex);
            return ESP_OK;
        }
        node = node->next;
    }

    // 创建新节点
    node = calloc(1, sizeof(ts_log_tag_level_t));
    if (node == NULL) {
        xSemaphoreGive(s_log_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    strncpy(node->tag, tag, TS_LOG_TAG_MAX_LEN - 1);
    node->level = level;
    node->next = s_log_ctx.tag_levels;
    s_log_ctx.tag_levels = node;

    xSemaphoreGive(s_log_ctx.mutex);
    return ESP_OK;
}

ts_log_level_t ts_log_get_tag_level(const char *tag)
{
    if (tag == NULL) {
        return s_log_ctx.global_level;
    }

    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    ts_log_tag_level_t *node = s_log_ctx.tag_levels;
    while (node != NULL) {
        if (strcmp(node->tag, tag) == 0) {
            ts_log_level_t level = node->level;
            xSemaphoreGive(s_log_ctx.mutex);
            return level;
        }
        node = node->next;
    }

    xSemaphoreGive(s_log_ctx.mutex);
    return s_log_ctx.global_level;
}

void ts_log_reset_tag_levels(void)
{
    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    ts_log_tag_level_t *node = s_log_ctx.tag_levels;
    while (node != NULL) {
        ts_log_tag_level_t *next = node->next;
        free(node);
        node = next;
    }
    s_log_ctx.tag_levels = NULL;

    xSemaphoreGive(s_log_ctx.mutex);
}

/* ============================================================================
 * 输出控制
 * ========================================================================== */

void ts_log_enable_output(ts_log_output_t output)
{
    s_log_ctx.output_mask |= output;
}

void ts_log_disable_output(ts_log_output_t output)
{
    s_log_ctx.output_mask &= ~output;
}

uint32_t ts_log_get_outputs(void)
{
    return s_log_ctx.output_mask;
}

esp_err_t ts_log_set_file_path(const char *path)
{
    if (path == NULL || strlen(path) >= sizeof(s_log_ctx.file_path)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    // 关闭当前文件
    if (s_log_ctx.log_file != NULL) {
        fclose(s_log_ctx.log_file);
        s_log_ctx.log_file = NULL;
    }

    strncpy(s_log_ctx.file_path, path, sizeof(s_log_ctx.file_path) - 1);
    s_log_ctx.file_index = 0;
    s_log_ctx.file_size = 0;

    xSemaphoreGive(s_log_ctx.mutex);
    return ESP_OK;
}

esp_err_t ts_log_flush(void)
{
    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    if (s_log_ctx.log_file != NULL) {
        fflush(s_log_ctx.log_file);
    }

    xSemaphoreGive(s_log_ctx.mutex);
    return ESP_OK;
}

/* ============================================================================
 * 日志缓冲区操作
 * ========================================================================== */

size_t ts_log_buffer_count(void)
{
    return s_log_ctx.buffer.count;
}

size_t ts_log_buffer_get(ts_log_entry_t *entries, size_t max_count, size_t start_index)
{
    if (entries == NULL || max_count == 0 || s_log_ctx.buffer.entries == NULL) {
        return 0;
    }

    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    size_t count = s_log_ctx.buffer.count;
    if (start_index >= count) {
        xSemaphoreGive(s_log_ctx.mutex);
        return 0;
    }

    size_t available = count - start_index;
    size_t to_copy = (available < max_count) ? available : max_count;

    // 计算起始位置
    size_t start = (s_log_ctx.buffer.head + s_log_ctx.buffer.capacity - count + start_index) 
                   % s_log_ctx.buffer.capacity;

    for (size_t i = 0; i < to_copy; i++) {
        size_t idx = (start + i) % s_log_ctx.buffer.capacity;
        memcpy(&entries[i], &s_log_ctx.buffer.entries[idx], sizeof(ts_log_entry_t));
    }

    xSemaphoreGive(s_log_ctx.mutex);
    return to_copy;
}

void ts_log_buffer_clear(void)
{
    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);
    s_log_ctx.buffer.head = 0;
    s_log_ctx.buffer.count = 0;
    xSemaphoreGive(s_log_ctx.mutex);
}

/* ============================================================================
 * 日志回调
 * ========================================================================== */

esp_err_t ts_log_add_callback(ts_log_callback_t callback,
                               ts_log_level_t min_level,
                               void *user_data,
                               ts_log_callback_handle_t *handle)
{
    if (callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ts_log_callback_node_t *node = calloc(1, sizeof(ts_log_callback_node_t));
    if (node == NULL) {
        return ESP_ERR_NO_MEM;
    }

    node->callback = callback;
    node->min_level = min_level;
    node->user_data = user_data;

    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);
    node->next = s_log_ctx.callbacks;
    s_log_ctx.callbacks = node;
    xSemaphoreGive(s_log_ctx.mutex);

    if (handle != NULL) {
        *handle = (ts_log_callback_handle_t)node;
    }

    return ESP_OK;
}

esp_err_t ts_log_remove_callback(ts_log_callback_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    ts_log_callback_node_t *prev = NULL;
    ts_log_callback_node_t *node = s_log_ctx.callbacks;
    ts_log_callback_node_t *target = (ts_log_callback_node_t *)handle;

    while (node != NULL) {
        if (node == target) {
            if (prev == NULL) {
                s_log_ctx.callbacks = node->next;
            } else {
                prev->next = node->next;
            }
            xSemaphoreGive(s_log_ctx.mutex);
            free(node);
            return ESP_OK;
        }
        prev = node;
        node = node->next;
    }

    xSemaphoreGive(s_log_ctx.mutex);
    return ESP_ERR_NOT_FOUND;
}

/* ============================================================================
 * 工具函数
 * ========================================================================== */

const char *ts_log_level_to_string(ts_log_level_t level)
{
    static const char *level_names[] = {
        "NONE", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE"
    };

    if (level >= TS_LOG_MAX) {
        return "UNKNOWN";
    }
    return level_names[level];
}

ts_log_level_t ts_log_level_from_string(const char *str)
{
    if (str == NULL) {
        return TS_LOG_INFO;
    }

    if (strcasecmp(str, "none") == 0 || strcmp(str, "0") == 0) {
        return TS_LOG_NONE;
    }
    if (strcasecmp(str, "error") == 0 || strcasecmp(str, "e") == 0 || strcmp(str, "1") == 0) {
        return TS_LOG_ERROR;
    }
    if (strcasecmp(str, "warn") == 0 || strcasecmp(str, "warning") == 0 || 
        strcasecmp(str, "w") == 0 || strcmp(str, "2") == 0) {
        return TS_LOG_WARN;
    }
    if (strcasecmp(str, "info") == 0 || strcasecmp(str, "i") == 0 || strcmp(str, "3") == 0) {
        return TS_LOG_INFO;
    }
    if (strcasecmp(str, "debug") == 0 || strcasecmp(str, "d") == 0 || strcmp(str, "4") == 0) {
        return TS_LOG_DEBUG;
    }
    if (strcasecmp(str, "verbose") == 0 || strcasecmp(str, "v") == 0 || strcmp(str, "5") == 0) {
        return TS_LOG_VERBOSE;
    }

    return TS_LOG_INFO;  // 默认
}

const char *ts_log_level_color(ts_log_level_t level)
{
    switch (level) {
        case TS_LOG_ERROR:   return LOG_COLOR_E;
        case TS_LOG_WARN:    return LOG_COLOR_W;
        case TS_LOG_INFO:    return LOG_COLOR_I;
        case TS_LOG_DEBUG:   return LOG_COLOR_D;
        case TS_LOG_VERBOSE: return LOG_COLOR_V;
        default:             return "";
    }
}

/* ============================================================================
 * 私有函数实现
 * ========================================================================== */

static void log_output_console(const ts_log_entry_t *entry)
{
    static const char level_chars[] = { 'N', 'E', 'W', 'I', 'D', 'V' };
    char level_char = (entry->level < TS_LOG_MAX) ? level_chars[entry->level] : '?';

    if (s_log_ctx.colors_enabled) {
        const char *color = ts_log_level_color(entry->level);
        
        if (s_log_ctx.timestamp_enabled) {
            printf("%s%c (%lu) %s: %s%s\n",
                   color,
                   level_char,
                   (unsigned long)entry->timestamp_ms,
                   entry->tag,
                   entry->message,
                   LOG_RESET_COLOR);
        } else {
            printf("%s%c %s: %s%s\n",
                   color,
                   level_char,
                   entry->tag,
                   entry->message,
                   LOG_RESET_COLOR);
        }
    } else {
        if (s_log_ctx.timestamp_enabled) {
            printf("%c (%lu) %s: %s\n",
                   level_char,
                   (unsigned long)entry->timestamp_ms,
                   entry->tag,
                   entry->message);
        } else {
            printf("%c %s: %s\n",
                   level_char,
                   entry->tag,
                   entry->message);
        }
    }
}

static void log_output_file(const ts_log_entry_t *entry)
{
    // 打开文件（如果需要）
    if (s_log_ctx.log_file == NULL) {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/tianshan_%d.log",
                 s_log_ctx.file_path, s_log_ctx.file_index);
        
        s_log_ctx.log_file = fopen(filepath, "a");
        if (s_log_ctx.log_file == NULL) {
            // 无法打开文件，禁用文件输出
            s_log_ctx.output_mask &= ~TS_LOG_OUTPUT_FILE;
            return;
        }
    }

    // 写入日志
    static const char level_chars[] = { 'N', 'E', 'W', 'I', 'D', 'V' };
    char level_char = (entry->level < TS_LOG_MAX) ? level_chars[entry->level] : '?';

    int written = fprintf(s_log_ctx.log_file, "%c %lu %s: %s\n",
                          level_char,
                          (unsigned long)entry->timestamp_ms,
                          entry->tag,
                          entry->message);

    if (written > 0) {
        s_log_ctx.file_size += written;
    }

    // 检查是否需要轮转
#ifdef CONFIG_TS_LOG_FILE_MAX_SIZE
    if (s_log_ctx.file_size >= CONFIG_TS_LOG_FILE_MAX_SIZE * 1024) {
        rotate_log_file();
    }
#endif
}

static void log_output_buffer(const ts_log_entry_t *entry)
{
    if (s_log_ctx.buffer.entries == NULL) {
        return;
    }

    // 复制到环形缓冲区
    memcpy(&s_log_ctx.buffer.entries[s_log_ctx.buffer.head], entry, sizeof(ts_log_entry_t));
    s_log_ctx.buffer.head = (s_log_ctx.buffer.head + 1) % s_log_ctx.buffer.capacity;

    if (s_log_ctx.buffer.count < s_log_ctx.buffer.capacity) {
        s_log_ctx.buffer.count++;
    }
}

static void notify_callbacks(const ts_log_entry_t *entry)
{
    xSemaphoreTake(s_log_ctx.mutex, portMAX_DELAY);

    ts_log_callback_node_t *node = s_log_ctx.callbacks;
    while (node != NULL) {
        if (entry->level <= node->min_level && node->callback != NULL) {
            node->callback(entry, node->user_data);
        }
        node = node->next;
    }

    xSemaphoreGive(s_log_ctx.mutex);
}

static ts_log_level_t get_effective_level(const char *tag)
{
    if (tag == NULL) {
        return s_log_ctx.global_level;
    }

    // 在锁外快速检查（可能存在竞争，但影响不大）
    ts_log_tag_level_t *node = s_log_ctx.tag_levels;
    while (node != NULL) {
        if (strcmp(node->tag, tag) == 0) {
            return node->level;
        }
        node = node->next;
    }

    return s_log_ctx.global_level;
}

static void rotate_log_file(void)
{
    if (s_log_ctx.log_file != NULL) {
        fclose(s_log_ctx.log_file);
        s_log_ctx.log_file = NULL;
    }

    s_log_ctx.file_index++;

#ifdef CONFIG_TS_LOG_FILE_MAX_FILES
    if (s_log_ctx.file_index >= CONFIG_TS_LOG_FILE_MAX_FILES) {
        s_log_ctx.file_index = 0;
    }
#endif

    s_log_ctx.file_size = 0;

    // 删除旧文件（如果存在）
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/tianshan_%d.log",
             s_log_ctx.file_path, s_log_ctx.file_index);
    remove(filepath);
}
