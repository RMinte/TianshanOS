/**
 * @file ts_api_ssh.c
 * @brief SSH API Handlers
 * 
 * 提供 SSH 相关操作的 API 端点：
 * - ssh.exec - 执行远程命令
 * - ssh.test - 测试连接
 * - ssh.copyid - 部署公钥
 * - ssh.revoke - 撤销公钥
 * - ssh.keygen - 生成密钥对
 * - ssh.hosts.* - SSH 主机凭证管理（持久化）
 * 
 * 所有 SSH 连接操作都包含主机指纹验证（Known Hosts），
 * 由 trust_new 和 accept_changed 参数控制行为。
 * 
 * 注意：交互式 Shell 不通过 API 实现
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-20
 */

#include "ts_api.h"
#include "ts_core.h"  /* TS_MALLOC_PSRAM, TS_STRDUP_PSRAM */
#include "ts_ssh_client.h"
#include "ts_keystore.h"
#include "ts_known_hosts.h"
#include "ts_ssh_hosts_config.h"
#include "ts_log.h"
#include <string.h>
#include <stdlib.h>

#define TAG "api_ssh"

/* 自定义错误码用于指纹验证 */
#define TS_API_ERR_HOST_MISMATCH  1001  /* 主机指纹不匹配 */
#define TS_API_ERR_HOST_NEW       1002  /* 新主机需要确认 */

/* 静态缓冲区用于存储主机、用户名等 */
static char s_host_buf[64];
static char s_user_buf[64];
static char s_pass_buf[128];
static char *s_key_buf = NULL;
static size_t s_key_len = 0;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 清理密钥缓冲区
 */
static void cleanup_key_buffer(void)
{
    if (s_key_buf) {
        memset(s_key_buf, 0, s_key_len);
        free(s_key_buf);
        s_key_buf = NULL;
        s_key_len = 0;
    }
}

/**
 * @brief 从参数中配置 SSH 会话
 */
static esp_err_t configure_ssh_from_params(const cJSON *params, ts_ssh_config_t *config)
{
    /* 主机和用户名 */
    const cJSON *host = cJSON_GetObjectItem(params, "host");
    const cJSON *user = cJSON_GetObjectItem(params, "user");
    const cJSON *port = cJSON_GetObjectItem(params, "port");
    
    if (!host || !cJSON_IsString(host) || !user || !cJSON_IsString(user)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 复制到静态缓冲区 */
    strncpy(s_host_buf, host->valuestring, sizeof(s_host_buf) - 1);
    s_host_buf[sizeof(s_host_buf) - 1] = '\0';
    strncpy(s_user_buf, user->valuestring, sizeof(s_user_buf) - 1);
    s_user_buf[sizeof(s_user_buf) - 1] = '\0';
    
    config->host = s_host_buf;
    config->username = s_user_buf;
    config->port = cJSON_IsNumber(port) ? port->valueint : 22;
    
    /* 认证方式：密码或密钥 */
    const cJSON *password = cJSON_GetObjectItem(params, "password");
    const cJSON *keyid = cJSON_GetObjectItem(params, "keyid");
    const cJSON *keypath = cJSON_GetObjectItem(params, "keypath");
    
    cleanup_key_buffer();
    
    if (cJSON_IsString(password) && password->valuestring[0]) {
        strncpy(s_pass_buf, password->valuestring, sizeof(s_pass_buf) - 1);
        s_pass_buf[sizeof(s_pass_buf) - 1] = '\0';
        config->auth_method = TS_SSH_AUTH_PASSWORD;
        config->auth.password = s_pass_buf;
    } else if (cJSON_IsString(keyid) && keyid->valuestring[0]) {
        /* 从密钥存储读取私钥 */
        esp_err_t ret = ts_keystore_load_private_key(keyid->valuestring, &s_key_buf, &s_key_len);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to load key '%s': %s", keyid->valuestring, esp_err_to_name(ret));
            return ret;
        }
        config->auth_method = TS_SSH_AUTH_PUBLICKEY;
        config->auth.key.private_key = (const uint8_t *)s_key_buf;
        config->auth.key.private_key_len = s_key_len;
        config->auth.key.private_key_path = NULL;
        config->auth.key.passphrase = NULL;
    } else if (cJSON_IsString(keypath) && keypath->valuestring[0]) {
        config->auth_method = TS_SSH_AUTH_PUBLICKEY;
        config->auth.key.private_key = NULL;
        config->auth.key.private_key_len = 0;
        config->auth.key.private_key_path = keypath->valuestring;
        config->auth.key.passphrase = NULL;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/**
 * @brief 主机指纹验证辅助函数
 * 
 * 在 SSH 连接成功后验证主机指纹，根据参数决定行为：
 * - trust_new=true: 新主机自动信任并添加到 known hosts
 * - trust_new=false: 新主机返回错误，需要用户确认
 * - accept_changed=true: 指纹变化时接受新指纹
 * - accept_changed=false: 指纹变化时返回错误
 * 
 * @param session SSH 会话（已连接）
 * @param params API 参数（包含 trust_new, accept_changed）
 * @param result API 结果（用于返回错误信息）
 * @param host_info_out 输出主机信息（可选）
 * @return ESP_OK 验证通过，其他表示失败
 */
static esp_err_t verify_host_fingerprint(ts_ssh_session_t session,
                                          const cJSON *params,
                                          ts_api_result_t *result,
                                          ts_known_host_t *host_info_out)
{
    /* 获取验证参数 */
    const cJSON *trust_new_j = cJSON_GetObjectItem(params, "trust_new");
    const cJSON *accept_changed_j = cJSON_GetObjectItem(params, "accept_changed");
    
    bool trust_new = cJSON_IsBool(trust_new_j) ? cJSON_IsTrue(trust_new_j) : true;
    bool accept_changed = cJSON_IsBool(accept_changed_j) ? cJSON_IsTrue(accept_changed_j) : false;
    
    /* 验证主机指纹 */
    ts_host_verify_result_t verify_result;
    ts_known_host_t host_info = {0};
    esp_err_t ret = ts_known_hosts_verify(session, &verify_result, &host_info);
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to verify host fingerprint");
        return ret;
    }
    
    /* 输出主机信息 */
    if (host_info_out) {
        *host_info_out = host_info;
    }
    
    switch (verify_result) {
        case TS_HOST_VERIFY_OK:
            /* 指纹匹配，验证通过 */
            TS_LOGI(TAG, "Host key verified: %s:%u", host_info.host, host_info.port);
            return ESP_OK;
            
        case TS_HOST_VERIFY_NOT_FOUND:
            /* 新主机 */
            if (trust_new) {
                /* 自动信任新主机 */
                ret = ts_known_hosts_add(session);
                if (ret == ESP_OK) {
                    TS_LOGI(TAG, "New host trusted: %s:%u (fingerprint: %.16s...)", 
                            host_info.host, host_info.port, host_info.fingerprint);
                    return ESP_OK;
                } else {
                    ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to save host key");
                    return ret;
                }
            } else {
                /* 需要用户确认 */
                cJSON *data = cJSON_CreateObject();
                cJSON_AddStringToObject(data, "status", "new_host");
                cJSON_AddStringToObject(data, "host", host_info.host);
                cJSON_AddNumberToObject(data, "port", host_info.port);
                cJSON_AddStringToObject(data, "fingerprint", host_info.fingerprint);
                cJSON_AddStringToObject(data, "message", 
                    "New host - set trust_new=true or use hosts.add to trust this host");
                result->code = TS_API_ERR_HOST_NEW;
                result->message = TS_STRDUP_PSRAM("New host requires confirmation");
                result->data = data;
                return ESP_ERR_INVALID_STATE;
            }
            
        case TS_HOST_VERIFY_MISMATCH:
            /* 指纹变化 - 可能的中间人攻击！ */
            if (accept_changed) {
                /* 用户明确接受变化 */
                TS_LOGW(TAG, "Host key changed and accepted: %s:%u", host_info.host, host_info.port);
                ret = ts_known_hosts_add(session);
                if (ret == ESP_OK) {
                    return ESP_OK;
                } else {
                    ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to update host key");
                    return ret;
                }
            } else {
                /* 拒绝连接 - 返回详细信息 */
                TS_LOGW(TAG, "Host key mismatch rejected: %s:%u", host_info.host, host_info.port);
                
                /* 获取存储的指纹 */
                ts_known_host_t stored_info = {0};
                ts_known_hosts_get(host_info.host, host_info.port, &stored_info);
                
                cJSON *data = cJSON_CreateObject();
                cJSON_AddStringToObject(data, "status", "mismatch");
                cJSON_AddStringToObject(data, "host", host_info.host);
                cJSON_AddNumberToObject(data, "port", host_info.port);
                cJSON_AddStringToObject(data, "current_fingerprint", host_info.fingerprint);
                cJSON_AddStringToObject(data, "stored_fingerprint", stored_info.fingerprint);
                cJSON_AddStringToObject(data, "message", 
                    "WARNING: Host key has changed! This could indicate a man-in-the-middle attack. "
                    "Set accept_changed=true only if you are sure the server was reinstalled.");
                result->code = TS_API_ERR_HOST_MISMATCH;
                result->message = TS_STRDUP_PSRAM("Host key mismatch - possible MITM attack");
                result->data = data;
                return ESP_ERR_INVALID_STATE;
            }
            
        default:
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Host verification error");
            return ESP_FAIL;
    }
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief ssh.exec - Execute remote command
 * 
 * Params: { 
 *   "host": "192.168.1.100", 
 *   "user": "root",
 *   "password": "xxx" | "keyid": "default" | "keypath": "/sdcard/id_rsa",
 *   "port": 22,
 *   "command": "ls -la",
 *   "timeout_ms": 30000,
 *   "trust_new": true,        // 新主机是否自动信任（默认 true）
 *   "accept_changed": false   // 是否接受指纹变化（默认 false）
 * }
 * 
 * Response (success): {
 *   "exit_code": 0,
 *   "stdout": "...",
 *   "stderr": "...",
 *   "host_status": "trusted" | "new_trusted",
 *   "fingerprint": "sha256:..."
 * }
 * 
 * Response (host_mismatch): {
 *   "status": "mismatch",
 *   "current_fingerprint": "...",
 *   "stored_fingerprint": "...",
 *   "message": "WARNING: ..."
 * }
 */
static esp_err_t api_ssh_exec(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *cmd = cJSON_GetObjectItem(params, "command");
    if (!cmd || !cJSON_IsString(cmd)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'command' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 配置 SSH */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    esp_err_t ret = configure_ssh_from_params(params, &config);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid SSH configuration");
        cleanup_key_buffer();
        return ret;
    }
    
    /* 超时设置 */
    const cJSON *timeout = cJSON_GetObjectItem(params, "timeout_ms");
    if (cJSON_IsNumber(timeout)) {
        config.timeout_ms = timeout->valueint;
    }
    
    /* 创建会话 */
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to create session");
        cleanup_key_buffer();
        return ret;
    }
    
    /* 连接（TCP 层） */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        const char *err = ts_ssh_get_error(session);
        ts_api_result_error(result, TS_API_ERR_CONNECTION, err ? err : "Failed to connect");
        ts_ssh_session_destroy(session);
        cleanup_key_buffer();
        return ret;
    }
    
    /* 验证主机指纹 */
    ts_known_host_t host_info = {0};
    ret = verify_host_fingerprint(session, params, result, &host_info);
    if (ret != ESP_OK) {
        /* result 已在 verify_host_fingerprint 中设置 */
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        cleanup_key_buffer();
        return ret;
    }
    
    /* 执行命令 */
    ts_ssh_exec_result_t exec_result = {0};
    ret = ts_ssh_exec(session, cmd->valuestring, &exec_result);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "exit_code", exec_result.exit_code);
        
        if (exec_result.stdout_data) {
            cJSON_AddStringToObject(data, "stdout", exec_result.stdout_data);
        } else {
            cJSON_AddStringToObject(data, "stdout", "");
        }
        
        if (exec_result.stderr_data) {
            cJSON_AddStringToObject(data, "stderr", exec_result.stderr_data);
        } else {
            cJSON_AddStringToObject(data, "stderr", "");
        }
        
        /* 添加主机验证信息 */
        cJSON_AddStringToObject(data, "host_status", "trusted");
        cJSON_AddStringToObject(data, "fingerprint", host_info.fingerprint);
        
        ts_api_result_ok(result, data);
        ts_ssh_exec_result_free(&exec_result);
    } else {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Command execution failed");
    }
    
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    cleanup_key_buffer();
    
    return ret;
}

/**
 * @brief ssh.test - Test SSH connection
 * 
 * Params: { 
 *   "host": "192.168.1.100", 
 *   "user": "root",
 *   "password": "xxx" | "keyid": "default" | "keypath": "/sdcard/id_rsa",
 *   "port": 22,
 *   "trust_new": true,        // 新主机是否自动信任（默认 true）
 *   "accept_changed": false   // 是否接受指纹变化（默认 false）
 * }
 * 
 * Response (success): {
 *   "success": true,
 *   "host": "...",
 *   "port": 22,
 *   "user": "...",
 *   "host_status": "trusted" | "new_trusted",
 *   "fingerprint": "sha256:..."
 * }
 */
static esp_err_t api_ssh_test(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 配置 SSH */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    esp_err_t ret = configure_ssh_from_params(params, &config);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid SSH configuration");
        cleanup_key_buffer();
        return ret;
    }
    
    /* 创建会话 */
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to create session");
        cleanup_key_buffer();
        return ret;
    }
    
    /* 测试连接（TCP 层） */
    ret = ts_ssh_connect(session);
    
    if (ret != ESP_OK) {
        const char *err = ts_ssh_get_error(session);
        cJSON *data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "success", false);
        cJSON_AddStringToObject(data, "error", err ? err : "Connection failed");
        ts_api_result_ok(result, data);
        ts_ssh_session_destroy(session);
        cleanup_key_buffer();
        return ESP_OK;
    }
    
    /* 验证主机指纹 */
    ts_known_host_t host_info = {0};
    ret = verify_host_fingerprint(session, params, result, &host_info);
    if (ret != ESP_OK) {
        /* 主机验证失败（新主机或密钥不匹配）
         * result 中已包含详细信息，直接返回成功（HTTP 200）
         * 前端通过 result.code 和 result.data.status 判断实际状态
         */
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        cleanup_key_buffer();
        
        /* 如果是 MISMATCH 或 NEW_HOST，返回 ESP_OK 让 HTTP 层返回 200
         * 实际错误信息已在 result 中设置 */
        if (result->code == TS_API_ERR_HOST_MISMATCH || 
            result->code == TS_API_ERR_HOST_NEW) {
            return ESP_OK;  // HTTP 200，但 result.code 指示实际问题
        }
        return ret;  // 其他错误继续返回错误码
    }
    
    /* 连接成功 */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "host", config.host);
    cJSON_AddNumberToObject(data, "port", config.port);
    cJSON_AddStringToObject(data, "user", config.username);
    cJSON_AddStringToObject(data, "host_status", "trusted");
    cJSON_AddStringToObject(data, "fingerprint", host_info.fingerprint);
    
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    cleanup_key_buffer();
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief ssh.copyid - Deploy public key to remote server
 * 
 * 将密钥存储中的公钥部署到远程服务器的 ~/.ssh/authorized_keys
 * 流程：
 * 1. 使用密码认证连接到远程服务器
 * 2. 验证主机指纹（Known Hosts）
 * 3. 部署公钥到 authorized_keys
 * 4. 使用公钥认证验证部署是否成功
 * 
 * Params: { 
 *   "host": "192.168.1.100", 
 *   "user": "root",
 *   "password": "xxx",
 *   "keyid": "default",
 *   "port": 22,
 *   "verify": true,
 *   "trust_new": true,        // 新主机是否自动信任（默认 true）
 *   "accept_changed": false   // 是否接受指纹变化（默认 false）
 * }
 */
static esp_err_t api_ssh_copyid(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 获取必需参数 */
    const cJSON *host = cJSON_GetObjectItem(params, "host");
    const cJSON *user = cJSON_GetObjectItem(params, "user");
    const cJSON *password = cJSON_GetObjectItem(params, "password");
    const cJSON *keyid = cJSON_GetObjectItem(params, "keyid");
    const cJSON *port = cJSON_GetObjectItem(params, "port");
    const cJSON *verify = cJSON_GetObjectItem(params, "verify");
    
    if (!host || !cJSON_IsString(host) || !host->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'host' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    if (!user || !cJSON_IsString(user) || !user->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'user' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    if (!password || !cJSON_IsString(password) || !password->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'password' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    if (!keyid || !cJSON_IsString(keyid) || !keyid->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'keyid' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    int ssh_port = cJSON_IsNumber(port) ? port->valueint : 22;
    bool do_verify = cJSON_IsBool(verify) ? cJSON_IsTrue(verify) : true;
    
    /* 加载公钥 */
    char *pubkey_data = NULL;
    size_t pubkey_len = 0;
    esp_err_t ret = ts_keystore_load_public_key(keyid->valuestring, &pubkey_data, &pubkey_len);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Key not found in keystore");
        return ret;
    }
    
    /* 配置 SSH 连接（使用密码认证） */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    strncpy(s_host_buf, host->valuestring, sizeof(s_host_buf) - 1);
    strncpy(s_user_buf, user->valuestring, sizeof(s_user_buf) - 1);
    strncpy(s_pass_buf, password->valuestring, sizeof(s_pass_buf) - 1);
    
    config.host = s_host_buf;
    config.port = ssh_port;
    config.username = s_user_buf;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = s_pass_buf;
    
    /* 创建会话并连接 */
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to create session");
        free(pubkey_data);
        return ret;
    }
    
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        const char *err = ts_ssh_get_error(session);
        ts_api_result_error(result, TS_API_ERR_CONNECTION, err ? err : "Failed to connect");
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        return ret;
    }
    
    /* 验证主机指纹 */
    ts_known_host_t host_info = {0};
    ret = verify_host_fingerprint(session, params, result, &host_info);
    if (ret != ESP_OK) {
        /* result 已在 verify_host_fingerprint 中设置 */
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        return ret;
    }
    
    /* 构建部署命令（与 CLI 逻辑一致） */
    char *deploy_cmd = TS_MALLOC_PSRAM(pubkey_len + 512);
    if (!deploy_cmd) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Out of memory");
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        return ESP_ERR_NO_MEM;
    }
    
    snprintf(deploy_cmd, pubkey_len + 512,
             "mkdir -p ~/.ssh && chmod 700 ~/.ssh && "
             "echo '%s' >> ~/.ssh/authorized_keys && "
             "chmod 600 ~/.ssh/authorized_keys && "
             "echo 'Key deployed successfully'",
             pubkey_data);
    
    /* 执行部署命令 */
    ts_ssh_exec_result_t exec_result = {0};
    ret = ts_ssh_exec(session, deploy_cmd, &exec_result);
    free(deploy_cmd);
    
    bool deploy_ok = (ret == ESP_OK && exec_result.exit_code == 0);
    char *stderr_msg = NULL;
    if (exec_result.stderr_data && strlen(exec_result.stderr_data) > 0) {
        stderr_msg = TS_STRDUP_PSRAM(exec_result.stderr_data);
    }
    ts_ssh_exec_result_free(&exec_result);
    
    /* 断开密码连接 */
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    
    if (!deploy_ok) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, 
                           stderr_msg ? stderr_msg : "Deploy command failed");
        free(stderr_msg);
        free(pubkey_data);
        return ESP_FAIL;
    }
    free(stderr_msg);
    
    /* 验证公钥认证（可选） */
    bool verified = false;
    if (do_verify) {
        /* 加载私钥 */
        cleanup_key_buffer();
        ret = ts_keystore_load_private_key(keyid->valuestring, &s_key_buf, &s_key_len);
        if (ret == ESP_OK) {
            /* 使用公钥认证重新连接 */
            ts_ssh_config_t verify_config = TS_SSH_DEFAULT_CONFIG();
            verify_config.host = s_host_buf;
            verify_config.port = ssh_port;
            verify_config.username = s_user_buf;
            verify_config.auth_method = TS_SSH_AUTH_PUBLICKEY;
            verify_config.auth.key.private_key = (const uint8_t *)s_key_buf;
            verify_config.auth.key.private_key_len = s_key_len;
            
            ts_ssh_session_t verify_session = NULL;
            if (ts_ssh_session_create(&verify_config, &verify_session) == ESP_OK) {
                if (ts_ssh_connect(verify_session) == ESP_OK) {
                    verified = true;
                    ts_ssh_disconnect(verify_session);
                }
                ts_ssh_session_destroy(verify_session);
            }
            cleanup_key_buffer();
        }
    }
    
    free(pubkey_data);
    
    /* 部署成功后，自动注册到 SSH 主机配置（无论验证是否成功） */
    if (deploy_ok) {
        /* 生成主机 ID：user@host:port */
        char auto_id[TS_SSH_HOST_ID_MAX];
        if (ssh_port == 22) {
            snprintf(auto_id, sizeof(auto_id), "%s@%s", 
                     user->valuestring, host->valuestring);
        } else {
            snprintf(auto_id, sizeof(auto_id), "%s@%s:%d", 
                     user->valuestring, host->valuestring, ssh_port);
        }
        
        ts_ssh_host_config_t host_config = {
            .port = (uint16_t)ssh_port,
            .auth_type = TS_SSH_HOST_AUTH_KEY,
            .enabled = true,
        };
        strncpy(host_config.id, auto_id, sizeof(host_config.id) - 1);
        strncpy(host_config.host, host->valuestring, sizeof(host_config.host) - 1);
        strncpy(host_config.username, user->valuestring, sizeof(host_config.username) - 1);
        strncpy(host_config.keyid, keyid->valuestring, sizeof(host_config.keyid) - 1);
        
        esp_err_t add_ret = ts_ssh_hosts_config_add(&host_config);
        if (add_ret == ESP_OK) {
            TS_LOGI(TAG, "Auto-registered SSH host: %s", auto_id);
        }
    }
    
    /* 返回结果 */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "deployed", true);
    cJSON_AddBoolToObject(data, "verified", verified);
    cJSON_AddStringToObject(data, "host", host->valuestring);
    cJSON_AddNumberToObject(data, "port", ssh_port);
    cJSON_AddStringToObject(data, "user", user->valuestring);
    cJSON_AddStringToObject(data, "keyid", keyid->valuestring);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief ssh.revoke - Revoke (remove) deployed public key from remote server
 * 
 * 从远程服务器的 ~/.ssh/authorized_keys 中移除已部署的公钥
 * 
 * Params: { 
 *   "host": "192.168.1.100", 
 *   "user": "root",
 *   "password": "xxx",
 *   "keyid": "default",
 *   "port": 22,
 *   "trust_new": true,        // 新主机是否自动信任（默认 true）
 *   "accept_changed": false   // 是否接受指纹变化（默认 false）
 * }
 */
static esp_err_t api_ssh_revoke(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 获取必需参数 */
    const cJSON *host = cJSON_GetObjectItem(params, "host");
    const cJSON *user = cJSON_GetObjectItem(params, "user");
    const cJSON *password = cJSON_GetObjectItem(params, "password");
    const cJSON *keyid = cJSON_GetObjectItem(params, "keyid");
    const cJSON *port = cJSON_GetObjectItem(params, "port");
    
    if (!host || !cJSON_IsString(host) || !host->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'host' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    if (!user || !cJSON_IsString(user) || !user->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'user' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    if (!password || !cJSON_IsString(password) || !password->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'password' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    if (!keyid || !cJSON_IsString(keyid) || !keyid->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'keyid' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    int ssh_port = cJSON_IsNumber(port) ? port->valueint : 22;
    
    /* 加载公钥 */
    char *pubkey_data = NULL;
    size_t pubkey_len = 0;
    esp_err_t ret = ts_keystore_load_public_key(keyid->valuestring, &pubkey_data, &pubkey_len);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Key not found in keystore");
        return ret;
    }
    
    /* 解析公钥：提取 key_type 和 key_data */
    char *pubkey_copy = TS_STRDUP_PSRAM(pubkey_data);
    if (!pubkey_copy) {
        free(pubkey_data);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Out of memory");
        return ESP_ERR_NO_MEM;
    }
    
    char *key_type = strtok(pubkey_copy, " ");
    char *key_data = strtok(NULL, " ");
    
    if (!key_type || !key_data) {
        free(pubkey_data);
        free(pubkey_copy);
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid public key format");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 构建用于匹配的密钥签名（type + 前100字符） */
    char key_signature[160];
    size_t sig_len = strlen(key_data) > 100 ? 100 : strlen(key_data);
    snprintf(key_signature, sizeof(key_signature), "%s %.*s", key_type, (int)sig_len, key_data);
    
    /* 配置 SSH 连接（使用密码认证） */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    strncpy(s_host_buf, host->valuestring, sizeof(s_host_buf) - 1);
    strncpy(s_user_buf, user->valuestring, sizeof(s_user_buf) - 1);
    strncpy(s_pass_buf, password->valuestring, sizeof(s_pass_buf) - 1);
    
    config.host = s_host_buf;
    config.port = ssh_port;
    config.username = s_user_buf;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = s_pass_buf;
    
    /* 创建会话并连接 */
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to create session");
        free(pubkey_data);
        free(pubkey_copy);
        return ret;
    }
    
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        const char *err = ts_ssh_get_error(session);
        ts_api_result_error(result, TS_API_ERR_CONNECTION, err ? err : "Failed to connect");
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        free(pubkey_copy);
        return ret;
    }
    
    /* 验证主机指纹 */
    ts_known_host_t host_info = {0};
    ret = verify_host_fingerprint(session, params, result, &host_info);
    if (ret != ESP_OK) {
        /* result 已在 verify_host_fingerprint 中设置 */
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        free(pubkey_copy);
        return ret;
    }
    
    /* 1. 检查密钥是否存在 */
    char *check_cmd = TS_MALLOC_PSRAM(512);
    if (!check_cmd) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Out of memory");
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        free(pubkey_copy);
        return ESP_ERR_NO_MEM;
    }
    
    snprintf(check_cmd, 512,
        "if [ -f ~/.ssh/authorized_keys ]; then "
        "  grep -cF '%s' ~/.ssh/authorized_keys 2>/dev/null || echo '0'; "
        "else "
        "  echo '0'; "
        "fi",
        key_signature);
    
    ts_ssh_exec_result_t exec_result = {0};
    ret = ts_ssh_exec(session, check_cmd, &exec_result);
    free(check_cmd);
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to check key");
        ts_ssh_exec_result_free(&exec_result);
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        free(pubkey_copy);
        return ret;
    }
    
    int key_count = exec_result.stdout_data ? atoi(exec_result.stdout_data) : 0;
    ts_ssh_exec_result_free(&exec_result);
    
    if (key_count == 0) {
        /* 密钥不存在 */
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        free(pubkey_copy);
        
        cJSON *data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "revoked", false);
        cJSON_AddBoolToObject(data, "found", false);
        cJSON_AddStringToObject(data, "message", "Key not found on remote server");
        ts_api_result_ok(result, data);
        return ESP_OK;
    }
    
    /* 2. 执行删除操作 */
    char *revoke_cmd = TS_MALLOC_PSRAM(512);
    if (!revoke_cmd) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Out of memory");
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        free(pubkey_copy);
        return ESP_ERR_NO_MEM;
    }
    
    snprintf(revoke_cmd, 512,
        "cp ~/.ssh/authorized_keys ~/.ssh/authorized_keys.bak 2>/dev/null; "
        "grep -vF '%s' ~/.ssh/authorized_keys > ~/.ssh/authorized_keys.tmp 2>/dev/null && "
        "mv ~/.ssh/authorized_keys.tmp ~/.ssh/authorized_keys && "
        "chmod 600 ~/.ssh/authorized_keys && "
        "echo 'REVOKE_OK'",
        key_signature);
    
    ret = ts_ssh_exec(session, revoke_cmd, &exec_result);
    free(revoke_cmd);
    
    bool revoke_ok = (ret == ESP_OK && exec_result.stdout_data && 
                      strstr(exec_result.stdout_data, "REVOKE_OK") != NULL);
    ts_ssh_exec_result_free(&exec_result);
    
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    free(pubkey_data);
    free(pubkey_copy);
    
    if (!revoke_ok) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to revoke key");
        return ESP_FAIL;
    }
    
    /* 返回结果 */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "revoked", true);
    cJSON_AddBoolToObject(data, "found", true);
    cJSON_AddNumberToObject(data, "removed_count", key_count);
    cJSON_AddStringToObject(data, "host", host->valuestring);
    cJSON_AddNumberToObject(data, "port", ssh_port);
    cJSON_AddStringToObject(data, "user", user->valuestring);
    cJSON_AddStringToObject(data, "keyid", keyid->valuestring);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief ssh.keygen - Generate SSH key pair
 * 
 * Params: { 
 *   "id": "mykey",
 *   "type": "ecdsa" | "rsa-2048" | "rsa-4096",
 *   "comment": "optional comment"
 * }
 */
static esp_err_t api_ssh_keygen(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *id = cJSON_GetObjectItem(params, "id");
    if (!id || !cJSON_IsString(id)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'id' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 检查密钥是否已存在 */
    if (ts_keystore_key_exists(id->valuestring)) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "Key already exists");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* 解析密钥类型 */
    const cJSON *type_json = cJSON_GetObjectItem(params, "type");
    const char *type_str = cJSON_IsString(type_json) ? type_json->valuestring : "ecdsa";
    
    ts_keystore_key_type_t type = TS_KEYSTORE_TYPE_ECDSA_P256;
    if (strcmp(type_str, "rsa-2048") == 0) {
        type = TS_KEYSTORE_TYPE_RSA_2048;
    } else if (strcmp(type_str, "rsa-4096") == 0) {
        type = TS_KEYSTORE_TYPE_RSA_4096;
    } else if (strcmp(type_str, "ecdsa-p384") == 0) {
        type = TS_KEYSTORE_TYPE_ECDSA_P384;
    }
    
    /* 注释 */
    const cJSON *comment = cJSON_GetObjectItem(params, "comment");
    const char *comment_str = cJSON_IsString(comment) ? comment->valuestring : NULL;
    
    /* 生成密钥 */
    esp_err_t ret = ts_keystore_generate_key(id->valuestring, type, comment_str);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Key generation failed");
        return ret;
    }
    
    /* 获取公钥 */
    char *pubkey = NULL;
    size_t pubkey_len = 0;
    ret = ts_keystore_load_public_key(id->valuestring, &pubkey, &pubkey_len);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "id", id->valuestring);
    cJSON_AddStringToObject(data, "type", type_str);
    
    if (ret == ESP_OK && pubkey) {
        cJSON_AddStringToObject(data, "public_key", pubkey);
        free(pubkey);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                      SSH Hosts Config API                                  */
/*===========================================================================*/

/**
 * @brief ssh.hosts.list - 列出所有 SSH 主机配置
 */
static esp_err_t api_ssh_hosts_list(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    cJSON *data = cJSON_CreateObject();
    cJSON *hosts_arr = cJSON_AddArrayToObject(data, "hosts");
    
    ts_ssh_host_config_t configs[TS_SSH_HOSTS_MAX];
    size_t count = 0;
    
    if (ts_ssh_hosts_config_list(configs, TS_SSH_HOSTS_MAX, &count) == ESP_OK) {
        for (size_t i = 0; i < count; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "id", configs[i].id);
            cJSON_AddStringToObject(item, "host", configs[i].host);
            cJSON_AddNumberToObject(item, "port", configs[i].port);
            cJSON_AddStringToObject(item, "username", configs[i].username);
            cJSON_AddStringToObject(item, "auth_type", 
                configs[i].auth_type == TS_SSH_HOST_AUTH_KEY ? "key" : "password");
            if (configs[i].keyid[0]) {
                cJSON_AddStringToObject(item, "keyid", configs[i].keyid);
            }
            cJSON_AddBoolToObject(item, "enabled", configs[i].enabled);
            cJSON_AddNumberToObject(item, "created", configs[i].created_time);
            cJSON_AddNumberToObject(item, "last_used", configs[i].last_used_time);
            cJSON_AddItemToArray(hosts_arr, item);
        }
    }
    
    cJSON_AddNumberToObject(data, "count", (int)count);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief ssh.hosts.add - 添加 SSH 主机配置
 * 
 * Params: {
 *   "id": "agx0",
 *   "host": "192.168.55.100",
 *   "port": 22,
 *   "username": "root",
 *   "auth_type": "key" | "password",
 *   "keyid": "default" (如果 auth_type=key)
 * }
 */
static esp_err_t api_ssh_hosts_add(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_OK;
    }
    
    const cJSON *id = cJSON_GetObjectItem(params, "id");
    const cJSON *host = cJSON_GetObjectItem(params, "host");
    const cJSON *port = cJSON_GetObjectItem(params, "port");
    const cJSON *username = cJSON_GetObjectItem(params, "username");
    const cJSON *auth_type = cJSON_GetObjectItem(params, "auth_type");
    const cJSON *keyid = cJSON_GetObjectItem(params, "keyid");
    
    if (!id || !cJSON_IsString(id) || !id->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'id' parameter");
        return ESP_OK;
    }
    if (!host || !cJSON_IsString(host) || !host->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'host' parameter");
        return ESP_OK;
    }
    if (!username || !cJSON_IsString(username) || !username->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'username' parameter");
        return ESP_OK;
    }
    
    ts_ssh_host_config_t config = {
        .port = (port && cJSON_IsNumber(port)) ? (uint16_t)port->valueint : 22,
        .auth_type = TS_SSH_HOST_AUTH_KEY, /* 默认密钥认证 */
        .enabled = true,
    };
    
    strncpy(config.id, id->valuestring, sizeof(config.id) - 1);
    strncpy(config.host, host->valuestring, sizeof(config.host) - 1);
    strncpy(config.username, username->valuestring, sizeof(config.username) - 1);
    
    if (auth_type && cJSON_IsString(auth_type)) {
        if (strcmp(auth_type->valuestring, "password") == 0) {
            config.auth_type = TS_SSH_HOST_AUTH_PASSWORD;
        }
    }
    
    if (keyid && cJSON_IsString(keyid)) {
        strncpy(config.keyid, keyid->valuestring, sizeof(config.keyid) - 1);
    }
    
    esp_err_t ret = ts_ssh_hosts_config_add(&config);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "added", true);
        cJSON_AddStringToObject(data, "id", config.id);
        ts_api_result_ok(result, data);
    } else if (ret == ESP_ERR_NO_MEM) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Max hosts reached");
    } else {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to add host");
    }
    
    return ESP_OK;
}

/**
 * @brief ssh.hosts.remove - 删除 SSH 主机配置
 */
static esp_err_t api_ssh_hosts_remove(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_OK;
    }
    
    const cJSON *id = cJSON_GetObjectItem(params, "id");
    if (!id || !cJSON_IsString(id)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'id' parameter");
        return ESP_OK;
    }
    
    esp_err_t ret = ts_ssh_hosts_config_remove(id->valuestring);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddBoolToObject(data, "removed", true);
        cJSON_AddStringToObject(data, "id", id->valuestring);
        ts_api_result_ok(result, data);
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Host not found");
    } else {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to remove host");
    }
    
    return ESP_OK;
}

/**
 * @brief ssh.hosts.get - 获取 SSH 主机配置
 */
static esp_err_t api_ssh_hosts_get(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_OK;
    }
    
    const cJSON *id = cJSON_GetObjectItem(params, "id");
    if (!id || !cJSON_IsString(id)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'id' parameter");
        return ESP_OK;
    }
    
    ts_ssh_host_config_t config;
    esp_err_t ret = ts_ssh_hosts_config_get(id->valuestring, &config);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "id", config.id);
        cJSON_AddStringToObject(data, "host", config.host);
        cJSON_AddNumberToObject(data, "port", config.port);
        cJSON_AddStringToObject(data, "username", config.username);
        cJSON_AddStringToObject(data, "auth_type", 
            config.auth_type == TS_SSH_HOST_AUTH_KEY ? "key" : "password");
        if (config.keyid[0]) {
            cJSON_AddStringToObject(data, "keyid", config.keyid);
        }
        cJSON_AddBoolToObject(data, "enabled", config.enabled);
        cJSON_AddNumberToObject(data, "created", config.created_time);
        cJSON_AddNumberToObject(data, "last_used", config.last_used_time);
        ts_api_result_ok(result, data);
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Host not found");
    } else {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to get host");
    }
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t ssh_endpoints[] = {
    {
        .name = "ssh.exec",
        .description = "Execute remote command via SSH",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_exec,
        .requires_auth = true,
    },
    {
        .name = "ssh.test",
        .description = "Test SSH connection",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_test,
        .requires_auth = true,
    },
    {
        .name = "ssh.copyid",
        .description = "Deploy public key to remote server",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_copyid,
        .requires_auth = true,
    },
    {
        .name = "ssh.revoke",
        .description = "Revoke (remove) deployed public key from remote server",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_revoke,
        .requires_auth = true,
    },
    {
        .name = "ssh.keygen",
        .description = "Generate SSH key pair",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_keygen,
        .requires_auth = true,
    },
    /* SSH Host Config APIs */
    {
        .name = "ssh.hosts.list",
        .description = "List all SSH host configurations",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_hosts_list,
        .requires_auth = false,
    },
    {
        .name = "ssh.hosts.add",
        .description = "Add SSH host configuration",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_hosts_add,
        .requires_auth = true,
    },
    {
        .name = "ssh.hosts.remove",
        .description = "Remove SSH host configuration",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_hosts_remove,
        .requires_auth = true,
    },
    {
        .name = "ssh.hosts.get",
        .description = "Get SSH host configuration by ID",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_hosts_get,
        .requires_auth = false,
    },
};

esp_err_t ts_api_ssh_register(void)
{
    TS_LOGI(TAG, "Registering SSH APIs...");
    return ts_api_register_multiple(ssh_endpoints, 
                                     sizeof(ssh_endpoints) / sizeof(ssh_endpoints[0]));
}
