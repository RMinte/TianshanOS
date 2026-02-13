/**
 * @file ts_i18n.h
 * @brief TianShanOS Internationalization (i18n) System
 * 
 * Multi-language support for console messages and UI strings.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_I18N_H
#define TS_I18N_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Language Codes                                */
/*===========================================================================*/

/**
 * @brief Supported languages
 */
typedef enum {
    TS_LANG_EN = 0,             /**< English (default) */
    TS_LANG_ZH_CN,              /**< Simplified Chinese */
    TS_LANG_MAX
} ts_language_t;

/*===========================================================================*/
/*                              String IDs                                    */
/*===========================================================================*/

/**
 * @brief String identifiers for localization
 */
typedef enum {
    /* System messages */
    TS_STR_WELCOME = 0,
    TS_STR_VERSION,
    TS_STR_READY,
    TS_STR_ERROR,
    TS_STR_SUCCESS,
    TS_STR_FAILED,
    TS_STR_UNKNOWN_CMD,
    TS_STR_HELP_HEADER,
    TS_STR_USAGE,
    
    /* Common prompts */
    TS_STR_YES,
    TS_STR_NO,
    TS_STR_OK,
    TS_STR_CANCEL,
    TS_STR_CONFIRM,
    TS_STR_LOADING,
    TS_STR_PLEASE_WAIT,
    
    /* Device status */
    TS_STR_DEVICE_INFO,
    TS_STR_UPTIME,
    TS_STR_FREE_HEAP,
    TS_STR_CHIP_MODEL,
    TS_STR_FIRMWARE_VER,
    TS_STR_TEMPERATURE,
    
    /* Network messages */
    TS_STR_WIFI_CONNECTED,
    TS_STR_WIFI_DISCONNECTED,
    TS_STR_WIFI_SCANNING,
    TS_STR_WIFI_CONNECTING,
    TS_STR_IP_ADDRESS,
    TS_STR_MAC_ADDRESS,
    TS_STR_SIGNAL_STRENGTH,
    
    /* LED messages */
    TS_STR_LED_CONTROLLER,
    TS_STR_LED_COUNT,
    TS_STR_BRIGHTNESS,
    TS_STR_EFFECT,
    TS_STR_COLOR,
    
    /* Power messages */
    TS_STR_VOLTAGE,
    TS_STR_CURRENT,
    TS_STR_POWER,
    TS_STR_POWER_GOOD,
    TS_STR_POWER_OFF,
    
    /* Error messages */
    TS_STR_ERR_INVALID_ARG,
    TS_STR_ERR_NOT_FOUND,
    TS_STR_ERR_NO_MEM,
    TS_STR_ERR_TIMEOUT,
    TS_STR_ERR_NOT_SUPPORTED,
    TS_STR_ERR_INVALID_STATE,
    TS_STR_ERR_IO,
    
    /* Reboot/shutdown */
    TS_STR_REBOOTING,
    TS_STR_SHUTTING_DOWN,
    TS_STR_REBOOT_IN,
    
    /* Help messages */
    TS_STR_HELP_USE_CMD,           /**< "Use '<command> --help' for command details" */
    TS_STR_SSH_HELP_USE,           /**< "Use 'ssh --help' for usage information" */
    TS_STR_SSH_HELP_USAGE,
    TS_STR_SSH_HELP_DESC,
    TS_STR_SSH_HELP_CONN_OPTS,
    TS_STR_SSH_HELP_HOST,
    TS_STR_SSH_HELP_PORT,
    TS_STR_SSH_HELP_USER,
    TS_STR_SSH_HELP_PASSWORD,
    TS_STR_SSH_HELP_KEY,
    TS_STR_SSH_HELP_KEYID,
    TS_STR_SSH_HELP_EXEC,
    TS_STR_SSH_HELP_SHELL,
    TS_STR_SSH_HELP_FORWARD,
    TS_STR_SSH_HELP_TEST,
    TS_STR_SSH_HELP_TIMEOUT,
    TS_STR_SSH_HELP_VERBOSE,
    TS_STR_SSH_HELP_KEY_MGMT,
    TS_STR_SSH_HELP_KEYGEN,
    TS_STR_SSH_HELP_COPYID,
    TS_STR_SSH_HELP_REVOKE,
    TS_STR_SSH_HELP_TYPE,
    TS_STR_SSH_HELP_OUTPUT,
    TS_STR_SSH_HELP_COMMENT,
    TS_STR_SSH_HELP_GENERAL,
    TS_STR_SSH_HELP_HELP_OPT,
    TS_STR_SSH_HELP_EXAMPLES,
    TS_STR_SSH_HELP_EX_KEYGEN,
    TS_STR_SSH_HELP_EX_KEYGEN_CMD,
    TS_STR_SSH_HELP_EX_STORED,
    TS_STR_SSH_HELP_EX_KEY_LIST,
    TS_STR_SSH_HELP_EX_KEY_GEN,
    TS_STR_SSH_HELP_EX_SSH,
    TS_STR_SSH_HELP_EX_COPYID,
    TS_STR_SSH_HELP_EX_COPYID_CMD,
    TS_STR_SSH_HELP_EX_REVOKE,
    TS_STR_SSH_HELP_EX_REVOKE_CMD,
    TS_STR_SSH_HELP_EX_FILE,
    TS_STR_SSH_HELP_EX_FILE_CMD,
    TS_STR_SSH_HELP_NOTE,
    TS_STR_SSH_HELP_NOTE_HOSTS,
    
    TS_STR_MAX
} ts_string_id_t;

/*===========================================================================*/
/*                              Functions                                     */
/*===========================================================================*/

/**
 * @brief Initialize i18n system
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_i18n_init(void);

/**
 * @brief Deinitialize i18n system
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_i18n_deinit(void);

/**
 * @brief Set current language
 * 
 * @param lang Language code
 * @return ESP_OK on success
 */
esp_err_t ts_i18n_set_language(ts_language_t lang);

/**
 * @brief Get current language
 * 
 * @return Current language code
 */
ts_language_t ts_i18n_get_language(void);

/**
 * @brief Get language name string
 * 
 * @param lang Language code
 * @return Language name string
 */
const char *ts_i18n_get_language_name(ts_language_t lang);

/**
 * @brief Get localized string by ID
 * 
 * @param id String identifier
 * @return Localized string, or English fallback
 */
const char *ts_i18n_get(ts_string_id_t id);

/**
 * @brief Get localized string for specific language
 * 
 * @param lang Language code
 * @param id String identifier
 * @return Localized string
 */
const char *ts_i18n_get_lang(ts_language_t lang, ts_string_id_t id);

/**
 * @brief Format localized string with arguments
 * 
 * @param buf Output buffer
 * @param size Buffer size
 * @param id String identifier  
 * @param ... Format arguments
 * @return Number of characters written
 */
int ts_i18n_sprintf(char *buf, size_t size, ts_string_id_t id, ...);

/**
 * @brief Convenience macro for getting localized string
 */
#define TS_STR(id) ts_i18n_get(id)

/**
 * @brief Convenience macro for formatted localized string
 */
#define TS_STRF(buf, size, id, ...) ts_i18n_sprintf(buf, size, id, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* TS_I18N_H */
