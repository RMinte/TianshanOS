/**
 * @file ts_config_json.c
 * @brief TianShanOS Configuration - JSON Parser Implementation
 *
 * JSON 配置文件解析和生成实现
 * 使用 cJSON 库进行 JSON 处理
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "ts_config.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "ts_config_json";

/* ============================================================================
 * 私有函数声明
 * ========================================================================== */

static esp_err_t parse_json_value(const char *prefix, const char *key, cJSON *value);
static esp_err_t parse_json_object(const char *prefix, cJSON *obj);
static char *read_file_content(const char *filepath, size_t *size);
static esp_err_t write_file_content(const char *filepath, const char *content);

/* ============================================================================
 * 公共 API 实现
 * ========================================================================== */

esp_err_t ts_config_load_json_file(const char *filepath)
{
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Loading JSON config: %s", filepath);

    // 读取文件内容
    size_t file_size = 0;
    char *content = read_file_content(filepath, &file_size);
    if (content == NULL) {
        ESP_LOGE(TAG, "Failed to read file: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    // 解析 JSON
    cJSON *root = cJSON_Parse(content);
    free(content);

    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "JSON parse error before: %s", error_ptr);
        }
        return ESP_ERR_INVALID_ARG;
    }

    // 遍历 JSON 对象
    esp_err_t ret = parse_json_object("", root);

    cJSON_Delete(root);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "JSON config loaded successfully");
    }

    return ret;
}

esp_err_t ts_config_save_json_file(const char *filepath)
{
    if (filepath == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Saving JSON config: %s", filepath);

    // 创建 JSON 根对象
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // TODO: 遍历所有配置项，添加到 JSON
    // 这需要配置管理器提供遍历接口
    // 目前只创建空对象

    // 生成 JSON 字符串
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    // 写入文件
    esp_err_t ret = write_file_content(filepath, json_str);
    cJSON_free(json_str);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "JSON config saved successfully");
    }

    return ret;
}

esp_err_t ts_config_load_json_string(const char *json_str)
{
    if (json_str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = parse_json_object("", root);
    cJSON_Delete(root);

    return ret;
}

/* ============================================================================
 * 私有函数实现
 * ========================================================================== */

/**
 * @brief 解析单个 JSON 值并存储到配置系统
 */
static esp_err_t parse_json_value(const char *prefix, const char *key, cJSON *value)
{
    // 构造完整的配置键名
    char full_key[TS_CONFIG_KEY_MAX_LEN];
    if (prefix != NULL && strlen(prefix) > 0) {
        snprintf(full_key, sizeof(full_key), "%s.%s", prefix, key);
    } else {
        strncpy(full_key, key, sizeof(full_key) - 1);
        full_key[sizeof(full_key) - 1] = '\0';
    }

    esp_err_t ret = ESP_OK;

    if (cJSON_IsBool(value)) {
        ret = ts_config_set_bool(full_key, cJSON_IsTrue(value));
    } else if (cJSON_IsNumber(value)) {
        // 判断是整数还是浮点数
        double d = value->valuedouble;
        if (d == (int64_t)d && d >= INT32_MIN && d <= INT32_MAX) {
            ret = ts_config_set_int32(full_key, (int32_t)d);
        } else if (d == (int64_t)d) {
            ret = ts_config_set_int64(full_key, (int64_t)d);
        } else {
            ret = ts_config_set_double(full_key, d);
        }
    } else if (cJSON_IsString(value)) {
        ret = ts_config_set_string(full_key, value->valuestring);
    } else if (cJSON_IsObject(value)) {
        // 递归处理嵌套对象
        ret = parse_json_object(full_key, value);
    } else if (cJSON_IsArray(value)) {
        // 数组暂时作为 JSON 字符串存储
        char *arr_str = cJSON_PrintUnformatted(value);
        if (arr_str != NULL) {
            ret = ts_config_set_string(full_key, arr_str);
            cJSON_free(arr_str);
        }
    } else if (cJSON_IsNull(value)) {
        // null 值跳过
        ESP_LOGD(TAG, "Skipping null value: %s", full_key);
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set config '%s': %s", full_key, esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 递归解析 JSON 对象
 */
static esp_err_t parse_json_object(const char *prefix, cJSON *obj)
{
    if (!cJSON_IsObject(obj)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, obj) {
        if (item->string != NULL) {
            parse_json_value(prefix, item->string, item);
        }
    }

    return ESP_OK;
}

/**
 * @brief 读取文件内容
 */
static char *read_file_content(const char *filepath, size_t *size)
{
    FILE *fp = fopen(filepath, "r");
    if (fp == NULL) {
        return NULL;
    }

    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > TS_CONFIG_VALUE_MAX_SIZE * 100) {
        fclose(fp);
        return NULL;
    }

    // 分配内存
    char *content = malloc(file_size + 1);
    if (content == NULL) {
        fclose(fp);
        return NULL;
    }

    // 读取内容
    size_t read_size = fread(content, 1, file_size, fp);
    fclose(fp);

    if (read_size != (size_t)file_size) {
        free(content);
        return NULL;
    }

    content[file_size] = '\0';
    
    if (size != NULL) {
        *size = file_size;
    }

    return content;
}

/**
 * @brief 写入内容到文件
 */
static esp_err_t write_file_content(const char *filepath, const char *content)
{
    FILE *fp = fopen(filepath, "w");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        return ESP_FAIL;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, fp);
    fclose(fp);

    if (written != len) {
        ESP_LOGE(TAG, "Failed to write all content to file");
        return ESP_FAIL;
    }

    return ESP_OK;
}
