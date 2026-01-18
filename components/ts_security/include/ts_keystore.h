/**
 * @file ts_keystore.h
 * @brief TianShanOS Secure Key Storage API
 * 
 * Provides secure storage for SSH private keys using ESP32's NVS encryption.
 * Keys are encrypted at rest using the HMAC-based NVS encryption scheme,
 * which derives encryption keys from an HMAC key stored in eFuse.
 * 
 * Features:
 * - AES-XTS encryption for keys stored in NVS
 * - Multiple key support with unique key IDs
 * - Key metadata (type, creation time, comment)
 * - Integration with SSH client for seamless authentication
 * 
 * @note This module requires NVS encryption to be configured:
 *       - CONFIG_NVS_ENCRYPTION=y
 *       - CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y
 * 
 * @copyright Copyright (c) 2026 TianShanOS Project
 */

#ifndef TS_KEYSTORE_H
#define TS_KEYSTORE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length of a key ID string
 */
#define TS_KEYSTORE_ID_MAX_LEN      32

/**
 * @brief Maximum length of a key comment
 */
#define TS_KEYSTORE_COMMENT_MAX_LEN 64

/**
 * @brief Maximum number of keys that can be stored
 */
#define TS_KEYSTORE_MAX_KEYS        8

/**
 * @brief Maximum private key size (RSA-4096 PEM is ~3KB)
 */
#define TS_KEYSTORE_PRIVKEY_MAX_LEN 4096

/**
 * @brief Maximum public key size (OpenSSH format)
 */
#define TS_KEYSTORE_PUBKEY_MAX_LEN  1024

/**
 * @brief Key type enumeration
 */
typedef enum {
    TS_KEYSTORE_TYPE_UNKNOWN = 0,   /**< Unknown key type */
    TS_KEYSTORE_TYPE_RSA_2048,      /**< RSA 2048-bit */
    TS_KEYSTORE_TYPE_RSA_4096,      /**< RSA 4096-bit */
    TS_KEYSTORE_TYPE_ECDSA_P256,    /**< ECDSA P-256 (secp256r1) */
    TS_KEYSTORE_TYPE_ECDSA_P384,    /**< ECDSA P-384 (secp384r1) */
} ts_keystore_key_type_t;

/**
 * @brief Key metadata structure
 */
typedef struct {
    char id[TS_KEYSTORE_ID_MAX_LEN];         /**< Unique key identifier */
    ts_keystore_key_type_t type;              /**< Key type */
    char comment[TS_KEYSTORE_COMMENT_MAX_LEN]; /**< Key comment */
    uint32_t created_at;                       /**< Creation timestamp (Unix epoch) */
    uint32_t last_used;                        /**< Last used timestamp */
    bool has_public_key;                       /**< Whether public key is stored */
} ts_keystore_key_info_t;

/**
 * @brief Key pair structure for import/export
 */
typedef struct {
    char *private_key;           /**< Private key in PEM format */
    size_t private_key_len;      /**< Private key length */
    char *public_key;            /**< Public key in OpenSSH format (optional) */
    size_t public_key_len;       /**< Public key length */
} ts_keystore_keypair_t;

/**
 * @brief Initialize the keystore
 * 
 * Initializes the secure NVS partition for key storage.
 * If NVS encryption is enabled, this will use the encrypted partition.
 * 
 * @return 
 *      - ESP_OK on success
 *      - ESP_ERR_NVS_NO_FREE_PAGES if NVS partition is full
 *      - ESP_ERR_NVS_NEW_VERSION_FOUND if NVS data version changed
 *      - ESP_FAIL on other errors
 */
esp_err_t ts_keystore_init(void);

/**
 * @brief Deinitialize the keystore
 * 
 * Closes the NVS handle and releases resources.
 * 
 * @return ESP_OK always
 */
esp_err_t ts_keystore_deinit(void);

/**
 * @brief Check if keystore is initialized
 * 
 * @return true if initialized, false otherwise
 */
bool ts_keystore_is_initialized(void);

/**
 * @brief Store a key pair
 * 
 * Stores a private key (and optionally public key) in the secure storage.
 * The private key is encrypted at rest using NVS encryption.
 * 
 * @param[in] id        Unique key identifier (max TS_KEYSTORE_ID_MAX_LEN chars)
 * @param[in] keypair   Key pair to store
 * @param[in] type      Key type
 * @param[in] comment   Optional comment (can be NULL)
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if id or keypair is invalid
 *      - ESP_ERR_NO_MEM if out of memory
 *      - ESP_ERR_NVS_NOT_ENOUGH_SPACE if storage is full
 *      - ESP_ERR_INVALID_STATE if keystore not initialized
 */
esp_err_t ts_keystore_store_key(const char *id, 
                                 const ts_keystore_keypair_t *keypair,
                                 ts_keystore_key_type_t type,
                                 const char *comment);

/**
 * @brief Load a private key
 * 
 * Retrieves the private key from secure storage.
 * Caller is responsible for freeing the returned buffer.
 * 
 * @param[in]  id               Key identifier
 * @param[out] private_key      Pointer to receive allocated private key buffer
 * @param[out] private_key_len  Pointer to receive private key length
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if key doesn't exist
 *      - ESP_ERR_NO_MEM if out of memory
 *      - ESP_ERR_INVALID_STATE if keystore not initialized
 */
esp_err_t ts_keystore_load_private_key(const char *id,
                                        char **private_key,
                                        size_t *private_key_len);

/**
 * @brief Load a public key
 * 
 * Retrieves the public key from secure storage.
 * Caller is responsible for freeing the returned buffer.
 * 
 * @param[in]  id              Key identifier
 * @param[out] public_key      Pointer to receive allocated public key buffer
 * @param[out] public_key_len  Pointer to receive public key length
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if key or public key doesn't exist
 *      - ESP_ERR_NO_MEM if out of memory
 *      - ESP_ERR_INVALID_STATE if keystore not initialized
 */
esp_err_t ts_keystore_load_public_key(const char *id,
                                       char **public_key,
                                       size_t *public_key_len);

/**
 * @brief Delete a key
 * 
 * Removes a key pair from secure storage.
 * 
 * @param[in] id  Key identifier
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if key doesn't exist
 *      - ESP_ERR_INVALID_STATE if keystore not initialized
 */
esp_err_t ts_keystore_delete_key(const char *id);

/**
 * @brief Check if a key exists
 * 
 * @param[in] id  Key identifier
 * 
 * @return true if key exists, false otherwise
 */
bool ts_keystore_key_exists(const char *id);

/**
 * @brief Get key info
 * 
 * Retrieves metadata about a stored key.
 * 
 * @param[in]  id    Key identifier
 * @param[out] info  Pointer to receive key info
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if key doesn't exist
 *      - ESP_ERR_INVALID_STATE if keystore not initialized
 */
esp_err_t ts_keystore_get_key_info(const char *id, ts_keystore_key_info_t *info);

/**
 * @brief List all stored keys
 * 
 * Retrieves a list of all stored key IDs and their metadata.
 * 
 * @param[out] keys       Array to receive key info (must be at least TS_KEYSTORE_MAX_KEYS)
 * @param[out] count      Pointer to receive actual number of keys
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if keys or count is NULL
 *      - ESP_ERR_INVALID_STATE if keystore not initialized
 */
esp_err_t ts_keystore_list_keys(ts_keystore_key_info_t *keys, size_t *count);

/**
 * @brief Update last used timestamp
 * 
 * Updates the last_used field for a key. Called automatically when
 * a key is used for SSH authentication.
 * 
 * @param[in] id  Key identifier
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if key doesn't exist
 */
esp_err_t ts_keystore_touch_key(const char *id);

/**
 * @brief Import a key from file
 * 
 * Convenience function to import a key pair from SD card or SPIFFS.
 * Reads private key from the specified path and public key from path.pub
 * 
 * @param[in] id        Key identifier to use
 * @param[in] path      Path to private key file (e.g., "/sdcard/id_rsa")
 * @param[in] comment   Optional comment
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if file doesn't exist
 *      - ESP_ERR_NO_MEM if out of memory
 */
esp_err_t ts_keystore_import_from_file(const char *id, 
                                        const char *path,
                                        const char *comment);

/**
 * @brief Export a key to file
 * 
 * Exports a key pair to SD card or SPIFFS.
 * 
 * @param[in] id    Key identifier
 * @param[in] path  Path for private key file
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if key doesn't exist
 */
esp_err_t ts_keystore_export_to_file(const char *id, const char *path);

/**
 * @brief Generate a new key pair and store it
 * 
 * Generates a new key pair and stores it in the keystore.
 * 
 * @param[in] id       Key identifier
 * @param[in] type     Key type to generate
 * @param[in] comment  Optional comment
 * 
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if type is invalid
 *      - ESP_ERR_NO_MEM if out of memory
 */
esp_err_t ts_keystore_generate_key(const char *id,
                                    ts_keystore_key_type_t type,
                                    const char *comment);

/**
 * @brief Get key type name string
 * 
 * @param[in] type  Key type
 * @return String representation of key type
 */
const char *ts_keystore_type_to_string(ts_keystore_key_type_t type);

/**
 * @brief Parse key type from string
 * 
 * @param[in] str  String representation (e.g., "rsa2048", "ecdsa")
 * @return Key type, or TS_KEYSTORE_TYPE_UNKNOWN if invalid
 */
ts_keystore_key_type_t ts_keystore_type_from_string(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* TS_KEYSTORE_H */
