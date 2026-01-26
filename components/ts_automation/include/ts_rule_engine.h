/**
 * @file ts_rule_engine.h
 * @brief TianShanOS Automation Engine - Rule Engine API
 *
 * Provides rule evaluation and action execution:
 * - Condition evaluation with multiple operators
 * - Action sequencing with delays
 * - Rule cooldown management
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_RULE_ENGINE_H
#define TS_RULE_ENGINE_H

#include "ts_automation_types.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                           Initialization                                   */
/*===========================================================================*/

/**
 * @brief Initialize rule engine
 *
 * @return ESP_OK on success
 */
esp_err_t ts_rule_engine_init(void);

/**
 * @brief Deinitialize rule engine
 *
 * @return ESP_OK on success
 */
esp_err_t ts_rule_engine_deinit(void);

/*===========================================================================*/
/*                           Rule Management                                  */
/*===========================================================================*/

/**
 * @brief Register a rule
 *
 * @param rule Rule definition
 * @return ESP_OK on success
 */
esp_err_t ts_rule_register(const ts_auto_rule_t *rule);

/**
 * @brief Unregister a rule
 *
 * @param id Rule ID
 * @return ESP_OK on success
 */
esp_err_t ts_rule_unregister(const char *id);

/**
 * @brief Enable a rule
 *
 * @param id Rule ID
 * @return ESP_OK on success
 */
esp_err_t ts_rule_enable(const char *id);

/**
 * @brief Disable a rule
 *
 * @param id Rule ID
 * @return ESP_OK on success
 */
esp_err_t ts_rule_disable(const char *id);

/**
 * @brief Get rule by ID
 *
 * @param id Rule ID
 * @return Rule pointer or NULL
 */
const ts_auto_rule_t *ts_rule_get(const char *id);

/**
 * @brief Get number of registered rules
 *
 * @return Rule count
 */
int ts_rule_count(void);

/*===========================================================================*/
/*                           Evaluation                                       */
/*===========================================================================*/

/**
 * @brief Evaluate all rules
 *
 * Checks all enabled rules and triggers actions for matching conditions.
 * Called periodically by automation engine.
 *
 * @return Number of rules triggered
 */
int ts_rule_evaluate_all(void);

/**
 * @brief Evaluate a specific rule
 *
 * @param id Rule ID
 * @param triggered Output: true if rule was triggered
 * @return ESP_OK on success
 */
esp_err_t ts_rule_evaluate(const char *id, bool *triggered);

/**
 * @brief Manually trigger a rule (bypass conditions)
 *
 * @param id Rule ID
 * @return ESP_OK on success
 */
esp_err_t ts_rule_trigger(const char *id);

/*===========================================================================*/
/*                           Condition Evaluation                             */
/*===========================================================================*/

/**
 * @brief Evaluate a single condition
 *
 * @param condition Condition to evaluate
 * @return true if condition is met
 */
bool ts_rule_eval_condition(const ts_auto_condition_t *condition);

/**
 * @brief Evaluate a condition group
 *
 * @param group Condition group
 * @return true if group conditions are met
 */
bool ts_rule_eval_condition_group(const ts_auto_condition_group_t *group);

/*===========================================================================*/
/*                           Action Execution                                 */
/*===========================================================================*/

/**
 * @brief Action result callback
 *
 * @param action Action that was executed
 * @param result Result code
 * @param user_data User data
 */
typedef void (*ts_action_result_cb_t)(const ts_auto_action_t *action, 
                                       esp_err_t result, void *user_data);

/**
 * @brief Execute a single action
 *
 * @param action Action to execute
 * @return ESP_OK on success
 */
esp_err_t ts_action_execute(const ts_auto_action_t *action);

/**
 * @brief Execute action array with delays
 *
 * @param actions Array of actions
 * @param count Number of actions
 * @param callback Optional result callback
 * @param user_data Callback user data
 * @return ESP_OK if execution started
 */
esp_err_t ts_action_execute_array(const ts_auto_action_t *actions, int count,
                                   ts_action_result_cb_t callback, void *user_data);

/*===========================================================================*/
/*                           Rule Access                                      */
/*===========================================================================*/

/**
 * @brief Get rule by index
 *
 * @param index Rule index (0 to rule_count-1)
 * @param rule Output rule structure
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if index out of range
 */
esp_err_t ts_rule_get_by_index(int index, ts_auto_rule_t *rule);

/**
 * @brief Get total number of registered rules
 *
 * @return Rule count
 */
int ts_rule_count(void);

/*===========================================================================*/
/*                           Statistics                                       */
/*===========================================================================*/

/**
 * @brief Rule engine statistics
 */
typedef struct {
    uint32_t total_evaluations;          /**< Total rule evaluations */
    uint32_t total_triggers;             /**< Total rule triggers */
    uint32_t total_actions;              /**< Total actions executed */
    uint32_t failed_actions;             /**< Failed action count */
    int64_t last_evaluation_ms;          /**< Last evaluation timestamp */
} ts_rule_engine_stats_t;

/**
 * @brief Get rule engine statistics
 *
 * @param stats Output statistics
 * @return ESP_OK on success
 */
esp_err_t ts_rule_engine_get_stats(ts_rule_engine_stats_t *stats);

/**
 * @brief Reset statistics
 *
 * @return ESP_OK on success
 */
esp_err_t ts_rule_engine_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_RULE_ENGINE_H */
