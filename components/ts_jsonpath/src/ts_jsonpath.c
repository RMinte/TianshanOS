/**
 * @file ts_jsonpath.c
 * @brief Lightweight JSONPath implementation
 *
 * 轻量级 JSONPath 实现，用于动态数据结构映射。
 * 内存分配优先使用 PSRAM。
 */

#include "ts_jsonpath.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_heap_caps.h"

static const char *TAG = "jsonpath";

/*===========================================================================*/
/*                         Memory Allocation Helpers                          */
/*===========================================================================*/

/**
 * @brief 分配内存，优先使用 PSRAM
 */
static void *jp_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!ptr) {
        ptr = malloc(size);
    }
    return ptr;
}

/**
 * @brief 复制字符串到 PSRAM
 */
static char *jp_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = jp_malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

/*===========================================================================*/
/*                           Path Tokenizer                                   */
/*===========================================================================*/

typedef enum {
    TOKEN_KEY,      // 对象键名
    TOKEN_INDEX,    // 数组索引 [N]
    TOKEN_WILDCARD, // 数组通配符 [*]
    TOKEN_END       // 路径结束
} token_type_t;

typedef struct {
    token_type_t type;
    char key[64];   // 键名或空
    int index;      // 数组索引（支持负数）
} path_token_t;

/**
 * @brief 解析路径中的下一个 token
 *
 * @param path 当前路径位置指针
 * @param token 输出 token
 * @return 下一个位置，NULL 表示结束或错误
 */
static const char *parse_next_token(const char *path, path_token_t *token)
{
    if (!path || *path == '\0') {
        token->type = TOKEN_END;
        return NULL;
    }
    
    // 跳过开头的点
    if (*path == '.') {
        path++;
    }
    
    if (*path == '\0') {
        token->type = TOKEN_END;
        return NULL;
    }
    
    // 检查是否是数组访问 [...]
    if (*path == '[') {
        path++;  // 跳过 '['
        
        if (*path == '*') {
            // 通配符 [*]
            token->type = TOKEN_WILDCARD;
            path++;  // 跳过 '*'
            if (*path != ']') {
                return NULL;  // 语法错误
            }
            path++;  // 跳过 ']'
            return path;
        }
        
        // 数组索引 [N] 或 [-N]
        token->type = TOKEN_INDEX;
        int sign = 1;
        if (*path == '-') {
            sign = -1;
            path++;
        }
        
        token->index = 0;
        while (isdigit((unsigned char)*path)) {
            token->index = token->index * 10 + (*path - '0');
            path++;
        }
        token->index *= sign;
        
        if (*path != ']') {
            return NULL;  // 语法错误
        }
        path++;  // 跳过 ']'
        return path;
    }
    
    // 对象键名
    token->type = TOKEN_KEY;
    size_t i = 0;
    while (*path && *path != '.' && *path != '[' && i < sizeof(token->key) - 1) {
        token->key[i++] = *path++;
    }
    token->key[i] = '\0';
    
    if (i == 0) {
        return NULL;  // 空键名
    }
    
    return path;
}

/*===========================================================================*/
/*                         Core JSONPath Functions                            */
/*===========================================================================*/

/**
 * @brief 深拷贝 cJSON 对象（分配到 PSRAM）
 */
static cJSON *jp_deep_copy(const cJSON *item)
{
    if (!item) return NULL;
    
    // 使用 cJSON_Duplicate，然后让其使用默认分配器
    // cJSON 内部会使用我们无法控制的分配器，但结果是可用的
    return cJSON_Duplicate(item, true);
}

/**
 * @brief 递归执行路径查询
 */
static cJSON *query_recursive(const cJSON *current, const char *remaining_path, bool *is_wildcard)
{
    path_token_t token;
    const char *next = parse_next_token(remaining_path, &token);
    
    if (token.type == TOKEN_END) {
        // 路径结束，返回当前节点的拷贝
        return jp_deep_copy(current);
    }
    
    if (!current) {
        return NULL;
    }
    
    switch (token.type) {
        case TOKEN_KEY: {
            // 对象属性访问
            if (!cJSON_IsObject(current)) {
                return NULL;
            }
            const cJSON *child = cJSON_GetObjectItemCaseSensitive(current, token.key);
            if (!child) {
                return NULL;
            }
            return query_recursive(child, next, is_wildcard);
        }
        
        case TOKEN_INDEX: {
            // 数组索引访问
            if (!cJSON_IsArray(current)) {
                return NULL;
            }
            int size = cJSON_GetArraySize(current);
            int idx = token.index;
            
            // 处理负索引
            if (idx < 0) {
                idx = size + idx;
            }
            
            if (idx < 0 || idx >= size) {
                return NULL;  // 越界
            }
            
            const cJSON *child = cJSON_GetArrayItem(current, idx);
            return query_recursive(child, next, is_wildcard);
        }
        
        case TOKEN_WILDCARD: {
            // 数组通配符 - 返回所有子元素处理后的结果数组
            if (!cJSON_IsArray(current)) {
                return NULL;
            }
            
            *is_wildcard = true;
            cJSON *result_array = cJSON_CreateArray();
            if (!result_array) {
                return NULL;
            }
            
            int size = cJSON_GetArraySize(current);
            for (int i = 0; i < size; i++) {
                const cJSON *child = cJSON_GetArrayItem(current, i);
                bool dummy_wildcard = false;
                cJSON *child_result = query_recursive(child, next, &dummy_wildcard);
                if (child_result) {
                    cJSON_AddItemToArray(result_array, child_result);
                }
            }
            
            if (cJSON_GetArraySize(result_array) == 0) {
                cJSON_Delete(result_array);
                return NULL;
            }
            
            return result_array;
        }
        
        default:
            return NULL;
    }
}

/*===========================================================================*/
/*                          Public API Implementation                         */
/*===========================================================================*/

cJSON *ts_jsonpath_get(const cJSON *root, const char *path)
{
    if (!root || !path || *path == '\0') {
        return NULL;
    }
    
    bool is_wildcard = false;
    return query_recursive(root, path, &is_wildcard);
}

esp_err_t ts_jsonpath_query(const cJSON *root, const char *path, 
                            ts_jsonpath_result_t *result)
{
    if (!result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(result, 0, sizeof(*result));
    
    if (!root || !path || *path == '\0') {
        result->error = jp_strdup("Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }
    
    bool is_wildcard = false;
    result->value = query_recursive(root, path, &is_wildcard);
    result->is_array = is_wildcard;
    
    if (!result->value) {
        result->error = jp_strdup("Path not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    if (is_wildcard && cJSON_IsArray(result->value)) {
        result->matched_count = cJSON_GetArraySize(result->value);
    } else {
        result->matched_count = 1;
    }
    
    return ESP_OK;
}

void ts_jsonpath_result_free(ts_jsonpath_result_t *result)
{
    if (!result) return;
    
    if (result->value) {
        cJSON_Delete(result->value);
        result->value = NULL;
    }
    if (result->error) {
        free(result->error);
        result->error = NULL;
    }
}

bool ts_jsonpath_validate(const char *path)
{
    if (!path || *path == '\0') {
        return false;
    }
    
    const char *p = path;
    path_token_t token;
    
    while (p && *p) {
        p = parse_next_token(p, &token);
        if (token.type == TOKEN_END) {
            break;
        }
        if (p == NULL && token.type != TOKEN_END) {
            return false;  // 解析错误
        }
    }
    
    return true;
}

int ts_jsonpath_get_multi(const cJSON *root, const char **paths, 
                          int count, cJSON **results)
{
    if (!root || !paths || !results || count <= 0) {
        return 0;
    }
    
    int found = 0;
    for (int i = 0; i < count; i++) {
        results[i] = ts_jsonpath_get(root, paths[i]);
        if (results[i]) {
            found++;
        }
    }
    
    return found;
}

double ts_jsonpath_get_number(const cJSON *root, const char *path, double default_val)
{
    cJSON *val = ts_jsonpath_get(root, path);
    if (!val) {
        return default_val;
    }
    
    double result = default_val;
    if (cJSON_IsNumber(val)) {
        result = val->valuedouble;
    }
    cJSON_Delete(val);
    return result;
}

int ts_jsonpath_get_int(const cJSON *root, const char *path, int default_val)
{
    cJSON *val = ts_jsonpath_get(root, path);
    if (!val) {
        return default_val;
    }
    
    int result = default_val;
    if (cJSON_IsNumber(val)) {
        result = (int)val->valuedouble;
    }
    cJSON_Delete(val);
    return result;
}

bool ts_jsonpath_get_bool(const cJSON *root, const char *path, bool default_val)
{
    cJSON *val = ts_jsonpath_get(root, path);
    if (!val) {
        return default_val;
    }
    
    bool result = default_val;
    if (cJSON_IsBool(val)) {
        result = cJSON_IsTrue(val);
    }
    cJSON_Delete(val);
    return result;
}

char *ts_jsonpath_get_string(const cJSON *root, const char *path)
{
    cJSON *val = ts_jsonpath_get(root, path);
    if (!val) {
        return NULL;
    }
    
    char *result = NULL;
    if (cJSON_IsString(val) && val->valuestring) {
        result = jp_strdup(val->valuestring);
    }
    cJSON_Delete(val);
    return result;
}
