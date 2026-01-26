/**
 * @file ts_automation.h
 * @brief TianShanOS Automation Engine - Main Header
 *
 * The automation engine provides:
 * - Data source management (builtin ESP32, WebSocket, REST)
 * - Rule-based trigger system
 * - Action execution (LED, SSH, GPIO, etc.)
 * - Variable storage with expression evaluation
 *
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-26
 */

#ifndef TS_AUTOMATION_H
#define TS_AUTOMATION_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Version                                       */
/*===========================================================================*/

#define TS_AUTOMATION_VERSION_MAJOR    1
#define TS_AUTOMATION_VERSION_MINOR    0
#define TS_AUTOMATION_VERSION_PATCH    0

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief Automation engine state
 */
typedef enum {
    TS_AUTO_STATE_UNINITIALIZED = 0,    /**< Not initialized */
    TS_AUTO_STATE_INITIALIZED,           /**< Initialized but not running */
    TS_AUTO_STATE_RUNNING,               /**< Running */
    TS_AUTO_STATE_PAUSED,                /**< Paused */
    TS_AUTO_STATE_ERROR                  /**< Error state */
} ts_automation_state_t;

/**
 * @brief Automation engine status
 */
typedef struct {
    ts_automation_state_t state;         /**< Current state */
    uint32_t uptime_ms;                  /**< Engine uptime in ms */
    uint16_t sources_count;              /**< Number of registered sources */
    uint16_t sources_active;             /**< Number of active sources */
    uint16_t rules_count;                /**< Number of registered rules */
    uint16_t rules_active;               /**< Number of active (triggered) rules */
    uint16_t variables_count;            /**< Number of variables */
    uint32_t actions_executed;           /**< Total actions executed */
    uint32_t rule_triggers;              /**< Total rule triggers */
    const char *config_path;             /**< Current config file path */
    bool config_modified;                /**< Config modified since load */
} ts_automation_status_t;

/**
 * @brief Configuration for automation engine initialization
 */
typedef struct {
    const char *config_path;             /**< Path to config file (NULL for default) */
    bool auto_start;                     /**< Auto-start after init */
    bool load_from_nvs;                  /**< Try loading from NVS if file not found */
} ts_automation_config_t;

/**
 * @brief Default configuration
 */
#define TS_AUTOMATION_CONFIG_DEFAULT() { \
    .config_path = NULL,                 \
    .auto_start = true,                  \
    .load_from_nvs = true,               \
}

/*===========================================================================*/
/*                           Initialization                                   */
/*===========================================================================*/

/**
 * @brief Initialize the automation engine
 *
 * Loads configuration from SD card or NVS and initializes all subsystems.
 *
 * @param config Configuration options, or NULL for defaults
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 *      - ESP_ERR_NO_MEM if memory allocation fails
 *      - ESP_ERR_NOT_FOUND if config file not found
 */
esp_err_t ts_automation_init(const ts_automation_config_t *config);

/**
 * @brief Deinitialize the automation engine
 *
 * Stops all sources, rules, and releases resources.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_automation_deinit(void);

/**
 * @brief Check if automation engine is initialized
 *
 * @return true if initialized
 */
bool ts_automation_is_initialized(void);

/*===========================================================================*/
/*                           Control                                          */
/*===========================================================================*/

/**
 * @brief Start the automation engine
 *
 * Starts data collection, rule evaluation, and action execution.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_automation_start(void);

/**
 * @brief Stop the automation engine
 *
 * Stops all activity but keeps configuration loaded.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_automation_stop(void);

/**
 * @brief Pause the automation engine
 *
 * Pauses rule evaluation but continues data collection.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_automation_pause(void);

/**
 * @brief Resume the automation engine
 *
 * @return ESP_OK on success
 */
esp_err_t ts_automation_resume(void);

/*===========================================================================*/
/*                           Configuration                                    */
/*===========================================================================*/

/**
 * @brief Reload configuration from file
 *
 * @return ESP_OK on success
 */
esp_err_t ts_automation_reload(void);

/**
 * @brief Save current configuration to file
 *
 * @param path File path, or NULL for current path
 * @return ESP_OK on success
 */
esp_err_t ts_automation_save(const char *path);

/**
 * @brief Get current configuration as JSON string
 *
 * @param buffer Output buffer
 * @param buffer_size Buffer size
 * @return Actual JSON length, or -1 on error
 */
int ts_automation_get_config_json(char *buffer, size_t buffer_size);

/**
 * @brief Apply configuration from JSON string
 *
 * @param json JSON configuration string
 * @return ESP_OK on success
 */
esp_err_t ts_automation_apply_config_json(const char *json);

/*===========================================================================*/
/*                           Status                                           */
/*===========================================================================*/

/**
 * @brief Get automation engine status
 *
 * @param status Output status structure
 * @return ESP_OK on success
 */
esp_err_t ts_automation_get_status(ts_automation_status_t *status);

/**
 * @brief Get version string
 *
 * @return Version string "major.minor.patch"
 */
const char *ts_automation_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_AUTOMATION_H */
