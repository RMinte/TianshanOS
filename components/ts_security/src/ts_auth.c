/**
 * @file ts_auth.c
 * @brief Authentication Helpers
 */

#include "ts_security.h"
#include "ts_crypto.h"
#include "ts_log.h"
#include <string.h>

#define TAG "ts_auth"

/**
 * @brief Verify password against stored hash
 */
esp_err_t ts_auth_verify_password(const char *username, const char *password,
                                   ts_perm_level_t *level)
{
    if (!username || !password) return ESP_ERR_INVALID_ARG;
    
    // TODO: Implement proper password storage and verification
    // For now, use simple default credentials
    
    if (strcmp(username, "admin") == 0 && strcmp(password, "tianshan") == 0) {
        if (level) *level = TS_PERM_ADMIN;
        return ESP_OK;
    }
    
    if (strcmp(username, "root") == 0 && strcmp(password, "root") == 0) {
        if (level) *level = TS_PERM_ROOT;
        return ESP_OK;
    }
    
    if (strcmp(username, "user") == 0 && strcmp(password, "user") == 0) {
        if (level) *level = TS_PERM_WRITE;
        return ESP_OK;
    }
    
    return ESP_ERR_INVALID_ARG;
}

/**
 * @brief Set password for user
 */
esp_err_t ts_auth_set_password(const char *username, const char *password)
{
    if (!username || !password) return ESP_ERR_INVALID_ARG;
    
    // Hash password
    uint8_t hash[32];
    esp_err_t ret = ts_crypto_hash(TS_HASH_SHA256, password, strlen(password), hash, sizeof(hash));
    if (ret != ESP_OK) return ret;
    
    // Store hash
    char key_name[48];
    snprintf(key_name, sizeof(key_name), "pwd_%s", username);
    
    return ts_security_store_key(key_name, hash, sizeof(hash));
}

/**
 * @brief Login and create session
 */
esp_err_t ts_auth_login(const char *username, const char *password,
                         uint32_t *session_id)
{
    ts_perm_level_t level;
    esp_err_t ret = ts_auth_verify_password(username, password, &level);
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Login failed for user: %s", username);
        return ret;
    }
    
    ret = ts_security_create_session(username, level, session_id);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "User %s logged in (level %d)", username, level);
    }
    return ret;
}

/**
 * @brief Logout and destroy session
 */
esp_err_t ts_auth_logout(uint32_t session_id)
{
    return ts_security_destroy_session(session_id);
}

/**
 * @brief Validate request authentication
 */
esp_err_t ts_auth_validate_request(const char *auth_header, uint32_t *session_id,
                                    ts_perm_level_t *level)
{
    if (!auth_header) return ESP_ERR_INVALID_ARG;
    
    // Support "Bearer <token>" format
    if (strncmp(auth_header, "Bearer ", 7) == 0) {
        const char *token = auth_header + 7;
        esp_err_t ret = ts_security_validate_token(token, session_id);
        if (ret == ESP_OK && level) {
            ts_session_t session;
            if (ts_security_validate_session(*session_id, &session) == ESP_OK) {
                *level = session.level;
            }
        }
        return ret;
    }
    
    return ESP_ERR_NOT_SUPPORTED;
}
