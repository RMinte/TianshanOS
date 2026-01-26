/**
 * @file ts_jsonpath.h
 * @brief Lightweight JSONPath implementation for TianShanOS
 *
 * Provides dynamic JSON data extraction using path expressions.
 * Designed for automation engine data mapping.
 *
 * Supported syntax:
 *   - a.b.c          Object property chain
 *   - a[0]           Array index access
 *   - a[*]           Array wildcard (returns all elements)
 *   - a.b[0].c       Mixed paths
 *   - a[-1]          Negative index (from end)
 *
 * Memory: Results are allocated in PSRAM when available.
 *         Caller must free returned cJSON objects with cJSON_Delete().
 *
 * @copyright Copyright (c) 2025 TianShanOS
 */

#ifndef TS_JSONPATH_H
#define TS_JSONPATH_H

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JSONPath query result
 */
typedef struct {
    cJSON *value;           ///< Extracted value (caller owns, must cJSON_Delete)
    bool is_array;          ///< True if wildcard expanded to array
    int matched_count;      ///< Number of matched elements (1 for single, N for wildcard)
    char *error;            ///< Error message if failed (caller must free)
} ts_jsonpath_result_t;

/**
 * @brief Extract value from JSON using path expression
 *
 * @param root Source JSON object
 * @param path JSONPath expression (e.g., "cpu.cores[0].usage")
 * @return Extracted cJSON value (deep copy, caller must cJSON_Delete)
 *         Returns NULL if path not found or invalid
 *
 * @note Result is allocated in PSRAM when available
 *
 * Example:
 * @code
 * cJSON *data = cJSON_Parse(json_str);
 * cJSON *cpu = ts_jsonpath_get(data, "cpu.total_usage");
 * if (cpu) {
 *     double usage = cpu->valuedouble;
 *     cJSON_Delete(cpu);
 * }
 * cJSON_Delete(data);
 * @endcode
 */
cJSON *ts_jsonpath_get(const cJSON *root, const char *path);

/**
 * @brief Extract value with detailed result information
 *
 * @param root Source JSON object
 * @param path JSONPath expression
 * @param[out] result Result structure (caller must free contents)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if path invalid
 *
 * @note Call ts_jsonpath_result_free() to release result contents
 */
esp_err_t ts_jsonpath_query(const cJSON *root, const char *path, 
                            ts_jsonpath_result_t *result);

/**
 * @brief Free result structure contents
 *
 * @param result Result to free (structure itself not freed)
 */
void ts_jsonpath_result_free(ts_jsonpath_result_t *result);

/**
 * @brief Check if path expression is valid syntax
 *
 * @param path JSONPath expression to validate
 * @return true if syntax is valid
 */
bool ts_jsonpath_validate(const char *path);

/**
 * @brief Extract multiple paths at once (batch operation)
 *
 * More efficient than multiple ts_jsonpath_get calls for same root.
 *
 * @param root Source JSON object
 * @param paths Array of path expressions
 * @param count Number of paths
 * @param[out] results Array of results (caller allocates, size=count)
 * @return Number of successful extractions
 *
 * Example:
 * @code
 * const char *paths[] = {"cpu.total_usage", "memory.percent", "temperature.cpu"};
 * cJSON *results[3];
 * int found = ts_jsonpath_get_multi(data, paths, 3, results);
 * // Use results...
 * for (int i = 0; i < 3; i++) {
 *     if (results[i]) cJSON_Delete(results[i]);
 * }
 * @endcode
 */
int ts_jsonpath_get_multi(const cJSON *root, const char **paths, 
                          int count, cJSON **results);

/**
 * @brief Get value as specific type with default
 *
 * Convenience functions that extract and convert in one call.
 */
double ts_jsonpath_get_number(const cJSON *root, const char *path, double default_val);
int ts_jsonpath_get_int(const cJSON *root, const char *path, int default_val);
bool ts_jsonpath_get_bool(const cJSON *root, const char *path, bool default_val);

/**
 * @brief Get string value (caller must free returned string)
 *
 * @param root Source JSON object
 * @param path JSONPath expression
 * @return Duplicated string (PSRAM allocated) or NULL
 */
char *ts_jsonpath_get_string(const cJSON *root, const char *path);

#ifdef __cplusplus
}
#endif

#endif // TS_JSONPATH_H
