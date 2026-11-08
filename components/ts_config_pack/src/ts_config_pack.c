/**
 * @file ts_config_pack.c
 * @brief TianShanOS Encrypted Configuration Package Implementation
 * 
 * 实现配置包的加密、解密、签名和验证逻辑。
 * 
 * 加密流程:
 * 1. 生成临时 EC 密钥对 (ephemeral)
 * 2. ECDH 密钥协商 (ephemeral_priv × recipient_pub)
 * 3. HKDF 密钥派生 (shared_secret → AES key)
 * 4. AES-256-GCM 加密
 * 5. ECDSA 签名
 * 
 * 解密流程:
 * 1. 验证签名者证书链
 * 2. 验证签名
 * 3. ECDH 密钥协商 (device_priv × ephemeral_pub)
 * 4. HKDF 密钥派生
 * 5. AES-256-GCM 解密
 * 6. 验证内容哈希
 */

#include "ts_storage.h"
#include "ts_config_pack.h"
#include "ts_core.h"
#include "ts_cert.h"
#include "ts_crypto.h"
#include "ts_config_pack_trust.h"
#include "ts_time_sync.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>

#ifndef CONFIG_TS_CONFIG_PACK_MAX_SIZE
#define CONFIG_TS_CONFIG_PACK_MAX_SIZE 65536
#endif

static bool unambiguous_json(const cJSON *j, unsigned depth) {
    if (depth > 32) return false;
    for (const cJSON *a = j->child; a; a = a->next) {
        if (cJSON_IsObject(j)) {
            for (const cJSON *b = a->next; b; b = b->next)
                if (a->string && b->string && !strcmp(a->string, b->string)) return false;
        }
        if (!unambiguous_json(a, depth + 1)) return false;
    }
    return true;
}
static cJSON *parse_json_bounded(const char *text, size_t length, size_t limit) {
    if (!text || !length || length > limit || memchr(text, 0, length))
        return NULL;
    bool string = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = text[i];
        if (string && c == '\\') {
            if (i + 5 < length && text[i+1] == 'u' && !memcmp(text+i+2, "0000", 4)) return NULL;
            ++i;
        } else if (c == '"') string = !string;
        else if (!string && (c == '{' || c == '[')) { if (++depth > 32) return NULL; }
        else if (!string && (c == '}' || c == ']')) { if (!depth) return NULL; --depth; }
    }
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(text, length, &end, false);
    if (!j) return NULL;
    while (end < text + length && isspace((unsigned char)*end)) ++end;
    if (end != text + length || !unambiguous_json(j, 0)) { cJSON_Delete(j); return NULL; }
    return j;
}
cJSON *ts_config_pack_parse_json(const char *text, size_t length) {
    return parse_json_bounded(text,length,CONFIG_TS_CONFIG_PACK_MAX_SIZE);
}
/* Validate the outer transport before cJSON can discard a decoded NUL or
 * choose one of duplicate parameters. JSON escaping can expand bytes sixfold. */
cJSON *ts_config_pack_parse_import_request(const char *text,size_t length) {
    return parse_json_bounded(text,length,(size_t)CONFIG_TS_CONFIG_PACK_MAX_SIZE*6+1024);
}

static const char *TAG = "ts_config_pack";

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

/** ECDH P-256 公钥长度 (uncompressed: 0x04 || X || Y) */
#define ECDH_PUBKEY_LEN         65

/** HKDF salt 长度 */
#define HKDF_SALT_LEN           32

/** AES-256 密钥长度 */
#define AES_KEY_LEN             32

/** AES-GCM IV 长度 */
#define AES_IV_LEN              12

/** AES-GCM tag 长度 */
#define AES_TAG_LEN             16

/** SHA-256 哈希长度 */
#define SHA256_LEN              32

/** 证书指纹长度 (hex string) */
#define CERT_FINGERPRINT_LEN    64

/** 最大签名长度 (ECDSA P-256 DER) */
#define MAX_SIGNATURE_LEN       72

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/** 内部：加密参数 */
typedef struct {
    uint8_t ephemeral_pubkey[ECDH_PUBKEY_LEN];
    uint8_t salt[HKDF_SALT_LEN];
    uint8_t iv[AES_IV_LEN];
    uint8_t tag[AES_TAG_LEN];
    char recipient_fingerprint[CERT_FINGERPRINT_LEN + 1];
} ts_config_pack_crypto_params_t;

/*===========================================================================*/
/*                          Static Variables                                  */
/*===========================================================================*/

static bool s_initialized = false;

static ts_config_pack_result_t load_cached_mem(const char *bytes, size_t length,
    ts_config_pack_t **pack, bool require_code_signing, bool existing_file);

/*===========================================================================*/
/*                          Forward Declarations                              */
/*===========================================================================*/

static ts_config_pack_result_t parse_tscfg_json(const char *json, size_t len,
                                                  cJSON **root,
                                                  ts_config_pack_crypto_params_t *params,
                                                  char **payload_b64,
                                                  char **signer_cert_pem,
                                                  char **signature_b64,
                                                  ts_config_pack_sig_info_t *sig_info);
static ts_config_pack_result_t verify_signature(const char *signer_cert_pem,
                                                  const uint8_t *data_to_sign,
                                                  size_t data_len,
                                                  const uint8_t *signature,
                                                  size_t sig_len);


static ts_config_pack_result_t verify_pack_mem(const char *text, size_t length, bool require_code_signing, bool existing_file,
    const ts_config_pack_acceptance_t *accepted, ts_config_pack_t **pack,
    ts_config_pack_acceptance_t *acceptance, uint32_t *credential_generation,
    ts_config_pack_sig_info_t *signature_info);

/*===========================================================================*/
/*                          Error Messages                                    */
/*===========================================================================*/

static const char *s_error_messages[] = {
    [TS_CONFIG_PACK_OK]             = "Success",
    [TS_CONFIG_PACK_ERR_PARSE]      = "JSON parse error",
    [TS_CONFIG_PACK_ERR_VERSION]    = "Unsupported version",
    [TS_CONFIG_PACK_ERR_RECIPIENT]  = "Not intended for this device",
    [TS_CONFIG_PACK_ERR_CERT_CHAIN] = "Certificate chain validation failed",
    [TS_CONFIG_PACK_ERR_SIGNATURE]  = "Signature verification failed",
    [TS_CONFIG_PACK_ERR_DECRYPT]    = "Decryption failed",
    [TS_CONFIG_PACK_ERR_INTEGRITY]  = "Content hash mismatch",
    [TS_CONFIG_PACK_ERR_EXPIRED]    = "Package expired",
    [TS_CONFIG_PACK_ERR_NO_MEM]     = "Memory allocation failed",
    [TS_CONFIG_PACK_ERR_IO]         = "File I/O error",
    [TS_CONFIG_PACK_ERR_PERMISSION] = "Permission denied (not a developer device)",
    [TS_CONFIG_PACK_ERR_INVALID_ARG]= "Invalid argument",
    [TS_CONFIG_PACK_ERR_NOT_INIT]   = "System not initialized",
    [TS_CONFIG_PACK_ERR_TIME_UNVERIFIED] = "Trusted time is not available",
    [TS_CONFIG_PACK_ERR_TRUST_NOT_CONFIGURED] = "Configuration signing root is not configured",
    [TS_CONFIG_PACK_ERR_SIGNER_ROLE] = "Certificate is not authorized for configuration signing",
    [TS_CONFIG_PACK_ERR_CREDENTIAL_CHANGED] = "Credentials changed during validation",
    [TS_CONFIG_PACK_ERR_UNSUPPORTED] = "Generic configuration pack application is not supported",
};

const char *ts_config_pack_strerror(ts_config_pack_result_t result)
{
    if (result < 0 || result >= sizeof(s_error_messages) / sizeof(s_error_messages[0])) {
        return "Unknown error";
    }
    return s_error_messages[result];
}

/** The reason token the Web UI translates (the page keeps ts_config_pack_strerror()'s English only for its folded details). */
const char *ts_config_pack_reason(ts_config_pack_result_t result)
{
    switch (result) {
        case TS_CONFIG_PACK_OK:              return "ok";
        case TS_CONFIG_PACK_ERR_UNSUPPORTED: return "pack_apply_unsupported";
        case TS_CONFIG_PACK_ERR_PARSE:       return "pack_parse_failed";
        case TS_CONFIG_PACK_ERR_VERSION:     return "pack_version_unsupported";
        case TS_CONFIG_PACK_ERR_RECIPIENT:   return "pack_wrong_device";
        case TS_CONFIG_PACK_ERR_CERT_CHAIN:  return "pack_cert_invalid";
        case TS_CONFIG_PACK_ERR_SIGNATURE:   return "pack_signature_invalid";
        case TS_CONFIG_PACK_ERR_DECRYPT:     return "pack_decrypt_failed";
        case TS_CONFIG_PACK_ERR_INTEGRITY:   return "pack_corrupted";
        case TS_CONFIG_PACK_ERR_EXPIRED:     return "pack_expired";
        case TS_CONFIG_PACK_ERR_NO_MEM:      return "no_memory";
        case TS_CONFIG_PACK_ERR_IO:          return "pack_io_failed";
        case TS_CONFIG_PACK_ERR_PERMISSION:  return "pack_not_allowed";
        case TS_CONFIG_PACK_ERR_NOT_INIT:    return "pack_not_ready";
        case TS_CONFIG_PACK_ERR_INVALID_ARG: return "invalid_argument";
        case TS_CONFIG_PACK_ERR_TIME_UNVERIFIED:        return "time_unverified";
        case TS_CONFIG_PACK_ERR_TRUST_NOT_CONFIGURED:   return "signing_trust_unconfigured";
        case TS_CONFIG_PACK_ERR_SIGNER_ROLE:            return "signer_unauthorized";
        case TS_CONFIG_PACK_ERR_CREDENTIAL_CHANGED:     return "credential_changed";
        default:                             return "pack_verify_failed";
    }
}

/*===========================================================================*/
/*                          Initialization                                    */
/*===========================================================================*/

esp_err_t ts_config_pack_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Config pack system initialized");
    s_initialized = true;
    return ESP_OK;
}

bool ts_config_pack_can_export(void)
{
    /* 检查设备证书的 OU 字段是否包含 "Developer" */
    ts_cert_info_t info;
    if (ts_cert_get_info(&info) != ESP_OK) {
        return false;
    }
    
    /* 检查 OU 字段是否为 "Developer" */
    bool can_export = strcmp(info.subject_ou, "Developer") == 0;
    if (!can_export) {
        ESP_LOGD(TAG, "Device OU='%s', export not allowed", info.subject_ou);
    }
    return can_export;
}

/*===========================================================================*/
/*                          Load Functions                                    */
/*===========================================================================*/

/* One bounded immutable read is used by both encrypted and plain configuration
 * paths. A selected encrypted file never falls back after an I/O/trust error. */
static ts_config_pack_result_t read_config_bytes(const char *path, char **out, size_t *length)
{
    *out = NULL; *length = 0;
    FILE *file = fopen(path, "rb");
    if (!file) return TS_CONFIG_PACK_ERR_IO;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long size = ok ? ftell(file) : -1;
    ts_config_pack_result_t result = TS_CONFIG_PACK_ERR_IO;
    if (size <= 0 || size > CONFIG_TS_CONFIG_PACK_MAX_SIZE) {
        result = TS_CONFIG_PACK_ERR_PARSE;
    } else if (fseek(file, 0, SEEK_SET) == 0) {
        char *bytes = TS_MALLOC_PSRAM((size_t)size + 1);
        if (!bytes) result = TS_CONFIG_PACK_ERR_NO_MEM;
        else {
            if (fread(bytes, 1, size, file) == (size_t)size && !ferror(file)) {
                bytes[size] = 0; *out = bytes; *length = size;
                result = TS_CONFIG_PACK_OK;
            } else free(bytes);
        }
    }
    if (fclose(file) && result == TS_CONFIG_PACK_OK) {
        free(*out); *out = NULL; *length = 0; result = TS_CONFIG_PACK_ERR_IO;
    }
    return result;
}

/* Public acceptance metadata is held in internal NVS, never read from package
 * fields or an SD sidecar. The short index cannot authorize a hash collision:
 * the full package digest, issuer, signer and recipient are rechecked below. */
static bool acceptance_matches(const ts_config_pack_acceptance_t *record, const char *hash)
{
    return record->policy == TS_CONFIG_PACK_ACCEPT_POLICY && record->accepted_at > 0 &&
        record->package_sha256[64] == 0 && record->signer_sha256[64] == 0 &&
        record->root_sha256[64] == 0 && record->recipient[64] == 0 &&
        !strcmp(record->package_sha256, hash) &&
        !strcmp(record->root_sha256, CONFIG_TS_CONFIG_PACK_SIGNING_ROOT_SHA256);
}

static esp_err_t read_acceptance(const char *index, ts_config_pack_acceptance_t *record)
{
    memset(record, 0, sizeof(*record));
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("ts_pack_accept", NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (ret != ESP_OK) return ret;
    size_t length = 0;
    ret = nvs_get_blob(handle, index, NULL, &length);
    if (ret == ESP_OK && length == sizeof(*record))
        ret = nvs_get_blob(handle, index, record, &length);
    nvs_close(handle);
    return ret == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : ret;
}

static esp_err_t write_acceptance(const char *index, const ts_config_pack_acceptance_t *record)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open("ts_pack_accept", NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    ret = nvs_set_blob(handle, index, record, sizeof(*record));
    if (ret == ESP_OK) ret = nvs_commit(handle);
    nvs_close(handle);
    return ret;
}

ts_config_pack_result_t ts_config_pack_load(const char *path, ts_config_pack_t **pack)
{
    if (!pack) return TS_CONFIG_PACK_ERR_INVALID_ARG;
    *pack = NULL;
    if (!path) return TS_CONFIG_PACK_ERR_INVALID_ARG;
    if (!s_initialized) return TS_CONFIG_PACK_ERR_NOT_INIT;
    char *bytes = NULL; size_t length = 0;
    ts_config_pack_result_t result = read_config_bytes(path, &bytes, &length);
    if (result == TS_CONFIG_PACK_OK) result = load_cached_mem(bytes, length, pack, false, true);
    free(bytes);
    return result;
}

static ts_config_pack_result_t lookup_acceptance(const char *bytes, size_t length,
    ts_config_pack_acceptance_t *accepted, char index[16], bool *reload)
{
    if (!bytes || !length || length > CONFIG_TS_CONFIG_PACK_MAX_SIZE)
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    if (!s_initialized) return TS_CONFIG_PACK_ERR_NOT_INIT;
    unsigned char digest[32]; char hash[65];
    if (ts_crypto_hash(TS_HASH_SHA256, bytes, length, digest, sizeof digest) != ESP_OK ||
        ts_crypto_hex_encode(digest, sizeof digest, hash, sizeof hash) != ESP_OK)
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    memcpy(index, hash, 15); index[15] = 0;
    if (read_acceptance(index, accepted) != ESP_OK) return TS_CONFIG_PACK_ERR_IO;
    *reload = acceptance_matches(accepted, hash);
    return TS_CONFIG_PACK_OK;
}

static ts_config_pack_result_t load_cached_mem(const char *bytes, size_t length,
    ts_config_pack_t **pack, bool require_code_signing, bool existing_file)
{
    if (!pack) return TS_CONFIG_PACK_ERR_INVALID_ARG;
    *pack = NULL;
    char index[16]; bool reload = false;
    ts_config_pack_acceptance_t accepted = {0}, verified = {0};
    ts_config_pack_result_t result = lookup_acceptance(bytes, length, &accepted, index, &reload);
    if (result != TS_CONFIG_PACK_OK) return result;
    result = verify_pack_mem(bytes, length, require_code_signing, existing_file,
                             reload ? &accepted : NULL, pack, &verified, NULL, NULL);
    if (result == TS_CONFIG_PACK_OK && !reload && verified.accepted_at > 0) {
        ts_config_pack_acceptance_t readback;
        if (write_acceptance(index, &verified) != ESP_OK ||
            read_acceptance(index, &readback) != ESP_OK || memcmp(&readback, &verified, sizeof verified)) {
            ts_config_pack_free(*pack); *pack = NULL; result = TS_CONFIG_PACK_ERR_IO;
        }
    }
    return result;
}

ts_config_pack_result_t ts_config_pack_load_mem(const char *bytes, size_t length, ts_config_pack_t **pack)
{
    return load_cached_mem(bytes, length, pack, false, false);
}

/* Command-bearing legacy automation keeps strict code-signing usage while
 * reusing the same exact-byte acceptance cache. The payload cannot choose it. */
ts_config_pack_result_t ts_config_pack_load_rule_mem(const char *bytes, size_t length, ts_config_pack_t **pack)
{
    return load_cached_mem(bytes, length, pack, true, false);
}

void ts_config_pack_free(ts_config_pack_t *pack)
{
    if (!pack) return;
    
    free(pack->name);
    free(pack->description);
    free(pack->content);
    free(pack->source_file);
    free(pack->target_device);
    free(pack);
}

/*===========================================================================*/
/*                          Create Functions                                  */
/*===========================================================================*/

ts_config_pack_result_t ts_config_pack_create(
    const char *name,
    const char *json_content,
    size_t json_len,
    const ts_config_pack_export_opts_t *opts,
    char **output,
    size_t *output_len)
{
    if (!name || !json_content || !opts || !output || !output_len) {
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    if (!s_initialized) {
        return TS_CONFIG_PACK_ERR_NOT_INIT;
    }
    
    /* 检查导出权限 */
    if (!ts_config_pack_can_export()) {
        ESP_LOGE(TAG, "This device is not authorized to export config packs");
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    /* PEM parsers require a terminating NUL; callers may supply exact bytes. */
    if (!opts->recipient_cert_pem || !opts->recipient_cert_len || opts->recipient_cert_len > TS_CERT_PEM_MAX_LEN)
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    const char *terminator = memchr(opts->recipient_cert_pem, 0, opts->recipient_cert_len);
    if (terminator && terminator != opts->recipient_cert_pem + opts->recipient_cert_len - 1)
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    size_t recipient_length = opts->recipient_cert_len - (terminator != NULL);
    char *recipient_pem = TS_MALLOC_PSRAM(recipient_length + 1);
    if (!recipient_pem) return TS_CONFIG_PACK_ERR_NO_MEM;
    memcpy(recipient_pem, opts->recipient_cert_pem, recipient_length);
    recipient_pem[recipient_length] = 0;
    /* 解析接收方证书，获取公钥 */
    ts_keypair_t recipient_key = NULL;
    esp_err_t ret = ts_crypto_keypair_import(recipient_pem,
                                              recipient_length + 1,
                                              &recipient_key);
    if (ret != ESP_OK) {
        free(recipient_pem);
        ESP_LOGE(TAG, "Failed to parse recipient certificate");
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    /* 计算接收方证书指纹 */
    char recipient_fingerprint[CERT_FINGERPRINT_LEN + 1];
    ret = ts_config_pack_fingerprint_certificate(recipient_pem, recipient_length + 1,
                                    recipient_fingerprint, sizeof(recipient_fingerprint));
    free(recipient_pem);
    if (ret != ESP_OK) {
        ts_crypto_keypair_free(recipient_key);
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Export: recipient fingerprint = %s (cert_len=%zu)", 
             recipient_fingerprint, opts->recipient_cert_len);
    
    /* 生成临时密钥对 */
    ts_keypair_t ephemeral_key = NULL;
    ret = ts_crypto_keypair_generate(TS_CRYPTO_KEY_EC_P256, &ephemeral_key);
    if (ret != ESP_OK) {
        ts_crypto_keypair_free(recipient_key);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    /* 导出临时公钥 */
    uint8_t ephemeral_pubkey[ECDH_PUBKEY_LEN];
    size_t pubkey_len = sizeof(ephemeral_pubkey);
    ret = ts_crypto_keypair_export_public_raw(ephemeral_key, ephemeral_pubkey, &pubkey_len);
    if (ret != ESP_OK) {
        ts_crypto_keypair_free(ephemeral_key);
        ts_crypto_keypair_free(recipient_key);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    /* ECDH 密钥协商 */
    uint8_t shared_secret[32];
    size_t shared_len = sizeof(shared_secret);
    uint8_t recipient_pubkey[ECDH_PUBKEY_LEN];
    size_t recipient_pubkey_len=sizeof recipient_pubkey;
    ret=ts_crypto_keypair_export_public_raw(recipient_key,recipient_pubkey,&recipient_pubkey_len);
    if(ret==ESP_OK&&recipient_pubkey_len==sizeof recipient_pubkey)
        ret = ts_crypto_ecdh_compute_shared_raw(ephemeral_key,recipient_pubkey,recipient_pubkey_len,shared_secret,&shared_len);
    else ret=ESP_ERR_INVALID_ARG;
    
    /* 立即销毁临时私钥 */
    ts_crypto_keypair_free(ephemeral_key);
    ts_crypto_keypair_free(recipient_key);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ECDH key agreement failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* 生成随机 salt 和 IV */
    uint8_t salt[HKDF_SALT_LEN];
    uint8_t iv[AES_IV_LEN];
    if(ts_crypto_random(salt,sizeof salt)!=ESP_OK||ts_crypto_random(iv,sizeof iv)!=ESP_OK){
        memset(shared_secret,0,sizeof shared_secret);
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* HKDF 密钥派生 */
    uint8_t aes_key[AES_KEY_LEN];
    ret = ts_crypto_hkdf(salt, sizeof(salt),
                          shared_secret, shared_len,
                          TS_CONFIG_PACK_HKDF_INFO, strlen(TS_CONFIG_PACK_HKDF_INFO),
                          aes_key, sizeof(aes_key));
    
    /* 清除 shared secret */
    memset(shared_secret, 0, sizeof(shared_secret));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HKDF key derivation failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* AES-GCM 加密 */
    size_t ciphertext_len = json_len;
    uint8_t *ciphertext = TS_MALLOC_PSRAM(ciphertext_len);
    uint8_t tag[AES_TAG_LEN];
    
    if (!ciphertext) {
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    ret = ts_crypto_aes_gcm_encrypt(aes_key, sizeof(aes_key),
                                     iv, sizeof(iv),
                                     NULL, 0,  /* 无 AAD */
                                     json_content, json_len,
                                     ciphertext, tag);
    
    /* 清除 AES 密钥 */
    memset(aes_key, 0, sizeof(aes_key));
    
    if (ret != ESP_OK) {
        free(ciphertext);
        ESP_LOGE(TAG, "AES-GCM encryption failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* 计算内容哈希 */
    uint8_t content_hash[SHA256_LEN];
    ts_crypto_hash(TS_HASH_SHA256, json_content, json_len, content_hash, sizeof(content_hash));
    
    /* 获取设备私钥用于签名 - 使用堆分配避免栈溢出 */
    char *key_pem = TS_MALLOC_PSRAM(TS_CERT_KEY_MAX_LEN);
    if (!key_pem) {
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    size_t key_len = TS_CERT_KEY_MAX_LEN;
    ret = ts_cert_get_private_key(key_pem, &key_len);
    if (ret != ESP_OK) {
        free(key_pem);
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    ts_keypair_t signer_key = NULL;
    ret = ts_crypto_keypair_import(key_pem, key_len, &signer_key);
    memset(key_pem, 0, TS_CERT_KEY_MAX_LEN);  /* 清除私钥 */
    free(key_pem);
    
    if (ret != ESP_OK) {
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    /* 计算待签名数据的哈希 */
    uint8_t data_hash[SHA256_LEN];
    if (ts_crypto_hash(TS_HASH_SHA256, ciphertext, ciphertext_len, data_hash, sizeof(data_hash)) != ESP_OK) {
        ts_crypto_keypair_free(signer_key);free(ciphertext);
        return TS_CONFIG_PACK_ERR_SIGNATURE;
    }
    
    /* ECDSA 签名 */
    uint8_t signature[MAX_SIGNATURE_LEN];
    size_t sig_len = sizeof(signature);
    ret = ts_crypto_ecdsa_sign(signer_key, data_hash, sizeof(data_hash),
                                signature, &sig_len);
    ts_crypto_keypair_free(signer_key);
    
    if (ret != ESP_OK) {
        free(ciphertext);
        ESP_LOGE(TAG, "ECDSA signing failed");
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    /* 获取设备证书 - 使用堆分配避免栈溢出 */
    char *cert_pem = TS_MALLOC_PSRAM(TS_CERT_PEM_MAX_LEN);
    if (!cert_pem) {
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    size_t cert_len = TS_CERT_PEM_MAX_LEN;
    ret = ts_cert_get_certificate(cert_pem, &cert_len);
    if (ret != ESP_OK) {
        free(cert_pem);
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    /* 构建 JSON 输出 */
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(root, "tscfg_version", TS_CONFIG_PACK_VERSION);
    cJSON_AddStringToObject(root, "format", "encrypted");
    
    /* metadata */
    cJSON *metadata = cJSON_CreateObject();
    cJSON_AddStringToObject(metadata, "name", name);
    if (opts->description) {
        cJSON_AddStringToObject(metadata, "description", opts->description);
    }
    
    /* 获取当前时间 */
    time_t now = time(NULL);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    cJSON_AddStringToObject(metadata, "created_at", time_str);
    
    /* 获取设备 ID */
    ts_cert_info_t cert_info;
    if (ts_cert_get_info(&cert_info) == ESP_OK) {
        cJSON_AddStringToObject(metadata, "created_by", cert_info.subject_cn);
    }
    
    /* TODO: 解析接收方证书获取 target_device */
    cJSON_AddStringToObject(metadata, "target_device", "unknown");
    
    char source_file[128];
    snprintf(source_file, sizeof(source_file), "%s.json", name);
    cJSON_AddStringToObject(metadata, "source_file", source_file);
    
    /* Base64 编码 content_hash */
    char hash_b64[64];
    size_t hash_b64_len = sizeof(hash_b64);
    ts_crypto_base64_encode(content_hash, sizeof(content_hash), hash_b64, &hash_b64_len);
    cJSON_AddStringToObject(metadata, "content_hash", hash_b64);
    
    cJSON_AddItemToObject(root, "metadata", metadata);
    
    /* encryption */
    cJSON *encryption = cJSON_CreateObject();
    cJSON_AddStringToObject(encryption, "algorithm", TS_CONFIG_PACK_ALGORITHM);
    cJSON_AddStringToObject(encryption, "kdf", TS_CONFIG_PACK_KDF);
    
    /* Base64 编码各字段 */
    char pubkey_b64[128], salt_b64[64], iv_b64[32], tag_b64[32];
    size_t b64_len;
    
    b64_len = sizeof(pubkey_b64);
    ts_crypto_base64_encode(ephemeral_pubkey, pubkey_len, pubkey_b64, &b64_len);
    cJSON_AddStringToObject(encryption, "ephemeral_public_key", pubkey_b64);
    
    b64_len = sizeof(salt_b64);
    ts_crypto_base64_encode(salt, sizeof(salt), salt_b64, &b64_len);
    cJSON_AddStringToObject(encryption, "salt", salt_b64);
    
    b64_len = sizeof(iv_b64);
    ts_crypto_base64_encode(iv, sizeof(iv), iv_b64, &b64_len);
    cJSON_AddStringToObject(encryption, "iv", iv_b64);
    
    b64_len = sizeof(tag_b64);
    ts_crypto_base64_encode(tag, sizeof(tag), tag_b64, &b64_len);
    cJSON_AddStringToObject(encryption, "tag", tag_b64);
    
    cJSON_AddStringToObject(encryption, "recipient_cert_fingerprint", recipient_fingerprint);
    cJSON_AddItemToObject(root, "encryption", encryption);
    
    /* signature */
    cJSON *sig_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(sig_obj, "algorithm", TS_CONFIG_PACK_SIG_ALGORITHM);
    cJSON_AddStringToObject(sig_obj, "signer_certificate", cert_pem);
    
    char sig_b64[128];
    b64_len = sizeof(sig_b64);
    ts_crypto_base64_encode(signature, sig_len, sig_b64, &b64_len);
    cJSON_AddStringToObject(sig_obj, "signature", sig_b64);
    cJSON_AddStringToObject(sig_obj, "signed_at", time_str);
    cJSON_AddBoolToObject(sig_obj, "is_official", true);  /* Developer 设备签名 */
    cJSON_AddItemToObject(root, "signature", sig_obj);
    
    /* payload (Base64 ciphertext) */
    size_t payload_b64_max = ((ciphertext_len + 2) / 3) * 4 + 1;
    char *payload_b64 = TS_MALLOC_PSRAM(payload_b64_max);
    if (!payload_b64) {
        free(cert_pem);
        free(ciphertext);
        cJSON_Delete(root);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    b64_len = payload_b64_max;
    ts_crypto_base64_encode(ciphertext, ciphertext_len, payload_b64, &b64_len);
    cJSON_AddStringToObject(root, "payload", payload_b64);
    
    free(ciphertext);
    free(payload_b64);
    
    /* 输出 JSON */
    char *json_output = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(cert_pem);  /* cert_pem 内容已复制到 JSON，可以释放 */
    
    if (!json_output) {
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    *output = json_output;
    *output_len = strlen(json_output);
    
    ESP_LOGI(TAG, "Created config pack '%s' (%zu bytes)", name, *output_len);
    return TS_CONFIG_PACK_OK;
}

esp_err_t ts_config_pack_save(const char *path, const char *tscfg_json, size_t tscfg_len)
{
    if (!path || !tscfg_json) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ts_storage_write_file(path, tscfg_json, tscfg_len);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Saved config pack to %s", path);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Verify Functions                                  */
/*===========================================================================*/

ts_config_pack_result_t ts_config_pack_verify(const char *path, ts_config_pack_sig_info_t *info)
{
    if (info) memset(info, 0, sizeof(*info));
    if (!path) return TS_CONFIG_PACK_ERR_INVALID_ARG;
    char *bytes = NULL; size_t length = 0;
    ts_config_pack_result_t result = read_config_bytes(path, &bytes, &length);
    if (result == TS_CONFIG_PACK_OK) result = ts_config_pack_verify_mem(bytes, length, info);
    free(bytes);
    return result;
}

ts_config_pack_result_t ts_config_pack_verify_mem(const char *bytes, size_t length,
                                                  ts_config_pack_sig_info_t *info)
{
    if (info) memset(info, 0, sizeof(*info));
    char index[16]; bool reload = false;
    ts_config_pack_acceptance_t accepted = {0};
    ts_config_pack_result_t result = lookup_acceptance(bytes, length, &accepted, index, &reload);
    if (result != TS_CONFIG_PACK_OK) return result;
    /* Inspection may verify an outbound package for another recipient. It
     * never grants offline acceptance; import/load also authenticate GCM. */
    return verify_pack_mem(bytes, length, false, false, reload ? &accepted : NULL, NULL, NULL, NULL, info);
}

/*===========================================================================*/
/*                          Import Functions                                  */
/*===========================================================================*/

/** 配置包存储目录（与 .json 配置文件同目录，便于优先级处理） */
#define CONFIG_PACK_DIR "/sdcard/config"

/**
 * @brief 确保目录存在
 */
/* Unauthenticated display time is never used for admission or expiry. */
static int64_t display_created_at(const cJSON *metadata)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(metadata, "created_at");
    int y, m, d, h, minute, second, end = 0;
    if (!cJSON_IsString(value) || sscanf(value->valuestring, "%4d-%2d-%2dT%2d:%2d:%2dZ%n",
        &y, &m, &d, &h, &minute, &second, &end) != 6 || value->valuestring[end] ||
        y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31 ||
        h < 0 || h > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return 0;
    y -= m <= 2;
    int era = y / 400, year = y - era * 400;
    int day = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t days = era * 146097 + year * 365 + year / 4 - year / 100 + day - 719468;
    return days * 86400 + h * 3600 + minute * 60 + second;
}

static void pack_metadata(const ts_config_pack_t *pack, ts_config_pack_metadata_t *metadata)
{
    if (!metadata) return;
    memset(metadata, 0, sizeof(*metadata));
    snprintf(metadata->name, sizeof(metadata->name), "%s", pack->name ? pack->name : "unnamed");
    snprintf(metadata->description, sizeof(metadata->description), "%s", pack->description ? pack->description : "");
    snprintf(metadata->source_file, sizeof(metadata->source_file), "%s", pack->source_file ? pack->source_file : "");
    snprintf(metadata->target_device, sizeof(metadata->target_device), "%s", pack->target_device ? pack->target_device : "");
    metadata->created_at = pack->created_at;
    metadata->sig_info = pack->sig_info;
}

ts_config_pack_result_t ts_config_pack_import(const char *bytes, size_t length,
    ts_config_pack_metadata_t *metadata, char *saved_path, size_t saved_path_len)
{
    if (saved_path && saved_path_len) saved_path[0] = 0;
    if (metadata) memset(metadata, 0, sizeof(*metadata));
    ts_config_pack_t *pack = NULL;
    ts_config_pack_result_t result = ts_config_pack_load_mem(bytes, length, &pack);
    if (result != TS_CONFIG_PACK_OK) return result;
    /* v1 display metadata cannot escape the fixed import directory. */
    const char *name = pack->name ? pack->name : "unnamed";
    size_t count = strlen(name);
    bool safe = count > 0 && count < 64;
    for (size_t i = 0; safe && i < count; ++i)
        safe = (unsigned char)name[i] >= 0x20 && (unsigned char)name[i] != 0x7f &&
               name[i] != '/' && name[i] != '\\';
    safe = safe && strcmp(name, ".") && strcmp(name, "..");
    char path[128];
    if (!safe) result = TS_CONFIG_PACK_ERR_INVALID_ARG;
    else if (ts_storage_mkdir_p(CONFIG_PACK_DIR) != ESP_OK) result = TS_CONFIG_PACK_ERR_IO;
    else {
        snprintf(path, sizeof(path), "%s/%s.tscfg", CONFIG_PACK_DIR, name);
        if (ts_storage_replace_file(path, bytes, length) != ESP_OK) result = TS_CONFIG_PACK_ERR_IO;
        else {
            pack_metadata(pack, metadata);
            if (saved_path && saved_path_len) snprintf(saved_path, saved_path_len, "%s", path);
        }
    }
    ts_config_pack_free(pack);
    return result;
}

ts_config_pack_result_t ts_config_pack_validate_file(const char *path,
                                                      ts_config_pack_metadata_t *metadata)
{
    if (metadata) memset(metadata, 0, sizeof(*metadata));
    ts_config_pack_t *pack = NULL;
    ts_config_pack_result_t result = ts_config_pack_load(path, &pack);
    if (result == TS_CONFIG_PACK_OK) pack_metadata(pack, metadata);
    ts_config_pack_free(pack);
    return result;
}

ts_config_pack_result_t ts_config_pack_apply_file(
    const char *path,
    cJSON **applied_modules)
{
    if (applied_modules) *applied_modules = NULL;
    if (!path) return TS_CONFIG_PACK_ERR_INVALID_ARG;
    if (!s_initialized) return TS_CONFIG_PACK_ERR_NOT_INIT;
    /* The former path only enumerated names. Application belongs to each
     * configuration owner; this legacy API has no module apply implementation. */
    return TS_CONFIG_PACK_ERR_UNSUPPORTED;
}

esp_err_t ts_config_pack_list(
    char (*names)[64],
    size_t max_count,
    size_t *count)
{
    if (!names || !count || max_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *count = 0;
    
    DIR *dir = opendir(CONFIG_PACK_DIR);
    if (!dir) {
        /* 目录不存在是正常的（没有导入过配置包） */
        return ESP_OK;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && *count < max_count) {
        /* 跳过 . 和 .. */
        if (entry->d_name[0] == '.') continue;
        
        /* 检查 .tscfg 扩展名 */
        size_t name_len = strlen(entry->d_name);
        if (name_len > 6 && strcmp(entry->d_name + name_len - 6, ".tscfg") == 0) {
            /* 复制名字（去掉扩展名） */
            size_t copy_len = name_len - 6;
            if (copy_len >= 64) copy_len = 63;
            strncpy(names[*count], entry->d_name, copy_len);
            names[*count][copy_len] = '\0';
            (*count)++;
        }
    }
    
    closedir(dir);
    return ESP_OK;
}

ts_config_pack_result_t ts_config_pack_get_content(
    const char *name,
    char **content,
    size_t *content_len)
{
    if (!name || !content || !content_len) {
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    /* 构建文件路径 */
    char path[128];
    snprintf(path, sizeof(path), "%s/%s.tscfg", CONFIG_PACK_DIR, name);
    
    /* 使用现有的加载函数（会解密） */
    ts_config_pack_t *pack = NULL;
    ts_config_pack_result_t result = ts_config_pack_load(path, &pack);
    
    if (result != TS_CONFIG_PACK_OK) {
        return result;
    }
    
    /* 移交内容所有权 */
    *content = pack->content;
    *content_len = pack->content_len;
    pack->content = NULL;  /* 防止 free 释放 */
    
    ts_config_pack_free(pack);
    return TS_CONFIG_PACK_OK;
}

/*===========================================================================*/
/*                          Utility Functions                                 */
/*===========================================================================*/

esp_err_t ts_config_pack_export_device_cert(char *cert_pem, size_t *cert_len)
{
    return ts_cert_get_certificate(cert_pem, cert_len);
}

esp_err_t ts_config_pack_get_cert_fingerprint(char *fingerprint, size_t len)
{
    if (!fingerprint || len < CERT_FINGERPRINT_LEN + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char *cert_pem = TS_MALLOC_PSRAM(TS_CERT_PEM_MAX_LEN);
    if (!cert_pem) return ESP_ERR_NO_MEM;
    size_t cert_len = TS_CERT_PEM_MAX_LEN;
    esp_err_t ret = ts_cert_get_certificate(cert_pem, &cert_len);
    if (ret == ESP_OK) {
        ret = ts_config_pack_fingerprint_certificate(cert_pem, cert_len, fingerprint, len);
    } else {
        ESP_LOGE(TAG, "Failed to read device certificate for pack identity: 0x%x", (unsigned)ret);
    }
    free(cert_pem);
    return ret;
}

/*===========================================================================*/
/*                          Internal Functions                                */
/*===========================================================================*/

/**
 * @brief 计算证书 SHA-256 指纹
 * 
 * 注意：只计算实际 PEM 内容的哈希，去除尾部空白和 null 终止符，
 * 确保相同证书无论复制方式如何都能得到一致的指纹。
 */
esp_err_t ts_config_pack_fingerprint_certificate(const char *cert_pem, size_t cert_len,
                                           char *fingerprint, size_t fp_len)
{
    if (!cert_pem || !fingerprint || fp_len < CERT_FINGERPRINT_LEN + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 计算实际内容长度，去除尾部空白和 null */
    size_t actual_len = strnlen(cert_pem, cert_len);
    while (actual_len > 0 && 
           (cert_pem[actual_len - 1] == ' ' || 
            cert_pem[actual_len - 1] == '\t' ||
            cert_pem[actual_len - 1] == '\r' ||
            cert_pem[actual_len - 1] == '\n')) {
        actual_len--;
    }
    
    if (actual_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 计算 PEM 内容的 SHA-256（不包含尾部空白） */
    uint8_t hash[SHA256_LEN];
    esp_err_t ret = ts_crypto_hash(TS_HASH_SHA256, cert_pem, actual_len, hash, sizeof(hash));
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* 转换为 hex 字符串 */
    return ts_crypto_hex_encode(hash, sizeof(hash), fingerprint, fp_len);
}

/**
 * @brief 解析 .tscfg JSON 文件
 */
static ts_config_pack_result_t parse_tscfg_json(
    const char *json, size_t len,
    cJSON **root,
    ts_config_pack_crypto_params_t *params,
    char **payload_b64,
    char **signer_cert_pem,
    char **signature_b64,
    ts_config_pack_sig_info_t *sig_info)
{
    *root = ts_config_pack_parse_json(json, len);
    if (!cJSON_IsObject(*root)) {
        ESP_LOGE(TAG, "JSON parse error");
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    /* 验证版本 */
    cJSON *version = cJSON_GetObjectItemCaseSensitive(*root, "tscfg_version");
    if (!version || !cJSON_IsString(version)) {
        ESP_LOGE(TAG, "Missing tscfg_version");
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    if (strcmp(cJSON_GetStringValue(version), TS_CONFIG_PACK_VERSION) != 0) {
        ESP_LOGE(TAG, "Unsupported version: %s", cJSON_GetStringValue(version));
        return TS_CONFIG_PACK_ERR_VERSION;
    }
    
    /* 提取加密参数 */
    cJSON *encryption = cJSON_GetObjectItemCaseSensitive(*root, "encryption");
    if (!cJSON_IsObject(encryption)) {
        ESP_LOGE(TAG, "Missing encryption section");
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    cJSON *ephemeral_pub = cJSON_GetObjectItemCaseSensitive(encryption, "ephemeral_public_key");
    cJSON *salt = cJSON_GetObjectItemCaseSensitive(encryption, "salt");
    cJSON *iv = cJSON_GetObjectItemCaseSensitive(encryption, "iv");
    cJSON *tag = cJSON_GetObjectItemCaseSensitive(encryption, "tag");
    cJSON *recipient_fp = cJSON_GetObjectItemCaseSensitive(encryption, "recipient_cert_fingerprint");
    cJSON *algorithm = cJSON_GetObjectItemCaseSensitive(encryption, "algorithm");
    cJSON *kdf = cJSON_GetObjectItemCaseSensitive(encryption, "kdf");

    if (!cJSON_IsString(ephemeral_pub) || !cJSON_IsString(salt) || !cJSON_IsString(iv) ||
        !cJSON_IsString(tag) || !cJSON_IsString(recipient_fp) ||
        !cJSON_IsString(algorithm) || strcmp(algorithm->valuestring, TS_CONFIG_PACK_ALGORITHM) ||
        !cJSON_IsString(kdf) || strcmp(kdf->valuestring, TS_CONFIG_PACK_KDF) ||
        strlen(recipient_fp->valuestring) != CERT_FINGERPRINT_LEN) {
        ESP_LOGE(TAG, "Missing encryption parameters");
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    /* Base64 解码加密参数 */
    const char *pub_str = cJSON_GetStringValue(ephemeral_pub);
    size_t out_len = sizeof(params->ephemeral_pubkey);
    if (ts_crypto_base64_decode(pub_str, strlen(pub_str), 
                                 params->ephemeral_pubkey, &out_len) != ESP_OK ||
        out_len != sizeof(params->ephemeral_pubkey) || params->ephemeral_pubkey[0] != 4) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    out_len = sizeof(params->salt);
    if (ts_crypto_base64_decode(cJSON_GetStringValue(salt), strlen(cJSON_GetStringValue(salt)),
                                 params->salt, &out_len) != ESP_OK || out_len != sizeof(params->salt)) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    out_len = sizeof(params->iv);
    if (ts_crypto_base64_decode(cJSON_GetStringValue(iv), strlen(cJSON_GetStringValue(iv)),
                                 params->iv, &out_len) != ESP_OK || out_len != sizeof(params->iv)) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    out_len = sizeof(params->tag);
    if (ts_crypto_base64_decode(cJSON_GetStringValue(tag), strlen(cJSON_GetStringValue(tag)),
                                 params->tag, &out_len) != ESP_OK || out_len != sizeof(params->tag)) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    strncpy(params->recipient_fingerprint, cJSON_GetStringValue(recipient_fp),
            sizeof(params->recipient_fingerprint) - 1);
    
    /* 提取 payload */
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(*root, "payload");
    if (!cJSON_IsString(payload) || !payload->valuestring[0]) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    *payload_b64 = cJSON_GetStringValue(payload);
    
    /* 提取签名信息 */
    cJSON *signature_obj = cJSON_GetObjectItemCaseSensitive(*root, "signature");
    if (!cJSON_IsObject(signature_obj)) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    cJSON *signer_cert = cJSON_GetObjectItemCaseSensitive(signature_obj, "signer_certificate");
    cJSON *sig = cJSON_GetObjectItemCaseSensitive(signature_obj, "signature");
    cJSON *is_official = cJSON_GetObjectItemCaseSensitive(signature_obj, "is_official");
    const cJSON *sig_algorithm = cJSON_GetObjectItemCaseSensitive(signature_obj, "algorithm");
    
    if (!cJSON_IsString(signer_cert) || !signer_cert->valuestring[0] ||
        strlen(signer_cert->valuestring) >= TS_CERT_PEM_MAX_LEN ||
        !cJSON_IsString(sig) || !sig->valuestring[0] || !cJSON_IsString(sig_algorithm) ||
        strcmp(sig_algorithm->valuestring, TS_CONFIG_PACK_SIG_ALGORITHM)) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    *signer_cert_pem = cJSON_GetStringValue(signer_cert);
    *signature_b64 = cJSON_GetStringValue(sig);
    
    /* 填充签名信息 */
    if (sig_info) {
        sig_info->valid = false;  /* 稍后验证 */
        (void)is_official;
        sig_info->is_official = false; /* Unauthenticated metadata is never authorization. */
        sig_info->signed_at = 0;  /* 默认值 */
        
        /* Outer signed_at is unauthenticated display metadata; do not change process timezone. */
        /* 解析签名者证书获取 CN 和 OU */
        ts_cert_info_t cert_info;
        if (ts_cert_parse_certificate(*signer_cert_pem, strlen(*signer_cert_pem) + 1, 
                                       &cert_info) == ESP_OK) {
            strncpy(sig_info->signer_cn, cert_info.subject_cn, sizeof(sig_info->signer_cn) - 1);
            strncpy(sig_info->signer_ou, cert_info.subject_ou, sizeof(sig_info->signer_ou) - 1);
            /* 根据 OU 字段更新 is_official 标记 */
            /* Only the verified rule-pack path may report trusted Developer status. */
        }
    }
    
    return TS_CONFIG_PACK_OK;
}

/**
 * @brief 验证签名
 */
static ts_config_pack_result_t verify_signature(
    const char *signer_cert_pem,
    const uint8_t *data_to_sign,
    size_t data_len,
    const uint8_t *signature,
    size_t sig_len)
{
    /* 导入签名者公钥 */
    ts_keypair_t signer_key = NULL;
    esp_err_t ret = ts_crypto_keypair_import(signer_cert_pem, strlen(signer_cert_pem) + 1,
                                              &signer_key);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse signer certificate");
        return TS_CONFIG_PACK_ERR_CERT_CHAIN;
    }
    
    /* 计算数据哈希 */
    uint8_t hash[SHA256_LEN];
    if (ts_crypto_hash(TS_HASH_SHA256, data_to_sign, data_len, hash, sizeof(hash)) != ESP_OK) {
        ts_crypto_keypair_free(signer_key);
        return TS_CONFIG_PACK_ERR_SIGNATURE;
    }
    
    /* 验证签名 */
    ret = ts_crypto_ecdsa_verify(signer_key, hash, sizeof(hash), signature, sig_len);
    ts_crypto_keypair_free(signer_key);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Signature verification failed");
        return TS_CONFIG_PACK_ERR_SIGNATURE;
    }
    
    /* 
     * TODO: 验证证书链
     * 签名本身已验证成功，但还应该验证签名者证书是否由受信任的 CA 签发
     */
#if CONFIG_TS_CONFIG_PACK_VERIFY_CERT_CHAIN
    /* 
     * 应该验证 signer_cert_pem 是由受信任的 CA 签发的
     * 这需要加载 CA 链并进行验证
     */
    ESP_LOGD(TAG, "Certificate chain verification not yet implemented (signature OK)");
#endif
    
    return TS_CONFIG_PACK_OK;
}

/**
 * @brief 解密 payload
 */
static ts_config_pack_result_t decrypt_payload_key(
    const ts_config_pack_crypto_params_t *params,
    const char *key_pem,
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    char **plaintext,
    size_t *plaintext_len)
{
    ts_keypair_t device_key = NULL;
    esp_err_t ret = ts_crypto_keypair_import(key_pem, strlen(key_pem) + 1, &device_key);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to import device key");
        return TS_CONFIG_PACK_ERR_RECIPIENT;
    }
    
    /* ECDH 密钥协商 */
    uint8_t shared_secret[32];
    size_t shared_len = sizeof(shared_secret);
    ret = ts_crypto_ecdh_compute_shared_raw(device_key,
                                             params->ephemeral_pubkey,
                                             ECDH_PUBKEY_LEN,
                                             shared_secret, &shared_len);
    ts_crypto_keypair_free(device_key);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ECDH key agreement failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* HKDF 密钥派生 */
    uint8_t aes_key[AES_KEY_LEN];
    ret = ts_crypto_hkdf(params->salt, HKDF_SALT_LEN,
                          shared_secret, shared_len,
                          TS_CONFIG_PACK_HKDF_INFO, strlen(TS_CONFIG_PACK_HKDF_INFO),
                          aes_key, sizeof(aes_key));
    
    memset(shared_secret, 0, sizeof(shared_secret));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HKDF key derivation failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* AES-GCM 解密 */
    char *decrypted = TS_MALLOC_PSRAM(ciphertext_len + 1);
    if (!decrypted) {
        memset(aes_key, 0, sizeof(aes_key));
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    ret = ts_crypto_aes_gcm_decrypt(aes_key, sizeof(aes_key),
                                     params->iv, AES_IV_LEN,
                                     NULL, 0,  /* 无 AAD */
                                     ciphertext, ciphertext_len,
                                     params->tag, decrypted);
    
    memset(aes_key, 0, sizeof(aes_key));
    
    if (ret != ESP_OK) {
        free(decrypted);
        ESP_LOGE(TAG, "AES-GCM decryption failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    decrypted[ciphertext_len] = '\0';
    *plaintext = decrypted;
    *plaintext_len = ciphertext_len;
    
    return TS_CONFIG_PACK_OK;
}

static ts_config_pack_result_t verify_pack_mem(const char *text, size_t length, bool require_code_signing, bool existing_file,
    const ts_config_pack_acceptance_t *accepted, ts_config_pack_t **pack,
    ts_config_pack_acceptance_t *acceptance, uint32_t *credential_generation, ts_config_pack_sig_info_t *signature_info)
{
    if (pack) *pack = NULL;
    uint32_t time_generation = ts_time_sync_security_generation();
    cJSON *root = NULL;
    ts_config_pack_crypto_params_t params = {0};
    char *payload = NULL, *signer = NULL, *signature_text = NULL;
    ts_config_pack_sig_info_t info = {0};
    ts_cert_snapshot_t snapshot = {0};
    unsigned char *ciphertext = NULL;
    char *plaintext = NULL;
    ts_config_pack_acceptance_t local = {0};
    ts_config_pack_result_t ret = parse_tscfg_json(text, length, &root, &params,
                                                 &payload, &signer, &signature_text, &info);
    if (ret != TS_CONFIG_PACK_OK) goto done;
    esp_err_t snapshot_result = ts_cert_get_pack_snapshot(&snapshot);
    if (snapshot_result != ESP_OK) {
        ret = snapshot_result == ESP_ERR_NO_MEM ? TS_CONFIG_PACK_ERR_NO_MEM : TS_CONFIG_PACK_ERR_CERT_CHAIN; goto done;
    }
    char recipient[65], hash[65];
    unsigned char digest[32];
    if (ts_config_pack_fingerprint_certificate(snapshot.certificate, strlen(snapshot.certificate) + 1,
                                 recipient, sizeof recipient) != ESP_OK ||
        ts_crypto_hash(TS_HASH_SHA256, text, length, digest, sizeof digest) != ESP_OK ||
        ts_crypto_hex_encode(digest, sizeof digest, hash, sizeof hash) != ESP_OK) {
        ret = TS_CONFIG_PACK_ERR_INVALID_ARG; goto done;
    }
    if (pack && strcmp(recipient, params.recipient_fingerprint)) { ret = TS_CONFIG_PACK_ERR_RECIPIENT; goto done; }
    ret = ts_config_pack_signer_trust(signer, &snapshot, hash, params.recipient_fingerprint, require_code_signing, existing_file, accepted, &local);
    if (ret != TS_CONFIG_PACK_OK) goto done;
    size_t cipher_len = strlen(payload) * 3 / 4 + 4;
    ciphertext = TS_MALLOC_PSRAM(cipher_len);
    if (!ciphertext) { ret = TS_CONFIG_PACK_ERR_NO_MEM; goto done; }
    unsigned char signature[MAX_SIGNATURE_LEN]; size_t sig_len = sizeof signature;
    if (ts_crypto_base64_decode(payload, strlen(payload), ciphertext, &cipher_len) != ESP_OK || !cipher_len ||
        ts_crypto_base64_decode(signature_text, strlen(signature_text), signature, &sig_len) != ESP_OK || !sig_len) {
        ret = TS_CONFIG_PACK_ERR_PARSE; goto done;
    }
    ret = verify_signature(signer, ciphertext, cipher_len, signature, sig_len);
    if (ret != TS_CONFIG_PACK_OK) goto done;
    size_t plain_len = 0;
    if (pack) {
        ret = decrypt_payload_key(&params, snapshot.key, ciphertext, cipher_len, &plaintext, &plain_len);
        if (ret != TS_CONFIG_PACK_OK) goto done;
        cJSON *content = ts_config_pack_parse_json(plaintext, plain_len);
        if (!content) { ret = TS_CONFIG_PACK_ERR_PARSE; goto done; }
        cJSON_Delete(content);
    }
    ts_cert_pki_status_t status = {0};
    if (ts_cert_get_status(&status) != ESP_OK || status.generation != snapshot.generation ||
        (!accepted && time_generation != ts_time_sync_security_generation())) {
        ret = TS_CONFIG_PACK_ERR_CREDENTIAL_CHANGED; goto done;
    }
    if (!pack) {
        info.valid = info.is_official = true;
        if (signature_info) *signature_info = info;
        goto done;
    }
    ts_config_pack_t *loaded = TS_CALLOC_PSRAM(1, sizeof(*loaded));
    if (!loaded) { ret = TS_CONFIG_PACK_ERR_NO_MEM; goto done; }
    loaded->content = plaintext; plaintext = NULL; loaded->content_len = plain_len;
    cJSON *metadata = cJSON_GetObjectItemCaseSensitive(root, "metadata");
    const char *fields[] = {"name", "description", "source_file", "target_device"};
    char **outputs[] = {&loaded->name, &loaded->description, &loaded->source_file, &loaded->target_device};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(metadata, fields[i]);
        if (cJSON_IsString(value)) {
            *outputs[i] = strdup(value->valuestring);
            if (!*outputs[i]) { ts_config_pack_free(loaded); ret = TS_CONFIG_PACK_ERR_NO_MEM; goto done; }
        }
    }
    loaded->acceptance = local;
    loaded->created_at = display_created_at(metadata);
    info.valid = info.is_official = true; loaded->sig_info = info;
    if (acceptance) *acceptance = local;
    if (credential_generation) *credential_generation = snapshot.generation;
    *pack = loaded;
done:
    cJSON_Delete(root); free(ciphertext); free(plaintext); ts_cert_free_snapshot(&snapshot);
    return ret;
}

ts_config_pack_result_t ts_config_pack_load_verified_mem(const char *text, size_t length,
    const ts_config_pack_acceptance_t *accepted, ts_config_pack_t **pack,
    ts_config_pack_acceptance_t *acceptance, uint32_t *credential_generation)
{
    if (!pack) return TS_CONFIG_PACK_ERR_INVALID_ARG;
    return verify_pack_mem(text, length, true, false, accepted, pack, acceptance, credential_generation, NULL);
}

// ============================================================================
// 通用配置加载辅助函数 (Generic Config Loading Helpers)
// ============================================================================

/**
 * @brief 检查 .tscfg 版本是否存在
 * 
 * 给定 .json 文件路径，检查是否存在对应的 .tscfg 加密版本
 */
bool ts_config_pack_tscfg_exists(const char *json_path)
{
    if (!json_path) {
        return false;
    }
    
    // 检查是否以 .json 结尾
    size_t len = strlen(json_path);
    if (len < 5 || strcmp(json_path + len - 5, ".json") != 0) {
        return false;
    }
    
    // 构建 .tscfg 路径
    char tscfg_path[256];
    size_t base_len = len - 5;  // 去掉 ".json"
    if (base_len + 6 >= sizeof(tscfg_path)) {  // +6 for ".tscfg"
        return false;
    }
    
    memcpy(tscfg_path, json_path, base_len);
    strcpy(tscfg_path + base_len, ".tscfg");
    
    // 检查文件是否存在
    struct stat st;
    return (stat(tscfg_path, &st) == 0);
}

/**
 * @brief 智能加载配置文件（.tscfg 优先）
 * 
 * 给定 .json 文件路径，优先加载对应的 .tscfg 加密版本（如存在）
 * 否则回退到加载普通 .json 文件
 * 
 * @param json_path      原始 .json 文件路径
 * @param content        输出：文件内容（调用者负责释放）
 * @param content_len    输出：内容长度（可选，可传 NULL）
 * @param used_tscfg     输出：是否使用了 .tscfg 版本（可选，可传 NULL）
 * @return ESP_OK 成功，其他表示错误
 */
esp_err_t ts_config_pack_load_with_priority(
    const char *json_path,
    char **content,
    size_t *content_len,
    bool *used_tscfg)
{
    if (!json_path || !content) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *content = NULL;
    if (content_len) *content_len = 0;
    if (used_tscfg) *used_tscfg = false;
    
    // 检查是否以 .json 结尾
    size_t len = strlen(json_path);
    if (len < 5 || strcmp(json_path + len - 5, ".json") != 0) {
        ESP_LOGE(TAG, "Invalid path (not .json): %s", json_path);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 构建 .tscfg 路径
    char tscfg_path[256];
    size_t base_len = len - 5;
    if (base_len + 6 >= sizeof(tscfg_path)) {
        ESP_LOGE(TAG, "Path too long: %s", json_path);
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(tscfg_path, json_path, base_len);
    strcpy(tscfg_path + base_len, ".tscfg");
    
    // 检查 .tscfg 是否存在
    struct stat st;
    if (stat(tscfg_path, &st) == 0) {
        ts_config_pack_t *pack = NULL;
        ts_config_pack_result_t result = ts_config_pack_load(tscfg_path, &pack);
        if (result != TS_CONFIG_PACK_OK) {
            ts_config_pack_free(pack);
            return result == TS_CONFIG_PACK_ERR_NO_MEM ? ESP_ERR_NO_MEM : ESP_FAIL;
        }
        *content = pack->content;
        if (content_len) *content_len = pack->content_len;
        if (used_tscfg) *used_tscfg = true;
        pack->content = NULL;
        ts_config_pack_free(pack);
        return ESP_OK;
    }
    if (errno != ENOENT) return ESP_FAIL;

    if (stat(json_path, &st) != 0)
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    size_t length = 0;
    ts_config_pack_result_t result = read_config_bytes(json_path, content, &length);
    if (result != TS_CONFIG_PACK_OK)
        return result == TS_CONFIG_PACK_ERR_NO_MEM ? ESP_ERR_NO_MEM : ESP_FAIL;
    if (content_len) *content_len = length;
    return ESP_OK;
}
