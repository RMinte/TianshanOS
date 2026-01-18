/**
 * @file ts_keystore.c
 * @brief TianShanOS Secure Key Storage Implementation
 * 
 * 使用 ESP32 NVS 加密分区存储 SSH 私钥。
 * 支持 HMAC 方案（推荐）和 Flash 加密方案。
 * 
 * NVS 命名空间：ts_keystore
 * 键值格式：
 *   - {id}_priv: 私钥 PEM 数据
 *   - {id}_pub:  公钥 OpenSSH 格式
 *   - {id}_meta: 元数据 (JSON)
 *   - _index:    已存储的所有 key ID 列表
 */

#include "ts_keystore.h"
#include "ts_crypto.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "ts_keystore";

/* NVS 命名空间 */
#define KEYSTORE_NAMESPACE      "ts_keystore"
#define KEYSTORE_INDEX_KEY      "_index"

/* 最大索引字符串长度 */
#define MAX_INDEX_LEN           (TS_KEYSTORE_MAX_KEYS * (TS_KEYSTORE_ID_MAX_LEN + 1))

/* 模块状态 */
static struct {
    bool initialized;
    nvs_handle_t nvs_handle;
} s_keystore = {
    .initialized = false,
    .nvs_handle = 0,
};

/* ========== 内部辅助函数 ========== */

/**
 * @brief 构造 NVS 键名
 */
static void make_nvs_key(char *buf, size_t buf_len, const char *id, const char *suffix)
{
    snprintf(buf, buf_len, "%s%s", id, suffix);
}

/**
 * @brief 解析索引字符串为 ID 列表
 */
static int parse_index(const char *index_str, char ids[][TS_KEYSTORE_ID_MAX_LEN], size_t max_ids)
{
    if (!index_str || !ids || max_ids == 0) {
        return 0;
    }
    
    int count = 0;
    char *copy = strdup(index_str);
    if (!copy) return 0;
    
    char *token = strtok(copy, ",");
    while (token && count < max_ids) {
        strncpy(ids[count], token, TS_KEYSTORE_ID_MAX_LEN - 1);
        ids[count][TS_KEYSTORE_ID_MAX_LEN - 1] = '\0';
        count++;
        token = strtok(NULL, ",");
    }
    
    free(copy);
    return count;
}

/**
 * @brief 添加 ID 到索引
 */
static esp_err_t add_to_index(const char *id)
{
    char index_str[MAX_INDEX_LEN] = {0};
    size_t len = sizeof(index_str);
    
    /* 读取现有索引 */
    esp_err_t ret = nvs_get_str(s_keystore.nvs_handle, KEYSTORE_INDEX_KEY, index_str, &len);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        return ret;
    }
    
    /* 检查是否已存在 */
    if (strstr(index_str, id) != NULL) {
        return ESP_OK;  /* 已存在 */
    }
    
    /* 追加新 ID */
    if (strlen(index_str) > 0) {
        strncat(index_str, ",", sizeof(index_str) - strlen(index_str) - 1);
    }
    strncat(index_str, id, sizeof(index_str) - strlen(index_str) - 1);
    
    /* 写回 */
    return nvs_set_str(s_keystore.nvs_handle, KEYSTORE_INDEX_KEY, index_str);
}

/**
 * @brief 从索引中移除 ID
 */
static esp_err_t remove_from_index(const char *id)
{
    char index_str[MAX_INDEX_LEN] = {0};
    size_t len = sizeof(index_str);
    
    esp_err_t ret = nvs_get_str(s_keystore.nvs_handle, KEYSTORE_INDEX_KEY, index_str, &len);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* 重建索引，排除指定 ID */
    char ids[TS_KEYSTORE_MAX_KEYS][TS_KEYSTORE_ID_MAX_LEN];
    int count = parse_index(index_str, ids, TS_KEYSTORE_MAX_KEYS);
    
    char new_index[MAX_INDEX_LEN] = {0};
    for (int i = 0; i < count; i++) {
        if (strcmp(ids[i], id) != 0) {
            if (strlen(new_index) > 0) {
                strncat(new_index, ",", sizeof(new_index) - strlen(new_index) - 1);
            }
            strncat(new_index, ids[i], sizeof(new_index) - strlen(new_index) - 1);
        }
    }
    
    return nvs_set_str(s_keystore.nvs_handle, KEYSTORE_INDEX_KEY, new_index);
}

/**
 * @brief 存储元数据
 */
static esp_err_t store_metadata(const char *id, const ts_keystore_key_info_t *info)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return ESP_ERR_NO_MEM;
    
    cJSON_AddStringToObject(json, "type", ts_keystore_type_to_string(info->type));
    cJSON_AddStringToObject(json, "comment", info->comment);
    cJSON_AddNumberToObject(json, "created_at", info->created_at);
    cJSON_AddNumberToObject(json, "last_used", info->last_used);
    cJSON_AddBoolToObject(json, "has_pubkey", info->has_public_key);
    
    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (!str) return ESP_ERR_NO_MEM;
    
    char key[TS_KEYSTORE_ID_MAX_LEN + 8];
    make_nvs_key(key, sizeof(key), id, "_meta");
    
    esp_err_t ret = nvs_set_str(s_keystore.nvs_handle, key, str);
    free(str);
    
    return ret;
}

/**
 * @brief 加载元数据
 */
static esp_err_t load_metadata(const char *id, ts_keystore_key_info_t *info)
{
    char key[TS_KEYSTORE_ID_MAX_LEN + 8];
    make_nvs_key(key, sizeof(key), id, "_meta");
    
    /* 获取长度 */
    size_t len = 0;
    esp_err_t ret = nvs_get_str(s_keystore.nvs_handle, key, NULL, &len);
    if (ret != ESP_OK) return ret;
    
    char *str = malloc(len);
    if (!str) return ESP_ERR_NO_MEM;
    
    ret = nvs_get_str(s_keystore.nvs_handle, key, str, &len);
    if (ret != ESP_OK) {
        free(str);
        return ret;
    }
    
    /* 解析 JSON */
    cJSON *json = cJSON_Parse(str);
    free(str);
    
    if (!json) return ESP_ERR_INVALID_RESPONSE;
    
    memset(info, 0, sizeof(*info));
    strncpy(info->id, id, sizeof(info->id) - 1);
    
    cJSON *item;
    if ((item = cJSON_GetObjectItem(json, "type"))) {
        info->type = ts_keystore_type_from_string(item->valuestring);
    }
    if ((item = cJSON_GetObjectItem(json, "comment"))) {
        strncpy(info->comment, item->valuestring, sizeof(info->comment) - 1);
    }
    if ((item = cJSON_GetObjectItem(json, "created_at"))) {
        info->created_at = (uint32_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(json, "last_used"))) {
        info->last_used = (uint32_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(json, "has_pubkey"))) {
        info->has_public_key = cJSON_IsTrue(item);
    }
    
    cJSON_Delete(json);
    return ESP_OK;
}

/* ========== 公开 API ========== */

esp_err_t ts_keystore_init(void)
{
    if (s_keystore.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing secure keystore...");
    
    /* 打开 NVS 命名空间 */
    esp_err_t ret = nvs_open(KEYSTORE_NAMESPACE, NVS_READWRITE, &s_keystore.nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_keystore.initialized = true;
    ESP_LOGI(TAG, "Keystore initialized");
    
    return ESP_OK;
}

esp_err_t ts_keystore_deinit(void)
{
    if (!s_keystore.initialized) {
        return ESP_OK;
    }
    
    nvs_close(s_keystore.nvs_handle);
    s_keystore.nvs_handle = 0;
    s_keystore.initialized = false;
    
    ESP_LOGI(TAG, "Keystore deinitialized");
    return ESP_OK;
}

bool ts_keystore_is_initialized(void)
{
    return s_keystore.initialized;
}

esp_err_t ts_keystore_store_key(const char *id, 
                                 const ts_keystore_keypair_t *keypair,
                                 ts_keystore_key_type_t type,
                                 const char *comment)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!id || strlen(id) == 0 || strlen(id) >= TS_KEYSTORE_ID_MAX_LEN) {
        ESP_LOGE(TAG, "Invalid key ID");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!keypair || !keypair->private_key || keypair->private_key_len == 0) {
        ESP_LOGE(TAG, "Invalid keypair");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Storing key '%s' (type=%s, privkey_len=%zu)", 
             id, ts_keystore_type_to_string(type), keypair->private_key_len);
    
    char nvs_key[TS_KEYSTORE_ID_MAX_LEN + 8];
    esp_err_t ret;
    
    /* 存储私钥 */
    make_nvs_key(nvs_key, sizeof(nvs_key), id, "_priv");
    ret = nvs_set_blob(s_keystore.nvs_handle, nvs_key, 
                       keypair->private_key, keypair->private_key_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store private key: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* 存储公钥（如果有） */
    bool has_pubkey = false;
    if (keypair->public_key && keypair->public_key_len > 0) {
        make_nvs_key(nvs_key, sizeof(nvs_key), id, "_pub");
        ret = nvs_set_blob(s_keystore.nvs_handle, nvs_key,
                           keypair->public_key, keypair->public_key_len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to store public key: %s", esp_err_to_name(ret));
            /* 继续，公钥是可选的 */
        } else {
            has_pubkey = true;
        }
    }
    
    /* 存储元数据 */
    ts_keystore_key_info_t info = {
        .type = type,
        .created_at = (uint32_t)time(NULL),
        .last_used = 0,
        .has_public_key = has_pubkey,
    };
    strncpy(info.id, id, sizeof(info.id) - 1);
    if (comment) {
        strncpy(info.comment, comment, sizeof(info.comment) - 1);
    }
    
    ret = store_metadata(id, &info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store metadata: %s", esp_err_to_name(ret));
    }
    
    /* 添加到索引 */
    ret = add_to_index(id);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update index: %s", esp_err_to_name(ret));
    }
    
    /* 提交更改 */
    ret = nvs_commit(s_keystore.nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Key '%s' stored successfully", id);
    return ESP_OK;
}

esp_err_t ts_keystore_load_private_key(const char *id,
                                        char **private_key,
                                        size_t *private_key_len)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!id || !private_key || !private_key_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char nvs_key[TS_KEYSTORE_ID_MAX_LEN + 8];
    make_nvs_key(nvs_key, sizeof(nvs_key), id, "_priv");
    
    /* 获取长度 */
    size_t len = 0;
    esp_err_t ret = nvs_get_blob(s_keystore.nvs_handle, nvs_key, NULL, &len);
    if (ret != ESP_OK) {
        return (ret == ESP_ERR_NVS_NOT_FOUND) ? ESP_ERR_NOT_FOUND : ret;
    }
    
    /* 分配内存并读取 */
    char *buf = malloc(len + 1);  /* +1 for null terminator */
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    
    ret = nvs_get_blob(s_keystore.nvs_handle, nvs_key, buf, &len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }
    
    buf[len] = '\0';  /* Null terminate for PEM string */
    *private_key = buf;
    *private_key_len = len;
    
    /* 更新使用时间 */
    ts_keystore_touch_key(id);
    
    ESP_LOGI(TAG, "Loaded private key '%s' (%zu bytes)", id, len);
    return ESP_OK;
}

esp_err_t ts_keystore_load_public_key(const char *id,
                                       char **public_key,
                                       size_t *public_key_len)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!id || !public_key || !public_key_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char nvs_key[TS_KEYSTORE_ID_MAX_LEN + 8];
    make_nvs_key(nvs_key, sizeof(nvs_key), id, "_pub");
    
    /* 获取长度 */
    size_t len = 0;
    esp_err_t ret = nvs_get_blob(s_keystore.nvs_handle, nvs_key, NULL, &len);
    if (ret != ESP_OK) {
        return (ret == ESP_ERR_NVS_NOT_FOUND) ? ESP_ERR_NOT_FOUND : ret;
    }
    
    /* 分配内存并读取 */
    char *buf = malloc(len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    
    ret = nvs_get_blob(s_keystore.nvs_handle, nvs_key, buf, &len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }
    
    buf[len] = '\0';
    *public_key = buf;
    *public_key_len = len;
    
    ESP_LOGI(TAG, "Loaded public key '%s' (%zu bytes)", id, len);
    return ESP_OK;
}

esp_err_t ts_keystore_delete_key(const char *id)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Deleting key '%s'", id);
    
    char nvs_key[TS_KEYSTORE_ID_MAX_LEN + 8];
    
    /* 删除私钥 */
    make_nvs_key(nvs_key, sizeof(nvs_key), id, "_priv");
    nvs_erase_key(s_keystore.nvs_handle, nvs_key);
    
    /* 删除公钥 */
    make_nvs_key(nvs_key, sizeof(nvs_key), id, "_pub");
    nvs_erase_key(s_keystore.nvs_handle, nvs_key);
    
    /* 删除元数据 */
    make_nvs_key(nvs_key, sizeof(nvs_key), id, "_meta");
    nvs_erase_key(s_keystore.nvs_handle, nvs_key);
    
    /* 从索引中移除 */
    remove_from_index(id);
    
    nvs_commit(s_keystore.nvs_handle);
    
    ESP_LOGI(TAG, "Key '%s' deleted", id);
    return ESP_OK;
}

bool ts_keystore_key_exists(const char *id)
{
    if (!s_keystore.initialized || !id) {
        return false;
    }
    
    char nvs_key[TS_KEYSTORE_ID_MAX_LEN + 8];
    make_nvs_key(nvs_key, sizeof(nvs_key), id, "_priv");
    
    size_t len = 0;
    return nvs_get_blob(s_keystore.nvs_handle, nvs_key, NULL, &len) == ESP_OK;
}

esp_err_t ts_keystore_get_key_info(const char *id, ts_keystore_key_info_t *info)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!id || !info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return load_metadata(id, info);
}

esp_err_t ts_keystore_list_keys(ts_keystore_key_info_t *keys, size_t *count)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!keys || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 读取索引 */
    char index_str[MAX_INDEX_LEN] = {0};
    size_t len = sizeof(index_str);
    
    esp_err_t ret = nvs_get_str(s_keystore.nvs_handle, KEYSTORE_INDEX_KEY, index_str, &len);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        return ESP_OK;
    } else if (ret != ESP_OK) {
        return ret;
    }
    
    /* 解析索引 */
    char ids[TS_KEYSTORE_MAX_KEYS][TS_KEYSTORE_ID_MAX_LEN];
    int num_keys = parse_index(index_str, ids, TS_KEYSTORE_MAX_KEYS);
    
    /* 加载每个密钥的元数据 */
    size_t valid_count = 0;
    for (int i = 0; i < num_keys && valid_count < TS_KEYSTORE_MAX_KEYS; i++) {
        if (load_metadata(ids[i], &keys[valid_count]) == ESP_OK) {
            valid_count++;
        }
    }
    
    *count = valid_count;
    return ESP_OK;
}

esp_err_t ts_keystore_touch_key(const char *id)
{
    if (!s_keystore.initialized || !id) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_keystore_key_info_t info;
    esp_err_t ret = load_metadata(id, &info);
    if (ret != ESP_OK) {
        return ret;
    }
    
    info.last_used = (uint32_t)time(NULL);
    return store_metadata(id, &info);
}

esp_err_t ts_keystore_import_from_file(const char *id, 
                                        const char *path,
                                        const char *comment)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!id || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Importing key from %s as '%s'", path, id);
    
    /* 读取私钥文件 */
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open private key file: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    fseek(f, 0, SEEK_END);
    long priv_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (priv_len <= 0 || priv_len > TS_KEYSTORE_PRIVKEY_MAX_LEN) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    
    char *priv_key = malloc(priv_len + 1);
    if (!priv_key) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_len = fread(priv_key, 1, priv_len, f);
    fclose(f);
    priv_key[read_len] = '\0';
    
    /* 尝试读取公钥文件 */
    char pub_path[256];
    snprintf(pub_path, sizeof(pub_path), "%s.pub", path);
    
    char *pub_key = NULL;
    size_t pub_len = 0;
    
    f = fopen(pub_path, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        pub_len = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (pub_len > 0 && pub_len < TS_KEYSTORE_PUBKEY_MAX_LEN) {
            pub_key = malloc(pub_len + 1);
            if (pub_key) {
                size_t read = fread(pub_key, 1, pub_len, f);
                pub_key[read] = '\0';
                pub_len = read;
            }
        }
        fclose(f);
    }
    
    /* 检测密钥类型 */
    ts_keystore_key_type_t type = TS_KEYSTORE_TYPE_UNKNOWN;
    if (strstr(priv_key, "BEGIN RSA PRIVATE KEY")) {
        /* 需要进一步检测位数 */
        if (priv_len > 2500) {
            type = TS_KEYSTORE_TYPE_RSA_4096;
        } else {
            type = TS_KEYSTORE_TYPE_RSA_2048;
        }
    } else if (strstr(priv_key, "BEGIN EC PRIVATE KEY")) {
        if (priv_len > 300) {
            type = TS_KEYSTORE_TYPE_ECDSA_P384;
        } else {
            type = TS_KEYSTORE_TYPE_ECDSA_P256;
        }
    }
    
    /* 存储密钥 */
    ts_keystore_keypair_t keypair = {
        .private_key = priv_key,
        .private_key_len = read_len,
        .public_key = pub_key,
        .public_key_len = pub_len,
    };
    
    esp_err_t ret = ts_keystore_store_key(id, &keypair, type, comment);
    
    free(priv_key);
    if (pub_key) free(pub_key);
    
    return ret;
}

esp_err_t ts_keystore_export_to_file(const char *id, const char *path)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!id || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Exporting key '%s' to %s", id, path);
    
    /* 加载私钥 */
    char *priv_key = NULL;
    size_t priv_len = 0;
    
    esp_err_t ret = ts_keystore_load_private_key(id, &priv_key, &priv_len);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* 写入私钥文件 */
    FILE *f = fopen(path, "w");
    if (!f) {
        free(priv_key);
        return ESP_ERR_NOT_FOUND;
    }
    
    fwrite(priv_key, 1, priv_len, f);
    fclose(f);
    free(priv_key);
    
    /* 尝试导出公钥 */
    char *pub_key = NULL;
    size_t pub_len = 0;
    
    ret = ts_keystore_load_public_key(id, &pub_key, &pub_len);
    if (ret == ESP_OK && pub_key) {
        char pub_path[256];
        snprintf(pub_path, sizeof(pub_path), "%s.pub", path);
        
        f = fopen(pub_path, "w");
        if (f) {
            fwrite(pub_key, 1, pub_len, f);
            fclose(f);
        }
        free(pub_key);
    }
    
    ESP_LOGI(TAG, "Key '%s' exported to %s", id, path);
    return ESP_OK;
}

esp_err_t ts_keystore_generate_key(const char *id,
                                    ts_keystore_key_type_t type,
                                    const char *comment)
{
    if (!s_keystore.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!id || type == TS_KEYSTORE_TYPE_UNKNOWN) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Generating %s key as '%s'", ts_keystore_type_to_string(type), id);
    
    /* 映射 keystore 类型到 crypto 类型 */
    ts_crypto_key_type_t crypto_type;
    switch (type) {
        case TS_KEYSTORE_TYPE_RSA_2048:
            crypto_type = TS_CRYPTO_KEY_RSA_2048;
            break;
        case TS_KEYSTORE_TYPE_RSA_4096:
            crypto_type = TS_CRYPTO_KEY_RSA_4096;
            break;
        case TS_KEYSTORE_TYPE_ECDSA_P256:
            crypto_type = TS_CRYPTO_KEY_EC_P256;
            break;
        case TS_KEYSTORE_TYPE_ECDSA_P384:
            crypto_type = TS_CRYPTO_KEY_EC_P384;
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    /* 生成密钥对 */
    ts_keypair_t keypair = NULL;
    esp_err_t ret = ts_crypto_keypair_generate(crypto_type, &keypair);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate keypair: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* 导出私钥 PEM */
    char *priv_key = malloc(TS_KEYSTORE_PRIVKEY_MAX_LEN);
    if (!priv_key) {
        ts_crypto_keypair_free(keypair);
        return ESP_ERR_NO_MEM;
    }
    size_t priv_len = TS_KEYSTORE_PRIVKEY_MAX_LEN;
    ret = ts_crypto_keypair_export_private(keypair, priv_key, &priv_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to export private key: %s", esp_err_to_name(ret));
        free(priv_key);
        ts_crypto_keypair_free(keypair);
        return ret;
    }
    
    /* 导出公钥 OpenSSH 格式 */
    char *pub_key = malloc(TS_KEYSTORE_PUBKEY_MAX_LEN);
    size_t pub_len = 0;
    if (pub_key) {
        pub_len = TS_KEYSTORE_PUBKEY_MAX_LEN;
        ret = ts_crypto_keypair_export_openssh(keypair, pub_key, &pub_len, comment);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to export public key: %s", esp_err_to_name(ret));
            free(pub_key);
            pub_key = NULL;
            pub_len = 0;
        }
    }
    
    ts_crypto_keypair_free(keypair);
    
    /* 存储密钥 */
    ts_keystore_keypair_t kp = {
        .private_key = priv_key,
        .private_key_len = priv_len,
        .public_key = pub_key,
        .public_key_len = pub_len,
    };
    
    ret = ts_keystore_store_key(id, &kp, type, comment);
    
    free(priv_key);
    if (pub_key) free(pub_key);
    
    return ret;
}

const char *ts_keystore_type_to_string(ts_keystore_key_type_t type)
{
    switch (type) {
        case TS_KEYSTORE_TYPE_RSA_2048:   return "rsa2048";
        case TS_KEYSTORE_TYPE_RSA_4096:   return "rsa4096";
        case TS_KEYSTORE_TYPE_ECDSA_P256: return "ecdsa-p256";
        case TS_KEYSTORE_TYPE_ECDSA_P384: return "ecdsa-p384";
        default:                          return "unknown";
    }
}

ts_keystore_key_type_t ts_keystore_type_from_string(const char *str)
{
    if (!str) return TS_KEYSTORE_TYPE_UNKNOWN;
    
    if (strcmp(str, "rsa2048") == 0 || strcmp(str, "rsa") == 0) {
        return TS_KEYSTORE_TYPE_RSA_2048;
    }
    if (strcmp(str, "rsa4096") == 0) {
        return TS_KEYSTORE_TYPE_RSA_4096;
    }
    if (strcmp(str, "ecdsa-p256") == 0 || strcmp(str, "ecdsa") == 0 || strcmp(str, "ec256") == 0) {
        return TS_KEYSTORE_TYPE_ECDSA_P256;
    }
    if (strcmp(str, "ecdsa-p384") == 0 || strcmp(str, "ec384") == 0) {
        return TS_KEYSTORE_TYPE_ECDSA_P384;
    }
    
    return TS_KEYSTORE_TYPE_UNKNOWN;
}
