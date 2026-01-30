/**
 * @file ts_ota.h
 * @brief TianShanOS OTA (Over-The-Air) Update System
 *
 * This module provides firmware update functionality via:
 * - HTTPS download from remote server
 * - SD card local file
 * - WebUI upload
 *
 * Features:
 * - Dual OTA partition support (A/B update)
 * - Automatic rollback on boot failure
 * - Progress callback for UI updates
 * - Version checking
 * - Optional signature verification
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_OTA_H
#define TS_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA update source type
 */
typedef enum {
    TS_OTA_SOURCE_HTTPS,    ///< Download from HTTPS URL (direct flash, esp_https_ota)
    TS_OTA_SOURCE_SDCARD,   ///< Load from SD card file (direct flash)
    TS_OTA_SOURCE_UPLOAD,   ///< Upload via WebUI/API
    TS_OTA_SOURCE_DOWNLOAD, ///< Download to recovery dir first, then flash (unified mode)
} ts_ota_source_t;

/**
 * @brief OTA update state
 */
typedef enum {
    TS_OTA_STATE_IDLE,              ///< No OTA in progress
    TS_OTA_STATE_CHECKING,          ///< Checking for updates
    TS_OTA_STATE_DOWNLOADING,       ///< Downloading firmware
    TS_OTA_STATE_VERIFYING,         ///< Verifying firmware
    TS_OTA_STATE_WRITING,           ///< Writing to flash
    TS_OTA_STATE_PENDING_REBOOT,    ///< Ready, waiting for reboot
    TS_OTA_STATE_ERROR,             ///< Error occurred
} ts_ota_state_t;

/**
 * @brief OTA error codes
 */
typedef enum {
    TS_OTA_ERR_NONE = 0,
    TS_OTA_ERR_INVALID_PARAM,
    TS_OTA_ERR_NO_PARTITION,
    TS_OTA_ERR_PARTITION_FULL,
    TS_OTA_ERR_CONNECTION_FAILED,
    TS_OTA_ERR_DOWNLOAD_FAILED,
    TS_OTA_ERR_VERIFY_FAILED,
    TS_OTA_ERR_WRITE_FAILED,
    TS_OTA_ERR_VERSION_MISMATCH,
    TS_OTA_ERR_SIGNATURE_INVALID,
    TS_OTA_ERR_FILE_NOT_FOUND,
    TS_OTA_ERR_ALREADY_RUNNING,
    TS_OTA_ERR_ABORTED,
    TS_OTA_ERR_TIMEOUT,
    TS_OTA_ERR_INTERNAL,
} ts_ota_error_t;

/**
 * @brief OTA progress information
 */
typedef struct {
    ts_ota_state_t state;       ///< Current state
    ts_ota_error_t error;       ///< Error code (if state == ERROR)
    size_t total_size;          ///< Total firmware size (0 if unknown)
    size_t received_size;       ///< Bytes received so far
    uint8_t progress_percent;   ///< Progress percentage (0-100)
    const char *status_msg;     ///< Human-readable status message
} ts_ota_progress_t;

/**
 * @brief OTA progress callback function type
 *
 * @param progress Progress information
 * @param user_data User-provided context
 */
typedef void (*ts_ota_progress_cb_t)(const ts_ota_progress_t *progress, void *user_data);

/**
 * @brief OTA update configuration
 */
typedef struct {
    ts_ota_source_t source;         ///< Update source type
    const char *url;                ///< HTTPS URL or file path
    const char *cert_pem;           ///< Server certificate (NULL for default)
    bool skip_cert_verify;          ///< Skip certificate verification (debug only)
    bool auto_reboot;               ///< Automatically reboot after update
    bool allow_downgrade;           ///< Allow downgrade to older version
    ts_ota_progress_cb_t progress_cb; ///< Progress callback (optional)
    void *user_data;                ///< User data for callback
} ts_ota_config_t;

/**
 * @brief Firmware version information
 */
typedef struct {
    char version[64];           ///< Version string (e.g., "1.0.0+commit.dirty.timestamp")
    char project_name[32];      ///< Project name
    char compile_time[32];      ///< Compile timestamp
    char compile_date[16];      ///< Compile date
    char idf_version[32];       ///< ESP-IDF version
    uint32_t secure_version;    ///< Secure version counter (anti-rollback)
} ts_ota_version_info_t;

/**
 * @brief Partition information
 */
typedef struct {
    char label[17];             ///< Partition label
    uint32_t address;           ///< Start address
    uint32_t size;              ///< Size in bytes
    bool is_running;            ///< Currently running from this partition
    bool is_bootable;           ///< Partition contains valid app
    ts_ota_version_info_t version; ///< Firmware version (if bootable)
} ts_ota_partition_info_t;

/**
 * @brief OTA status information
 */
typedef struct {
    ts_ota_state_t state;
    ts_ota_partition_info_t running;    ///< Currently running partition
    ts_ota_partition_info_t next;       ///< Next update partition
    bool pending_verify;                ///< New firmware pending verification
    uint32_t rollback_timeout;          ///< Seconds until auto-rollback
    uint32_t last_update_time;          ///< Timestamp of last successful update
} ts_ota_status_t;

// ============================================================================
//                              Core API
// ============================================================================

/**
 * @brief Initialize OTA subsystem
 *
 * Must be called before any other OTA functions.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_init(void);

/**
 * @brief Deinitialize OTA subsystem
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_deinit(void);

/**
 * @brief Start OTA update
 *
 * This function starts the OTA update process asynchronously.
 * Progress can be monitored via the callback function.
 *
 * @param config Update configuration
 * @return ESP_OK if update started successfully
 */
esp_err_t ts_ota_start(const ts_ota_config_t *config);

/**
 * @brief Abort ongoing OTA update
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_abort(void);

/**
 * @brief Get current OTA status
 *
 * @param status Output status structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_status(ts_ota_status_t *status);

/**
 * @brief Get current OTA progress
 *
 * @param progress Output progress structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_progress(ts_ota_progress_t *progress);

// ============================================================================
//                           Rollback API
// ============================================================================

/**
 * @brief Mark current firmware as valid
 *
 * Call this after successful boot to prevent automatic rollback.
 * Should be called after system verification (e.g., network connectivity).
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_mark_valid(void);

/**
 * @brief Check if current firmware is pending verification
 *
 * @return true if pending verification, false otherwise
 */
bool ts_ota_is_pending_verify(void);

/**
 * @brief Check if rollback is possible
 * 
 * Checks if there's a valid, bootable app in the other OTA partition
 * that can be rolled back to.
 *
 * @return true if rollback is possible, false otherwise
 */
bool ts_ota_can_rollback(void);

/**
 * @brief Rollback to previous firmware
 *
 * This will set the boot partition to the previous firmware
 * and reboot the system.
 *
 * @return ESP_OK on success (will not return on success as system reboots)
 */
esp_err_t ts_ota_rollback(void);

/**
 * @brief Get rollback timeout remaining
 *
 * @return Seconds remaining until auto-rollback, 0 if not pending
 */
uint32_t ts_ota_get_rollback_timeout(void);

// ============================================================================
//                           Version API
// ============================================================================

/**
 * @brief Get running firmware version info
 *
 * @param info Output version info structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_running_version(ts_ota_version_info_t *info);

/**
 * @brief Compare two version strings
 *
 * @param v1 First version string
 * @param v2 Second version string
 * @return -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
 */
int ts_ota_compare_versions(const char *v1, const char *v2);

/**
 * @brief Check if update is available from URL
 *
 * @param url URL to check for updates
 * @param available Output: true if newer version available
 * @param new_version Output: new version string (if available)
 * @param new_version_len Length of new_version buffer
 * @return ESP_OK on success
 */
esp_err_t ts_ota_check_update(const char *url, bool *available,
                               char *new_version, size_t new_version_len);

// ============================================================================
//                           Partition API
// ============================================================================

/**
 * @brief Get running partition information
 *
 * @param info Output partition info structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_running_partition_info(ts_ota_partition_info_t *info);

/**
 * @brief Get next update partition information
 *
 * @param info Output partition info structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_next_partition_info(ts_ota_partition_info_t *info);

/**
 * @brief Get boot partition information
 *
 * @param info Output partition info structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_boot_partition_info(ts_ota_partition_info_t *info);

// ============================================================================
//                           Upload API (for WebUI)
// ============================================================================

/**
 * @brief Begin firmware upload
 *
 * Call this before writing firmware data chunks.
 *
 * @param total_size Total firmware size (0 if unknown)
 * @return ESP_OK on success
 */
esp_err_t ts_ota_upload_begin(size_t total_size);

/**
 * @brief Write firmware data chunk
 *
 * @param data Data buffer
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t ts_ota_upload_write(const void *data, size_t len);

/**
 * @brief Finish firmware upload
 *
 * Finalizes the update and optionally reboots.
 *
 * @param auto_reboot Reboot after completion
 * @return ESP_OK on success
 */
esp_err_t ts_ota_upload_end(bool auto_reboot);

/**
 * @brief Abort firmware upload
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_upload_abort(void);

// ============================================================================
//                           Event Definitions
// ============================================================================

/**
 * @brief OTA event base - use ts_event.h definition for consistency
 */
// Note: TS_EVENT_BASE_OTA is defined in ts_event.h as "ts_ota"
// ============================================================================
//                           Internal API (for OTA sub-modules)
// ============================================================================

/**
 * @brief Update OTA progress (internal use by ts_ota_https/sdcard)
 * 
 * @param state Current OTA state
 * @param received Bytes received so far
 * @param total Total bytes expected
 * @param msg Status message
 */
void ts_ota_update_progress(ts_ota_state_t state, size_t received, size_t total, const char *msg);

/**
 * @brief Set OTA error state (internal use by ts_ota_https/sdcard)
 * 
 * @param error Error code
 * @param msg Error message
 */
void ts_ota_set_error(ts_ota_error_t error, const char *msg);

/**
 * @brief Mark OTA as completed (internal use by ts_ota_https/sdcard)
 * 
 * @param msg Status message
 */
void ts_ota_set_completed(const char *msg);

/**
 * @brief Abort HTTPS OTA download (internal use)
 */
void ts_ota_abort_https(void);

/**
 * @brief Check if HTTPS OTA is running
 * @return true if running
 */
bool ts_ota_https_is_running(void);

// ============================================================================
//                           WWW Partition OTA API
// ============================================================================

/**
 * @brief Initialize WWW OTA module
 * @return ESP_OK on success
 */
esp_err_t ts_ota_www_init(void);

/**
 * @brief Start WWW partition OTA from URL
 *
 * Downloads www.bin and writes to the www (SPIFFS) partition.
 * This is typically the second step after app OTA completes.
 *
 * @param url URL to download www.bin from
 * @param skip_cert_verify Skip SSL certificate verification
 * @param progress_cb Progress callback (optional)
 * @param user_data User data for callback
 * @return ESP_OK if started successfully
 */
esp_err_t ts_ota_www_start(const char *url, bool skip_cert_verify,
                            ts_ota_progress_cb_t progress_cb, void *user_data);

/**
 * @brief Start WWW partition OTA from SD card file
 *
 * Reads www.bin from SD card and writes to the www (SPIFFS) partition.
 *
 * @param filepath Path to www.bin file on SD card
 * @param progress_cb Progress callback (optional)
 * @param user_data User data for callback
 * @return ESP_OK if started successfully
 */
esp_err_t ts_ota_www_start_sdcard(const char *filepath,
                                   ts_ota_progress_cb_t progress_cb,
                                   void *user_data);

/**
 * @brief Abort WWW partition OTA
 * @return ESP_OK on success
 */
esp_err_t ts_ota_www_abort(void);

/**
 * @brief Check if WWW OTA is running
 * @return true if running
 */
bool ts_ota_www_is_running(void);

/**
 * @brief Get WWW OTA progress
 * @param progress Output progress structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_www_get_progress(ts_ota_progress_t *progress);

// ============================================================================
//                           SD Card Recovery
// ============================================================================

/**
 * @brief Check for SD card recovery and execute if needed
 *
 * This function should be called immediately after SD card is mounted.
 * It checks for /sdcard/recovery/ directory and performs recovery if:
 * - manifest.json has different version than current
 * - force flag is set in manifest
 *
 * Recovery will:
 * 1. Flash firmware to OTA partition
 * 2. Flash WebUI to www partition (if present)
 * 3. Delete recovery files (if delete_after is true)
 * 4. Reboot device
 *
 * @return
 *   - ESP_OK: No recovery needed or recovery successful (will reboot)
 *   - ESP_ERR_NOT_FOUND: No recovery directory/files
 *   - Other: Recovery failed
 */
esp_err_t ts_ota_check_recovery(void);

/**
 * @brief Create recovery manifest file
 *
 * Used to prepare for recovery update. After creating manifest,
 * copy firmware and www.bin to /sdcard/recovery/ and reboot.
 *
 * @param version Target version string (for comparison)
 * @param force Force recovery regardless of version
 * @return ESP_OK on success
 */
esp_err_t ts_ota_create_recovery_manifest(const char *version, bool force);

// ============================================================================
//                    Unified Download API (Recovery Directory Mode)
// ============================================================================

/**
 * @brief Download firmware from URL to recovery directory
 *
 * All OTA sources (HTTPS/HTTP) use this unified download API.
 * Files are saved to /sdcard/recovery/ then flashed using sd card OTA.
 *
 * @param url Firmware download URL (http:// or https://)
 * @param skip_cert_verify Skip SSL certificate verification
 * @param progress_cb Progress callback (optional)
 * @param user_data User data for callback
 * @param auto_flash Automatically flash after download
 * @return ESP_OK if download started successfully
 */
esp_err_t ts_ota_download_firmware(const char *url, bool skip_cert_verify,
                                    ts_ota_progress_cb_t progress_cb, void *user_data,
                                    bool auto_flash);

/**
 * @brief Save uploaded firmware/www data to recovery directory
 *
 * Used by WebUI upload handlers. Data is saved to /sdcard/recovery/
 * then optionally flashed.
 *
 * @param data Binary data
 * @param len Data length
 * @param is_firmware true for firmware, false for www
 * @param auto_flash Automatically flash after saving
 * @return ESP_OK on success
 */
esp_err_t ts_ota_save_upload(const void *data, size_t len, bool is_firmware, bool auto_flash);

/**
 * @brief Flash firmware from recovery directory
 *
 * Reads /sdcard/recovery/TianShanOS.bin and writes to OTA partition.
 *
 * @param auto_reboot Reboot after flashing
 * @return ESP_OK if started successfully
 */
esp_err_t ts_ota_flash_from_recovery(bool auto_reboot);

/**
 * @brief Flash www from recovery directory
 *
 * Reads /sdcard/recovery/www.bin and writes to www partition.
 *
 * @return ESP_OK if started successfully
 */
esp_err_t ts_ota_www_from_recovery(void);

/**
 * @brief Check recovery directory status
 *
 * @param has_firmware Output: firmware file exists
 * @param has_www Output: www file exists
 * @param firmware_version Output: firmware version (64 byte buffer)
 * @return ESP_OK on success
 */
esp_err_t ts_ota_check_recovery_files(bool *has_firmware, bool *has_www, char *firmware_version);

/**
 * @brief Clean recovery directory
 *
 * Removes all files in /sdcard/recovery/
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_clean_recovery(void);

/**
 * @brief Abort ongoing download
 */
void ts_ota_download_abort(void);

/**
 * @brief Check if download is running
 * @return true if running
 */
bool ts_ota_download_is_running(void);

// ============================================================================
//                           Event IDs
// ============================================================================
/**
 * @brief OTA event IDs
 */
typedef enum {
    TS_EVENT_OTA_STARTED,           ///< OTA update started
    TS_EVENT_OTA_PROGRESS,          ///< Progress update
    TS_EVENT_OTA_COMPLETED,         ///< Update completed successfully
    TS_EVENT_OTA_FAILED,            ///< Update failed
    TS_EVENT_OTA_ABORTED,           ///< Update aborted by user
    TS_EVENT_OTA_PENDING_REBOOT,    ///< Waiting for reboot
    TS_EVENT_OTA_ROLLBACK_PENDING,  ///< Rollback is pending
    TS_EVENT_OTA_ROLLBACK_EXECUTED, ///< Rollback was executed
    TS_EVENT_OTA_VALIDATED,         ///< Firmware validated
} ts_event_ota_id_t;

#ifdef __cplusplus
}
#endif

#endif // TS_OTA_H
