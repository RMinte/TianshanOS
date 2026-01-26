/**
 * @file ts_variable.h
 * @brief TianShanOS Automation Engine - Variable Storage API
 *
 * Provides variable storage with:
 * - Hierarchical namespacing (e.g., "agx.power", "lpmu.0.status")
 * - Change notification via event bus
 * - Optional NVS persistence
 * - Expression evaluation for computed variables
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_VARIABLE_H
#define TS_VARIABLE_H

#include "ts_automation_types.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                           Event Definitions                                */
/*===========================================================================*/

/**
 * @brief Variable change event data
 */
typedef struct {
    const char *name;                    /**< Variable name */
    ts_auto_value_t old_value;           /**< Previous value */
    ts_auto_value_t new_value;           /**< New value */
} ts_variable_change_event_t;

/*===========================================================================*/
/*                           Initialization                                   */
/*===========================================================================*/

/**
 * @brief Initialize variable storage
 *
 * @return ESP_OK on success
 */
esp_err_t ts_variable_init(void);

/**
 * @brief Deinitialize variable storage
 *
 * @return ESP_OK on success
 */
esp_err_t ts_variable_deinit(void);

/*===========================================================================*/
/*                           Variable Registration                            */
/*===========================================================================*/

/**
 * @brief Register a new variable
 *
 * @param var Variable definition
 * @return ESP_OK on success
 */
esp_err_t ts_variable_register(const ts_auto_variable_t *var);

/**
 * @brief Unregister a variable
 *
 * @param name Variable name
 * @return ESP_OK on success
 */
esp_err_t ts_variable_unregister(const char *name);

/**
 * @brief Unregister all variables associated with a source
 *
 * @param source_id Source ID to match
 * @return Number of variables removed
 */
int ts_variable_unregister_by_source(const char *source_id);

/**
 * @brief Check if variable exists
 *
 * @param name Variable name
 * @return true if exists
 */
bool ts_variable_exists(const char *name);

/*===========================================================================*/
/*                           Value Access                                     */
/*===========================================================================*/

/**
 * @brief Get variable value
 *
 * @param name Variable name
 * @param value Output value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_get(const char *name, ts_auto_value_t *value);

/**
 * @brief Get variable as boolean
 *
 * @param name Variable name
 * @param value Output value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_get_bool(const char *name, bool *value);

/**
 * @brief Get variable as integer
 *
 * @param name Variable name
 * @param value Output value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_get_int(const char *name, int32_t *value);

/**
 * @brief Get variable as float
 *
 * @param name Variable name
 * @param value Output value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_get_float(const char *name, double *value);

/**
 * @brief Get variable as string
 *
 * @param name Variable name
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return ESP_OK on success
 */
esp_err_t ts_variable_get_string(const char *name, char *buffer, size_t buffer_size);

/*===========================================================================*/
/*                           Value Modification                               */
/*===========================================================================*/

/**
 * @brief Set variable value
 *
 * @param name Variable name
 * @param value New value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_set(const char *name, const ts_auto_value_t *value);

/**
 * @brief Set variable to boolean
 *
 * @param name Variable name
 * @param value Boolean value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_set_bool(const char *name, bool value);

/**
 * @brief Set variable to integer
 *
 * @param name Variable name
 * @param value Integer value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_set_int(const char *name, int32_t value);

/**
 * @brief Set variable to float
 *
 * @param name Variable name
 * @param value Float value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_set_float(const char *name, double value);

/**
 * @brief Set variable to string
 *
 * @param name Variable name
 * @param value String value
 * @return ESP_OK on success
 */
esp_err_t ts_variable_set_string(const char *name, const char *value);

/*===========================================================================*/
/*                           Enumeration                                      */
/*===========================================================================*/

/**
 * @brief Variable enumeration callback
 *
 * @param var Variable definition
 * @param user_data User data
 * @return true to continue, false to stop
 */
typedef bool (*ts_variable_enum_cb_t)(const ts_auto_variable_t *var, void *user_data);

/**
 * @brief Enumerate all variables
 *
 * @param prefix Optional prefix filter (NULL for all)
 * @param callback Callback function
 * @param user_data User data for callback
 * @return Number of variables enumerated
 */
int ts_variable_enumerate(const char *prefix, ts_variable_enum_cb_t callback, void *user_data);

/**
 * @brief Get number of registered variables
 *
 * @return Variable count
 */
int ts_variable_count(void);

/*===========================================================================*/
/*                           Iteration                                        */
/*===========================================================================*/

/**
 * @brief Variable iteration context
 */
typedef struct {
    int index;                           /**< Current index */
    int _internal;                       /**< Internal use */
} ts_variable_iterate_ctx_t;

/**
 * @brief Iterate over all variables
 *
 * Usage:
 *   ts_variable_iterate_ctx_t ctx = {0};
 *   ts_auto_variable_t var;
 *   while (ts_variable_iterate(&ctx, &var) == ESP_OK) { ... }
 *
 * @param ctx Iteration context (initialize with {0})
 * @param var Output variable
 * @return ESP_OK if variable returned, ESP_ERR_NOT_FOUND when done
 */
esp_err_t ts_variable_iterate(ts_variable_iterate_ctx_t *ctx, ts_auto_variable_t *var);

/*===========================================================================*/
/*                           Persistence                                      */
/*===========================================================================*/

/**
 * @brief Save all persistent variables to NVS
 *
 * @return ESP_OK on success
 */
esp_err_t ts_variable_save_all(void);

/**
 * @brief Load all persistent variables from NVS
 *
 * @return ESP_OK on success
 */
esp_err_t ts_variable_load_all(void);

/*===========================================================================*/
/*                           JSON Export/Import                               */
/*===========================================================================*/

/**
 * @brief Export all variables to JSON
 *
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return JSON length, or -1 on error
 */
int ts_variable_export_json(char *buffer, size_t buffer_size);

/**
 * @brief Import variables from JSON
 *
 * @param json JSON string
 * @return ESP_OK on success
 */
esp_err_t ts_variable_import_json(const char *json);

#ifdef __cplusplus
}
#endif

#endif /* TS_VARIABLE_H */
