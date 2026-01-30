/**
 * @file ts_ap
 * @brief Authentication API and
 * 提供 auth.login / auth.lout / auth.status / auth.change_password API
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-30
 */

#include "ts_api.h"
#include "ts_security.h"
#include "ts_log.h"
#include "esp_timer.h"
#include <string.h>

#define TAG "api_auth"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static const char *perm_level_to_string(ts_perm_level_t level)
{
    switch (level) {
        case TS_PERM_NONE:  return "none";
        case TS_PERM_READ:  return "read";
        case TS_PERM_WRITE: return "write";
        case TS_PERM_ADMIN: return "admin";
        case TS_PERM_ROOT:  return "root";
        default:            return "unknown";
    }
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief auth.login - User login
 * 
 * Params: { "username": "admin", "password": "rm01" }
 * Returns: { "token": "...", "level": "admin", "expires_in": 86400, "password_changed": false }
 */
static esp_err_t api_auth_login(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *username_item = cJSON_GetObjectItem(params, "username");
    const cJSON *password_item = cJSON_GetObjectItem(params, "password");
    
    if (!cJSON_IsString(username_item) || !cJSON_IsString(password_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, 
                           "Missing required parameters: username, password");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *username = username_item->valuestring;
    const char *password = password_item->valuestring;
    
    uint32_t session_id;
    char token[64];
    
    esp_err_t ret = ts_auth_login(username, password, &session_id, token, sizeof(token));
    
    if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_error(result, TS_API_ERR_AUTH, 
                           "Account locked due to too many failed attempts");
        return ret;
    }
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_AUTH, "Invalid username or password");
        return ret;
    }
    
    /* 获取权限级别 */
    ts_perm_level_t level;
    ts_auth_verify_password(username, password, &level);
    
    /* 检查是否需要修改密码 */
    bool password_changed = ts_auth_password_changed(username);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "token", token);
    cJSON_AddStringToObject(data, "username", username);
    cJSON_AddStringToObject(data, "level", perm_level_to_string(level));
#ifdef CONFIG_TS_SECURITY_TOKEN_EXPIRE_SEC
    cJSON_AddNumberToObject(data, "expires_in", CONFIG_TS_SECURITY_TOKEN_EXPIRE_SEC);
#else
    cJSON_AddNumberToObject(data, "expires_in", 86400);  /* 24 hours */
#endif
    cJSON_AddBoolToObject(data, "password_changed", password_changed);
    
    ts_api_result_ok(result, data);
    TS_LOGI(TAG, "User '%s' logged in successfully", username);
    return ESP_OK;
}

/**
 * @brief auth.logout - User logout
 * 
 * Params: { "token": "..." }
 * Returns: { "success": true }
 */
static esp_err_t api_auth_logout(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *token_item = cJSON_GetObjectItem(params, "token");
    
    if (!cJSON_IsString(token_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: token");
        return ESP_ERR_INVALID_ARG;
    }
    
    uint32_t session_id;
    esp_err_t ret = ts_security_validate_token(token_item->valuestring, &session_id);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_AUTH, "Invalid or expired token");
        return ret;
    }
    
    ret = ts_auth_logout(session_id);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", ret == ESP_OK);
    
    ts_api_result_ok(result, data);
    TS_LOGI(TAG, "Session %08lx logged out", (unsigned long)session_id);
    return ESP_OK;
}

/**
 * @brief auth.status - Check auth status
 * 
 * Params: { "token": "..." }
 * Returns: { "valid": true, "username": "admin", "level": "admin", "expires_in": 3600 }
 */
static esp_err_t api_auth_status(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *token_item = cJSON_GetObjectItem(params, "token");
    
    cJSON *data = cJSON_CreateObject();
    
    if (!cJSON_IsString(token_item)) {
        cJSON_AddBoolToObject(data, "valid", false);
        cJSON_AddStringToObject(data, "message", "No token provided");
        ts_api_result_ok(result, data);
        return ESP_OK;
    }
    
    uint32_t session_id;
    esp_err_t ret = ts_security_validate_token(token_item->valuestring, &session_id);
    
    if (ret != ESP_OK) {
        cJSON_AddBoolToObject(data, "valid", false);
        cJSON_AddStringToObject(data, "message", "Invalid or expired token");
        ts_api_result_ok(result, data);
        return ESP_OK;
    }
    
    ts_session_t session;
    ret = ts_security_validate_session(session_id, &session);
    
    if (ret != ESP_OK) {
        cJSON_AddBoolToObject(data, "valid", false);
        cJSON_AddStringToObject(data, "message", "Session expired");
        ts_api_result_ok(result, data);
        return ESP_OK;
    }
    
    /* Token 有效 */
    cJSON_AddBoolToObject(data, "valid", true);
    cJSON_AddStringToObject(data, "username", session.client_id);
    cJSON_AddStringToObject(data, "level", perm_level_to_string(session.level));
    
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
    int32_t expires_in = (int32_t)(session.expires_at - now);
    if (expires_in < 0) expires_in = 0;
    cJSON_AddNumberToObject(data, "expires_in", expires_in);
    
    /* 检查是否需要修改密码 */
    bool password_changed = ts_auth_password_changed(session.client_id);
    cJSON_AddBoolToObject(data, "password_changed", password_changed);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief auth.change_password - Change user password
 * 
 * Params: { "token": "...", "old_password": "...", "new_password": "..." }
 * Returns: { "success": true }
 */
static esp_err_t api_auth_change_password(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *token_item = cJSON_GetObjectItem(params, "token");
    const cJSON *old_pwd_item = cJSON_GetObjectItem(params, "old_password");
    const cJSON *new_pwd_item = cJSON_GetObjectItem(params, "new_password");
    
    if (!cJSON_IsString(token_item)) {
        ts_api_result_error(result, TS_API_ERR_AUTH, "Missing token");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!cJSON_IsString(old_pwd_item) || !cJSON_IsString(new_pwd_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, 
                           "Missing required parameters: old_password, new_password");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 验证 token */
    uint32_t session_id;
    esp_err_t ret = ts_security_validate_token(token_item->valuestring, &session_id);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_AUTH, "Invalid or expired token");
        return ret;
    }
    
    /* 获取用户名 */
    ts_session_t session;
    ret = ts_security_validate_session(session_id, &session);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_AUTH, "Session expired");
        return ret;
    }
    
    /* 验证新密码长度 */
    const char *new_password = new_pwd_item->valuestring;
    size_t len = strlen(new_password);
    if (len < 4 || len > 64) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, 
                           "Password must be 4-64 characters");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 修改密码 */
    ret = ts_auth_change_password(session.client_id, 
                                   old_pwd_item->valuestring,
                                   new_password);
    
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_ARG) {
            ts_api_result_error(result, TS_API_ERR_AUTH, "Old password is incorrect");
        } else {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to change password");
        }
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "message", "Password changed successfully");
    
    ts_api_result_ok(result, data);
    TS_LOGI(TAG, "Password changed for user '%s'", session.client_id);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t s_auth_endpoints[] = {
    {
        .name = "auth.login",
        .description = "User login",
        .category = TS_API_CAT_SECURITY,
        .handler = api_auth_login,
        .requires_auth = false,  /* 登录本身不需要认证 */
    },
    {
        .name = "auth.logout",
        .description = "User logout",
        .category = TS_API_CAT_SECURITY,
        .handler = api_auth_logout,
        .requires_auth = false,  /* 登出使用 token 验证 */
    },
    {
        .name = "auth.status",
        .description = "Check auth status",
        .category = TS_API_CAT_SECURITY,
        .handler = api_auth_status,
        .requires_auth = false,  /* 状态检查使用 token 验证 */
    },
    {
        .name = "auth.change_password",
        .description = "Change password",
        .category = TS_API_CAT_SECURITY,
        .handler = api_auth_change_password,
        .requires_auth = false,  /* 密码修改使用 token 验证 */
    },
};

esp_err_t ts_api_auth_register(void)
{
    /* 初始化认证模块 */
    esp_err_t ret = ts_auth_init();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to init auth module: %s", esp_err_to_name(ret));
        return ret;
    }
    
    for (size_t i = 0; i < sizeof(s_auth_endpoints) / sizeof(s_auth_endpoints[0]); i++) {
        ret = ts_api_register(&s_auth_endpoints[i]);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to register auth API: %s", s_auth_endpoints[i].name);
            return ret;
        }
    }
    
    TS_LOGI(TAG, "Auth APIs registered");
    return ESP_OK;
}
