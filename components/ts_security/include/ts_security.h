/**
 * @file ts_security.h
 * @brief TianShanOS Security API
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Permission levels */
typedef enum {
    TS_PERM_NONE = 0,
    TS_PERM_READ = 1,
    TS_PERM_WRITE = 2,
    TS_PERM_ADMIN = 3,
    TS_PERM_ROOT = 4
} ts_perm_level_t;

/** Key types */
typedef enum {
    TS_KEY_TYPE_AES128,
    TS_KEY_TYPE_AES256,
    TS_KEY_TYPE_RSA2048,
    TS_KEY_TYPE_RSA4096,
    TS_KEY_TYPE_EC_P256,
    TS_KEY_TYPE_ED25519
} ts_key_type_t;

/** Certificate types */
typedef enum {
    TS_CERT_TYPE_CA,
    TS_CERT_TYPE_SERVER,
    TS_CERT_TYPE_CLIENT
} ts_cert_type_t;

/** Session info */
typedef struct {
    uint32_t session_id;
    ts_perm_level_t level;
    uint32_t created_at;
    uint32_t expires_at;
    char client_id[32];
} ts_session_t;

/**
 * @brief Initialize security subsystem
 */
esp_err_t ts_security_init(void);

/**
 * @brief Deinitialize security subsystem
 */
esp_err_t ts_security_deinit(void);

/**
 * @brief Generate random bytes
 */
esp_err_t ts_security_random(void *buf, size_t len);

/**
 * @brief Generate a new key pair
 */
esp_err_t ts_security_generate_key(const char *name, ts_key_type_t type);

/**
 * @brief Load key from storage
 */
esp_err_t ts_security_load_key(const char *name, void *key, size_t *key_len);

/**
 * @brief Store key to storage
 */
esp_err_t ts_security_store_key(const char *name, const void *key, size_t key_len);

/**
 * @brief Delete key
 */
esp_err_t ts_security_delete_key(const char *name);

/**
 * @brief Load certificate
 */
esp_err_t ts_security_load_cert(const char *name, ts_cert_type_t type,
                                 void *cert, size_t *cert_len);

/**
 * @brief Store certificate
 */
esp_err_t ts_security_store_cert(const char *name, ts_cert_type_t type,
                                  const void *cert, size_t cert_len);

/**
 * @brief Create session
 */
esp_err_t ts_security_create_session(const char *client_id, ts_perm_level_t level,
                                      uint32_t *session_id);

/**
 * @brief Validate session
 */
esp_err_t ts_security_validate_session(uint32_t session_id, ts_session_t *session);

/**
 * @brief Destroy session
 */
esp_err_t ts_security_destroy_session(uint32_t session_id);

/**
 * @brief Check permission
 */
bool ts_security_check_permission(uint32_t session_id, ts_perm_level_t required);

/**
 * @brief Generate auth token
 */
esp_err_t ts_security_generate_token(uint32_t session_id, char *token, size_t max_len);

/**
 * @brief Validate auth token
 */
esp_err_t ts_security_validate_token(const char *token, uint32_t *session_id);

#ifdef __cplusplus
}
#endif
