/**
 * @file ts_auth.c
 * @brief Authentication Helpers - 用户认证与会话管理
 * 
 * 支持 admin 和 root 两个用户，密码使用原有加盐 SHA-256 存储于 NVS
 * 
 * 安全设计：
 * - 密码哈希仅存储在 NVS，不导出到 SD 卡
 * - admin 密码可由 root 会话恢复；root/admin 均可通过受控恢复固件恢复默认密码
 */

#include "ts_security.h"
#include "ts_auth_layout.h"
#include "ts_crypto.h"
#include "ts_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

#define TAG "ts_auth"

#define MAX_LOGIN_ATTEMPTS    5
#define LOGIN_LOCKOUT_SEC     300  /* 5 分钟锁定 */

typedef ts_auth_user_credential_t user_credential_t;

static nvs_handle_t s_auth_nvs;
static bool s_auth_initialized = false;
static uint8_t s_auth_format_version;
/* Serialize password verification + session issuance with credential updates.
 * Otherwise a login verified before a change can issue a session after revocation. */
static StaticSemaphore_t s_auth_gate_storage;
static SemaphoreHandle_t s_auth_gate;
/* Per-boot monotonic deadlines; on-flash nonzero lockout_until is only a marker.
 * Never compare a previous boot's uptime to this boot's clock. */
static uint32_t s_lockout_deadlines[2]; /* admin, root */

/*===========================================================================*/
/*                          内部函数                                          */
/*===========================================================================*/

/**
 * @brief 获取用户凭据
 */
static esp_err_t load_user_credential(const char *username, user_credential_t *cred)
{
    char key[32];
    snprintf(key, sizeof(key), "cred_%s", username);
    
    unsigned char bytes[TS_AUTH_MAX_CREDENTIAL_SIZE];
    size_t len = sizeof(bytes);
    esp_err_t ret = nvs_get_blob(s_auth_nvs, key, bytes, &len);
    if (ret == ESP_OK) ret = ts_auth_credential_decode(bytes, len, cred);
    memset(bytes, 0, sizeof(bytes));
    return ret;
}

/**
 * @brief 保存用户凭据
 */
static esp_err_t save_user_credential(const char *username, const user_credential_t *cred, bool revoke_sessions)
{
    char key[32];
    snprintf(key, sizeof(key), "cred_%s", username);
    
    size_t length = cred->format ? sizeof(*cred) : TS_AUTH_LEGACY_CREDENTIAL_SIZE;
    esp_err_t ret = nvs_set_blob(s_auth_nvs, key, cred, length);
    if (ret != ESP_OK) return ret;
    /* A failed commit may already expose the new credential. Never let an old
     * session survive a password replacement accepted by the storage owner. */
    if (revoke_sessions) ts_security_destroy_sessions_by_client(username);
    ret = nvs_commit(s_auth_nvs);
    if (ret == ESP_OK) {
        user_credential_t readback;
        ret = load_user_credential(username, &readback);
        if (ret == ESP_OK && memcmp(cred, &readback, sizeof(readback))) ret = ESP_FAIL;
        memset(&readback, 0, sizeof(readback));
    }
    return ret != ESP_OK && revoke_sessions ? ESP_ERR_NOT_FINISHED : ret;
}

/* Only retire the unreleased prototype marker after both stored records are
 * actually readable by stock 0.6.4. Never relabel an unreadable credential. */
static esp_err_t finish_credential_migration(void)
{
    if (s_auth_format_version == TS_AUTH_CONFIG_VERSION) return ESP_OK;
    const char *users[] = {"admin", "root"};
    for (unsigned i = 0; i < 2; ++i) {
        user_credential_t credential;
        esp_err_t ret = load_user_credential(users[i], &credential);
        bool original = ret == ESP_OK && credential.format == 0;
        memset(&credential, 0, sizeof(credential));
        if (ret != ESP_OK) return ret;
        if (!original) return ESP_ERR_NOT_FINISHED;
    }
    esp_err_t ret = nvs_set_u8(s_auth_nvs, TS_AUTH_CONFIG_VERSION_KEY, TS_AUTH_CONFIG_VERSION);
    if (ret == ESP_OK) ret = nvs_commit(s_auth_nvs);
    uint8_t readback = 0;
    if (ret == ESP_OK) ret = nvs_get_u8(s_auth_nvs, TS_AUTH_CONFIG_VERSION_KEY, &readback);
    if (ret == ESP_OK && readback != TS_AUTH_CONFIG_VERSION) ret = ESP_FAIL;
    if (ret == ESP_OK) s_auth_format_version = TS_AUTH_CONFIG_VERSION;
    return ret;
}
/**
 * @brief 写入用户密码凭据，并清除失败计数/锁定状态
 */
static esp_err_t write_user_password_credential(const char *username, const char *password,
                                                 bool password_changed)
{
    if (!username || !password) return ESP_ERR_INVALID_ARG;

    size_t pwd_len = strlen(password);
    if (pwd_len < 4 || pwd_len > 64) {
        TS_LOGW(TAG, "Password length invalid (4-64 chars required)");
        return ESP_ERR_INVALID_ARG;
    }

    user_credential_t cred;
    esp_err_t ret = ts_auth_credential_create(password, password_changed, &cred);
    if (ret != ESP_OK) return ret;
    ret = save_user_credential(username, &cred, true);
    if (ret == ESP_OK) {
        esp_err_t migrated = finish_credential_migration();
        if (migrated != ESP_OK && migrated != ESP_ERR_NOT_FINISHED) ret = ESP_ERR_NOT_FINISHED;
    }
    memset(&cred, 0, sizeof(cred));
    if (ret == ESP_OK)
        s_lockout_deadlines[strcmp(username, "root") == 0 ? 1 : 0] = 0;
    return ret;
}

/**
 * @brief 强制重新创建用户凭据
 */
static esp_err_t force_create_user(const char *username, ts_perm_level_t level)
{
    /* 根据用户类型使用不同默认密码 */
    const char *default_pwd = (level == TS_PERM_ROOT)
                                  ? TS_AUTH_DEFAULT_ROOT_PASSWORD
                                  : TS_AUTH_DEFAULT_ADMIN_PASSWORD;
    TS_LOGI(TAG, "Creating/resetting user '%s'", username);

    return write_user_password_credential(username, default_pwd, false);
}

/**
 * @brief 获取当前时间（秒）
 */
static uint32_t get_current_time_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static bool credential_is_locked(const user_credential_t *cred)
{
    return cred->lockout_until != 0 || cred->failed_attempts >= MAX_LOGIN_ATTEMPTS;
}

/**
 * @brief 准备已有用户；读取失败不能当作恢复默认口令的授权
 */
static esp_err_t init_existing_user(const char *username)
{
    user_credential_t cred;
    esp_err_t ret = load_user_credential(username, &cred);
    if (ret == ESP_OK && credential_is_locked(&cred)) {
        unsigned index = strcmp(username, "root") == 0 ? 1 : 0;
        s_lockout_deadlines[index] = get_current_time_sec() + LOGIN_LOCKOUT_SEC;
    }
    if (ret == ESP_OK && cred.format == TS_AUTH_CREDENTIAL_MAGIC) {
        const char *password = !strcmp(username, "root") ? TS_AUTH_DEFAULT_ROOT_PASSWORD : TS_AUTH_DEFAULT_ADMIN_PASSWORD;
        bool matches = false;
        ret = ts_auth_credential_verify(&cred, password, &matches);
        if (ret == ESP_OK && matches) {
            user_credential_t original;
            ret = ts_auth_credential_create(password, cred.password_changed, &original);
            if (ret == ESP_OK) {
                original.failed_attempts = cred.failed_attempts;
                original.lockout_until = cred.lockout_until;
                ret = save_user_credential(username, &original, false);
            }
            memset(&original, 0, sizeof(original));
        }
    }
    memset(&cred, 0, sizeof(cred));
    /* Preserve the retry budget; prototype conversion never resets a password. */
    return ret;
}

/*===========================================================================*/
/*                          公开 API                                          */
/*===========================================================================*/

/**
 * @brief 初始化认证模块
 */
esp_err_t ts_auth_init(void)
{
    if (s_auth_initialized) return ESP_OK;
    if (!s_auth_gate) s_auth_gate = xSemaphoreCreateMutexStatic(&s_auth_gate_storage);
    if (!s_auth_gate) return ESP_ERR_NO_MEM;
    
    esp_err_t ret = nvs_open(TS_AUTH_NVS_NAMESPACE, NVS_READWRITE, &s_auth_nvs);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }
    
    memset(s_lockout_deadlines, 0, sizeof(s_lockout_deadlines));

    /* Only an empty, unversioned namespace is a new device. Errors or unknown
     * layouts are not permission to overwrite an existing credential. */
    uint8_t stored_version = 0;
    ret = nvs_get_u8(s_auth_nvs, TS_AUTH_CONFIG_VERSION_KEY, &stored_version);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        user_credential_t existing;
        ret = load_user_credential("admin", &existing);
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            if (ret == ESP_OK) ret = ESP_ERR_INVALID_STATE;
            goto failed;
        }
        ret = load_user_credential("root", &existing);
        if (ret != ESP_ERR_NVS_NOT_FOUND) {
            if (ret == ESP_OK) ret = ESP_ERR_INVALID_STATE;
            goto failed;
        }
        s_auth_format_version = TS_AUTH_CONFIG_VERSION;
        ret = force_create_user("admin", TS_PERM_ADMIN);
        if (ret == ESP_OK) ret = force_create_user("root", TS_PERM_ROOT);
        if (ret == ESP_OK)
            ret = nvs_set_u8(s_auth_nvs, TS_AUTH_CONFIG_VERSION_KEY, TS_AUTH_CONFIG_VERSION);
        if (ret == ESP_OK) ret = nvs_commit(s_auth_nvs);
        if (ret != ESP_OK) goto failed;
    } else if (ret == ESP_OK && (stored_version == TS_AUTH_CONFIG_VERSION ||
                                stored_version == TS_AUTH_PROTOTYPE_CONFIG_VERSION)) {
        s_auth_format_version = stored_version;
        ret = init_existing_user("admin");
        if (ret == ESP_OK) ret = init_existing_user("root");
        if (ret != ESP_OK) goto failed;
        ret = finish_credential_migration();
        if (ret != ESP_OK && ret != ESP_ERR_NOT_FINISHED) goto failed;
    } else {
        if (ret == ESP_OK) {
            TS_LOGE(TAG, "Unsupported auth config version %u; migration required", stored_version);
            ret = ESP_ERR_INVALID_STATE;
        }
        goto failed;
    }
    
    s_auth_initialized = true;
    TS_LOGI(TAG, "Auth module initialized (version %d)", TS_AUTH_CONFIG_VERSION);
    return ESP_OK;

failed:
    nvs_close(s_auth_nvs);
    s_auth_nvs = 0;
    return ret;
}

/**
 * @brief 验证用户密码
 */
static esp_err_t verify_password_locked(const char *username, const char *password,
                                   ts_perm_level_t *level)
{
    if (!username || !password) return ESP_ERR_INVALID_ARG;
    
    /* 只允许 admin 和 root 用户 */
    ts_perm_level_t user_level;
    if (strcmp(username, "admin") == 0) {
        user_level = TS_PERM_ADMIN;
    } else if (strcmp(username, "root") == 0) {
        user_level = TS_PERM_ROOT;
    } else {
        TS_LOGW(TAG, "Unknown user: %s", username);
        return ESP_ERR_NOT_FOUND;
    }
    
    user_credential_t cred;
    esp_err_t ret = load_user_credential(username, &cred);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to load credential for %s", username);
        return ret;
    }
    
    /* 检查是否被锁定 */
    uint32_t now = get_current_time_sec();
    unsigned index = user_level == TS_PERM_ROOT ? 1 : 0;
    if (s_lockout_deadlines[index] > now) {
        TS_LOGW(TAG, "User %s is locked out for %lu more seconds",
                username, (unsigned long)(s_lockout_deadlines[index] - now));
        return ESP_ERR_INVALID_STATE;
    }
    bool expired_lock = credential_is_locked(&cred);
    if (expired_lock) {
        cred.lockout_until = 0;
        cred.failed_attempts = 0;
    }
    
    bool matches;
    ret = ts_auth_credential_verify(&cred, password, &matches);
    if (ret != ESP_OK) return ret;

    if (!matches) {
        /* 密码错误，增加失败计数 */
        cred.failed_attempts++;
        if (cred.failed_attempts >= MAX_LOGIN_ATTEMPTS) {
            cred.lockout_until = 1; /* persistent marker, not an uptime deadline */
            s_lockout_deadlines[index] = now + LOGIN_LOCKOUT_SEC;
            cred.failed_attempts = 0;
            TS_LOGW(TAG, "User %s locked out due to too many failed attempts", username);
        }
        ret = save_user_credential(username, &cred, false);
        return ret == ESP_OK ? ESP_ERR_INVALID_ARG : ret;
    }
    
    /* Restore the original verifier after a successful prototype verification.
     * A crash leaves either the old or new complete blob under the same key. */
    bool migrated = cred.format == TS_AUTH_CREDENTIAL_MAGIC;
    if (migrated) {
        user_credential_t upgraded;
        ret = ts_auth_credential_create(password, cred.password_changed, &upgraded);
        if (ret != ESP_OK) return ret;
        cred = upgraded;
        memset(&upgraded, 0, sizeof(upgraded));
    }
    if (migrated || expired_lock || cred.failed_attempts || cred.lockout_until) {
        cred.failed_attempts = 0;
        cred.lockout_until = 0;
        ret = save_user_credential(username, &cred, false);
        if (ret != ESP_OK) return ret;
        if (migrated) {
            ret = finish_credential_migration();
            if (ret != ESP_OK && ret != ESP_ERR_NOT_FINISHED) return ret;
        }
    }
    s_lockout_deadlines[index] = 0;
    
    if (level) *level = user_level;
    return ESP_OK;
}

esp_err_t ts_auth_verify_password(const char *username, const char *password,
                                   ts_perm_level_t *level)
{
    if (!s_auth_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_auth_gate, portMAX_DELAY);
    esp_err_t ret = verify_password_locked(username, password, level);
    xSemaphoreGive(s_auth_gate);
    return ret;
}

/**
 * @brief 修改密码
 */
esp_err_t ts_auth_change_password(const char *username, const char *old_password,
                                   const char *new_password)
{
    if (!username || !old_password || !new_password) return ESP_ERR_INVALID_ARG;
    
    /* 新密码长度检查 */
    size_t new_len = strlen(new_password);
    if (new_len < 4 || new_len > 64) {
        TS_LOGW(TAG, "New password length invalid (4-64 chars required)");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_auth_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_auth_gate, portMAX_DELAY);
    /* Verify and replace under the same gate as login. */
    esp_err_t ret = verify_password_locked(username, old_password, NULL);
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Old password verification failed for %s", username);
        xSemaphoreGive(s_auth_gate);
        return ret;
    }
    
    ret = write_user_password_credential(username, new_password, true);
    xSemaphoreGive(s_auth_gate);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Password changed for user %s", username);
    }
    
    return ret;
}

/**
 * @brief Root 管理功能：设置 admin 密码
 */
esp_err_t ts_auth_set_admin_password(const char *new_password)
{
    if (!s_auth_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_auth_gate, portMAX_DELAY);
    esp_err_t ret = write_user_password_credential("admin", new_password, true);
    xSemaphoreGive(s_auth_gate);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Password set for admin by root");
    }

    return ret;
}

/**
 * @brief Root 管理功能：设置 root 密码
 */
esp_err_t ts_auth_set_root_password(const char *new_password)
{
    if (!s_auth_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_auth_gate, portMAX_DELAY);
    esp_err_t ret = write_user_password_credential("root", new_password, true);
    xSemaphoreGive(s_auth_gate);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Password set for root by root");
    }

    return ret;
}

/**
 * @brief 检查用户是否已修改初始密码
 */
bool ts_auth_password_changed(const char *username)
{
    if (!username) return true;
    
    user_credential_t cred;
    if (load_user_credential(username, &cred) != ESP_OK) {
        return true;  /* 找不到用户，视为已修改 */
    }
    
    return cred.password_changed;
}

/**
 * @brief 登录并创建会话
 */
esp_err_t ts_auth_login(const char *username, const char *password,
                         uint32_t *session_id, char *token, size_t token_len)
{
    if (!s_auth_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_auth_gate, portMAX_DELAY);
    ts_perm_level_t level;
    esp_err_t ret = verify_password_locked(username, password, &level);
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Login failed for user: %s", username);
        goto done;
    }
    
    ret = ts_security_create_session(username, level, session_id);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to create session for %s", username);
        goto done;
    }
    
    /* 生成 token */
    if (token && token_len > 0) {
        ret = ts_security_generate_token(*session_id, token, token_len);
        if (ret != ESP_OK) {
            ts_security_destroy_session(*session_id);
            goto done;
        }
    }
    
    TS_LOGI(TAG, "User %s logged in (level %d)", username, level);
done:
    xSemaphoreGive(s_auth_gate);
    return ret;
}

/**
 * @brief 登出
 */
esp_err_t ts_auth_logout(uint32_t session_id)
{
    return ts_security_destroy_session(session_id);
}

/**
 * @brief 验证请求认证
 */
esp_err_t ts_auth_validate_request(const char *auth_header, uint32_t *session_id,
                                    ts_perm_level_t *level)
{
    if (!auth_header) return ESP_ERR_INVALID_ARG;
    
    /* 支持 "Bearer <token>" 格式 */
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

/**
 * @brief 重置用户密码为默认值（管理功能）
 */
esp_err_t ts_auth_reset_password(const char *username)
{
    if (!username) return ESP_ERR_INVALID_ARG;
    
    /* 只允许重置 admin 和 root */
    const char *default_pwd;
    if (strcmp(username, "admin") == 0) {
        default_pwd = TS_AUTH_DEFAULT_ADMIN_PASSWORD;
    } else if (strcmp(username, "root") == 0) {
        default_pwd = TS_AUTH_DEFAULT_ROOT_PASSWORD;
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (!s_auth_initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_auth_gate, portMAX_DELAY);
    esp_err_t ret = write_user_password_credential(username, default_pwd, false);
    xSemaphoreGive(s_auth_gate);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Password reset to default for user %s", username);
    }
    
    return ret;
}
