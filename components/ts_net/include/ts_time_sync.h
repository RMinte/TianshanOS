/**
 * @file ts_time_sync.h
 * @brief TianShanOS Time Synchronization Module
 * 
 * Provides time synchronization via:
 * 1. NTP servers (primary method when network available)
 * 2. HTTP API from browser (fallback/manual sync)
 * 
 * @note ESP32 has no battery-backed RTC, time resets on reboot
 */

#ifndef TS_TIME_SYNC_H
#define TS_TIME_SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Time sync status
 */
typedef enum {
    TS_TIME_SYNC_NOT_STARTED,   /**< Sync not started */
    TS_TIME_SYNC_IN_PROGRESS,   /**< Sync in progress */
    TS_TIME_SYNC_COMPLETED,     /**< Sync completed successfully */
    TS_TIME_SYNC_FAILED,        /**< Sync failed */
} ts_time_sync_status_t;

/**
 * @brief Time sync source
 */
typedef enum {
    TS_TIME_SOURCE_NONE,        /**< Time not synced */
    TS_TIME_SOURCE_NTP,         /**< Synced from NTP server */
    TS_TIME_SOURCE_HTTP,        /**< Synced from browser/HTTP API */
    TS_TIME_SOURCE_MANUAL,      /**< Manually set */
} ts_time_source_t;

/**
 * @brief Time sync info
 */
typedef struct {
    ts_time_sync_status_t status;   /**< Current sync status */
    ts_time_source_t source;        /**< Time source */
    time_t last_sync;               /**< Last successful sync timestamp */
    char ntp_server[64];            /**< Current NTP server */
    int64_t offset_us;              /**< Time offset from last sync (microseconds) */
    uint32_t sync_count;            /**< Number of successful syncs */
} ts_time_sync_info_t;

/**
 * @brief Time sync configuration
 */
typedef struct {
    const char *ntp_server1;        /**< Primary NTP server (NULL to disable) */
    const char *ntp_server2;        /**< Secondary NTP server (optional) */
    const char *ntp_server3;        /**< Tertiary NTP server (optional) */
    const char *timezone;           /**< Timezone string (e.g., "CST-8") */
    uint32_t sync_interval_ms;      /**< NTP sync interval (default: 1 hour) */
    bool auto_start;                /**< Auto start NTP on init */
} ts_time_sync_config_t;

/**
 * @brief Default configuration
 */
#define TS_TIME_SYNC_CONFIG_DEFAULT() { \
    .ntp_server1 = "10.10.99.100", \
    .ntp_server2 = "10.10.99.99", \
    .ntp_server3 = "10.10.99.98", \
    .timezone = "CST-8", \
    .sync_interval_ms = 3600000, \
    .auto_start = true, \
}

/**
 * @brief Initialize time sync module
 * 
 * @param config Configuration (NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t ts_time_sync_init(const ts_time_sync_config_t *config);

/**
 * @brief Deinitialize time sync module
 */
void ts_time_sync_deinit(void);

/**
 * @brief Start NTP synchronization
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_time_sync_start_ntp(void);

/**
 * @brief Stop NTP synchronization
 */
void ts_time_sync_stop_ntp(void);

/**
 * @brief Set NTP server
 * 
 * @param server NTP server hostname or IP
 * @param index Server index (0 = primary, 1 = secondary)
 * @return ESP_OK on success
 */
esp_err_t ts_time_sync_set_ntp_server(const char *server, int index);

/**
 * @brief Set timezone
 * 
 * @param tz Timezone string (e.g., "CST-8", "EST5EDT")
 * @return ESP_OK on success
 */
esp_err_t ts_time_sync_set_timezone(const char *tz);

/**
 * @brief Set time from external source (browser/HTTP)
 * 
 * @param timestamp_ms Unix timestamp in milliseconds
 * @param source Time source identifier
 * @return ESP_OK on success
 */
esp_err_t ts_time_sync_set_time(int64_t timestamp_ms, ts_time_source_t source);

/**
 * @brief Get current time sync info
 * 
 * @param info Output info structure
 * @return ESP_OK on success
 */
esp_err_t ts_time_sync_get_info(ts_time_sync_info_t *info);

/**
 * @brief Check if time is synchronized
 * 
 * @return true if time has been synced
 */
bool ts_time_sync_is_synced(void);

/**
 * @brief Get current time in milliseconds
 * 
 * @return Unix timestamp in milliseconds, 0 if not synced
 */
int64_t ts_time_sync_get_time_ms(void);

/**
 * @brief Force immediate NTP sync
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_time_sync_force_ntp(void);

/**
 * @brief Check if system time needs synchronization
 * 
 * Returns true if system time is before year 2025, indicating
 * that time has not been properly set (ESP32 defaults to 1970).
 * 
 * @return true if time sync is needed (year < 2025)
 */
bool ts_time_sync_needs_sync(void);

/**
 * @brief Minimum valid year for time validation
 * 
 * System time before this year is considered invalid and needs sync.
 */
#define TS_TIME_MIN_VALID_YEAR 2025

#ifdef __cplusplus
}
#endif

#endif /* TS_TIME_SYNC_H */
