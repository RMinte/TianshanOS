/**
 * @file ts_script.h
 * @brief TianShanOS Script Engine
 * 
 * Simple script engine for console command automation.
 * Supports basic control flow, variables, and batch execution.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_SCRIPT_H
#define TS_SCRIPT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief Script execution result
 */
typedef enum {
    TS_SCRIPT_OK = 0,
    TS_SCRIPT_ERROR,
    TS_SCRIPT_SYNTAX_ERROR,
    TS_SCRIPT_CMD_ERROR,
    TS_SCRIPT_ABORT,
    TS_SCRIPT_CONTINUE,
    TS_SCRIPT_BREAK
} ts_script_result_t;

/**
 * @brief Script variable value
 */
typedef struct {
    char name[32];
    char value[128];
} ts_script_var_t;

/**
 * @brief Script context
 */
typedef struct ts_script_ctx_s *ts_script_ctx_t;

/*===========================================================================*/
/*                              Functions                                     */
/*===========================================================================*/

/**
 * @brief Initialize script engine
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_script_init(void);

/**
 * @brief Deinitialize script engine
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_script_deinit(void);

/**
 * @brief Create script context
 * 
 * @return Script context, or NULL on error
 */
ts_script_ctx_t ts_script_ctx_create(void);

/**
 * @brief Destroy script context
 * 
 * @param ctx Script context
 */
void ts_script_ctx_destroy(ts_script_ctx_t ctx);

/**
 * @brief Set variable in context
 * 
 * @param ctx Script context
 * @param name Variable name
 * @param value Variable value
 * @return ESP_OK on success
 */
esp_err_t ts_script_set_var(ts_script_ctx_t ctx, const char *name, const char *value);

/**
 * @brief Get variable from context
 * 
 * @param ctx Script context
 * @param name Variable name
 * @return Variable value, or NULL if not found
 */
const char *ts_script_get_var(ts_script_ctx_t ctx, const char *name);

/**
 * @brief Execute a single script line
 * 
 * @param ctx Script context
 * @param line Script line to execute
 * @return Script result
 */
ts_script_result_t ts_script_exec_line(ts_script_ctx_t ctx, const char *line);

/**
 * @brief Execute script from string
 * 
 * @param script Script content (multiple lines)
 * @return ESP_OK on success
 */
esp_err_t ts_script_exec_string(const char *script);

/**
 * @brief Execute script from file
 * 
 * @param path Script file path
 * @return ESP_OK on success
 */
esp_err_t ts_script_exec_file(const char *path);

/**
 * @brief Register script console commands
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_script_register_cmds(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_SCRIPT_H */
