/**
 * @file ts_cert.h
 * @brief TianShanOS PKI Certificate Management
 * 
 * Provides X.509 certificate and CSR operations for device identity:
 * - ECDSA P-256 key pair generation
 * - CSR creation with SAN (Subject Alternative Name) extension
 * - Certificate storage and retrieval from NVS
 * - Certificate chain validation
 * 
 * This component implements the device-side PKI functionality for mTLS.
 * The CA server (step-ca) signs CSRs to issue device certificates.
 * 
 * WORKFLOW:
 * =========
 * 1. Device generates ECDSA P-256 key pair (stored in NVS)
 * 2. Device creates CSR with device ID and IP SAN
 * 3. CSR is submitted to CA (via network or TF card)
 * 4. CA signs and returns certificate
 * 5. Certificate is stored in NVS for mTLS use
 * 
 * @copyright Copyright (c) 2026 TianShanOS Project
 */

#ifndef TS_CERT_H
#define TS_CERT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

/** Maximum device ID length (e.g., "TIANSHAN-RM01-0001") */
#define TS_CERT_DEVICE_ID_MAX_LEN   64

/** Maximum IP SAN entries */
#define TS_CERT_MAX_IP_SANS         4

/** Maximum DNS SAN entries */
#define TS_CERT_MAX_DNS_SANS        4

/** Maximum CSR PEM size */
#define TS_CERT_CSR_MAX_LEN         2048

/** Maximum certificate PEM size */
#define TS_CERT_PEM_MAX_LEN         4096

/** Maximum private key PEM size */
#define TS_CERT_KEY_MAX_LEN         512

/** Maximum CA chain PEM size (Root + Intermediate) */
#define TS_CERT_CA_CHAIN_MAX_LEN    4096

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief PKI activation status
 */
typedef enum {
    TS_CERT_STATUS_NOT_INITIALIZED = 0,  /**< No key pair generated */
    TS_CERT_STATUS_KEY_GENERATED,        /**< Key pair exists, no CSR yet */
    TS_CERT_STATUS_CSR_PENDING,          /**< CSR generated, awaiting certificate */
    TS_CERT_STATUS_ACTIVATED,            /**< Certificate installed, ready for mTLS */
    TS_CERT_STATUS_EXPIRED,              /**< Certificate expired */
    TS_CERT_STATUS_ERROR                 /**< Error state */
} ts_cert_status_t;

/**
 * @brief Certificate information
 */
typedef struct {
    char subject_cn[TS_CERT_DEVICE_ID_MAX_LEN];  /**< Subject Common Name */
    char issuer_cn[TS_CERT_DEVICE_ID_MAX_LEN];   /**< Issuer Common Name */
    int64_t not_before;                          /**< Valid from (Unix timestamp) */
    int64_t not_after;                           /**< Valid until (Unix timestamp) */
    char serial[64];                             /**< Serial number (hex) */
    bool is_valid;                               /**< Current validity */
    int days_until_expiry;                       /**< Days until expiration */
} ts_cert_info_t;

/**
 * @brief CSR generation options
 */
typedef struct {
    const char *device_id;                       /**< Device identifier (CN) */
    const char *organization;                    /**< Organization (O), optional */
    const char *org_unit;                        /**< Organizational Unit (OU), optional */
    uint32_t ip_sans[TS_CERT_MAX_IP_SANS];       /**< IP addresses for SAN (network order) */
    uint8_t ip_san_count;                        /**< Number of IP SANs */
    const char *dns_sans[TS_CERT_MAX_DNS_SANS];  /**< DNS names for SAN */
    uint8_t dns_san_count;                       /**< Number of DNS SANs */
} ts_cert_csr_opts_t;

/**
 * @brief PKI status information
 */
typedef struct {
    ts_cert_status_t status;                     /**< Current status */
    bool has_private_key;                        /**< Private key exists in NVS */
    bool has_certificate;                        /**< Certificate exists in NVS */
    bool has_ca_chain;                           /**< CA chain exists in NVS */
    ts_cert_info_t cert_info;                    /**< Certificate details (if exists) */
} ts_cert_pki_status_t;

/*===========================================================================*/
/*                         Initialization                                     */
/*===========================================================================*/

/**
 * @brief Initialize the certificate component
 * 
 * Loads existing credentials from NVS if present.
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_cert_init(void);

/**
 * @brief Deinitialize and free resources
 */
void ts_cert_deinit(void);

/*===========================================================================*/
/*                         Key Pair Management                                */
/*===========================================================================*/

/**
 * @brief Generate a new ECDSA P-256 key pair
 * 
 * The private key is stored in NVS. Overwrites existing key if present.
 * 
 * @return ESP_OK on success
 * @return ESP_ERR_NO_MEM if memory allocation fails
 */
esp_err_t ts_cert_generate_keypair(void);

/**
 * @brief Check if a private key exists
 * 
 * @return true if key exists in NVS
 */
bool ts_cert_has_keypair(void);

/**
 * @brief Delete the stored key pair
 * 
 * WARNING: This also invalidates any issued certificate.
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_cert_delete_keypair(void);

/*===========================================================================*/
/*                           CSR Generation                                   */
/*===========================================================================*/

/**
 * @brief Generate a Certificate Signing Request
 * 
 * Creates a CSR using the stored private key with the specified options.
 * The CSR includes SAN extensions for IP addresses.
 * 
 * @param opts CSR generation options
 * @param csr_pem Output buffer for PEM-encoded CSR
 * @param csr_len Input: buffer size; Output: actual length
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if no private key exists
 * @return ESP_ERR_INVALID_ARG if opts is invalid
 */
esp_err_t ts_cert_generate_csr(const ts_cert_csr_opts_t *opts, 
                                char *csr_pem, size_t *csr_len);

/**
 * @brief Generate CSR with default options
 * 
 * Uses device ID from configuration and current IP address.
 * 
 * @param csr_pem Output buffer for PEM-encoded CSR
 * @param csr_len Input: buffer size; Output: actual length
 * @return ESP_OK on success
 */
esp_err_t ts_cert_generate_csr_default(char *csr_pem, size_t *csr_len);

/*===========================================================================*/
/*                        Certificate Management                              */
/*===========================================================================*/

/**
 * @brief Install a signed certificate
 * 
 * Stores the certificate in NVS after validating it matches the private key.
 * 
 * @param cert_pem PEM-encoded certificate
 * @param cert_len Certificate length
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if certificate is invalid
 * @return ESP_ERR_INVALID_STATE if certificate doesn't match private key
 */
esp_err_t ts_cert_install_certificate(const char *cert_pem, size_t cert_len);

/**
 * @brief Install CA certificate chain
 * 
 * Stores the CA chain (Intermediate + Root) for verifying peer certificates.
 * 
 * @param ca_chain_pem PEM-encoded CA chain
 * @param ca_chain_len CA chain length
 * @return ESP_OK on success
 */
esp_err_t ts_cert_install_ca_chain(const char *ca_chain_pem, size_t ca_chain_len);

/**
 * @brief Get the device certificate
 * 
 * @param cert_pem Output buffer for PEM-encoded certificate
 * @param cert_len Input: buffer size; Output: actual length
 * @return ESP_OK on success
 * @return ESP_ERR_NOT_FOUND if no certificate installed
 */
esp_err_t ts_cert_get_certificate(char *cert_pem, size_t *cert_len);

/**
 * @brief Get the private key (for mTLS)
 * 
 * WARNING: Handle with care - do not log or expose this data.
 * 
 * @param key_pem Output buffer for PEM-encoded private key
 * @param key_len Input: buffer size; Output: actual length
 * @return ESP_OK on success
 * @return ESP_ERR_NOT_FOUND if no key exists
 */
esp_err_t ts_cert_get_private_key(char *key_pem, size_t *key_len);

/**
 * @brief Get the CA certificate chain
 * 
 * @param ca_chain_pem Output buffer for PEM-encoded CA chain
 * @param ca_chain_len Input: buffer size; Output: actual length
 * @return ESP_OK on success
 * @return ESP_ERR_NOT_FOUND if no CA chain installed
 */
esp_err_t ts_cert_get_ca_chain(char *ca_chain_pem, size_t *ca_chain_len);

/*===========================================================================*/
/*                           Status & Info                                    */
/*===========================================================================*/

/**
 * @brief Get current PKI status
 * 
 * @param status Output status information
 * @return ESP_OK on success
 */
esp_err_t ts_cert_get_status(ts_cert_pki_status_t *status);

/**
 * @brief Refresh PKI status
 * 
 * Re-validates certificate against current system time.
 * Should be called after NTP time sync to update status.
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_cert_refresh_status(void);

/**
 * @brief Get certificate information
 * 
 * @param info Output certificate information
 * @return ESP_OK on success
 * @return ESP_ERR_NOT_FOUND if no certificate installed
 */
esp_err_t ts_cert_get_info(ts_cert_info_t *info);

/**
 * @brief Check if certificate is valid
 * 
 * Verifies certificate is not expired and matches the private key.
 * 
 * @return true if certificate is valid
 */
bool ts_cert_is_valid(void);

/**
 * @brief Get days until certificate expires
 * 
 * @return Days until expiry, negative if expired, INT_MAX if no certificate
 */
int ts_cert_days_until_expiry(void);

/*===========================================================================*/
/*                          Factory Reset                                     */
/*===========================================================================*/

/**
 * @brief Delete all PKI credentials
 * 
 * Removes private key, certificate, and CA chain from NVS.
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_cert_factory_reset(void);

/*===========================================================================*/
/*                          Utility Functions                                 */
/*===========================================================================*/

/**
 * @brief Convert status enum to string
 * 
 * @param status Status value
 * @return Status string
 */
const char *ts_cert_status_to_str(ts_cert_status_t status);

/**
 * @brief Parse certificate PEM and extract info
 * 
 * @param cert_pem PEM-encoded certificate
 * @param cert_len Certificate length
 * @param info Output certificate information
 * @return ESP_OK on success
 */
esp_err_t ts_cert_parse_certificate(const char *cert_pem, size_t cert_len, 
                                     ts_cert_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* TS_CERT_H */
