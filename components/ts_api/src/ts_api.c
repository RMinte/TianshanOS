/**
 * @file ts_api.c
 * @brief TianShanOS Core API Layer Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_api.h"
#include "ts_config_pack.h"
#include "ts_security.h"
#include "ts_storage.h"
#include "ts_core.h"  /* TS_STRDUP_PSRAM, TS_CALLOC_PSRAM */
#include "ts_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ts_api"
_Static_assert(TS_API_ACCESS_READ - TS_API_ACCESS_PUBLIC == TS_PERM_READ &&
               TS_API_ACCESS_WRITE - TS_API_ACCESS_PUBLIC == TS_PERM_WRITE &&
               TS_API_ACCESS_ADMIN - TS_API_ACCESS_PUBLIC == TS_PERM_ADMIN &&
               TS_API_ACCESS_ROOT - TS_API_ACCESS_PUBLIC == TS_PERM_ROOT, "API/security role mapping changed");

/*===========================================================================*/
/*                          Name Tables                                       */
/*===========================================================================*/

static const char *s_code_names[] = {
    [TS_API_OK]              = "OK",
    [TS_API_ERR_INVALID_ARG] = "INVALID_ARG",
    [TS_API_ERR_NOT_FOUND]   = "NOT_FOUND",
    [TS_API_ERR_NO_PERMISSION] = "NO_PERMISSION",
    [TS_API_ERR_BUSY]        = "BUSY",
    [TS_API_ERR_TIMEOUT]     = "TIMEOUT",
    [TS_API_ERR_NO_MEM]      = "NO_MEM",
    [TS_API_ERR_INTERNAL]    = "INTERNAL",
    [TS_API_ERR_NOT_SUPPORTED] = "NOT_SUPPORTED",
    [TS_API_ERR_HARDWARE]    = "HARDWARE",
    [TS_API_ERR_CONNECTION]  = "CONNECTION",
    [TS_API_ERR_AUTH]        = "AUTH"
};

static const char *s_category_names[] = {
    [TS_API_CAT_SYSTEM]  = "system",
    [TS_API_CAT_CONFIG]  = "config",
    [TS_API_CAT_HAL]     = "hal",
    [TS_API_CAT_LED]     = "led",
    [TS_API_CAT_FAN]     = "fan",
    [TS_API_CAT_POWER]   = "power",
    [TS_API_CAT_NETWORK] = "network",
    [TS_API_CAT_DEVICE]  = "device",
    [TS_API_CAT_STORAGE] = "storage",
    [TS_API_CAT_SECURITY] = "security"
};

/*===========================================================================*/
/*                          Endpoint Registry                                 */
/*===========================================================================*/

#ifndef CONFIG_TS_API_MAX_ENDPOINTS
#define CONFIG_TS_API_MAX_ENDPOINTS 128
#endif

#ifndef CONFIG_TS_API_MAX_NAME_LENGTH
#define CONFIG_TS_API_MAX_NAME_LENGTH 64
#endif

typedef struct {
    char name[CONFIG_TS_API_MAX_NAME_LENGTH];
    char *description;
    ts_api_category_t category;
    ts_api_handler_t handler;
    ts_api_session_handler_t session_handler;
    bool requires_auth;
    char *permission;
    ts_api_external_access_t external_access;
    uint8_t allowed_methods;
    bool used;
} api_entry_t;

/*===========================================================================*/
/*                          Private Data                                      */
/*===========================================================================*/

static struct {
    bool initialized;
    SemaphoreHandle_t mutex;
    api_entry_t *endpoints;
    size_t endpoint_count;
} s_api = {0};

/*===========================================================================*/
/*                          Private Functions                                 */
/*===========================================================================*/

static api_entry_t *find_endpoint(const char *name)
{
    for (size_t i = 0; i < CONFIG_TS_API_MAX_ENDPOINTS; i++) {
        if (s_api.endpoints[i].used && 
            strcmp(s_api.endpoints[i].name, name) == 0) {
            return &s_api.endpoints[i];
        }
    }
    return NULL;
}

static api_entry_t *find_free_slot(void)
{
    for (size_t i = 0; i < CONFIG_TS_API_MAX_ENDPOINTS; i++) {
        if (!s_api.endpoints[i].used) {
            return &s_api.endpoints[i];
        }
    }
    return NULL;
}

/*===========================================================================*/
/*                          Core API Implementation                           */
/*===========================================================================*/

esp_err_t ts_api_init(void)
{
    if (s_api.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Create mutex */
    s_api.mutex = xSemaphoreCreateMutex();
    if (s_api.mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    /* Allocate endpoint array in PSRAM */
    s_api.endpoints = heap_caps_calloc(CONFIG_TS_API_MAX_ENDPOINTS, sizeof(api_entry_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_api.endpoints == NULL) {
        TS_LOGW(TAG, "PSRAM not available, using DRAM for API endpoints");
        s_api.endpoints = calloc(CONFIG_TS_API_MAX_ENDPOINTS, sizeof(api_entry_t));
        if (s_api.endpoints == NULL) {
            vSemaphoreDelete(s_api.mutex);
            return ESP_ERR_NO_MEM;
        }
    } else {
        TS_LOGI(TAG, "API endpoints allocated in PSRAM (%zu bytes)",
                CONFIG_TS_API_MAX_ENDPOINTS * sizeof(api_entry_t));
    }
    
    s_api.endpoint_count = 0;
    s_api.initialized = true;
    
    TS_LOGI(TAG, "API layer initialized (max %d endpoints)", CONFIG_TS_API_MAX_ENDPOINTS);
    
    return ESP_OK;
}

esp_err_t ts_api_deinit(void)
{
    if (!s_api.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Free descriptions and permissions */
    for (size_t i = 0; i < CONFIG_TS_API_MAX_ENDPOINTS; i++) {
        if (s_api.endpoints[i].used) {
            free(s_api.endpoints[i].description);
            free(s_api.endpoints[i].permission);
        }
    }
    
    free(s_api.endpoints);
    vSemaphoreDelete(s_api.mutex);
    
    s_api.endpoints = NULL;
    s_api.mutex = NULL;
    s_api.endpoint_count = 0;
    s_api.initialized = false;
    
    TS_LOGI(TAG, "API layer deinitialized");
    
    return ESP_OK;
}

esp_err_t ts_api_register(const ts_api_endpoint_t *endpoint)
{
    if (!s_api.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (endpoint == NULL || endpoint->name == NULL || endpoint->handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (strlen(endpoint->name) >= CONFIG_TS_API_MAX_NAME_LENGTH) {
        TS_LOGE(TAG, "API name too long: %s", endpoint->name);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (endpoint->external_access > TS_API_ACCESS_TOKEN ||
        (endpoint->allowed_methods & ~TS_API_METHOD_ALL) ||
        (endpoint->external_access == TS_API_ACCESS_INTERNAL ? endpoint->allowed_methods != 0 : !endpoint->allowed_methods))
        return ESP_ERR_INVALID_ARG;
    /* Reject a contradictory management declaration at registration; never
     * rewrite an admitted operation's role from a name or permission string. */
    if (endpoint->external_access != TS_API_ACCESS_INTERNAL && endpoint->permission &&
        !strcmp(endpoint->permission, "cert.manage") &&
        ((endpoint->external_access != TS_API_ACCESS_ADMIN &&
          endpoint->external_access != TS_API_ACCESS_ROOT) ||
         (endpoint->allowed_methods & ~TS_API_METHOD_WRITE)))
        return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_api.mutex, portMAX_DELAY);
    
    /* Check for duplicate */
    if (find_endpoint(endpoint->name) != NULL) {
        xSemaphoreGive(s_api.mutex);
        TS_LOGE(TAG, "API already registered: %s", endpoint->name);
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Find free slot */
    api_entry_t *entry = find_free_slot();
    if (entry == NULL) {
        xSemaphoreGive(s_api.mutex);
        TS_LOGE(TAG, "No free API slots");
        return ESP_ERR_NO_MEM;
    }
    
    /* Fill entry */
    strncpy(entry->name, endpoint->name, CONFIG_TS_API_MAX_NAME_LENGTH - 1);
    entry->name[CONFIG_TS_API_MAX_NAME_LENGTH - 1] = '\0';
    
    entry->description = endpoint->description ? TS_STRDUP_PSRAM(endpoint->description) : NULL;
    entry->category = endpoint->category;
    entry->handler = endpoint->handler;
    entry->session_handler = endpoint->session_handler;
    entry->requires_auth = endpoint->requires_auth;
    entry->external_access = endpoint->external_access;
    entry->allowed_methods = endpoint->allowed_methods;
    entry->permission = endpoint->permission ? TS_STRDUP_PSRAM(endpoint->permission) : NULL;
    if ((endpoint->description && !entry->description) ||
        (endpoint->permission && !entry->permission)) {
        free(entry->description);
        free(entry->permission);
        memset(entry, 0, sizeof(*entry));
        xSemaphoreGive(s_api.mutex);
        return ESP_ERR_NO_MEM;
    }
    entry->used = true;
    
    s_api.endpoint_count++;
    
    xSemaphoreGive(s_api.mutex);
    
    TS_LOGD(TAG, "Registered API: %s", endpoint->name);
    
    return ESP_OK;
}

esp_err_t ts_api_register_multiple(const ts_api_endpoint_t *endpoints, size_t count)
{
    if (endpoints == NULL || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (size_t i = 0; i < count; i++) {
        esp_err_t ret = ts_api_register(&endpoints[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return ESP_OK;
}

esp_err_t ts_api_unregister(const char *name)
{
    if (!s_api.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_api.mutex, portMAX_DELAY);
    
    api_entry_t *entry = find_endpoint(name);
    if (entry == NULL) {
        xSemaphoreGive(s_api.mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    free(entry->description);
    free(entry->permission);
    memset(entry, 0, sizeof(api_entry_t));
    s_api.endpoint_count--;
    
    xSemaphoreGive(s_api.mutex);
    
    TS_LOGD(TAG, "Unregistered API: %s", name);
    
    return ESP_OK;
}

void ts_api_parse_query(cJSON *params, char *query)
{
    // Parse query string parameters
    char *saveptr = NULL;
    char *pair = strtok_r(query, "&", &saveptr);
    while (pair) {
        char *eq = strchr(pair, '=');
        if (eq) {
            *eq = '\0';
            char *key = pair;
            char *value = eq + 1;

            // URL decode the value
            char *src = value;
            char *dst = value;
            while (*src) {
                if (*src == '%' && src[1] && src[2]) {
                    int high = src[1];
                    int low = src[2];
                    // Convert hex
                    if (high >= '0' && high <= '9') high -= '0';
                    else if (high >= 'A' && high <= 'F') high = high - 'A' + 10;
                    else if (high >= 'a' && high <= 'f') high = high - 'a' + 10;
                    else { *dst++ = *src++; continue; }

                    if (low >= '0' && low <= '9') low -= '0';
                    else if (low >= 'A' && low <= 'F') low = low - 'A' + 10;
                    else if (low >= 'a' && low <= 'f') low = low - 'a' + 10;
                    else { *dst++ = *src++; continue; }

                    *dst++ = (char)((high << 4) | low);
                    src += 3;
                } else if (*src == '+') {
                    *dst++ = ' ';
                    src++;
                } else {
                    *dst++ = *src++;
                }
            }
            *dst = '\0';

            // Try to parse as number first
            char *endptr;
            long num = strtol(value, &endptr, 10);
            if (*endptr == '\0' && value[0] != '\0') {
                // It's a number
                cJSON_AddNumberToObject(params, key, num);
            } else {
                // It's a string
                cJSON_AddStringToObject(params, key, value);
            }
        }
        pair = strtok_r(NULL, "&", &saveptr);
    }
}


static esp_err_t api_call_dispatch(const char *name, const cJSON *params, uint32_t caller_session_id,
                                   bool external, ts_api_result_t *result)
{
    if (!s_api.initialized) {
        if (result) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "API not initialized");
        }
        return ESP_ERR_INVALID_STATE;
    }
    
    if (name == NULL || result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_api_result_init(result);
    
    xSemaphoreTake(s_api.mutex, portMAX_DELAY);
    
    api_entry_t *entry = find_endpoint(name);
    if (entry == NULL) {
        xSemaphoreGive(s_api.mutex);
        TS_LOGW(TAG, "API not found: %s (total registered: %d)", name, (int)s_api.endpoint_count);
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "API not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Copy handler to call outside mutex */
    ts_api_handler_t handler = entry->handler;
    ts_api_session_handler_t session_handler = entry->session_handler;
    
    xSemaphoreGive(s_api.mutex);
    
    /* Call handler */
    TS_LOGD(TAG, "Calling API: %s", name);
    esp_err_t ret = external && session_handler ? session_handler(params, caller_session_id, result) : handler(params, result);
    
    if (ret != ESP_OK && result->code == TS_API_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Handler error");
    }
    
    return ret;
}

esp_err_t ts_api_call(const char *name, const cJSON *params, ts_api_result_t *result)
{
    return api_call_dispatch(name, params, 0, false, result);
}

static void classify_external_access(ts_api_external_access_t mode, uint8_t methods,
                                     ts_api_access_t *access)
{
    access->external_access = mode;
    access->allowed_methods = mode == TS_API_ACCESS_INTERNAL ? 0 : methods;
    access->requires_auth = mode >= TS_API_ACCESS_READ && mode <= TS_API_ACCESS_ROOT;
    access->minimum_level = access->requires_auth ? (uint8_t)(mode - TS_API_ACCESS_PUBLIC) : TS_PERM_NONE;
}

esp_err_t ts_api_get_access(const char *name, ts_api_access_t *access)
{
    if (!name || !access) return ESP_ERR_INVALID_ARG;
    if (!s_api.initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_api.mutex, portMAX_DELAY);
    api_entry_t *entry = find_endpoint(name);
    if (entry) classify_external_access(entry->external_access, entry->allowed_methods, access);
    esp_err_t ret = entry ? ESP_OK : ESP_ERR_NOT_FOUND;
    xSemaphoreGive(s_api.mutex);
    return ret;
}

/* Authorization and dispatch share one registry snapshot. */
static esp_err_t copy_external_call(const char *name, ts_api_access_t *access,
                                   ts_api_handler_t *handler,
                                   ts_api_session_handler_t *session_handler)
{
    if (!s_api.initialized) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_api.mutex, portMAX_DELAY);
    api_entry_t *entry = find_endpoint(name);
    if (entry) {
        classify_external_access(entry->external_access, entry->allowed_methods, access);
        *handler = entry->handler;
        *session_handler = entry->session_handler;
    }
    esp_err_t ret = entry ? ESP_OK : ESP_ERR_NOT_FOUND;
    xSemaphoreGive(s_api.mutex);
    return ret;
}

/* External private results have an explicit metadata contract. New saved
 * fields are private until a field is deliberately added to this projection. */
static bool project_result_fields(cJSON *object, const char *fields, const cJSON *preserved)
{
    if (!cJSON_IsObject(object)) return false;
    cJSON *item = object->child;
    while (item) {
        cJSON *next = item->next;
        if (item == preserved) { item = next; continue; }
        bool allowed = false;
        for (const char *p = fields; *p;) {
            const char *end = strchr(p, '|');
            size_t length = end ? (size_t)(end - p) : strlen(p);
            if (item->string && strlen(item->string) == length && !memcmp(item->string, p, length))
                allowed = true;
            if (!end) break;
            p = end + 1;
        }
        if (allowed && !strcmp(item->string, "automatic_start")) {
            allowed = project_result_fields(item, "state|reason|retry_after_ms|", NULL);
        } else if (allowed && !strcmp(item->string, "ssh_commands")) {
            allowed = cJSON_IsArray(item);
            for (cJSON *command = item->child; command;) {
                cJSON *following = command->next;
                if (!cJSON_IsString(command)) cJSON_Delete(cJSON_DetachItemViaPointer(item, command));
                command = following;
            }
        } else allowed = allowed && (cJSON_IsString(item) || cJSON_IsNumber(item) || cJSON_IsBool(item) || cJSON_IsNull(item));
        if (!allowed) cJSON_Delete(cJSON_DetachItemViaPointer(object, item));
        item = next;
    }
    return true;
}

/* A NULL result probes the same policy, including effect/acknowledgement semantics. */
static esp_err_t private_result_projection(const char *name, ts_api_result_t *result, bool root, bool *effect_result)
{
    static const struct { const char *name, *collection, *fields; bool effect; } views[] = {
#define VIEW_FIELDS "id|name|label|type|enabled|active_version|"
        {"ssh.hosts.list", "hosts", VIEW_FIELDS "host|port|created|created_time|last_used|", false},
        {"ssh.hosts.get", NULL, VIEW_FIELDS "host|port|created|created_time|last_used|", false},
        {"ssh.commands.list", "commands", VIEW_FIELDS "host_id|icon|nohup|orphan|runtime_instance|lastExec|created|created_time|serviceMode|service_mode|varName|var_name|", false},
        {"ssh.commands.get", NULL, VIEW_FIELDS "host_id|icon|nohup|orphan|runtime_instance|lastExec|created|created_time|serviceMode|service_mode|varName|var_name|", false},
        {"automation.sources.list", "sources", VIEW_FIELDS "connected|last_update_ms|poll_interval_ms|reconnect_ms|mapping_count|update_mode|invalid|", false},
        {"automation.rules.list", "rules", VIEW_FIELDS "revision|runtime_active|manual_trigger|conditions_count|actions_count|trigger_count|last_trigger_ms|automatic_start|ssh_commands|", false},
        {"automation.rules.get", NULL, VIEW_FIELDS "revision|runtime_active|manual_trigger|conditions_count|actions_count|trigger_count|last_trigger_ms|", false},
        {"automation.actions.list", "templates", VIEW_FIELDS "async|use_count|last_used_at|created_at|", false},
        {"automation.actions.get", NULL, VIEW_FIELDS "async|use_count|last_used_at|created_at|", false},
        /* These two handlers already project their allowed public fields. */
        {"config.get", NULL, NULL, false},
        {"config.module.show", NULL, NULL, false},
        {"ota.server.get", NULL, "configured|", false},
        {"ota.progress", NULL, NULL, false},
        {"ota.www.progress", NULL, NULL, false},
        {"automation.actions.execute", NULL, "async|status|duration_ms|", true},
#undef VIEW_FIELDS
    };
    for (unsigned i = 0; i < sizeof(views) / sizeof(views[0]); ++i) {
        if (strcmp(name, views[i].name)) continue;
        if (effect_result) *effect_result = views[i].effect;
        if (!result || !result->data || root || !views[i].fields) return ESP_OK;
        if (views[i].effect) {
            /* The effect already happened. Redaction must not allocate, fail,
             * or turn its real acknowledgement into a retryable write error. */
            if (!project_result_fields(result->data, views[i].fields, NULL)) {
                cJSON_Delete(result->data);
                result->data = NULL;
            }
            return ESP_OK;
        }
        cJSON *rows = views[i].collection ? cJSON_GetObjectItemCaseSensitive(result->data, views[i].collection) : NULL;
        bool valid = views[i].collection ? cJSON_IsArray(rows) : cJSON_IsObject(result->data);
        if (views[i].collection && valid) {
            valid = project_result_fields(result->data,
                "count|total|active_version|load_error|paused|loaded|recovery_required|invalid_items|restart_required|", rows);
            cJSON *row;
            cJSON_ArrayForEach(row, rows) {
                if (!project_result_fields(row, views[i].fields, NULL) || !cJSON_AddBoolToObject(row, "restricted", true)) {
                    valid = false;
                    break;
                }
            }
        } else if (valid) valid = project_result_fields(result->data, views[i].fields, NULL);
        if (!valid || !cJSON_AddBoolToObject(result->data, "restricted", true)) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Configuration metadata unavailable");
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ts_api_call_for_session(const char *name, const cJSON *params,
                                  uint32_t session_id, ts_api_result_t *result)
{
    if (!name || !result) return ESP_ERR_INVALID_ARG;
    ts_api_result_init(result);
    ts_api_access_t access;
    ts_api_handler_t handler = NULL;
    ts_api_session_handler_t session_handler = NULL;
    esp_err_t policy_ret = copy_external_call(name, &access, &handler, &session_handler);
    if (policy_ret != ESP_OK) {
        ts_api_result_error(result, policy_ret == ESP_ERR_NOT_FOUND ? TS_API_ERR_NOT_FOUND : TS_API_ERR_INTERNAL,
                            policy_ret == ESP_ERR_NOT_FOUND ? "API not found" : "API policy unavailable");
        return policy_ret;
    }
    if (access.external_access == TS_API_ACCESS_INTERNAL) {
        ts_api_result_error(result, TS_API_ERR_NOT_SUPPORTED, "API not exposed externally");
        return ESP_ERR_NOT_SUPPORTED;
    }
    bool source_secrets = true;
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
    ts_session_t session;
    ts_perm_level_t level = TS_PERM_NONE;
    if (session_id && ts_security_validate_session(session_id, &session) == ESP_OK)
        level = session.level;
    /* Preserve the pre-existing sources credential projection. Other restored
     * configuration/security pages retain their original Admin data contract. */
    ts_perm_level_t private_level = !strcmp(name, "automation.sources.list") ? TS_PERM_ROOT : TS_PERM_ADMIN;
    source_secrets = level >= private_level;
    /* The generic store can contain arbitrary credentials. Only the language
     * preference is a public WebUI bootstrap value; raw reads require a logged-in administrator. */
    const cJSON *config_key = !strcmp(name, "config.get") ? cJSON_GetObjectItem(params, "key") : NULL;
    bool private_config = !strcmp(name, "config.get") &&
        !(cJSON_IsString(config_key) && !strcmp(config_key->valuestring, "system.language"));
    if ((access.requires_auth && level < access.minimum_level) ||
        (private_config && !source_secrets)) {
        ts_api_result_error(result, TS_API_ERR_AUTH, "Unauthorized");
        return ESP_ERR_NOT_ALLOWED;
    }
#else
    (void)access;
    (void)session_id;
#endif
    /* Invoke the handler copied with this policy, never a second name lookup
     * that could select a concurrently replaced operation. */
    esp_err_t ret = session_handler ? session_handler(params, session_id, result) : handler(params, result);
    if (ret != ESP_OK && result->code == TS_API_OK)
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Handler error");
    if (!strcmp(name, "config.module.show") && !source_secrets && result->data) {
        /* Both single-module and "all" responses are emitted by this API.
         * Omit secrets instead of returning a mask that a client might save. */
        cJSON *configs[] = {
            cJSON_GetObjectItemCaseSensitive(result->data, "config"),
            cJSON_GetObjectItemCaseSensitive(result->data, "wifi")
        };
        for (unsigned i = 0; i < sizeof(configs) / sizeof(configs[0]); ++i) {
            cJSON_DeleteItemFromObjectCaseSensitive(configs[i], "ap.password");
            cJSON_DeleteItemFromObjectCaseSensitive(configs[i], "sta.password");
        }
    }
    bool effect_result = false;
    if (private_result_projection(name, NULL, true, &effect_result) == ESP_OK) {
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
        ts_session_t current;
        bool live = ts_security_validate_session(session_id, &current) == ESP_OK;
        if (effect_result) {
            /* Keep the actual write outcome, but never its private output if
             * the initiating administrator is no longer authorized at delivery. */
            source_secrets = source_secrets && live && current.level >= private_level;
        } else if ((access.requires_auth && (!live || current.level < access.minimum_level)) ||
                   (source_secrets && (!live || current.level < private_level))) {
            ts_api_result_error(result, TS_API_ERR_AUTH, "Unauthorized");
            return ESP_ERR_NOT_ALLOWED;
        }
#endif
        esp_err_t projected = private_result_projection(name, result, source_secrets, NULL);
        if (projected != ESP_OK) return projected;
    }
    return ret;
}

esp_err_t ts_api_call_str(const char *name, const char *params_json, ts_api_result_t *result)
{
    cJSON *params = NULL;
    
    if (params_json && strlen(params_json) > 0) {
        params = name&&!strcmp(name,"automation.rules.import") ?
            ts_config_pack_parse_import_request(params_json,strlen(params_json)) : cJSON_Parse(params_json);
        if (params == NULL) {
            if (result) {
                ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid JSON");
            }
            return ESP_ERR_INVALID_ARG;
        }
    }
    
    esp_err_t ret = ts_api_call(name, params, result);
    
    if (params) {
        cJSON_Delete(params);
    }
    
    return ret;
}

cJSON *ts_api_list(ts_api_category_t category)
{
    if (!s_api.initialized) {
        return NULL;
    }
    
    cJSON *arr = cJSON_CreateArray();
    if (arr == NULL) {
        return NULL;
    }
    
    xSemaphoreTake(s_api.mutex, portMAX_DELAY);
    
    for (size_t i = 0; i < CONFIG_TS_API_MAX_ENDPOINTS; i++) {
        if (s_api.endpoints[i].used) {
            if (category >= TS_API_CAT_MAX || 
                s_api.endpoints[i].category == category) {
                cJSON_AddItemToArray(arr, 
                    cJSON_CreateString(s_api.endpoints[i].name));
            }
        }
    }
    
    xSemaphoreGive(s_api.mutex);
    
    return arr;
}

cJSON *ts_api_get_info(const char *name)
{
    if (!s_api.initialized || name == NULL) {
        return NULL;
    }
    
    xSemaphoreTake(s_api.mutex, portMAX_DELAY);
    
    api_entry_t *entry = find_endpoint(name);
    if (entry == NULL) {
        xSemaphoreGive(s_api.mutex);
        return NULL;
    }
    
    cJSON *info = cJSON_CreateObject();
    if (info) {
        cJSON_AddStringToObject(info, "name", entry->name);
        if (entry->description) {
            cJSON_AddStringToObject(info, "description", entry->description);
        }
        cJSON_AddStringToObject(info, "category", 
            ts_api_category_name(entry->category));
        ts_api_access_t access;
        classify_external_access(entry->external_access, entry->allowed_methods, &access);
        /* Partial metadata must never weaken the dispatch policy. */
        if (!cJSON_AddBoolToObject(info, "requires_auth", access.requires_auth) ||
            !cJSON_AddNumberToObject(info, "minimum_level", access.minimum_level) ||
            !cJSON_AddNumberToObject(info, "external_access", access.external_access) ||
            !cJSON_AddNumberToObject(info, "allowed_methods", access.allowed_methods) ||
            (entry->permission && !cJSON_AddStringToObject(info, "permission", entry->permission))) {
            cJSON_Delete(info);
            info = NULL;
        }
    }
    
    xSemaphoreGive(s_api.mutex);
    
    return info;
}

/*===========================================================================*/
/*                          Result Helpers                                    */
/*===========================================================================*/

void ts_api_result_init(ts_api_result_t *result)
{
    if (result) {
        result->code = TS_API_OK;
        result->message = NULL;
        result->data = NULL;
    }
}

void ts_api_result_free(ts_api_result_t *result)
{
    if (result) {
        free(result->message);
        if (result->data) {
            cJSON_Delete(result->data);
        }
        result->message = NULL;
        result->data = NULL;
    }
}

void ts_api_result_ok(ts_api_result_t *result, cJSON *data)
{
    if (result) {
        result->code = TS_API_OK;
        free(result->message);
        result->message = NULL;
        if (result->data) {
            cJSON_Delete(result->data);
        }
        result->data = data;
    }
}

void ts_api_result_error(ts_api_result_t *result, ts_api_result_code_t code,
                          const char *message)
{
    if (result) {
        result->code = code;
        free(result->message);
        result->message = message ? TS_STRDUP_PSRAM(message) : NULL;
        if (result->data) {
            cJSON_Delete(result->data);
            result->data = NULL;
        }
    }
}

void ts_api_result_reason(ts_api_result_t *result, ts_api_result_code_t code,
                         const char *token, const char *detail)
{
    ts_api_result_error(result, code, token);
    if (result && detail && detail[0]) {
        cJSON *data = cJSON_CreateObject();
        if (data && cJSON_AddStringToObject(data, "detail", detail)) {
            result->data = data;
        } else {
            cJSON_Delete(data);
        }
    }
}

cJSON *ts_api_result_to_json(const ts_api_result_t *result)
{
    if (result == NULL) {
        return NULL;
    }
    
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }
    
    cJSON_AddBoolToObject(json, "success", result->code == TS_API_OK);
    cJSON_AddStringToObject(json, "code", ts_api_code_name(result->code));
    
    if (result->message) {
        cJSON_AddStringToObject(json, "message", result->message);
    }
    
    if (result->data) {
        cJSON_AddItemToObject(json, "data", cJSON_Duplicate(result->data, 1));
    }
    
    return json;
}

char *ts_api_result_to_string(const ts_api_result_t *result)
{
    cJSON *json = ts_api_result_to_json(result);
    if (json == NULL) {
        return NULL;
    }
    
    char *str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    return str;
}

const char *ts_api_code_name(ts_api_result_code_t code)
{
    if (code >= TS_API_ERR_MAX) {
        return "UNKNOWN";
    }
    return s_code_names[code];
}

/*===========================================================================*/
/*                          Category Info                                     */
/*===========================================================================*/

const char *ts_api_category_name(ts_api_category_t category)
{
    if (category >= TS_API_CAT_MAX) {
        return "unknown";
    }
    return s_category_names[category];
}

ts_api_category_t ts_api_category_by_name(const char *name)
{
    if (name == NULL) {
        return TS_API_CAT_MAX;
    }
    
    for (int i = 0; i < TS_API_CAT_MAX; i++) {
        if (strcmp(s_category_names[i], name) == 0) {
            return (ts_api_category_t)i;
        }
    }
    
    return TS_API_CAT_MAX;
}

/*===========================================================================*/
/*                      Register All API Modules                              */
/*===========================================================================*/

esp_err_t ts_api_register_all(void)
{
    esp_err_t ret;
    
    TS_LOGI(TAG, "Registering all API modules...");
    
    /* System APIs */
    ret = ts_api_system_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register system APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Config APIs */
    ret = ts_api_config_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register config APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Device APIs */
    ret = ts_api_device_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register device APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* LED APIs */
    ret = ts_api_led_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register LED APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Network APIs */
    ret = ts_api_network_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register network APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Fan APIs */
    ret = ts_api_fan_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register fan APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Power APIs */
    ret = ts_api_power_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register power APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Temperature APIs */
    ret = ts_api_temp_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register temp APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Service APIs */
    ret = ts_api_service_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register service APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Storage APIs */
    ret = ts_api_storage_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register storage APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* GPIO APIs */
    ret = ts_api_gpio_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register GPIO APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Compute Monitor APIs */
    ret = ts_api_monitor_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register monitor APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* WiFi APIs */
    ret = ts_api_wifi_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register WiFi APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* DHCP Server APIs */
    ret = ts_api_dhcp_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register DHCP APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* NAT Gateway APIs */
    ret = ts_api_nat_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register NAT APIs: %s", esp_err_to_name(ret));
        return ret;
    }

    /* LPMU upper network access APIs */
    ret = ts_api_lpmu_access_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register LPMU access APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* SSH Known Hosts APIs */
    ret = ts_api_hosts_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register Hosts APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Key Management APIs */
    ret = ts_api_key_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register Key APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* SSH APIs */
    ret = ts_api_ssh_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register SSH APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* SFTP APIs */
    ret = ts_api_sftp_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register SFTP APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Time APIs */
    ret = ts_api_time_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register Time APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* OTA APIs */
    ret = ts_api_ota_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register OTA APIs: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Log APIs */
    ts_api_log_register();
    
    /* Automation APIs */
    ret = ts_api_automation_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register Automation APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Certificate (PKI) APIs */
    ret = ts_api_cert_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register Certificate APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Authentication APIs */
    ret = ts_api_auth_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register Auth APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* UI Configuration APIs */
    ret = ts_api_ui_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register UI APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Config Pack APIs */
    ret = ts_api_config_pack_register();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register Config Pack APIs: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Variable APIs 已废弃，统一使用 automation.variables.* API */
    
    TS_LOGI(TAG, "All API modules registered (%zu endpoints)", s_api.endpoint_count);
    
    return ESP_OK;
}
