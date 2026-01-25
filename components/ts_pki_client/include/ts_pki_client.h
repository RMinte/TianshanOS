/**
 * @file ts_pki_client.h
 * @brief TianShanOS PKI Auto-Enrollment Client
 *
 * Provides automatic certificate enrollment with the PKI server:
 * - CSR submission to PKI server
 * - Polling for certificate approval
 * - Certificate and CA chain installation
 * - Background auto-enrollment service
 *
 * AUTO-ENROLLMENT WORKFLOW:
 * =========================
 * 1. ESP32 boots → checks certificate status
 * 2. If no cert → generates ECDSA P-256 key pair
 * 3. Creates CSR with device ID and IP SAN
 * 4. Submits CSR to PKI server (POST /api/csr)
 * 5. Polls PKI server for approval (GET /api/csr/{device_id})
 * 6. On approval → downloads certificate and CA chain
 * 7. Installs certificates for mTLS use
 *
 * @copyright Copyright (c) 2026 TianShanOS Project
 */

#ifndef TS_PKI_CLIENT_H
#define TS_PKI_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

/** Default PKI server port */
#define TS_PKI_DEFAULT_PORT         57443

/** Default polling interval (seconds) */
#define TS_PKI_POLL_INTERVAL_SEC    10

/** Maximum polling attempts before giving up */
#define TS_PKI_MAX_POLL_ATTEMPTS    360  /* 1 hour at 10s interval */

/** HTTP timeout (milliseconds) */
#define TS_PKI_HTTP_TIMEOUT_MS      30000

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief Enrollment status from PKI server
 */
typedef enum {
    TS_PKI_ENROLL_PENDING = 0,       /**< CSR submitted, awaiting approval */
    TS_PKI_ENROLL_APPROVED,          /**< Certificate approved and ready */
    TS_PKI_ENROLL_REJECTED,          /**< CSR was rejected */
    TS_PKI_ENROLL_NOT_FOUND,         /**< No CSR found for device */
    TS_PKI_ENROLL_ERROR              /**< Communication or server error */
} ts_pki_enroll_status_t;

/**
 * @brief PKI client configuration
 */
typedef struct {
    char server_host[64];            /**< PKI server hostname or IP */
    uint16_t server_port;            /**< PKI server port (default: 8443) */
    bool use_https;                  /**< Use HTTPS (default: false for HTTP) */
    uint16_t poll_interval_sec;      /**< Polling interval in seconds */
    uint16_t max_poll_attempts;      /**< Max polling attempts (0 = infinite) */
    bool auto_start;                 /**< Start enrollment on init */
} ts_pki_client_config_t;

/**
 * @brief Enrollment progress callback
 */
typedef void (*ts_pki_enroll_callback_t)(ts_pki_enroll_status_t status, 
                                          const char *message, 
                                          void *user_data);

/*===========================================================================*/
/*                         Initialization                                     */
/*===========================================================================*/

/**
 * @brief Initialize PKI client with default configuration
 *
 * @return ESP_OK on success
 */
esp_err_t ts_pki_client_init(void);

/**
 * @brief Initialize PKI client with custom configuration
 *
 * @param config Client configuration
 * @return ESP_OK on success
 */
esp_err_t ts_pki_client_init_with_config(const ts_pki_client_config_t *config);

/**
 * @brief Deinitialize PKI client
 */
void ts_pki_client_deinit(void);

/**
 * @brief Get default configuration
 *
 * @param config Output configuration with defaults
 */
void ts_pki_client_get_default_config(ts_pki_client_config_t *config);

/*===========================================================================*/
/*                      Manual Enrollment Operations                          */
/*===========================================================================*/

/**
 * @brief Submit CSR to PKI server
 *
 * Generates CSR (if needed) and submits to PKI server.
 * Returns immediately - use polling or callback for result.
 *
 * @return ESP_OK if CSR submitted successfully
 * @return ESP_ERR_INVALID_STATE if already enrolled
 */
esp_err_t ts_pki_client_submit_csr(void);

/**
 * @brief Check enrollment status on PKI server
 *
 * @param status Output enrollment status
 * @return ESP_OK on success
 */
esp_err_t ts_pki_client_check_status(ts_pki_enroll_status_t *status);

/**
 * @brief Download and install approved certificate
 *
 * Call after enrollment status is APPROVED.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not approved yet
 */
esp_err_t ts_pki_client_install_certificate(void);

/**
 * @brief Perform full enrollment (blocking)
 *
 * Submits CSR and waits for approval, then installs certificate.
 * Blocks until complete, rejected, or timeout.
 *
 * @param timeout_sec Maximum seconds to wait (0 = use config max)
 * @return ESP_OK on success
 * @return ESP_ERR_TIMEOUT if approval times out
 */
esp_err_t ts_pki_client_enroll_blocking(uint32_t timeout_sec);

/*===========================================================================*/
/*                      Background Enrollment Service                         */
/*===========================================================================*/

/**
 * @brief Start background enrollment service
 *
 * Runs enrollment in background task with automatic retry.
 *
 * @param callback Progress callback (can be NULL)
 * @param user_data User data for callback
 * @return ESP_OK on success
 */
esp_err_t ts_pki_client_start_auto_enroll(ts_pki_enroll_callback_t callback,
                                           void *user_data);

/**
 * @brief Stop background enrollment service
 */
void ts_pki_client_stop_auto_enroll(void);

/**
 * @brief Check if auto-enrollment is running
 *
 * @return true if background service is active
 */
bool ts_pki_client_is_enrolling(void);

/*===========================================================================*/
/*                          Server Configuration                              */
/*===========================================================================*/

/**
 * @brief Set PKI server address
 *
 * @param host Server hostname or IP address
 * @param port Server port
 * @return ESP_OK on success
 */
esp_err_t ts_pki_client_set_server(const char *host, uint16_t port);

/**
 * @brief Get current PKI server address
 *
 * @param host Output buffer for hostname (at least 64 bytes)
 * @param port Output port
 * @return ESP_OK on success
 */
esp_err_t ts_pki_client_get_server(char *host, uint16_t *port);

/*===========================================================================*/
/*                          Utility Functions                                 */
/*===========================================================================*/

/**
 * @brief Convert enrollment status to string
 *
 * @param status Status value
 * @return Status string
 */
const char *ts_pki_enroll_status_to_str(ts_pki_enroll_status_t status);

/**
 * @brief Check if PKI server is reachable
 *
 * @return true if server responds to health check
 */
bool ts_pki_client_is_server_reachable(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_PKI_CLIENT_H */
