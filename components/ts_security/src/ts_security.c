#include "ts_ssh_log_watch.h"
#include "ts_ssh_service.h"
/**
 * @file ts_security.c
 * @brief Security Subsystem Implementation
 */

#include "ts_security.h"
#include "ts_config_partitions.h"
#include "ts_crypto.h"
#include "ts_ssh_hosts_config.h"
#include "ts_ssh_commands_config.h"
#include "ts_core.h"  /* TS_MALLOC_PSRAM */
#include "ts_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "ts_security"
#define NVS_NAMESPACE "ts_security"
#define MAX_SESSIONS 8

typedef struct {
    bool active;
    ts_session_t session;
    bool token_issued;
    uint8_t token_data[16];
} session_slot_t;

static nvs_handle_t s_nvs;
static nvs_handle_t s_keys_nvs;
static session_slot_t s_sessions[MAX_SESSIONS];
static bool s_initialized = false;
/* Volatile owners (jobs and WS clients) survive service reinitialization. Never
 * recycle their identity within this boot; zero is reserved for internal work. */
static atomic_uint_least32_t s_last_session_id = ATOMIC_VAR_INIT(0);
/* One owner for slot allocation, token issuance/comparison and revocation.
 * Keep the gate across service restarts, just like the identity counter. */
static StaticSemaphore_t s_session_gate_storage;
static SemaphoreHandle_t s_session_gate;
static bool session_lock(void)
{
    return s_session_gate && xSemaphoreTakeRecursive(s_session_gate, portMAX_DELAY) == pdTRUE;
}
static void session_unlock(void)
{
    xSemaphoreGiveRecursive(s_session_gate);
}


esp_err_t ts_security_init(void)
{
    if (s_initialized) return ESP_OK;
    if (!s_session_gate) {
        s_session_gate = xSemaphoreCreateRecursiveMutexStatic(&s_session_gate_storage);
        if (!s_session_gate) return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to open NVS namespace");
        return ret;
    }
    
    ret = nvs_open_from_partition(TS_CONFIG_KEYS_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &s_keys_nvs);
    if (ret != ESP_OK) { nvs_close(s_nvs); return ret; }

    if (!session_lock()) { nvs_close(s_nvs); nvs_close(s_keys_nvs); return ESP_ERR_INVALID_STATE; }
    memset(s_sessions, 0, sizeof(s_sessions));
    session_unlock();
    
    // 初始化 SSH 主机配置模块
    ret = ts_ssh_hosts_config_init();
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Failed to init SSH hosts config: %s", esp_err_to_name(ret));
        // 非致命错误，继续运行
    }
    
    // 初始化 SSH 指令配置模块
    ret = ts_ssh_commands_config_init();
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Failed to init SSH commands config: %s", esp_err_to_name(ret));
        // 非致命错误，继续运行
    }
    
    ret = ts_ssh_service_init();
    if (ret != ESP_OK) return ret;
    ret = ts_ssh_log_watch_init();
    if (ret != ESP_OK) return ret;
    s_initialized = true;
    TS_LOGI(TAG, "Security subsystem initialized");
    return ESP_OK;
}

esp_err_t ts_security_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    nvs_close(s_nvs);
    nvs_close(s_keys_nvs);
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_security_random(void *buf, size_t len)
{
    if (!buf) return ESP_ERR_INVALID_ARG;
    esp_fill_random(buf, len);
    return ESP_OK;
}

esp_err_t ts_security_generate_key(const char *name, ts_key_type_t type)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    
    size_t key_len;
    switch (type) {
        case TS_KEY_TYPE_AES128: key_len = 16; break;
        case TS_KEY_TYPE_AES256: key_len = 32; break;
        default:
            TS_LOGW(TAG, "Key type %d not yet implemented", type);
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    uint8_t *key = TS_MALLOC_PSRAM(key_len);
    if (!key) return ESP_ERR_NO_MEM;
    
    ts_security_random(key, key_len);
    
    esp_err_t ret = ts_security_store_key(name, key, key_len);
    
    // Clear key from memory
    memset(key, 0, key_len);
    free(key);
    
    return ret;
}

esp_err_t ts_security_load_key(const char *name, void *key, size_t *key_len)
{
    if (!name || !key || !key_len) return ESP_ERR_INVALID_ARG;
    
    char nvs_key[32];
    snprintf(nvs_key, sizeof(nvs_key), "key_%s", name);
    
    return nvs_get_blob(s_keys_nvs, nvs_key, key, key_len);
}

esp_err_t ts_security_store_key(const char *name, const void *key, size_t key_len)
{
    if (!name || !key) return ESP_ERR_INVALID_ARG;
    
    char nvs_key[32];
    snprintf(nvs_key, sizeof(nvs_key), "key_%s", name);
    
    esp_err_t ret = nvs_set_blob(s_keys_nvs, nvs_key, key, key_len);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_keys_nvs);
    }
    return ret;
}

esp_err_t ts_security_delete_key(const char *name)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    
    char nvs_key[32];
    snprintf(nvs_key, sizeof(nvs_key), "key_%s", name);
    
    esp_err_t ret = nvs_erase_key(s_keys_nvs, nvs_key);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_keys_nvs);
    }
    return ret;
}

esp_err_t ts_security_load_cert(const char *name, ts_cert_type_t type,
                                 void *cert, size_t *cert_len)
{
    if (!name || !cert || !cert_len) return ESP_ERR_INVALID_ARG;
    
    char nvs_key[32];
    snprintf(nvs_key, sizeof(nvs_key), "cert_%s", name);
    
    return nvs_get_blob(s_nvs, nvs_key, cert, cert_len);
}

esp_err_t ts_security_store_cert(const char *name, ts_cert_type_t type,
                                  const void *cert, size_t cert_len)
{
    if (!name || !cert) return ESP_ERR_INVALID_ARG;
    
    char nvs_key[32];
    snprintf(nvs_key, sizeof(nvs_key), "cert_%s", name);
    
    esp_err_t ret = nvs_set_blob(s_nvs, nvs_key, cert, cert_len);
    if (ret == ESP_OK) {
        nvs_commit(s_nvs);
    }
    return ret;
}

/**
 * @brief 驱逐指定 client_id 的所有会话（释放槽位）
 * @note 密码成功更新时撤销目标账户，也用于槽位满时按同用户驱逐
 */
static void ts_security_destroy_sessions_by_client_locked(const char *client_id)
{
    if (!client_id || client_id[0] == '\0') return;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active &&
            strcmp(s_sessions[i].session.client_id, client_id) == 0) {
            s_sessions[i].active = false;
        }
    }
}

static esp_err_t ts_security_create_session_locked(const char *client_id, ts_perm_level_t level,
                                      uint32_t *session_id)
{
    if (!session_id) return ESP_ERR_INVALID_ARG;
    
    uint_least32_t previous = atomic_load(&s_last_session_id);
    do {
        if (previous >= UINT32_MAX) return ESP_ERR_INVALID_STATE;
    } while (!atomic_compare_exchange_weak(&s_last_session_id, &previous, previous + 1));
    uint32_t sid = (uint32_t)previous + 1;

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!s_sessions[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        if (client_id && client_id[0] != '\0') {
            ts_security_destroy_sessions_by_client(client_id);
            for (int i = 0; i < MAX_SESSIONS; i++) {
                if (!s_sessions[i].active) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot < 0) {
            return ESP_ERR_NO_MEM;
        }
    }
    
    uint32_t now = esp_timer_get_time() / 1000000;
    
    memset(&s_sessions[slot], 0, sizeof(s_sessions[slot]));
    s_sessions[slot].active = true;
    s_sessions[slot].session.session_id = sid;
    s_sessions[slot].session.level = level;
    s_sessions[slot].session.created_at = now;
#ifdef CONFIG_TS_SECURITY_TOKEN_EXPIRE_SEC
    s_sessions[slot].session.expires_at = now + CONFIG_TS_SECURITY_TOKEN_EXPIRE_SEC;
#else
    s_sessions[slot].session.expires_at = now + 3600;
#endif
    
    if (client_id) {
        strncpy(s_sessions[slot].session.client_id, client_id, 
                sizeof(s_sessions[slot].session.client_id) - 1);
    }
    
    *session_id = sid;
    return ESP_OK;
}

static esp_err_t ts_security_validate_session_locked(uint32_t session_id, ts_session_t *session)
{
    uint32_t now = esp_timer_get_time() / 1000000;
    
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && s_sessions[i].session.session_id == session_id) {
            if (now > s_sessions[i].session.expires_at) {
                s_sessions[i].active = false;
                return ESP_ERR_TIMEOUT;
            }
            if (session) {
                *session = s_sessions[i].session;
            }
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t ts_security_destroy_session_locked(uint32_t session_id)
{
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && s_sessions[i].session.session_id == session_id) {
            s_sessions[i].active = false;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

bool ts_security_check_permission(uint32_t session_id, ts_perm_level_t required)
{
    ts_session_t session;
    if (ts_security_validate_session(session_id, &session) != ESP_OK) {
        return false;
    }
    return session.level >= required;
}

static esp_err_t ts_security_generate_token_locked(uint32_t session_id, char *token, size_t max_len)
{
    if (!token || max_len < 64) return ESP_ERR_INVALID_ARG;
    
    esp_err_t ret = ts_security_validate_session(session_id, NULL);
    if (ret != ESP_OK) return ret;

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && s_sessions[i].session.session_id == session_id) {
            // Keep the 16-byte Base64 wire length, with all bytes random.
            // The session identity and uptime are metadata, never bearer entropy.
            uint8_t data[16];
            ret = ts_security_random(data, sizeof(data));
            if (ret != ESP_OK) return ret;

            size_t out_len = max_len;
            ret = ts_crypto_base64_encode(data, sizeof(data), token, &out_len);
            if (ret == ESP_OK) {
                memcpy(s_sessions[i].token_data, data, sizeof(data));
                s_sessions[i].token_issued = true;
            }
            return ret;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t ts_security_validate_token_locked(const char *token, uint32_t *session_id)
{
    if (!token || !session_id) return ESP_ERR_INVALID_ARG;
    
    uint8_t data[16];
    size_t data_len = sizeof(data);
    
    esp_err_t ret = ts_crypto_base64_decode(token, strlen(token), data, &data_len);
    if (ret != ESP_OK || data_len != sizeof(data)) return ESP_ERR_INVALID_ARG;
    
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (s_sessions[i].active && s_sessions[i].token_issued) {
            uint8_t diff = 0;
            for (size_t j = 0; j < sizeof(data); j++) {
                diff |= data[j] ^ s_sessions[i].token_data[j];
            }
            if (diff != 0) continue;
            uint32_t sid = s_sessions[i].session.session_id;
            ret = ts_security_validate_session(sid, NULL);
            if (ret != ESP_OK) return ret;
            *session_id = sid;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

/* Public entry points serialize every access to the shared session slots. */
void ts_security_destroy_sessions_by_client(const char *client_id)
{
    if (!session_lock()) return;
    ts_security_destroy_sessions_by_client_locked(client_id);
    session_unlock();
}

esp_err_t ts_security_create_session(const char *client_id, ts_perm_level_t level, uint32_t *session_id)
{
    if (!session_lock()) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = ts_security_create_session_locked(client_id, level, session_id);
    session_unlock();
    if (ret == ESP_OK) TS_LOGI(TAG, "Session created");
    if (ret == ESP_ERR_NO_MEM) TS_LOGW(TAG, "No free session slots");
    return ret;
}

esp_err_t ts_security_validate_session(uint32_t session_id, ts_session_t *session)
{
    if (!session_lock()) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = ts_security_validate_session_locked(session_id, session);
    session_unlock();
    return ret;
}

esp_err_t ts_security_destroy_session(uint32_t session_id)
{
    if (!session_lock()) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = ts_security_destroy_session_locked(session_id);
    session_unlock();
    if (ret == ESP_OK) TS_LOGI(TAG, "Session destroyed");
    return ret;
}

esp_err_t ts_security_generate_token(uint32_t session_id, char *token, size_t max_len)
{
    if (!session_lock()) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = ts_security_generate_token_locked(session_id, token, max_len);
    session_unlock();
    return ret;
}

esp_err_t ts_security_validate_token(const char *token, uint32_t *session_id)
{
    if (!session_lock()) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = ts_security_validate_token_locked(token, session_id);
    session_unlock();
    return ret;
}
