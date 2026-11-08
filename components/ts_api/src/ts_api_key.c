/**
 * @file ts_api_key.c
 * @brief Key Management API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_api.h"
#include "ts_keystore.h"
#include "ts_security.h"
#include "ts_automation_config.h"
#include "ts_ssh_service.h"
#include "ts_log.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include <string.h>

#define TAG "api_key"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static const char *key_type_desc(ts_keystore_key_type_t type)
{
    switch (type) {
        case TS_KEYSTORE_TYPE_RSA_2048:  return "RSA 2048-bit";
        case TS_KEYSTORE_TYPE_RSA_4096:  return "RSA 4096-bit";
        case TS_KEYSTORE_TYPE_ECDSA_P256: return "ECDSA P-256";
        case TS_KEYSTORE_TYPE_ECDSA_P384: return "ECDSA P-384";
        default: return "Unknown";
    }
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief key.list - List all stored keys
 */
static cJSON *key_info_json(const ts_keystore_key_info_t *info, bool list_row)
{
    cJSON *data = cJSON_CreateObject();
    if (!data || !cJSON_AddStringToObject(data, "id", info->id) ||
        !cJSON_AddStringToObject(data, "type", ts_keystore_type_to_string(info->type)) ||
        !cJSON_AddStringToObject(data, "type_desc", key_type_desc(info->type)) ||
        !cJSON_AddStringToObject(data, "comment", info->comment) ||
        !cJSON_AddStringToObject(data, "alias", info->alias) ||
        !cJSON_AddNumberToObject(data, "created", info->created_at) ||
        !cJSON_AddNumberToObject(data, "last_used", info->last_used) ||
        !cJSON_AddBoolToObject(data, list_row ? "has_pubkey" : "has_public_key", info->has_public_key) ||
        !cJSON_AddBoolToObject(data, "exportable", info->exportable) ||
        !cJSON_AddBoolToObject(data, "hidden", info->hidden)) {
        cJSON_Delete(data);
        return NULL;
    }
    return data;
}

static esp_err_t api_key_list_impl(const cJSON *params, bool show_hidden, ts_api_result_t *result)
{
    (void)params;
    
    TS_LOGI(TAG, "API: key.list called");
    
    /* 使用 PSRAM 分配避免栈溢出 */
    ts_keystore_key_info_t *keys = heap_caps_malloc(
        sizeof(ts_keystore_key_info_t) * TS_KEYSTORE_MAX_KEYS,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!keys) {
        keys = malloc(sizeof(ts_keystore_key_info_t) * TS_KEYSTORE_MAX_KEYS);
    }
    if (!keys) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    size_t count = TS_KEYSTORE_MAX_KEYS;
    
    esp_err_t ret = ts_keystore_list_keys_ex(keys, &count, show_hidden);
    
    TS_LOGI(TAG, "API: ts_keystore_list_keys_ex returned %s, count=%zu", esp_err_to_name(ret), count);
    
    /* Preserve the empty view before the keystore service starts. */
    if (ret == ESP_ERR_INVALID_STATE) { ret = ESP_OK; count = 0; }

    if (ret != ESP_OK) {
        free(keys);
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to list keys");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON *keys_array = data ? cJSON_AddArrayToObject(data, "keys") : NULL;
    bool complete = keys_array && cJSON_AddNumberToObject(data, "count", count) &&
        cJSON_AddNumberToObject(data, "max_keys", TS_KEYSTORE_MAX_KEYS);
    for (size_t i = 0; complete && i < count; ++i) {
        cJSON *item = key_info_json(&keys[i], true);
        if (!item || !cJSON_AddItemToArray(keys_array, item)) {
            cJSON_Delete(item);
            complete = false;
        }
    }
    free(keys);
    if (!complete) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief key.info - Get key info
 * 
 * @param params { "id": "key_id" }
 */
static esp_err_t api_key_info_impl(const cJSON *params, bool show_hidden, ts_api_result_t *result)
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
    
    ts_keystore_key_info_t info;
    esp_err_t ret = ts_keystore_read_public_record(id->valuestring, show_hidden, &info, NULL, NULL);
    
    if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Key not found");
        return ESP_ERR_NOT_FOUND;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to get key info");
        return ret;
    }
    
    cJSON *data = key_info_json(&info, false);
    if (!data) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief key.generate - Generate a new key
 * 
 * @param params { "id": "key_id", "type": "rsa2048", "comment": "...", "exportable": false }
 */
static esp_err_t api_key_generate(const cJSON *params, ts_api_result_t *result)
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
    
    /* Parse key type */
    ts_keystore_key_type_t type = TS_KEYSTORE_TYPE_RSA_2048;  /* Default */
    const cJSON *type_json = cJSON_GetObjectItem(params, "type");
    if (type_json && cJSON_IsString(type_json)) {
        const char *type_str = type_json->valuestring;
        if (strcmp(type_str, "rsa4096") == 0) {
            type = TS_KEYSTORE_TYPE_RSA_4096;
        } else if (strcmp(type_str, "ec256") == 0 || strcmp(type_str, "ecdsa") == 0) {
            type = TS_KEYSTORE_TYPE_ECDSA_P256;
        } else if (strcmp(type_str, "ec384") == 0) {
            type = TS_KEYSTORE_TYPE_ECDSA_P384;
        }
    }
    
    /* Parse comment */
    const char *comment = "";
    const cJSON *comment_json = cJSON_GetObjectItem(params, "comment");
    if (comment_json && cJSON_IsString(comment_json)) {
        comment = comment_json->valuestring;
    }
    
    /* Parse exportable flag */
    bool exportable = false;
    const cJSON *exportable_json = cJSON_GetObjectItem(params, "exportable");
    if (exportable_json && cJSON_IsBool(exportable_json)) {
        exportable = cJSON_IsTrue(exportable_json);
    }
    
    /* Parse alias */
    const char *alias = NULL;
    const cJSON *alias_json = cJSON_GetObjectItem(params, "alias");
    if (alias_json && cJSON_IsString(alias_json)) {
        alias = alias_json->valuestring;
    }
    
    /* Parse hidden flag */
    bool hidden = false;
    const cJSON *hidden_json = cJSON_GetObjectItem(params, "hidden");
    if (hidden_json && cJSON_IsBool(hidden_json)) {
        hidden = cJSON_IsTrue(hidden_json);
    }
    
    /* Generate key using extended API with options */
    ts_keystore_gen_opts_t opts = {
        .exportable = exportable,
        .comment = comment,
        .alias = alias,
        .hidden = hidden,
    };
    
    char request_id[32] = "legacy";
    const cJSON *rid = cJSON_GetObjectItem(params, "request_id");
    if (cJSON_IsString(rid) && strlen(rid->valuestring) < sizeof(request_id)) {
        bool valid = rid->valuestring[0] != '\0';
        for (const char *p = rid->valuestring; *p; ++p) {
            if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                  (*p >= '0' && *p <= '9') || *p == '-')) valid = false;
        }
        if (valid) strcpy(request_id, rid->valuestring);
    }
    ts_keystore_generate_result_t detail;
    esp_err_t ret = ts_keystore_generate_key_with_result(id->valuestring, type, &opts, &detail);
    TS_LOGI(TAG, "key.generate request=%s storage ret=%s stage=%s stored=%d cleanup=%s", request_id,
            esp_err_to_name(ret), detail.failed_stage ? detail.failed_stage : "saved", detail.stored,
            esp_err_to_name(detail.cleanup_error));
    if (ret != ESP_OK) {
        const char *reason = ret == ESP_ERR_NO_MEM ? "Memory allocation failed" :
            ret == ESP_ERR_NVS_NOT_ENOUGH_SPACE ? "key_storage_full" :
            strcmp(detail.failed_stage, "id_occupied") == 0 ? "key_id_occupied" :
            strcmp(detail.failed_stage, "index_capacity") == 0 ? "key_limit_reached" :
            ret == ESP_ERR_INVALID_ARG ? "key_invalid_id" : "key_storage_failed";
        ts_api_result_error(result, ret == ESP_ERR_NO_MEM ? TS_API_ERR_NO_MEM :
                            ret == ESP_ERR_INVALID_ARG ? TS_API_ERR_INVALID_ARG : TS_API_ERR_INTERNAL, reason);
        cJSON *data = cJSON_CreateObject();
        if (data && cJSON_AddStringToObject(data, "id", id->valuestring) &&
            cJSON_AddStringToObject(data, "failed_stage", detail.failed_stage) &&
            cJSON_AddStringToObject(data, "esp_error", esp_err_to_name(ret)) &&
            cJSON_AddBoolToObject(data, "cleanup_complete", detail.cleanup_complete) &&
            cJSON_AddStringToObject(data, "cleanup_error", esp_err_to_name(detail.cleanup_error))) {
            result->data = data;
        } else {
            cJSON_Delete(data);
            TS_LOGE(TAG, "key.generate request=%s error response allocation failed", request_id);
        }
        return ret;
    }

    cJSON *data = cJSON_CreateObject();
    if (!data || !cJSON_AddBoolToObject(data, "generated", true) ||
        !cJSON_AddStringToObject(data, "id", id->valuestring) ||
        !cJSON_AddStringToObject(data, "type", ts_keystore_type_to_string(type))) {
        cJSON_Delete(data);
        TS_LOGE(TAG, "key.generate request=%s saved but response allocation failed", request_id);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "key_response_failed");
        return ESP_ERR_NO_MEM;
    }

    ts_api_result_ok(result, data);
    return ESP_OK;
}

/* A preview reports references only. It never connects to or stops a host. */
static esp_err_t key_delete_preview(const char *id, ts_api_result_t *result)
{
    cJSON *data=cJSON_CreateObject(), *hosts=data?cJSON_AddArrayToObject(data,"hosts"):NULL,
          *runs=data?cJSON_AddArrayToObject(data,"runs"):NULL;
    if(!data||!hosts||!runs){cJSON_Delete(data);return ESP_ERR_NO_MEM;}
    const ts_auto_config_view_t *view=ts_auto_config_acquire();
    const cJSON *definitions=cJSON_GetObjectItemCaseSensitive(ts_auto_config_json(view),"hosts"),*host;
    bool complete=cJSON_IsArray(definitions);
    cJSON_ArrayForEach(host,(cJSON_IsArray(definitions)?definitions:NULL)) {
        const char *key=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(host,"keyid"));
        const char *auth=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(host,"auth_type"));
        if(!key||strcmp(key,id)||(auth&&strcmp(auth,"key")))continue;
        const cJSON *name=cJSON_GetObjectItemCaseSensitive(host,"id");
        if(!cJSON_IsString(name)){complete=false;continue;}
        cJSON *copy=cJSON_Duplicate(name,true);
        if(!copy||!cJSON_AddItemToArray(hosts,copy)){cJSON_Delete(copy);ts_auto_config_release(view);cJSON_Delete(data);return ESP_ERR_NO_MEM;}
    }
    ts_auto_config_release(view);
    for(unsigned i=0;i<TS_SSH_RUN_SLOTS;++i) {
        ts_ssh_run_record_t record;ts_ssh_service_status_t state;
        esp_err_t ret=ts_ssh_service_get(i,&record,&state);
        if(ret==ESP_ERR_NOT_FOUND)continue;
        if(ret!=ESP_OK){complete=false;break;}
        if(record.host.auth_type!=TS_SSH_HOST_AUTH_KEY||strcmp(record.host.keyid,id))continue;
        cJSON *item=cJSON_CreateObject();
        if(!item||!cJSON_AddStringToObject(item,"id",record.run_id)||
           !cJSON_AddStringToObject(item,"name",record.name)||
           !cJSON_AddStringToObject(item,"host",record.host.host)||!cJSON_AddItemToArray(runs,item)) {
            cJSON_Delete(item);cJSON_Delete(data);return ESP_ERR_NO_MEM;
        }
    }
    if(!cJSON_AddBoolToObject(data,"complete",complete)){cJSON_Delete(data);return ESP_ERR_NO_MEM;}
    ts_api_result_ok(result,data);return ESP_OK;
}

/**
 * @brief key.delete - Delete a key
 * 
 * @param params { "id": "key_id" }
 */
static esp_err_t api_key_delete(const cJSON *params, ts_api_result_t *result)
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
    
    if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params,"preview")))
        return key_delete_preview(id->valuestring,result);
    /* Allocate the reply before a destructive write. */
    cJSON *data=cJSON_CreateObject();
    if(!data||!cJSON_AddBoolToObject(data,"deleted",true)||!cJSON_AddStringToObject(data,"id",id->valuestring)) {
        cJSON_Delete(data);return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = ts_keystore_delete_key(id->valuestring);
    if (ret == ESP_ERR_NOT_FOUND) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Key not found");
        return ESP_ERR_NOT_FOUND;
    } else if (ret != ESP_OK) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "key_delete_incomplete");
        return ret;
    }
    
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief key.export - Export public key
 * 
 * @param params { "id": "key_id" }
 * @return Public key in OpenSSH format
 */
static esp_err_t api_key_export_impl(const cJSON *params, bool show_hidden, ts_api_result_t *result)
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
    
    /* 加载公钥 */
    char *pubkey = NULL;
    size_t pubkey_len = 0;
    ts_keystore_key_info_t info;
    esp_err_t ret = ts_keystore_read_public_record(id->valuestring, show_hidden, &info, &pubkey, &pubkey_len);
    
    if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Key not found");
        return ESP_ERR_NOT_FOUND;
    } else if (ret != ESP_OK || !pubkey) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to export public key");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    bool complete = data && cJSON_AddStringToObject(data, "id", info.id) &&
        cJSON_AddStringToObject(data, "type", ts_keystore_type_to_string(info.type)) &&
        cJSON_AddStringToObject(data, "public_key", pubkey) &&
        cJSON_AddStringToObject(data, "comment", info.comment);
    free(pubkey);
    if (!complete) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief key.exportPrivate - Export private key (only for exportable keys)
 * 
 * @param params { "id": "key_id" }
 * @return Private key in PEM format
 */
static esp_err_t api_key_export_private(const cJSON *params, ts_api_result_t *result)
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
    
    ts_keystore_key_info_t info;
    char *privkey = NULL;
    size_t privkey_len = 0;
    esp_err_t ret = ts_keystore_read_exportable_private_record(id->valuestring, &info, &privkey, &privkey_len);
    if (ret == ESP_ERR_NOT_ALLOWED) {
        ts_api_result_error(result, TS_API_ERR_NO_PERMISSION, "Private key is not exportable");
        return ret;
    }
    if (ret != ESP_OK || !privkey) {
        ts_api_result_error(result, ret == ESP_ERR_NOT_FOUND ? TS_API_ERR_NOT_FOUND : TS_API_ERR_INTERNAL,
            ret == ESP_ERR_NOT_FOUND ? "Key not found" : "Failed to export private key");
        return ret == ESP_OK ? ESP_FAIL : ret;
    }

    cJSON *data = cJSON_CreateObject();
    bool complete = data && cJSON_AddStringToObject(data, "id", info.id) &&
        cJSON_AddStringToObject(data, "type", ts_keystore_type_to_string(info.type)) &&
        cJSON_AddStringToObject(data, "private_key", privkey) &&
        cJSON_AddStringToObject(data, "comment", info.comment) &&
        cJSON_AddStringToObject(data, "warning", "Keep this private key secure! Never share it publicly.");
    volatile char *wipe = privkey;
    for (size_t i = 0; i < privkey_len; ++i) wipe[i] = 0;
    free(privkey);
    if (!complete) {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static esp_err_t key_read_visibility(uint32_t session_id, bool *show_hidden,
    ts_api_result_t *result)
{
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
    ts_session_t caller;
    if (ts_security_validate_session(session_id, &caller) != ESP_OK || caller.level < TS_PERM_READ) {
        ts_api_result_error(result, TS_API_ERR_AUTH, "Unauthorized");
        return ESP_ERR_NOT_ALLOWED;
    }
    *show_hidden = caller.level >= TS_PERM_ADMIN;
#else
    (void)session_id; (void)result;
    *show_hidden = true;
#endif
    return ESP_OK;
}

static esp_err_t api_key_list(const cJSON *params, ts_api_result_t *result)
{
    return api_key_list_impl(params, true, result);
}

typedef esp_err_t (*key_read_handler_t)(const cJSON *, bool, ts_api_result_t *);
static esp_err_t key_read_for_session(const cJSON *params, uint32_t session_id,
    bool administrator_only, key_read_handler_t handler, ts_api_result_t *result)
{
    bool show_hidden;
    esp_err_t ret = key_read_visibility(session_id, &show_hidden, result);
    if (ret != ESP_OK) return ret;
    if (administrator_only && !show_hidden) {
        ts_api_result_error(result, TS_API_ERR_NO_PERMISSION, "Administrator permission required");
        return ESP_ERR_NOT_ALLOWED;
    }
    ret = handler(params, show_hidden, result);
    if (ret != ESP_OK || result->code != TS_API_OK) return ret;
    bool still_hidden;
    ret = key_read_visibility(session_id, &still_hidden, result);
    if (ret == ESP_OK && still_hidden != show_hidden) {
        ts_api_result_error(result, TS_API_ERR_AUTH, "Key read permission changed");
        ret = ESP_ERR_NOT_ALLOWED;
    }
    return ret;
}

static esp_err_t private_export_read(const cJSON *params, bool show_hidden, ts_api_result_t *result)
{
    (void)show_hidden;
    return api_key_export_private(params, result);
}

static esp_err_t api_key_info(const cJSON *params, ts_api_result_t *result)
{
    return api_key_info_impl(params, true, result);
}

static esp_err_t api_key_export(const cJSON *params, ts_api_result_t *result)
{
    return api_key_export_impl(params, true, result);
}

static esp_err_t api_key_list_for_session(const cJSON *params, uint32_t session_id, ts_api_result_t *result)
{
    return key_read_for_session(params, session_id, false, api_key_list_impl, result);
}

static esp_err_t api_key_info_for_session(const cJSON *params, uint32_t session_id, ts_api_result_t *result)
{
    return key_read_for_session(params, session_id, false, api_key_info_impl, result);
}

static esp_err_t api_key_export_for_session(const cJSON *params, uint32_t session_id, ts_api_result_t *result)
{
    return key_read_for_session(params, session_id, false, api_key_export_impl, result);
}

static esp_err_t api_key_export_private_for_session(const cJSON *params, uint32_t session_id, ts_api_result_t *result)
{
    return key_read_for_session(params, session_id, true, private_export_read, result);
}

esp_err_t ts_api_key_register(void)
{
    static const ts_api_endpoint_t endpoints[] = {
        {
            .name = "key.list",
            .external_access = TS_API_ACCESS_READ,
            .allowed_methods = TS_API_METHOD_READ,
            .description = "List all stored keys",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_key_list,
            .session_handler = api_key_list_for_session,
            .requires_auth = false,
        },
        {
            .name = "key.info",
            .external_access = TS_API_ACCESS_READ,
            .allowed_methods = TS_API_METHOD_READ,
            .description = "Get key info",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_key_info,
            .session_handler = api_key_info_for_session,
            .requires_auth = false,
        },
        {
            .name = "key.generate",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .description = "Generate a new key",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_key_generate,
            .requires_auth = true,
        },
        {
            .name = "key.delete",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .description = "Delete a key",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_key_delete,
            .requires_auth = true,
        },
        {
            .name = "key.export",
            .external_access = TS_API_ACCESS_READ,
            .allowed_methods = TS_API_METHOD_READ,
            .description = "Export public key",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_key_export,
            .session_handler = api_key_export_for_session,
            .requires_auth = false,
        },
        {
            .name = "key.exportPrivate",
            .external_access = TS_API_ACCESS_ADMIN,
            .allowed_methods = TS_API_METHOD_WRITE,
            .description = "Export private key (only for exportable keys)",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_key_export_private,
            .session_handler = api_key_export_private_for_session,
            .requires_auth = true,  /* 导出私钥需要认证 */
        },
    };
    
    return ts_api_register_multiple(endpoints, sizeof(endpoints) / sizeof(endpoints[0]));
}
