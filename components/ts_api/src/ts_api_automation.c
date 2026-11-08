#include "ts_http_fetch.h"
#include "ts_http_url.h"
#include "ts_api_ws_test_transport.h"
#include "ts_api_auto_config.h"
#include "ts_action_codec.h"
#include "ts_rule_codec.h"
#include "ts_ssh_service.h"
#include <math.h>
/**
 * @file ts_api_automation.c
 * @brief TianShanOS Automation Engine API
 *
 * Provides REST API endpoints for automation engine:
 * - automation.status - Get engine status
 * - automation.start/stop/pause/resume - Control engine
 * - automation.variables.list/get/set/delete - Variable management
 * - automation.rules.list/enable/disable/trigger - Rule management
 * - automation.sources.list - Data source listing
 * - automation.sources.export/import - Source config pack export/import
 * - automation.rules.export/import - Rule config pack export/import
 * - automation.actions.export/import - Action template config pack export/import
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_api.h"
#include "ts_security.h"
#include "ts_action_filter.h"
#include "ts_api_service.h"
#include "ts_automation.h"
#include "ts_variable.h"
#include "ts_rule_engine.h"
#include "ts_automation_config.h"
#include "ts_source_manager.h"
#include "ts_temp_source.h"
#include "ts_action_manager.h"
#include "ts_config_pack.h"
#include "ts_api_pack.h"
#include "ts_cert.h"
#include "ts_log.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <time.h>

static const char *TAG = "api_automation";

/*===========================================================================*/
/*                           Helper Functions                                 */
/*===========================================================================*/

static const char *state_to_string(ts_automation_state_t state)
{
    switch (state) {
        case TS_AUTO_STATE_UNINITIALIZED: return "uninitialized";
        case TS_AUTO_STATE_INITIALIZED: return "initialized";
        case TS_AUTO_STATE_RUNNING: return "running";
        case TS_AUTO_STATE_PAUSED: return "paused";
        case TS_AUTO_STATE_ERROR: return "error";
        default: return "unknown";
    }
}

static cJSON *value_to_json(const ts_auto_value_t *val)
{
    switch (val->type) {
        case TS_AUTO_VAL_NULL:
            return cJSON_CreateNull();
        case TS_AUTO_VAL_BOOL:
            return cJSON_CreateBool(val->bool_val);
        case TS_AUTO_VAL_INT:
            return cJSON_CreateNumber(val->int_val);
        case TS_AUTO_VAL_FLOAT:
            return cJSON_CreateNumber(val->float_val);
        case TS_AUTO_VAL_STRING:
            return cJSON_CreateString(val->str_val);
        default:
            return cJSON_CreateNull();
    }
}

/*===========================================================================*/
/*                           Status API                                       */
/*===========================================================================*/

/**
 * @brief automation.status - Get automation engine status
 */
static esp_err_t api_automation_status(const cJSON *params, ts_api_result_t *result)
{
    ts_automation_status_t status = {0};
    esp_err_t ret = ts_automation_get_status(&status);

    if (ret != ESP_OK) {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("Failed to get automation status");
        return ESP_OK;
    }

    result->data = cJSON_CreateObject();
    cJSON_AddStringToObject(result->data, "state", state_to_string(status.state));
    cJSON_AddNumberToObject(result->data, "uptime_ms", status.uptime_ms);
    cJSON_AddNumberToObject(result->data, "sources_count", status.sources_count);
    cJSON_AddNumberToObject(result->data, "sources_active", status.sources_active);
    cJSON_AddNumberToObject(result->data, "rules_count", status.rules_count);
    cJSON_AddNumberToObject(result->data, "rules_active", status.rules_active);
    cJSON_AddNumberToObject(result->data, "variables_count", status.variables_count);
    cJSON_AddNumberToObject(result->data, "actions_executed", status.actions_executed);
    cJSON_AddNumberToObject(result->data, "rule_triggers", status.rule_triggers);
    cJSON_AddStringToObject(result->data, "config_path", status.config_path ? status.config_path : "");
    cJSON_AddStringToObject(result->data, "version", ts_automation_get_version());
    cJSON *starts=cJSON_AddArrayToObject(result->data,"automatic_starts");
    for(int i=0;starts&&i<ts_rule_count();++i){
        ts_auto_rule_t rule;
        if(ts_rule_get_by_index(i,&rule)!=ESP_OK)continue;
        ts_rule_start_status_t start;
        if(rule.enabled&&ts_rule_start_status(rule.id,&start)){
            cJSON *item=cJSON_CreateObject();
            if(item){
                cJSON_AddStringToObject(item,"id",rule.id);
                cJSON_AddStringToObject(item,"state",start.state);
                if(start.reason)cJSON_AddStringToObject(item,"reason",start.reason);
                cJSON_AddNumberToObject(item,"retry_after_ms",start.retry_after_ms);
                cJSON_AddItemToArray(starts,item);
            }
        }
        ts_rule_release(&rule);
    }

    result->code = TS_API_OK;
    return ESP_OK;
}

/*===========================================================================*/
/*                           Control API                                      */
/*===========================================================================*/

/** The remote server answered the proxied request with an error status: the page gets the status as a number and the start
 * of the body (a few lines at most) as detail for the folded details. */
static void proxy_http_error(ts_api_result_t *result, int status, const char *body)
{
    char detail[161] = {0};
    if (body) snprintf(detail, sizeof(detail), "%.160s", body);
    ts_api_result_reason(result, TS_API_ERR_INTERNAL, "proxy_http_error", detail);
    if (!result->data) result->data = cJSON_CreateObject();
    if (result->data) cJSON_AddNumberToObject(result->data, "status", status);
}

static ts_automation_state_t automation_state(void)
{
    ts_automation_status_t status = {0};
    return ts_automation_get_status(&status) == ESP_OK ? status.state : TS_AUTO_STATE_UNINITIALIZED;
}

/* Why the engine refuses a start (or, with `pausing`, a pause) in its current state. A paused engine cannot be started
 * again until it is stopped, and a stopped one cannot be paused. */
static const char *engine_refusal_token(ts_automation_state_t state, bool pausing)
{
    if (state == TS_AUTO_STATE_PAUSED) return "engine_paused";
    if (pausing && state != TS_AUTO_STATE_RUNNING) return "engine_not_running";
    return "engine_unavailable";   /* stopping, still busy with actions, or not initialized */
}

/**
 * @brief automation.start - Start the automation engine
 */
static esp_err_t api_automation_start(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_automation_start();

    if (ret == ESP_OK) {
        result->code = TS_API_OK;
        result->message = strdup("Automation engine started");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_reason(result, TS_API_ERR_BUSY, engine_refusal_token(automation_state(), false), NULL);
    } else if (ret == ESP_ERR_NO_MEM) {
        ts_api_result_reason(result, TS_API_ERR_NO_MEM, "no_memory", NULL);
    } else {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("Failed to start automation engine");
    }

    return ESP_OK;
}

/**
 * @brief automation.stop - Stop the automation engine
 */
static esp_err_t api_automation_stop(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_automation_stop();

    if (ret == ESP_OK) {
        result->code = TS_API_OK;
        result->message = strdup("Automation engine stopped");
    } else if (ret == ESP_ERR_TIMEOUT) {
        ts_api_result_reason(result, TS_API_ERR_BUSY, "engine_stop_timeout", NULL);
    } else {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("Failed to stop automation engine");
    }

    return ESP_OK;
}

/**
 * @brief automation.pause - Pause the automation engine
 */
static esp_err_t api_automation_pause(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_automation_pause();

    if (ret == ESP_OK) {
        result->code = TS_API_OK;
        result->message = strdup("Automation engine paused");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_reason(result, TS_API_ERR_BUSY, engine_refusal_token(automation_state(), true), NULL);
    } else {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("Failed to pause automation engine");
    }

    return ESP_OK;
}

/**
 * @brief automation.resume - Resume the automation engine
 */
static esp_err_t api_automation_resume(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_automation_resume();

    if (ret == ESP_OK) {
        result->code = TS_API_OK;
        result->message = strdup("Automation engine resumed");
    } else {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("Failed to resume automation engine");
    }

    return ESP_OK;
}

/**
 * @brief automation.reload - Reload configuration
 */
/* Allocate the reply before a configuration mutation. Fixed strings are filled
 * in place afterwards; a committed save is never followed by reply allocation. */
static esp_err_t api_config_status(const cJSON *params,ts_api_result_t *result)
{
    (void)params;if(ts_api_auto_reply_prepare(result))ts_api_auto_reply_finish(result,ESP_OK,NULL);return ESP_OK;
}
static esp_err_t api_config_refresh(const cJSON *params,ts_api_result_t *result)
{
    (void)params;if(!ts_api_auto_reply_prepare(result))return ESP_OK;
    ts_auto_config_error_t error={0};esp_err_t ret=ts_auto_config_refresh(&error);
    ts_api_auto_reply_finish(result,ret,&error);return ESP_OK;
}
static esp_err_t api_config_backup(const cJSON *params,ts_api_result_t *result)
{
    if(!ts_api_auto_reply_prepare(result))return ESP_OK;
    ts_auto_config_error_t error={0};esp_err_t ret=ts_auto_config_backup(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"active_version")),&error);
    if(ret==ESP_OK)error.saved=true;
    ts_api_auto_reply_finish(result,ret,&error);return ESP_OK;
}
static bool config_operation(const cJSON *params,ts_auto_config_operation_t *operation)
{
    const char *name=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"operation"));
    if(!name)return false;
    if(!strcmp(name,"use_sd"))*operation=TS_AUTO_USE_SD;
    else if(!strcmp(name,"save_to_sd"))*operation=TS_AUTO_SAVE_TO_SD;
    else if(!strcmp(name,"restore_internal"))*operation=TS_AUTO_RESTORE_INTERNAL;
    else return false;
    return true;
}
static esp_err_t api_config_preview(const cJSON *params,ts_api_result_t *result)
{
    ts_auto_config_operation_t operation;
    if(!config_operation(params,&operation)){ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"invalid_request");return ESP_OK;}
    ts_auto_config_error_t error={0};cJSON *preview=NULL;
    esp_err_t ret=ts_auto_config_preview(operation,&preview,&error);
    if(ret==ESP_OK)ts_api_result_ok(result,preview);
    else {cJSON_Delete(preview);ts_api_result_error(result,TS_API_ERR_INVALID_ARG,error.reason?error.reason:"configuration_unavailable");}
    return ESP_OK;
}
static esp_err_t api_config_apply(const cJSON *params,ts_api_result_t *result)
{
    ts_auto_config_operation_t operation;
    if(!config_operation(params,&operation)){ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"invalid_request");return ESP_OK;}
    if(!ts_api_auto_reply_prepare(result))return ESP_OK;
    ts_auto_config_error_t error={0};esp_err_t ret=ts_auto_config_apply(operation,
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"active_version")),
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"target_version")),&error);
    if(ret==ESP_OK && operation!=TS_AUTO_USE_SD)error.saved=true;
    ts_api_auto_reply_finish(result,ret,&error);return ESP_OK;
}
static esp_err_t api_config_enable(const cJSON *params,ts_api_result_t *result)
{
    const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(params,"enabled");
    const char *version=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"active_version"));
    if(!cJSON_IsBool(enabled)||!version||!version[0]){ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"configuration_version_required");return ESP_OK;}
    if(!ts_api_auto_reply_prepare(result))return ESP_OK;
    ts_auto_config_error_t error={0};esp_err_t ret=ts_auto_config_enable(cJSON_IsTrue(enabled),version,&error);
    if(ret==ESP_OK)error.saved=true;
    ts_api_auto_reply_finish(result,ret,&error);return ESP_OK;
}
static esp_err_t api_automation_reload(const cJSON *params,ts_api_result_t *result)
{
    (void)params;if(!ts_api_auto_reply_prepare(result))return ESP_OK;
    ts_auto_config_error_t error={0};esp_err_t ret=ts_auto_config_reload(&error);
    ts_api_auto_reply_finish(result,ret,&error);return ESP_OK;
}

/*===========================================================================*/
/*                           Variables API                                    */
/*===========================================================================*/

/**
 * @brief automation.variables.list - List all variables
 */
static esp_err_t api_automation_variables_list(const cJSON *params, ts_api_result_t *result)
{
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params, "configured_only")))
        return ts_api_auto_list(TS_AUTO_CONFIG_VARIABLE, params, "variables", result);
    result->data = cJSON_CreateObject();
    cJSON *vars_array = cJSON_AddArrayToObject(result->data, "variables");
    int64_t now_ms = esp_timer_get_time() / 1000;
    const char *prefix = NULL;
    const char *source_id = NULL;
    size_t prefix_len = 0;
    bool include_value = true;
    bool include_meta = true;

    if (params) {
        cJSON *prefix_param = cJSON_GetObjectItem(params, "prefix");
        if (prefix_param && cJSON_IsString(prefix_param) && prefix_param->valuestring) {
            prefix = prefix_param->valuestring;
            prefix_len = strlen(prefix);
        }

        cJSON *source_id_param = cJSON_GetObjectItem(params, "source_id");
        if (source_id_param && cJSON_IsString(source_id_param) && source_id_param->valuestring) {
            source_id = source_id_param->valuestring;
        }

        cJSON *include_value_param = cJSON_GetObjectItem(params, "include_value");
        if (include_value_param && cJSON_IsBool(include_value_param)) {
            include_value = cJSON_IsTrue(include_value_param);
        }

        cJSON *include_meta_param = cJSON_GetObjectItem(params, "include_meta");
        if (include_meta_param && cJSON_IsBool(include_meta_param)) {
            include_meta = cJSON_IsTrue(include_meta_param);
        }
    }

    // 遍历所有变量（使用内部迭代器）
    ts_variable_iterate_ctx_t ctx = {0};
    ts_auto_variable_t var;

    while (ts_variable_iterate(&ctx, &var) == ESP_OK) {
        if (prefix_len > 0 && strncmp(var.name, prefix, prefix_len) != 0) {
            continue;
        }
        if (source_id && strcmp(var.source_id, source_id) != 0) {
            continue;
        }

        cJSON *var_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(var_obj, "name", var.name);
        if (include_value) {
            cJSON_AddItemToObject(var_obj, "value", value_to_json(&var.value));
        }

        // 类型字符串
        const char *type_str = "null";
        switch (var.value.type) {
            case TS_AUTO_VAL_BOOL: type_str = "bool"; break;
            case TS_AUTO_VAL_INT: type_str = "int"; break;
            case TS_AUTO_VAL_FLOAT: type_str = "float"; break;
            case TS_AUTO_VAL_STRING: type_str = "string"; break;
            default: break;
        }
        cJSON_AddStringToObject(var_obj, "type", type_str);
        cJSON_AddBoolToObject(var_obj, "persistent", (var.flags & TS_AUTO_VAR_PERSISTENT) != 0);
        cJSON_AddBoolToObject(var_obj, "readonly", (var.flags & TS_AUTO_VAR_READONLY) != 0);
        if (include_meta) {
            cJSON_AddNumberToObject(var_obj, "last_change_ms", (double)var.last_change_ms);
            cJSON_AddNumberToObject(var_obj, "last_update_ms", (double)var.last_update_ms);
            if (var.last_update_ms > 0) {
                int64_t age_ms = now_ms - var.last_update_ms;
                bool stale = (age_ms < 0 || age_ms > TS_TEMP_DATA_TIMEOUT_MS);
                if (age_ms < 0) age_ms = 0;
                cJSON_AddNumberToObject(var_obj, "age_ms", (double)age_ms);
                cJSON_AddBoolToObject(var_obj, "stale", stale);
            } else {
                cJSON_AddNullToObject(var_obj, "age_ms");
                cJSON_AddBoolToObject(var_obj, "stale", true);
            }
        }

        if (var.source_id[0] != '\0') {
            cJSON_AddStringToObject(var_obj, "source_id", var.source_id);
        }

        cJSON_AddItemToArray(vars_array, var_obj);
    }

    cJSON_AddNumberToObject(result->data, "count", cJSON_GetArraySize(vars_array));
    result->code = TS_API_OK;
    return ESP_OK;
}

/**
 * @brief automation.variables.get - Get a variable value
 */
static esp_err_t api_automation_variables_get(const cJSON *params, ts_api_result_t *result)
{
    cJSON *name_param = cJSON_GetObjectItem(params, "name");
    if (!name_param || !cJSON_IsString(name_param)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'name' parameter");
        return ESP_OK;
    }

    ts_auto_value_t value = {0};
    esp_err_t ret = ts_variable_get(name_param->valuestring, &value);

    if (ret != ESP_OK) {
        result->code = TS_API_ERR_NOT_FOUND;
        result->message = strdup("Variable not found");
        return ESP_OK;
    }

    result->data = cJSON_CreateObject();
    cJSON_AddStringToObject(result->data, "name", name_param->valuestring);
    cJSON_AddItemToObject(result->data, "value", value_to_json(&value));

    result->code = TS_API_OK;
    return ESP_OK;
}

/**
 * @brief automation.variables.set - Set a variable value
 *
 * Params:
 *   - name: Variable name (required)
 *   - value: Value to set (required)
 *   - create_only: If true, only create if not exists, don't overwrite (optional)
 */
static esp_err_t api_automation_variables_set(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_write(TS_AUTO_CONFIG_VARIABLE,params,TS_API_AUTO_UPSERT,result); }

static esp_err_t api_automation_variables_delete(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_delete(TS_AUTO_CONFIG_VARIABLE,params,result); }

/*===========================================================================*/
/*                           Rules API                                        */
/*===========================================================================*/

/**
 * @brief automation.rules.list - List all rules
 */
static esp_err_t api_automation_rules_list(const cJSON *params, ts_api_result_t *result)
{
    ts_auto_config_status_t status;
    const ts_auto_config_view_t *view=ts_auto_config_acquire_status(&status);
    const cJSON *rules=cJSON_GetObjectItemCaseSensitive(ts_auto_config_json(view),"rules"),*row;
    cJSON *data=cJSON_CreateObject(),*items=data?cJSON_AddArrayToObject(data,"rules"):NULL;
    bool valid_array=cJSON_IsArray(rules);
    if(!data||!items||!valid_array)goto failed;
    unsigned invalid=0;
    cJSON_ArrayForEach(row,rules){
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(row,"id");
        if(!cJSON_IsObject(row)||!cJSON_IsString(id)){++invalid;continue;}
        cJSON *item=cJSON_Duplicate(row,true);
        if(!item)goto failed;
        ts_auto_rule_t active={0};
        if(ts_rule_acquire(id->valuestring,&active)==ESP_OK){
            const cJSON *saved_revision=cJSON_GetObjectItemCaseSensitive(row,"revision");
            if(!status.error.reason && ts_auto_config_definitions(view) && cJSON_IsNumber(saved_revision) &&
               saved_revision->valuedouble==active.revision){
                cJSON_AddNumberToObject(item,"trigger_count",active.trigger_count);
                cJSON_AddNumberToObject(item,"last_trigger_ms",active.last_trigger_ms);
                ts_rule_start_status_t start;
                if(ts_rule_start_status(active.id,&start)){
                    cJSON *runtime=cJSON_AddObjectToObject(item,"automatic_start");
                    if(runtime){
                        cJSON_AddStringToObject(runtime,"state",start.state);
                        if(start.reason)cJSON_AddStringToObject(runtime,"reason",start.reason);
                        cJSON_AddNumberToObject(runtime,"retry_after_ms",start.retry_after_ms);
                    }
                }
            }
            ts_rule_release(&active);
        }
        const cJSON *conditions=cJSON_GetObjectItemCaseSensitive(row,"conditions");
        if(cJSON_IsObject(conditions))conditions=cJSON_GetObjectItemCaseSensitive(conditions,"items");
        cJSON_AddNumberToObject(item,"conditions_count",cJSON_GetArraySize(conditions));
        cJSON_AddNumberToObject(item,"actions_count",cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(row,"actions")));
        const ts_auto_document_t *document=ts_auto_config_definitions(view);
        cJSON *commands=cJSON_AddArrayToObject(item,"ssh_commands");
        if(!commands){cJSON_Delete(item);goto failed;}
        for(int n=0;document&&n<document->rule_count;++n){
            const ts_auto_rule_t *rule=&document->rules[n];
            if(strcmp(rule->id,id->valuestring))continue;
            for(unsigned a=0;a<rule->action_count;++a){
                const ts_auto_action_t *action=&rule->actions[a];
                if(action->template_id[0])
                    for(int t=0;t<document->action_count;++t)
                        if(!strcmp(document->actions[t].id,action->template_id)){action=&document->actions[t].action;break;}
                if(action->type!=TS_AUTO_ACT_SSH_CMD_REF)continue;
                for(int c=0;c<document->command_count;++c)
                    if(document->commands[c].nohup&&!strcmp(document->commands[c].id,action->ssh_ref.cmd_id)){
                        cJSON *command=cJSON_CreateString(action->ssh_ref.cmd_id);
                        if(!command||!cJSON_AddItemToArray(commands,command)){cJSON_Delete(command);cJSON_Delete(item);goto failed;}
                    }
            }
            break;
        }
        ts_auto_config_rule_status(view,&status,id->valuestring,item);
        if(!cJSON_AddItemToArray(items,item)){cJSON_Delete(item);goto failed;}
    }
    cJSON_AddBoolToObject(data,"loaded",status.loaded);
    cJSON_AddBoolToObject(data,"paused",status.paused);
    cJSON_AddBoolToObject(data,"recovery_required",status.uncertain||status.error.reason!=NULL);
    cJSON_AddStringToObject(data,"load_error",status.uncertain?"save_outcome_unknown":status.error.reason?status.error.reason:
                            !status.loaded?"configuration_unavailable":status.paused?"automation_paused":"");
    cJSON_AddStringToObject(data,"active_version",status.active_version);
    cJSON_AddNumberToObject(data,"invalid_items",invalid);
    cJSON_AddNumberToObject(data,"count",cJSON_GetArraySize(rules));
    cJSON_AddBoolToObject(data,"restart_required",false);
    ts_auto_config_release(view);ts_api_result_ok(result,data);return ESP_OK;
failed:
    cJSON_Delete(data);ts_auto_config_release(view);
    ts_api_result_error(result,TS_API_ERR_INTERNAL,!status.loaded?(status.error.reason?status.error.reason:"configuration_unavailable"):valid_array?"out_of_memory":"configuration_invalid");return ESP_OK;
}

/**
 * @brief automation.rules.enable - Enable a rule
 */


static esp_err_t api_automation_rules_enable(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_enabled(TS_AUTO_CONFIG_RULE,params,true,result); }

/**
 * @brief automation.rules.disable - Disable a rule
 */
static esp_err_t api_automation_rules_disable(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_enabled(TS_AUTO_CONFIG_RULE,params,false,result); }

/**
 * @brief automation.rules.trigger - Manually trigger a rule
 */
static cJSON *service_status_json(const ts_ssh_service_status_t *state)
{
    cJSON *data=cJSON_CreateObject();if(!data)return NULL;
    cJSON_AddStringToObject(data,"state",state->state[0]?state->state:"unknown");
    cJSON_AddStringToObject(data,"last_known",state->last_known);cJSON_AddStringToObject(data,"source",state->source);
    cJSON_AddNumberToObject(data,"confirmed_ms",state->confirmed_ms);cJSON_AddNumberToObject(data,"generation",state->generation);
    cJSON_AddBoolToObject(data,"busy",state->busy);cJSON_AddNumberToObject(data,"operation_id",state->operation_id);
    cJSON_AddStringToObject(data,"operation_phase",state->operation_phase);cJSON_AddStringToObject(data,"operation_kind",state->operation_kind);
    cJSON_AddStringToObject(data,"operation_reason",state->operation_reason);
    cJSON_AddNumberToObject(data,"operation_error",state->operation_error);return data;
}
static esp_err_t api_service_list(const cJSON *params,ts_api_result_t *result)
{
    (void)params;cJSON *data=cJSON_CreateObject(),*items=cJSON_CreateArray();
    if(!data||!items){cJSON_Delete(data);cJSON_Delete(items);return ESP_ERR_NO_MEM;}
    cJSON_AddItemToObject(data,"items",items);
    const ts_auto_config_view_t *view=ts_auto_config_acquire();
    const ts_auto_document_t *document=ts_auto_config_definitions(view);
    for(unsigned i=0;i<TS_SSH_RUN_SLOTS;++i){
        ts_ssh_run_record_t record;ts_ssh_service_status_t state;
        if(ts_ssh_service_get(i,&record,&state)!=ESP_OK)continue;
        cJSON *item=service_status_json(&state);
        if(!item){ts_auto_config_release(view);cJSON_Delete(data);return ESP_ERR_NO_MEM;}
        cJSON_AddStringToObject(item,"run_id",record.run_id);cJSON_AddStringToObject(item,"command_id",record.command_id);
        cJSON_AddStringToObject(item,"name",record.name);cJSON_AddStringToObject(item,"rule_id",record.rule);
        cJSON_AddStringToObject(item,"config_version",record.config_version);cJSON_AddStringToObject(item,"host_id",record.host.id);
        cJSON_AddStringToObject(item,"host",record.host.host);cJSON_AddNumberToObject(item,"port",record.host.port);
        cJSON_AddStringToObject(item,"username",record.host.username);
        cJSON_AddStringToObject(item,"keyid",record.host.keyid);
        char log_file[96];snprintf(log_file,sizeof(log_file),"/tmp/ts_nohup_%s.log",record.locator);
        cJSON_AddStringToObject(item,"log_file",log_file);
        cJSON_AddBoolToObject(item,"legacy",record.kind!=TS_SSH_RUN_GROUP);
        bool current=false;
        for(int c=0;document&&c<document->command_count;++c){
            const ts_ssh_command_config_t *command=&document->commands[c];
            if(strcmp(command->id,record.command_id))continue;
            for(int h=0;h<document->host_count;++h){
                const ts_ssh_host_config_t *host=&document->hosts[h];
                if(strcmp(host->id,command->host_id))continue;
                char selected[TS_SSH_RUN_ID_MAX];ts_ssh_service_status_t cached;
                current=ts_ssh_service_current(command->id,host,command->instance,command->object_id,selected,&cached)&&!strcmp(selected,record.run_id);
                break;
            }
            break;
        }
        cJSON_AddBoolToObject(item,"current",current);
        cJSON_AddItemToArray(items,item);
    }
    ts_auto_config_release(view);
    ts_api_result_ok(result,data);return ESP_OK;
}
static esp_err_t service_request(const cJSON *params, ts_api_result_t *result, bool stop) {
    const char *id=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"run_id"));
    if(!id||!id[0]){ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"run_id_required");return ESP_ERR_INVALID_ARG;}
    if(stop||cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params,"verify")))
        return ts_api_service_control(params,result,stop?TS_SERVICE_STOP:TS_SERVICE_VERIFY);
    ts_ssh_service_status_t state={0};esp_err_t ret=ts_ssh_service_cached(id,&state);
    cJSON *data=service_status_json(&state);if(!data)return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(data,"run_id",id);
    if(ret==ESP_OK)ts_api_result_ok(result,data);
    else{ts_api_result_error(result,TS_API_ERR_NOT_FOUND,"service_record_not_found");result->data=data;}
    return ret;
}
static esp_err_t api_service_status(const cJSON *params, ts_api_result_t *result) {
    return service_request(params, result, false);
}
static esp_err_t api_service_stop(const cJSON *params, ts_api_result_t *result) {
    return service_request(params, result, true);
}

/* Reason tokens for a refused manual trigger; the Web UI translates them (failure.reasons.*). */
static const char *trigger_reject_token(ts_rule_reject_t why)
{
    switch (why) {
    case TS_RULE_REJECT_NOT_ACCEPTING:      return "engine_not_accepting";
    case TS_RULE_REJECT_PAUSED:             return "automation_paused";
    case TS_RULE_REJECT_DISABLED:           return "rule_disabled";
    case TS_RULE_REJECT_MANUAL_NOT_ALLOWED: return "manual_not_allowed";
    case TS_RULE_REJECT_NOT_LOADED:         return "config_loading";
    case TS_RULE_REJECT_RECOVERY:           return "recovery_required";
    case TS_RULE_REJECT_CONFIG_BUSY:        return "configuration_busy";
    case TS_RULE_REJECT_RUNNING:            return "execution_busy";
    case TS_RULE_REJECT_SERVICE_STATE:      return "service_not_admissible";
    case TS_RULE_REJECT_CONFIG_VERSION:     return "configuration_changed";
    case TS_RULE_REJECT_CHANGED:            return "rule_changed";
    default:                                return NULL;
    }
}

/* "Try again later" refusals are BUSY, so an older Web UI still says so; the others carry no better class than INTERNAL. */
static ts_api_result_code_t trigger_reject_code(ts_rule_reject_t why)
{
    switch (why) {
    case TS_RULE_REJECT_NOT_ACCEPTING:
    case TS_RULE_REJECT_NOT_LOADED:
    case TS_RULE_REJECT_CONFIG_BUSY:
    case TS_RULE_REJECT_RUNNING:
    case TS_RULE_REJECT_CONFIG_VERSION:
    case TS_RULE_REJECT_CHANGED:            return TS_API_ERR_BUSY;
    default:                                return TS_API_ERR_INTERNAL;
    }
}

static esp_err_t api_automation_rules_trigger(const cJSON *params, ts_api_result_t *result)
{
    cJSON *id_param = cJSON_GetObjectItem(params, "id");
    if (!id_param || !cJSON_IsString(id_param)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'id' parameter");
        return ESP_OK;
    }

    const char *version=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"active_version"));
    if(!version||!version[0]){ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"configuration_version_required");return ESP_OK;}
    ESP_LOGI("api_auto", "Calling ts_rule_trigger('%s')", id_param->valuestring);
    ts_rule_reject_t why = TS_RULE_REJECT_NONE;
    const char *reason=NULL;
    esp_err_t ret = ts_rule_trigger_versioned(id_param->valuestring,version,&why,&reason);
    ESP_LOGI("api_auto", "ts_rule_trigger returned: %s (refusal %d)", esp_err_to_name(ret), (int)why);

    if (ret == ESP_OK) {
        result->code = TS_API_OK;
        result->message = strdup("Rule triggered");
    } else if (ret == ESP_ERR_NOT_FOUND) {
        result->code = TS_API_ERR_NOT_FOUND;
        result->message = strdup("Rule not found");
    } else if (reason) {
        ts_api_result_error(result,trigger_reject_code(why),reason);
    } else if (trigger_reject_token(why)) {
        result->code = trigger_reject_code(why);
        result->message = strdup(trigger_reject_token(why));
    } else {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("Failed to trigger rule");
    }

    return ESP_OK;
}

/**
 * @brief Convert operator to string
 */

/**
 * @brief automation.rules.get - Get rule details by ID
 */
static esp_err_t api_automation_rules_get(const cJSON *params,ts_api_result_t *result)
{
    const char *id=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"id"));
    if(!id||!ts_rule_id_valid(id)){ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"Missing 'id' parameter");return ESP_OK;}
    ts_auto_config_status_t status;const ts_auto_config_view_t *view=ts_auto_config_acquire_status(&status);
    const cJSON *rows=cJSON_GetObjectItemCaseSensitive(ts_auto_config_json(view),"rules"),*row;
    cJSON *copy=NULL;bool found=false;
    cJSON_ArrayForEach(row,rows){
        const cJSON *name=cJSON_GetObjectItemCaseSensitive(row,"id");
        if(cJSON_IsString(name)&&!strcmp(name->valuestring,id)){found=true;copy=cJSON_Duplicate(row,true);break;}
    }
    if(copy)ts_auto_config_rule_status(view,&status,id,copy);
    ts_auto_config_release(view);
    if(copy)ts_api_result_ok(result,copy);
    else ts_api_result_error(result,found?TS_API_ERR_NO_MEM:TS_API_ERR_NOT_FOUND,found?"out_of_memory":"Rule not found");
    return ESP_OK;
}

/**
 * @brief Parse operator from string
 */

/**
 * @brief Parse action type from string
 */

/**
 * @brief automation.rules.add - Add a new rule
 */
static esp_err_t api_automation_rules_add(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_write(TS_AUTO_CONFIG_RULE,params,TS_API_AUTO_CREATE,result); }

static esp_err_t api_automation_rules_update(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_write(TS_AUTO_CONFIG_RULE,params,TS_API_AUTO_UPDATE,result); }

/**
 * @brief automation.rules.delete - Delete a rule
 */
static esp_err_t api_automation_rules_delete(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_delete(TS_AUTO_CONFIG_RULE,params,result); }

/*===========================================================================*/
/*                           Sources API                                      */
/*===========================================================================*/

/**
 * @brief automation.sources.list - List all data sources
 */
static esp_err_t api_automation_sources_list(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_list(TS_AUTO_CONFIG_SOURCE,params,"sources",result); }


/**
 * @brief automation.sources.add - Add a new data source
 */
static esp_err_t api_automation_sources_add(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_write(TS_AUTO_CONFIG_SOURCE,params,TS_API_AUTO_CREATE,result); }

static esp_err_t api_automation_sources_update(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_write(TS_AUTO_CONFIG_SOURCE,params,TS_API_AUTO_UPDATE,result); }

/**
 * @brief automation.sources.delete - Delete a data source
 */
static esp_err_t api_automation_sources_delete(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_delete(TS_AUTO_CONFIG_SOURCE,params,result); }

/**
 * @brief automation.sources.add_mapping - Add a field mapping to an existing source
 *
 * This allows users to select specific JSON fields from the data source
 * to be monitored as independent variables.
 *
 * Parameters:
 *   - id: Source ID
 *   - json_path: JSONPath expression to extract value (e.g., "cpu.cores[0].usage")
 *   - var_name: Variable name to store the extracted value (e.g., "lpmu.cpu0_usage")
 *   - transform: (optional) Transform expression
 */
static esp_err_t api_automation_sources_add_mapping(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_mapping(params,false,result); }

/**
 * @brief automation.sources.remove_mapping - Remove a field mapping from a source
 *
 * Parameters:
 *   - id: Source ID
 *   - var_name: Variable name of the mapping to remove
 *   OR
 *   - index: Index of the mapping to remove (0-based)
 */
static esp_err_t api_automation_sources_remove_mapping(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_mapping(params,true,result); }

/**
 * @brief automation.sources.enable - Enable a data source
 */
static esp_err_t api_automation_sources_enable(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_enabled(TS_AUTO_CONFIG_SOURCE,params,true,result); }

/**
 * @brief automation.sources.disable - Disable a data source
 */
static esp_err_t api_automation_sources_disable(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_enabled(TS_AUTO_CONFIG_SOURCE,params,false,result); }

/*===========================================================================*/
/*                           Test Actions API                                 */
/*===========================================================================*/

/**
 * @brief automation.test.led - Test LED action
 */
static esp_err_t api_automation_test_led(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device = cJSON_GetObjectItem(params, "device");
    cJSON *index_j = cJSON_GetObjectItem(params, "index");
    cJSON *r = cJSON_GetObjectItem(params, "r");
    cJSON *g = cJSON_GetObjectItem(params, "g");
    cJSON *b = cJSON_GetObjectItem(params, "b");

    if (!device || !cJSON_IsString(device)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'device' parameter");
        return ESP_OK;
    }

    ts_auto_action_t action = {
        .type = TS_AUTO_ACT_LED,
        .led = {
            .index = index_j && cJSON_IsNumber(index_j) ? (uint8_t)index_j->valueint : 0xFF,
            .r = r && cJSON_IsNumber(r) ? (uint8_t)r->valueint : 0,
            .g = g && cJSON_IsNumber(g) ? (uint8_t)g->valueint : 0,
            .b = b && cJSON_IsNumber(b) ? (uint8_t)b->valueint : 0,
            .duration_ms = 0,
        }
    };
    strncpy(action.led.device, device->valuestring, sizeof(action.led.device) - 1);

    esp_err_t ret = ts_action_execute(&action);

    if (ret == ESP_OK) {
        result->code = TS_API_OK;
        result->message = strdup("LED action executed");
    } else {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("LED action failed");
    }

    return ESP_OK;
}

/**
 * @brief automation.test.gpio - Test GPIO action
 */
static esp_err_t api_automation_test_gpio(const cJSON *params, ts_api_result_t *result)
{
    cJSON *pin = cJSON_GetObjectItem(params, "pin");
    cJSON *level = cJSON_GetObjectItem(params, "level");
    cJSON *pulse_ms = cJSON_GetObjectItem(params, "pulse_ms");

    if (!pin || !cJSON_IsNumber(pin)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'pin' parameter");
        return ESP_OK;
    }

    ts_auto_action_t action = {
        .type = TS_AUTO_ACT_GPIO,
        .gpio = {
            .pin = (uint8_t)pin->valueint,
            .level = level && cJSON_IsTrue(level),
            .pulse_ms = pulse_ms && cJSON_IsNumber(pulse_ms) ? (uint32_t)pulse_ms->valueint : 0,
        }
    };

    esp_err_t ret = ts_action_execute(&action);

    if (ret == ESP_OK) {
        result->code = TS_API_OK;
        result->message = strdup("GPIO action executed");
    } else {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("GPIO action failed");
    }

    return ESP_OK;
}

/**
 * @brief automation.test.device - Test device control action
 */
static esp_err_t api_automation_test_device(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_name = cJSON_GetObjectItem(params, "device");
    cJSON *action_name = cJSON_GetObjectItem(params, "action");

    if (!device_name || !cJSON_IsString(device_name)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'device' parameter");
        return ESP_OK;
    }

    if (!action_name || !cJSON_IsString(action_name)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'action' parameter");
        return ESP_OK;
    }

    ts_auto_action_t action = {
        .type = TS_AUTO_ACT_DEVICE_CTRL,
    };
    strncpy(action.device.device, device_name->valuestring, sizeof(action.device.device) - 1);
    strncpy(action.device.action, action_name->valuestring, sizeof(action.device.action) - 1);

    esp_err_t ret = ts_action_execute(&action);

    if (ret == ESP_OK) {
        result->code = TS_API_OK;
        result->message = strdup("Device action executed");
    } else {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("Device action failed");
    }

    return ESP_OK;
}

/**
 * @brief automation.test.ssh - Test SSH command execution
 */
static esp_err_t api_automation_test_ssh(const cJSON *params, ts_api_result_t *result)
{
    cJSON *host_id = cJSON_GetObjectItem(params, "host_id");
    cJSON *command = cJSON_GetObjectItem(params, "command");
    cJSON *timeout_ms = cJSON_GetObjectItem(params, "timeout_ms");

    if (!host_id || !cJSON_IsString(host_id)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'host_id' parameter");
        return ESP_OK;
    }

    if (!command || !cJSON_IsString(command)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'command' parameter");
        return ESP_OK;
    }

    ts_auto_action_t action = {
        .type = TS_AUTO_ACT_SSH_CMD,
        .ssh = {
            .timeout_ms = (timeout_ms && cJSON_IsNumber(timeout_ms)) ?
                          (uint32_t)timeout_ms->valueint : 30000,
            .async = false,
        }
    };
    strncpy(action.ssh.host_ref, host_id->valuestring, sizeof(action.ssh.host_ref) - 1);
    strncpy(action.ssh.command, command->valuestring, sizeof(action.ssh.command) - 1);

    ts_action_result_t exec_result = {0};
    esp_err_t ret = ts_action_manager_execute(&action, &exec_result);

    result->data = cJSON_CreateObject();

    if (ret == ESP_OK && exec_result.status == TS_ACTION_STATUS_SUCCESS) {
        result->code = TS_API_OK;
        result->message = strdup("SSH command executed");
        cJSON_AddStringToObject(result->data, "output", exec_result.output);
        cJSON_AddNumberToObject(result->data, "exit_code", exec_result.exit_code);
        cJSON_AddNumberToObject(result->data, "duration_ms", exec_result.duration_ms);
    } else {
        result->code = TS_API_ERR_INTERNAL;
        if (exec_result.status == TS_ACTION_STATUS_TIMEOUT) {
            result->message = strdup("SSH command timed out");
        } else if (exec_result.output[0]) {
            result->message = strdup(exec_result.output);
        } else {
            result->message = strdup("SSH command failed");
        }
        if (exec_result.output[0]) {
            cJSON_AddStringToObject(result->data, "output", exec_result.output);
        }
    }

    return ESP_OK;
}

/*===========================================================================*/
/*                        Action Template API                                 */
/*===========================================================================*/

/**
 * @brief Convert action type string to enum
 */


/**
 * @brief automation.actions.list - List all action templates
 */
static esp_err_t api_automation_actions_list(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_list(TS_AUTO_CONFIG_ACTION,params,"templates",result); }


/**
 * @brief automation.actions.add - Add a new action template
 */


static esp_err_t api_automation_actions_add(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_write(TS_AUTO_CONFIG_ACTION,params,TS_API_AUTO_CREATE,result); }

static esp_err_t api_automation_actions_update(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_write(TS_AUTO_CONFIG_ACTION,params,TS_API_AUTO_UPDATE,result); }

/**
 * @brief automation.actions.get - Get a single action template by ID
 */
static esp_err_t api_automation_actions_get(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_get(TS_AUTO_CONFIG_ACTION,params,result); }

/**
 * @brief automation.actions.delete - Delete an action template
 */
static esp_err_t api_automation_actions_delete(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_delete(TS_AUTO_CONFIG_ACTION,params,result); }

/** The reply when an action template did not run to success. The page gets a reason; whatever the action itself printed
 * (or, without output, the status) goes to data.detail for the folded details, and data.output as before. */
static void action_execute_failure(ts_api_result_t *result, esp_err_t ret, const ts_action_result_t *exec)
{
    if(exec->reason){
        ts_api_result_reason(result,TS_API_ERR_INTERNAL,exec->reason,exec->output);
        if(result->data&&exec->output[0])cJSON_AddStringToObject(result->data,"output",exec->output);
        return;
    }
    char fallback[128];
    const char *detail = exec->output;
    if (ret == ESP_ERR_NOT_FOUND) {
        result->code = TS_API_ERR_NOT_FOUND;
        free(result->message);
        result->message = strdup("Template not found");
        return;
    }
    if (!detail[0]) {
        snprintf(fallback, sizeof(fallback), "Action failed: %s (status=%d)", esp_err_to_name(ret), exec->status);
        detail = fallback;
    }
    result->code = TS_API_ERR_INTERNAL;
    free(result->message);
    result->message = strdup(exec->status == TS_ACTION_STATUS_TIMEOUT ? "action_timeout" : "action_failed");
    if (!result->data) result->data = cJSON_CreateObject();
    if (result->data) {
        if (exec->output[0]) cJSON_AddStringToObject(result->data, "output", exec->output);
        cJSON_AddStringToObject(result->data, "detail", detail);
    }
}

/**
 * @brief automation.actions.execute - Execute an action template
 */
static esp_err_t api_automation_actions_execute(const cJSON *params, ts_api_result_t *result)
{
    cJSON *id = cJSON_GetObjectItem(params, "id");

    if (!id || !cJSON_IsString(id)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'id' parameter");
        return ESP_OK;
    }

    const cJSON *version=cJSON_GetObjectItemCaseSensitive(params,"active_version");
    if(!cJSON_IsString(version)||!version->valuestring[0]){
        ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"configuration_version_required");
        return ESP_OK;
    }
    TS_LOGI(TAG, "Executing action template: %s", id->valuestring);

    ts_action_result_t exec_result = {0};
    const char *reason=NULL;
    esp_err_t ret = ts_action_template_execute_versioned(id->valuestring,version->valuestring,&exec_result,&reason);
    if(reason){ts_api_result_error(result,TS_API_ERR_BUSY,reason);return ESP_OK;}

    TS_LOGI(TAG, "Execute result: ret=%s, status=%d", esp_err_to_name(ret), exec_result.status);

    result->data = cJSON_CreateObject();

    /* Handle async (queued) execution */
    if (ret == ESP_OK && exec_result.status == TS_ACTION_STATUS_QUEUED) {
        result->code = TS_API_OK;
        result->message = strdup("Action queued for async execution");
        cJSON_AddBoolToObject(result->data, "async", true);
        cJSON_AddStringToObject(result->data, "status", "queued");
    } else if (ret == ESP_OK && exec_result.status == TS_ACTION_STATUS_SUCCESS) {
        result->code = TS_API_OK;
        result->message = strdup("Action executed successfully");
        cJSON_AddBoolToObject(result->data, "async", false);
        cJSON_AddNumberToObject(result->data, "duration_ms", exec_result.duration_ms);
        if (exec_result.output[0]) {
            cJSON_AddStringToObject(result->data, "output", exec_result.output);
        }
    } else {
        action_execute_failure(result, ret, &exec_result);
    }

    return ESP_OK;
}
/*===========================================================================*/
/*                        Action Statistics API                               */
/*===========================================================================*/

/**
 * @brief automation.action.stats - Get action execution statistics
 */
static esp_err_t api_automation_action_stats(const cJSON *params, ts_api_result_t *result)
{
    (void)params;

    ts_action_stats_t stats;
    esp_err_t ret = ts_action_get_stats(&stats);

    if (ret != ESP_OK) {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("Failed to get action stats");
        return ESP_OK;
    }

    result->data = cJSON_CreateObject();
    cJSON_AddNumberToObject(result->data, "total_executed", stats.total_executed);
    cJSON_AddNumberToObject(result->data, "success_count", stats.total_success);
    cJSON_AddNumberToObject(result->data, "failed_count", stats.total_failed);
    cJSON_AddNumberToObject(result->data, "timeout_count", stats.total_timeout);
    cJSON_AddNumberToObject(result->data, "queue_pending", stats.queue_high_water);  // 使用 high water mark
    cJSON_AddBoolToObject(result->data, "queue_running", false);  // TODO: 添加队列运行状态追踪

    result->code = TS_API_OK;
    return ESP_OK;
}

/**
 * @brief automation.action.stats.reset - Reset action statistics
 */
static esp_err_t api_automation_action_stats_reset(const cJSON *params, ts_api_result_t *result)
{
    (void)params;

    ts_action_reset_stats();

    result->code = TS_API_OK;
    result->message = strdup("Action statistics reset");

    return ESP_OK;
}

/*===========================================================================*/
/*                     Proxy APIs for External Connection Test               */
/*===========================================================================*/

#include "esp_websocket_client.h"

/**
 * @brief automation.proxy.fetch - Fetch data from external REST API
 *
 * Used by WebUI to test API connections before creating data sources.
 * Bypasses browser CORS restrictions by proxying through ESP32.
 *
 * Special handling for local API calls (127.0.0.1 or self IP) - calls API directly.
 */
static bool proxy_fetch_current(void *context)
{
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
    uint32_t session_id = (uint32_t)(uintptr_t)context;
    if (session_id) {
        ts_session_t caller;
        return ts_security_validate_session(session_id, &caller) == ESP_OK && caller.level >= TS_PERM_ROOT;
    }
#else
    (void)context;
#endif
    return true;
}

static esp_err_t api_automation_proxy_fetch(const cJSON *params, ts_api_result_t *result)
{
    return ts_api_automation_proxy_fetch_for_session(params, 0, result);
}

esp_err_t ts_api_automation_proxy_fetch_for_session(const cJSON *params, uint32_t session_id,
                                                   ts_api_result_t *result)
{
    cJSON *url_json = cJSON_GetObjectItem(params, "url");
    cJSON *method_json = cJSON_GetObjectItem(params, "method");
    cJSON *headers_json = cJSON_GetObjectItem(params, "headers");

    if (!url_json || !cJSON_IsString(url_json)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'url' parameter");
        return ESP_OK;
    }

    const char *url = url_json->valuestring;
    const char *method_str = (method_json && cJSON_IsString(method_json)) ? method_json->valuestring : "GET";
    cJSON *timeout_json = cJSON_GetObjectItem(params, "timeout");
    int timeout_ms = 15000;
    struct http_parser_url parsed;
    if (!ts_http_url_parse(url, &parsed) ||
        (method_json && !cJSON_IsNull(method_json) && !cJSON_IsString(method_json)) ||
        (headers_json && !cJSON_IsNull(headers_json) && !cJSON_IsObject(headers_json)) ||
        (timeout_json && (!cJSON_IsNumber(timeout_json) || !isfinite(timeout_json->valuedouble) ||
            timeout_json->valuedouble < 1 || timeout_json->valuedouble > 30000 ||
            timeout_json->valuedouble != (int)timeout_json->valuedouble))) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid HTTP target, headers or timeout");
        return ESP_ERR_INVALID_ARG;
    }
    if (timeout_json) timeout_ms = (int)timeout_json->valuedouble;
    if (headers_json && cJSON_IsNull(headers_json)) headers_json = NULL;
    bool is_local = ts_http_url_is_local_api(url, &parsed);

    if (is_local) {
        // 提取 API 路径: /api/v1/system/memory -> system.memory
        const char *api_path = url + parsed.field_data[UF_PATH].off;
        api_path += 8;  // 跳过 "/api/v1/"

        // 将路径转换为 API 名称: system/memory -> system.memory
        char api_name[64] = {0};
        size_t i = 0, j = 0;
        size_t path_length = parsed.field_data[UF_PATH].len - 8;
        while (i < path_length && j < sizeof(api_name) - 1) {
            api_name[j++] = api_path[i] == '/' ? '.' : api_path[i];
            i++;
        }
        if (i != path_length) {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "API name is too long");
            return ESP_ERR_INVALID_ARG;
        }
        api_name[j] = '\0';

        TS_LOGI(TAG, "Local API dispatch requested");

        ts_api_access_t access;
        esp_err_t policy_ret = ts_api_get_access(api_name, &access);
        uint8_t method_bit = !strcasecmp(method_str, "GET") ? TS_API_METHOD_GET :
            !strcasecmp(method_str, "POST") ? TS_API_METHOD_POST : !strcasecmp(method_str, "PUT") ? TS_API_METHOD_PUT :
            !strcasecmp(method_str, "DELETE") ? TS_API_METHOD_DELETE : 0;
        if (policy_ret != ESP_OK || !method_bit || !(access.allowed_methods & method_bit)) {
            ts_api_result_error(result, policy_ret == ESP_ERR_NOT_FOUND ? TS_API_ERR_NOT_FOUND : TS_API_ERR_NOT_SUPPORTED,
                                policy_ret != ESP_OK ? "API policy unavailable" : "Method not allowed");
            return policy_ret != ESP_OK ? policy_ret : ESP_ERR_NOT_SUPPORTED;
        }
        // 直接调用本地 API
        ts_api_result_t local_result = {0};
        cJSON *api_params = cJSON_CreateObject();
        if (!api_params) {
            ts_api_result_error(result, TS_API_ERR_NO_MEM, "Parameter allocation failed");
            return ESP_ERR_NO_MEM;
        }
        const char *query = (parsed.field_set & (1U << UF_QUERY)) ? url + parsed.field_data[UF_QUERY].off : NULL;
        if (query) {
            char *copy = strndup(query, parsed.field_data[UF_QUERY].len);
            if (!copy) {
                cJSON_Delete(api_params);
                ts_api_result_error(result, TS_API_ERR_NO_MEM, "Query allocation failed");
                return ESP_ERR_NO_MEM;
            }
            ts_api_params_status_t parsed = ts_api_params_add_query(api_params, copy);
            free(copy);
            if (parsed != TS_API_PARAMS_OK) {
                cJSON_Delete(api_params);
                ts_api_result_error(result, parsed == TS_API_PARAMS_NO_MEM ? TS_API_ERR_NO_MEM : TS_API_ERR_INVALID_ARG,
                                    "Invalid or unavailable query");
                return parsed == TS_API_PARAMS_NO_MEM ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_ARG;
            }
        }

        cJSON *data = cJSON_CreateObject(), *body = cJSON_CreateObject();
        char *success = strdup("Local API call successful");
        bool prepared = data && body && success && cJSON_AddNumberToObject(data, "status", 200) &&
            cJSON_AddNumberToObject(data, "content_length", 0) && cJSON_AddNumberToObject(body, "code", 0);
        if (!prepared || !cJSON_AddItemToObjectCS(data, "body", body)) {
            cJSON_Delete(data); cJSON_Delete(body); cJSON_Delete(api_params); free(success);
            ts_api_result_error(result, TS_API_ERR_NO_MEM, "Response allocation failed");
            return ESP_ERR_NO_MEM;
        }
        esp_err_t ret = ts_api_call_for_session(api_name, api_params, session_id, &local_result);
        cJSON_Delete(api_params);
        if (ret == ESP_OK && local_result.code == TS_API_OK) {
            bool complete = !local_result.message || cJSON_AddStringToObject(body, "message", local_result.message);
            if (complete && local_result.data) {
                complete = cJSON_AddItemToObjectCS(body, "data", local_result.data);
                if (complete) local_result.data = NULL;
            }
            if (complete) {
                result->code = TS_API_OK; result->message = success; success = NULL;
                result->data = data; data = NULL;
            } else ts_api_result_error(result, TS_API_ERR_INTERNAL, "result_unknown");
        } else {
            result->code = local_result.code ? local_result.code : TS_API_ERR_INTERNAL;
            result->message = local_result.message; local_result.message = NULL;
            result->data = local_result.data; local_result.data = NULL;
        }
        free(success); cJSON_Delete(data); ts_api_result_free(&local_result);

        return ESP_OK;
    }

    // 外部 HTTP 请求
    /* C session 0 is the existing trusted internal handler contract. Every
     * external entry declares READ and supplies a verified nonzero session.
     * Local proxy calls above inherit that session and the destination policy. */
#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED
    if (session_id) {
        ts_session_t caller;
        if (ts_security_validate_session(session_id, &caller) != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_AUTH, "Unauthorized");
            return ESP_ERR_NOT_ALLOWED;
        }
        if (caller.level < TS_PERM_ROOT) {
            ts_api_result_error(result, TS_API_ERR_NO_PERMISSION, "Root permission required");
            return ESP_ERR_NOT_ALLOWED;
        }
    }
#endif

    esp_http_client_method_t method;
    if (!strcasecmp(method_str, "GET")) method = HTTP_METHOD_GET;
    else if (!strcasecmp(method_str, "POST")) method = HTTP_METHOD_POST;
    else if (!strcasecmp(method_str, "PUT")) method = HTTP_METHOD_PUT;
    else if (!strcasecmp(method_str, "DELETE")) method = HTTP_METHOD_DELETE;
    else {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Unsupported HTTP method");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *ca = cJSON_GetObjectItem(params, "cert_pem");
    cJSON *skip = cJSON_GetObjectItem(params, "skip_verify");
    if ((ca && !cJSON_IsString(ca)) || (skip && !cJSON_IsFalse(skip))) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Certificate verification is required");
        return ESP_ERR_INVALID_ARG;
    }
    char *success = strdup("Request successful");
    if (!success) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Response allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ts_http_fetch_config_t request = {.url = url, .method = method, .headers = headers_json,
        .cert_pem = ca ? ca->valuestring : NULL, .timeout_ms = (unsigned)timeout_ms, .maximum_body = 8192,
        .current = proxy_fetch_current, .context = (void *)(uintptr_t)session_id};
    ts_http_fetch_result_t reply = {0};
    esp_err_t error = ts_http_fetch(&request, &reply);
    if (error != ESP_OK) {
        free(success);
        if (!proxy_fetch_current((void *)(uintptr_t)session_id)) {
            ts_api_result_error(result, TS_API_ERR_NO_PERMISSION, "Root permission required");
            return ESP_ERR_NOT_ALLOWED;
        }
        ts_api_result_reason(result, error == ESP_ERR_INVALID_ARG ? TS_API_ERR_INVALID_ARG : TS_API_ERR_CONNECTION,
            "proxy_connect_failed", esp_err_to_name(error));
        return ESP_OK;
    }
    if (!proxy_fetch_current((void *)(uintptr_t)session_id)) {
        free(reply.body); free(success);
        ts_api_result_error(result, TS_API_ERR_NO_PERMISSION, "Root permission required");
        return ESP_ERR_NOT_ALLOWED;
    }
    if (reply.status >= 200 && reply.status < 400) {
        cJSON *data = cJSON_CreateObject();
        cJSON *body = cJSON_Parse(reply.body);
        if (!body) body = cJSON_CreateString(reply.body ? reply.body : "");
        if (!data || !body || !cJSON_AddNumberToObject(data, "status", reply.status) ||
            !cJSON_AddNumberToObject(data, "content_length", (double)reply.content_length) ||
            !cJSON_AddItemToObjectCS(data, "body", body)) {
            cJSON_Delete(data); cJSON_Delete(body);
            ts_api_result_error(result, TS_API_ERR_NO_MEM, "result_unknown");
        } else {
            result->code = TS_API_OK; result->data = data;
            result->message = success; success = NULL;
        }
    } else proxy_http_error(result, reply.status, reply.body);
    free(reply.body); free(success);
    return ESP_OK;
}

/**
 * 递归收集 JSON 中的所有可选路径
 * @param json JSON 对象
 * @param prefix 当前路径前缀
 * @param paths_array 输出的路径数组
 * @param max_depth 最大递归深度（防止无限递归）
 */
static void collect_json_paths(cJSON *json, const char *prefix, cJSON *paths_array, int max_depth)
{
    if (!json || !paths_array || max_depth <= 0) return;

    char path_buf[256];

    if (cJSON_IsObject(json)) {
        cJSON *item;
        cJSON_ArrayForEach(item, json) {
            if (prefix && prefix[0]) {
                snprintf(path_buf, sizeof(path_buf), "%s.%s", prefix, item->string);
            } else {
                snprintf(path_buf, sizeof(path_buf), "%s", item->string);
            }

            // 添加当前路径和类型信息
            cJSON *path_info = cJSON_CreateObject();
            cJSON_AddStringToObject(path_info, "path", path_buf);

            if (cJSON_IsBool(item)) {
                cJSON_AddStringToObject(path_info, "type", "bool");
                cJSON_AddBoolToObject(path_info, "value", cJSON_IsTrue(item));
            } else if (cJSON_IsNumber(item)) {
                cJSON_AddStringToObject(path_info, "type", "number");
                cJSON_AddNumberToObject(path_info, "value", item->valuedouble);
            } else if (cJSON_IsString(item)) {
                cJSON_AddStringToObject(path_info, "type", "string");
                // 截断长字符串
                if (strlen(item->valuestring) > 50) {
                    char truncated[64];
                    snprintf(truncated, sizeof(truncated), "%.50s...", item->valuestring);
                    cJSON_AddStringToObject(path_info, "value", truncated);
                } else {
                    cJSON_AddStringToObject(path_info, "value", item->valuestring);
                }
            } else if (cJSON_IsArray(item)) {
                cJSON_AddStringToObject(path_info, "type", "array");
                cJSON_AddNumberToObject(path_info, "length", cJSON_GetArraySize(item));
            } else if (cJSON_IsObject(item)) {
                cJSON_AddStringToObject(path_info, "type", "object");
            } else {
                cJSON_AddStringToObject(path_info, "type", "null");
            }

            cJSON_AddItemToArray(paths_array, path_info);

            // 递归处理子对象和数组
            if (cJSON_IsObject(item) || cJSON_IsArray(item)) {
                collect_json_paths(item, path_buf, paths_array, max_depth - 1);
            }
        }
    } else if (cJSON_IsArray(json)) {
        // 对数组只处理第一个元素作为示例
        int arr_size = cJSON_GetArraySize(json);
        if (arr_size > 0) {
            cJSON *first = cJSON_GetArrayItem(json, 0);
            snprintf(path_buf, sizeof(path_buf), "%s[0]", prefix);

            // 添加数组元素路径
            cJSON *path_info = cJSON_CreateObject();
            cJSON_AddStringToObject(path_info, "path", path_buf);

            if (cJSON_IsBool(first)) {
                cJSON_AddStringToObject(path_info, "type", "bool");
            } else if (cJSON_IsNumber(first)) {
                cJSON_AddStringToObject(path_info, "type", "number");
                cJSON_AddNumberToObject(path_info, "value", first->valuedouble);
            } else if (cJSON_IsString(first)) {
                cJSON_AddStringToObject(path_info, "type", "string");
            } else if (cJSON_IsObject(first)) {
                cJSON_AddStringToObject(path_info, "type", "object");
            } else if (cJSON_IsArray(first)) {
                cJSON_AddStringToObject(path_info, "type", "array");
            }
            cJSON_AddNumberToObject(path_info, "array_size", arr_size);
            cJSON_AddItemToArray(paths_array, path_info);

            // 递归处理数组元素
            if (cJSON_IsObject(first) || cJSON_IsArray(first)) {
                collect_json_paths(first, path_buf, paths_array, max_depth - 1);
            }
        }
    }
}

/**
 * 从 JSON 中按路径提取子对象（用于 WebSocket 测试）
 * @param json JSON 对象
 * @param path 路径字符串 (如 "data.cpu.usage", "items[0].name")
 * @return 提取的 cJSON 对象（不复制），失败返回 NULL
 */
static cJSON *extract_json_value(cJSON *json, const char *path)
{
    if (!json || !path || !path[0]) return json;

    // 跳过开头的 "$."
    const char *p = path;
    if (p[0] == '$' && p[1] == '.') p += 2;

    cJSON *current = json;
    char token[64];

    while (*p && current) {
        if (*p == '.' || *p == '/') { p++; continue; }

        // 处理数组索引 [n]
        if (*p == '[') {
            p++;
            int index = 0;
            while (*p >= '0' && *p <= '9') index = index * 10 + (*p++ - '0');
            if (*p == ']') p++;
            if (cJSON_IsArray(current)) {
                current = cJSON_GetArrayItem(current, index);
            } else {
                return NULL;
            }
            continue;
        }

        // 提取字段名
        size_t i = 0;
        while (*p && *p != '.' && *p != '[' && *p != '/' && i < sizeof(token) - 1) {
            token[i++] = *p++;
        }
        token[i] = '\0';
        if (i == 0) continue;

        // 检查纯数字（数组索引简写）
        bool is_numeric = true;
        for (size_t j = 0; j < i; j++) {
            if (token[j] < '0' || token[j] > '9') { is_numeric = false; break; }
        }

        if (is_numeric && cJSON_IsArray(current)) {
            current = cJSON_GetArrayItem(current, atoi(token));
        } else {
            current = cJSON_GetObjectItem(current, token);
        }
    }

    return current;
}

/* A diagnostic sample is one complete WS message, not an SDK read fragment.
 * Keep frame offsets independent from RFC continuation frames. The 64 KiB cap
 * includes all continuation frames and is checked before allocating. */
#define PROXY_WS_SAMPLE_MAX (64U * 1024U)
typedef struct {
    char *data;
    size_t length, capacity, frame_length, frame_offset;
    uint8_t frame_opcode;
    bool started, frame_open, frame_fin;
} proxy_ws_sample_t;

/* 0: incomplete/control, 1: complete; failure returns a stable reason. */
static int proxy_ws_sample_append(proxy_ws_sample_t *sample,
                                  const esp_websocket_event_data_t *event,
                                  const char **reason)
{
    if (event->op_code >= 8) return 0;
    if (event->data_len < 0 || event->payload_len < 0 || event->payload_offset < 0 ||
        (event->data_len && !event->data_ptr)) goto invalid;
    size_t length=(size_t)event->data_len, offset=(size_t)event->payload_offset;
    size_t total=(size_t)event->payload_len;
    if (offset>total || length>total-offset) goto invalid;
    if (!offset) {
        if (sample->frame_open) goto invalid;
        if (event->op_code==1 || event->op_code==2) {
            if (sample->started) goto invalid;
            sample->length=0;sample->started=true;
        } else if (event->op_code!=0 || !sample->started) goto invalid;
        if (total>PROXY_WS_SAMPLE_MAX-sample->length) goto too_large;
        sample->frame_length=total;sample->frame_offset=0;
        sample->frame_opcode=event->op_code;sample->frame_fin=event->fin;
        sample->frame_open=true;
    } else if (!sample->frame_open || total!=sample->frame_length ||
               event->op_code!=sample->frame_opcode || event->fin!=sample->frame_fin) goto invalid;
    if (offset!=sample->frame_offset) goto invalid;
    if (length>PROXY_WS_SAMPLE_MAX-sample->length) goto too_large;
    size_t needed=sample->length+length+1;
    if (needed>sample->capacity) {
        size_t capacity=(needed+4095U)&~(size_t)4095U;
        if (capacity>PROXY_WS_SAMPLE_MAX+1U) capacity=PROXY_WS_SAMPLE_MAX+1U;
        char *next=heap_caps_realloc(sample->data,capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (!next) next=heap_caps_realloc(sample->data,capacity,MALLOC_CAP_8BIT);
        if (!next) { *reason="no_memory";return -1; }
        sample->data=next;sample->capacity=capacity;
    }
    if (length) memcpy(sample->data+sample->length,event->data_ptr,length);
    sample->length+=length;sample->frame_offset+=length;
    sample->data[sample->length]='\0';
    if (sample->frame_offset==sample->frame_length) {
        sample->frame_open=false;
        if (sample->frame_fin) {
            sample->started=false;
            if (memchr(sample->data,'\0',sample->length)) goto invalid;
            return 1;
        }
    }
    return 0;
too_large:
    *reason="ws_message_too_large";return -1;
invalid:
    *reason="ws_invalid_frame";return -1;
}

// WebSocket 测试状态
typedef struct {
    proxy_ws_sample_t sample;       // Complete bounded message
    bool failed;
    char *error;                    // 错误信息
    bool connected;                 // 已连接标志
    bool received;                  // 已收到数据标志
    SemaphoreHandle_t connect_sem;  // 连接完成信号量
    SemaphoreHandle_t data_sem;     // 数据接收信号量
} ws_test_ctx_t;

static void ws_test_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ws_test_ctx_t *ctx = (ws_test_ctx_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            TS_LOGI(TAG, "WS test: connected");
            ctx->connected = true;
            if (ctx->connect_sem) xSemaphoreGive(ctx->connect_sem);
            break;

        case WEBSOCKET_EVENT_CLOSED:
        case WEBSOCKET_EVENT_DISCONNECTED:
            TS_LOGI(TAG, "WS test: disconnected");
            ctx->connected = false;
            // 如果未收到数据就断开，释放信号量避免永久等待
            if (!ctx->received && ctx->data_sem) {
                xSemaphoreGive(ctx->data_sem);
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (!ctx->received && !ctx->failed) {
                const char *reason=NULL;
                int state=proxy_ws_sample_append(&ctx->sample,data,&reason);
                if (state<0) {
                    ctx->failed=true;ctx->error=strdup(reason);
                    if (ctx->data_sem) xSemaphoreGive(ctx->data_sem);
                } else if (state>0) {
                    ctx->received=true;
                    if (ctx->data_sem) xSemaphoreGive(ctx->data_sem);
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            TS_LOGE(TAG, "WS test: error");
            ctx->failed=true;
            if (!ctx->error) ctx->error = strdup("ws_connect_failed");
            // 释放所有信号量
            if (ctx->connect_sem) xSemaphoreGive(ctx->connect_sem);
            if (ctx->data_sem) xSemaphoreGive(ctx->data_sem);
            break;

        default:
            break;
    }
}

/**
 * @brief automation.proxy.websocket_test - Test WebSocket connection
 *
 * Connects to WebSocket, optionally sends subscribe message, waits for data.
 *
 * Parameters:
 *   - uri: WebSocket URI (ws://host:port/path or wss://...)
 *   - timeout_ms: Connection and data timeout (default 10000)
 *   - subscribe: Optional JSON message to send after connect (for subscription)
 *   - json_path: Optional JSON path to extract from received message
 *
 * Returns:
 *   - connected: Whether connection was established
 *   - message: Raw message or extracted value
 *   - raw: Full raw message (if json_path is specified)
 */
/* One operation budget across diagnostic connect, send, probe and sample waits.
 * SDK stop/destroy still joins the transport task before freeing callback state. */
static bool proxy_test_timeout(const cJSON *value, unsigned fallback, unsigned *out)
{
    if (!value) { *out = fallback; return true; }
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        value->valuedouble < 1 || value->valuedouble > 30000 ||
        value->valuedouble != (unsigned)value->valuedouble) return false;
    *out = (unsigned)value->valuedouble;
    return true;
}

static unsigned proxy_test_remaining_ms(int64_t deadline)
{
    int64_t remaining = deadline - esp_timer_get_time();
    return remaining > 0 ? (unsigned)(remaining / 1000) : 0;
}

static bool proxy_test_send(esp_websocket_client_handle_t client, const char *text,
                            int64_t deadline)
{
    unsigned remaining = proxy_test_remaining_ms(deadline);
    size_t length = strlen(text);
    return remaining && esp_websocket_client_send_text(client, text, length,
        pdMS_TO_TICKS(remaining)) == (int)length;
}

static esp_err_t api_automation_proxy_ws_test(const cJSON *params, ts_api_result_t *result)
{
    cJSON *uri_json = cJSON_GetObjectItem(params, "uri");
    cJSON *timeout_json = cJSON_GetObjectItem(params, "timeout_ms");
    cJSON *subscribe_json = cJSON_GetObjectItem(params, "subscribe");
    cJSON *json_path_json = cJSON_GetObjectItem(params, "json_path");

    if (!uri_json || !cJSON_IsString(uri_json)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'uri' parameter");
        return ESP_OK;
    }

    const char *uri = uri_json->valuestring;
    struct http_parser_url parsed;
    if (!ts_ws_url_parse(uri, &parsed)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid WebSocket URL");
        return ESP_OK;
    }
    unsigned timeout_ms;
    if (!proxy_test_timeout(timeout_json, 10000, &timeout_ms) ||
        (subscribe_json && !cJSON_IsNull(subscribe_json) && (!cJSON_IsString(subscribe_json) ||
            strnlen(subscribe_json->valuestring, 4097) > 4096)) ||
        (json_path_json && !cJSON_IsNull(json_path_json) && (!cJSON_IsString(json_path_json) ||
            strnlen(json_path_json->valuestring, 256) > 255))) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid diagnostic timeout or message");
        return ESP_OK;
    }
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    const char *subscribe_msg = (subscribe_json && cJSON_IsString(subscribe_json)) ? subscribe_json->valuestring : NULL;
    const char *json_path = (json_path_json && cJSON_IsString(json_path_json)) ? json_path_json->valuestring : NULL;

    TS_LOGI(TAG, "WS test timeout=%u", timeout_ms);

    ws_test_ctx_t ctx = {
        .error = NULL,
        .connected = false,
        .received = false,
        .connect_sem = xSemaphoreCreateBinary(),
        .data_sem = xSemaphoreCreateBinary(),
    };

    if (!ctx.connect_sem || !ctx.data_sem) {
        if (ctx.connect_sem) vSemaphoreDelete(ctx.connect_sem);
        if (ctx.data_sem) vSemaphoreDelete(ctx.data_sem);
        result->code = TS_API_ERR_NO_MEM;
        result->message = strdup("no_memory");
        return ESP_OK;
    }

    cJSON *data = cJSON_CreateObject();
    if (!data) {
        vSemaphoreDelete(ctx.connect_sem);
        vSemaphoreDelete(ctx.data_sem);
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "no_memory");
        return ESP_OK;
    }
    result->data = data;

    ts_api_ws_test_transport_t transport;
    esp_err_t setup = ts_api_ws_test_transport_init(&transport, uri, deadline);
    if (setup != ESP_OK) {
        vSemaphoreDelete(ctx.connect_sem);
        vSemaphoreDelete(ctx.data_sem);
        ts_api_result_reason(result, setup == ESP_ERR_NO_MEM ? TS_API_ERR_NO_MEM : TS_API_ERR_CONNECTION,
            "proxy_init_failed", esp_err_to_name(setup));
        return ESP_OK;
    }

    // 配置 WebSocket 客户端
    esp_websocket_client_config_t ws_cfg = {
        .uri = uri,
        .port = transport.port,
        .ext_transport = transport.websocket,
        .buffer_size = 4096,
        .network_timeout_ms = timeout_ms,
        .disable_auto_reconnect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_cfg);
    if (!client) {
        ts_api_ws_test_transport_destroy(&transport);
        vSemaphoreDelete(ctx.connect_sem);
        vSemaphoreDelete(ctx.data_sem);
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("proxy_init_failed");
        return ESP_OK;
    }

    esp_err_t err = esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ws_test_handler, &ctx);

    // 启动连接
    if (err == ESP_OK) err = esp_websocket_client_start(client);
    if (err != ESP_OK) {
        esp_websocket_client_destroy(client);
        ts_api_ws_test_transport_destroy(&transport);
        vSemaphoreDelete(ctx.connect_sem);
        vSemaphoreDelete(ctx.data_sem);
        ts_api_result_reason(result, TS_API_ERR_CONNECTION, "ws_start_failed", esp_err_to_name(err));
        return ESP_OK;
    }

    // 等待连接建立
    bool connect_ok = proxy_test_remaining_ms(deadline) && (xSemaphoreTake(ctx.connect_sem, pdMS_TO_TICKS(proxy_test_remaining_ms(deadline))) == pdTRUE) && ctx.connected && !ctx.failed && proxy_test_remaining_ms(deadline);

    cJSON_AddBoolToObject(data, "connected", connect_ok);

    if (!connect_ok) {
        esp_websocket_client_stop(client);
        esp_websocket_client_destroy(client);
        ts_api_ws_test_transport_destroy(&transport);
        // 连接失败
        result->code = TS_API_ERR_INTERNAL;
        if (ctx.error) {
            result->message = ctx.error;
            ctx.error = NULL;
        } else {
            result->message = strdup("ws_connect_timeout");
        }
        cJSON_AddStringToObject(data, "error", result->message);
        result->data = data;

        vSemaphoreDelete(ctx.connect_sem);
        vSemaphoreDelete(ctx.data_sem);
        free(ctx.sample.data);
        return ESP_OK;
    }

    // 连接成功，发送订阅消息（如果有）
    bool subscribe_ok = true;
    if (subscribe_msg) {
        TS_LOGI(TAG, "WS test: sending subscribe message");
        subscribe_ok = proxy_test_send(client, subscribe_msg, deadline);
    }

    // 等待数据消息
    bool data_ok = subscribe_ok && proxy_test_remaining_ms(deadline) && (xSemaphoreTake(ctx.data_sem,
        pdMS_TO_TICKS(proxy_test_remaining_ms(deadline))) == pdTRUE) &&
        ctx.received && !ctx.failed && ctx.sample.data && proxy_test_remaining_ms(deadline);

    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    ts_api_ws_test_transport_destroy(&transport);
    if (!subscribe_ok && !ctx.error) ctx.error = strdup("ws_send_failed");

    if (data_ok && ctx.sample.data) {
        TS_LOGI(TAG, "WS test: message received, len=%d", (int)strlen(ctx.sample.data));

        // 尝试解析为 JSON
        cJSON *msg_json = cJSON_Parse(ctx.sample.data);

        if (msg_json) {
            // 如果指定了 json_path，提取值
            if (json_path && strlen(json_path) > 0) {
                cJSON *extracted = extract_json_value(msg_json, json_path);
                if (extracted) {
                    cJSON_AddItemToObject(data, "value", cJSON_Duplicate(extracted, true));
                    cJSON_AddStringToObject(data, "path", json_path);
                } else {
                    cJSON_AddNullToObject(data, "value");
                    cJSON_AddStringToObject(data, "path_error", "Path not found in response");
                }
                // 同时返回完整消息
                cJSON_AddItemToObject(data, "message", msg_json);
            } else {
                cJSON_AddItemToObject(data, "message", msg_json);
            }
        } else {
            // 非 JSON 消息，返回原始文本
            cJSON_AddStringToObject(data, "message", ctx.sample.data);
        }

        result->code = TS_API_OK;
        result->message = strdup("WebSocket test successful");

    } else {
        // 未收到数据
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup(ctx.error ? ctx.error : "ws_no_data");
        cJSON_AddStringToObject(data, "error", ctx.error ? ctx.error : "No data received within timeout");
    }

    result->data = data;

    // Callback state is stable after the SDK task has joined.
    free(ctx.sample.data);
    vSemaphoreDelete(ctx.connect_sem);
    vSemaphoreDelete(ctx.data_sem);
    if (ctx.error) free(ctx.error);

    return ESP_OK;
}

/*===========================================================================*/
/*                        Socket.IO Client Support                            */
/*===========================================================================*/

// Socket.IO 测试上下文
typedef struct {
    char *sid;                      // Session ID
    char *message;                  // 接收到的事件数据
    char *event_name;               // 接收到的事件名
    char *error;                    // 错误信息
    bool connected;                 // WebSocket 已连接
    bool upgraded;                  // 已完成协议升级
    bool received;                  // 已收到事件数据
    SemaphoreHandle_t connect_sem;  // 连接完成信号量
    SemaphoreHandle_t upgrade_sem;  // 升级完成信号量
    SemaphoreHandle_t data_sem;     // 数据接收信号量
    const char *target_event;       // 目标事件名（可选过滤）
    proxy_ws_sample_t sample;       // Complete bounded WS message
    bool failed;
} sio_test_ctx_t;

/**
 * @brief 解析 Socket.IO 消息
 *
 * Socket.IO Engine.IO 协议消息类型：
 * - 0: open (包含 sid)
 * - 2: ping
 * - 3: pong / probe response
 * - 4: message (Socket.IO 层)
 *   - 40: connect
 *   - 42: event ["event_name", data]
 *   - 43: ack
 */
/**
 * @brief 尝试解析完整的 42 事件消息
 */
static bool sio_try_parse_event(sio_test_ctx_t *ctx, const char *data, size_t len)
{
    if (!data || len<3) return false;
    // 查找 JSON 数组开始
    const char *json_start = NULL;
    for (size_t i = 0; i < len - 1; i++) {
        if (data[i] == '[') {
            json_start = &data[i];
            break;
        }
    }
    if (!json_start) return false;

    cJSON *arr = cJSON_Parse(json_start);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        return false;  // JSON 不完整，继续等待分片
    }

    cJSON *event = cJSON_GetArrayItem(arr, 0);
    cJSON *payload = cJSON_GetArrayItem(arr, 1);

    if (event && cJSON_IsString(event)) {
        const char *event_str = event->valuestring;

        // 检查是否是目标事件
        bool match = (!ctx->target_event || strlen(ctx->target_event) == 0 ||
                      strcmp(ctx->target_event, event_str) == 0);

        if (match && !ctx->received) {
            TS_LOGI(TAG, "Socket.IO event matched");

            char *name=strdup(event_str);
            char *message=payload ? cJSON_PrintUnformatted(payload) : strdup("null");
            if (!name || !message) {
                free(name);free(message);ctx->failed=true;
                if (!ctx->error) ctx->error=strdup("no_memory");
                if (ctx->data_sem) xSemaphoreGive(ctx->data_sem);
                cJSON_Delete(arr);return false;
            }
            free(ctx->event_name);free(ctx->message);
            ctx->event_name=name;ctx->message=message;
            ctx->received = true;
            if (ctx->data_sem) xSemaphoreGive(ctx->data_sem);
        }
    }

    cJSON_Delete(arr);
    return true;  // 解析成功
}

static void sio_parse_message(sio_test_ctx_t *ctx, const char *data, int len)
{
    if (!data || len < 1) return;

    char type = data[0];

    switch (type) {
        case '0': {
            // Engine.IO open - 解析 sid
            const char *json_start = strchr(data, '{');
            if (json_start) {
                cJSON *json = cJSON_Parse(json_start);
                if (json) {
                    cJSON *sid = cJSON_GetObjectItem(json, "sid");
                    if (sid && cJSON_IsString(sid)) {
                        if (ctx->sid) free(ctx->sid);
                        ctx->sid = strdup(sid->valuestring);
                        TS_LOGI(TAG, "Socket.IO session received");
                    }
                    cJSON_Delete(json);
                }
            }
            break;
        }

        case '3':
            // pong 或 probe response
            if (len >= 6 && strncmp(data, "3probe", 6) == 0) {
                TS_LOGI(TAG, "Socket.IO probe response received");
                ctx->upgraded = true;
                if (ctx->upgrade_sem) xSemaphoreGive(ctx->upgrade_sem);
            }
            break;

        case '4': {
            // Socket.IO message
            if (len < 2) break;
            char sio_type = data[1];

            if (sio_type == '0') {
                // Socket.IO connect acknowledgment
                TS_LOGI(TAG, "Socket.IO connected");
            }
            else if (sio_type == '2') {
                // Socket.IO event: 42["event_name", data]
                // SDK chunks and RFC continuations are assembled by the caller.
                sio_try_parse_event(ctx, data, (size_t)len);
            }
            break;
        }

        default:
            TS_LOGD(TAG, "Socket.IO unknown message type");
            break;
    }
}

static void sio_ws_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    sio_test_ctx_t *ctx = (sio_test_ctx_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            TS_LOGI(TAG, "Socket.IO WS connected");
            ctx->connected = true;
            if (ctx->connect_sem) xSemaphoreGive(ctx->connect_sem);
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            TS_LOGI(TAG, "Socket.IO WS disconnected");
            ctx->connected = false;
            // 释放所有等待的信号量
            if (!ctx->upgraded && ctx->upgrade_sem) xSemaphoreGive(ctx->upgrade_sem);
            if (!ctx->received && ctx->data_sem) xSemaphoreGive(ctx->data_sem);
            break;

        case WEBSOCKET_EVENT_DATA:
            if (!ctx->received && !ctx->failed) {
                const char *reason=NULL;
                int state=proxy_ws_sample_append(&ctx->sample,data,&reason);
                if (state<0) {
                    ctx->failed=true;ctx->error=strdup(reason);
                    if (ctx->upgrade_sem) xSemaphoreGive(ctx->upgrade_sem);
                    if (ctx->data_sem) xSemaphoreGive(ctx->data_sem);
                } else if (state>0) {
                    TS_LOGI(TAG, "Socket.IO received %zu bytes", ctx->sample.length);
                    sio_parse_message(ctx,ctx->sample.data,(int)ctx->sample.length);
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            TS_LOGE(TAG, "Socket.IO WS error");
            ctx->failed=true;
            if (!ctx->error) ctx->error = strdup("Socket.IO WebSocket error");
            if (ctx->connect_sem) xSemaphoreGive(ctx->connect_sem);
            if (ctx->upgrade_sem) xSemaphoreGive(ctx->upgrade_sem);
            if (ctx->data_sem) xSemaphoreGive(ctx->data_sem);
            break;

        default:
            break;
    }
}

/**
 * @brief automation.proxy.socketio_test - Test Socket.IO connection
 *
 * Connects to a Socket.IO server (v4 protocol), waits for specified event.
 *
 * Parameters:
 *   - url: Base URL (http://host:port)
 *   - event: Event name to listen for (default: any)
 *   - timeout_ms: Timeout in milliseconds (default: 15000)
 *   - json_path: Optional JSON path to extract from event data
 *
 * Returns:
 *   - connected: Whether connection was established
 *   - event: Event name received
 *   - data: Event payload (or extracted value if json_path specified)
 *
 * Example:
 *   {"url": "http://10.10.99.99:59090", "event": "lpmu_status_update", "json_path": "cpu.avg_usage"}
 */
static esp_err_t api_automation_proxy_socketio_test(const cJSON *params, ts_api_result_t *result)
{
    cJSON *url_json = cJSON_GetObjectItem(params, "url");
    cJSON *event_json = cJSON_GetObjectItem(params, "event");
    cJSON *timeout_json = cJSON_GetObjectItem(params, "timeout_ms");
    cJSON *json_path_json = cJSON_GetObjectItem(params, "json_path");

    if (!url_json || !cJSON_IsString(url_json)) {
        result->code = TS_API_ERR_INVALID_ARG;
        result->message = strdup("Missing 'url' parameter");
        return ESP_OK;
    }

    const char *base_url = url_json->valuestring;
    const char *target_event = (event_json && cJSON_IsString(event_json)) ? event_json->valuestring : NULL;
    unsigned timeout_ms;
    if (!proxy_test_timeout(timeout_json, 15000, &timeout_ms) ||
        (event_json && !cJSON_IsNull(event_json) && (!cJSON_IsString(event_json) ||
            strnlen(event_json->valuestring, 256) > 255)) ||
        (json_path_json && !cJSON_IsNull(json_path_json) && (!cJSON_IsString(json_path_json) ||
            strnlen(json_path_json->valuestring, 256) > 255))) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid diagnostic timeout or event");
        return ESP_OK;
    }
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    const char *json_path = (json_path_json && cJSON_IsString(json_path_json)) ? json_path_json->valuestring : NULL;

    TS_LOGI(TAG, "Socket.IO test timeout=%u", timeout_ms);

    // Polling and upgrade use exactly the same validated base and path.
    char polling_url[512];
    if (!ts_source_sio_url(base_url, NULL, polling_url, sizeof(polling_url))) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid Socket.IO URL");
        return ESP_OK;
    }

    ts_api_ws_test_transport_t transport = {0};

    // 初始化上下文
    sio_test_ctx_t ctx = {
        .sid = NULL,
        .message = NULL,
        .event_name = NULL,
        .error = NULL,
        .connected = false,
        .upgraded = false,
        .received = false,
        .connect_sem = xSemaphoreCreateBinary(),
        .upgrade_sem = xSemaphoreCreateBinary(),
        .data_sem = xSemaphoreCreateBinary(),
        .target_event = target_event,
    };

    if (!ctx.connect_sem || !ctx.upgrade_sem || !ctx.data_sem) {
        if (ctx.connect_sem) vSemaphoreDelete(ctx.connect_sem);
        if (ctx.upgrade_sem) vSemaphoreDelete(ctx.upgrade_sem);
        if (ctx.data_sem) vSemaphoreDelete(ctx.data_sem);
        result->code = TS_API_ERR_NO_MEM;
        result->message = strdup("no_memory");
        return ESP_OK;
    }

    cJSON *result_data = cJSON_CreateObject();
    if (!result_data) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "no_memory");
        goto cleanup;
    }
    result->data = result_data;

    unsigned remaining = proxy_test_remaining_ms(deadline);
    if (!remaining) {
        ts_api_result_error(result, TS_API_ERR_TIMEOUT, "sio_event_timeout");
        goto cleanup;
    }
    ts_http_fetch_config_t polling_request = {.url = polling_url, .method = HTTP_METHOD_GET,
        .timeout_ms = remaining, .maximum_body = 2047};
    ts_http_fetch_result_t polling = {0};
    esp_err_t err = ts_http_fetch(&polling_request, &polling);
    if (err != ESP_OK || polling.status != 200) {
        result->code = err == ESP_ERR_NO_MEM ? TS_API_ERR_NO_MEM : TS_API_ERR_CONNECTION;
        result->message = strdup(err == ESP_ERR_NO_MEM ? "proxy_init_failed" : "proxy_connect_failed");
        cJSON_AddBoolToObject(result_data, "connected", false);
        cJSON_AddStringToObject(result_data, "error", "HTTP polling failed");
        result->data = result_data;
        free(polling.body);
        goto cleanup;
    }
    const char *json_start = polling.body ? strchr(polling.body, '{') : NULL;
    cJSON *poll_json = json_start ? cJSON_Parse(json_start) : NULL;
    const cJSON *sid = poll_json ? cJSON_GetObjectItemCaseSensitive(poll_json, "sid") : NULL;
    if (cJSON_IsString(sid) && sid->valuestring[0]) ctx.sid = strdup(sid->valuestring);
    cJSON_Delete(poll_json);
    free(polling.body);
    if (!ctx.sid) {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("sio_no_session");
        cJSON_AddBoolToObject(result_data, "connected", false);
        cJSON_AddStringToObject(result_data, "error", "No session ID received");
        result->data = result_data;
        goto cleanup;
    }

    remaining = proxy_test_remaining_ms(deadline);
    if (!remaining) {
        ts_api_result_error(result, TS_API_ERR_TIMEOUT, "sio_event_timeout");
        goto cleanup;
    }
    // ========== Step 2: WebSocket 连接 ==========
    char ws_url[512];
    if (!ts_source_sio_url(base_url, ctx.sid, ws_url, sizeof(ws_url))) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid Socket.IO session or target");
        goto cleanup;
    }

    TS_LOGI(TAG, "Socket.IO WS connection prepared");

    esp_err_t setup = ts_api_ws_test_transport_init(&transport, ws_url, deadline);
    if (setup != ESP_OK) {
        result->code = setup == ESP_ERR_NO_MEM ? TS_API_ERR_NO_MEM : TS_API_ERR_CONNECTION;
        result->message = strdup("proxy_init_failed");
        cJSON_AddBoolToObject(result_data, "connected", false);
        goto cleanup;
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = ws_url,
        .port = transport.port,
        .ext_transport = transport.websocket,
        .buffer_size = 4096,
        .network_timeout_ms = remaining,
        .disable_auto_reconnect = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_websocket_client_handle_t ws_client = esp_websocket_client_init(&ws_cfg);
    if (!ws_client) {
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("proxy_init_failed");
        cJSON_AddBoolToObject(result_data, "connected", false);
        result->data = result_data;
        goto cleanup;
    }

    err = esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, sio_ws_handler, &ctx);

    if (err == ESP_OK) err = esp_websocket_client_start(ws_client);
    if (err != ESP_OK) {
        esp_websocket_client_destroy(ws_client);
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("ws_start_failed");
        cJSON_AddBoolToObject(result_data, "connected", false);
        result->data = result_data;
        goto cleanup;
    }

    // 等待 WebSocket 连接
    bool ws_connected = proxy_test_remaining_ms(deadline) && (xSemaphoreTake(ctx.connect_sem, pdMS_TO_TICKS(proxy_test_remaining_ms(deadline))) == pdTRUE) && ctx.connected && !ctx.failed && proxy_test_remaining_ms(deadline);

    if (!ws_connected) {
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
        result->code = TS_API_ERR_INTERNAL;
        result->message = strdup("ws_connect_timeout");
        cJSON_AddBoolToObject(result_data, "connected", false);
        cJSON_AddStringToObject(result_data, "error", "WebSocket connection failed");
        result->data = result_data;
        goto cleanup;
    }

    // ========== Step 3: 发送升级探针 ==========
    TS_LOGI(TAG, "Socket.IO sending probe");
    if (!proxy_test_send(ws_client, "2probe", deadline)) {
        ts_api_result_error(result, TS_API_ERR_CONNECTION, "ws_send_failed");
        goto cleanup_ws;
    }

    // 等待 probe 响应
    bool probe_ok = proxy_test_remaining_ms(deadline) && (xSemaphoreTake(ctx.upgrade_sem, pdMS_TO_TICKS(proxy_test_remaining_ms(deadline) < 5000 ? proxy_test_remaining_ms(deadline) : 5000)) == pdTRUE) && ctx.upgraded && proxy_test_remaining_ms(deadline);

    if (!probe_ok) {
        TS_LOGW(TAG, "Socket.IO probe timeout, continuing anyway");
        // 某些服务器可能不需要 probe，继续尝试
    }

    // ========== Step 4: 发送升级确认 ==========
    TS_LOGI(TAG, "Socket.IO sending upgrade");
    if (!proxy_test_send(ws_client, "5", deadline)) {
        ts_api_result_error(result, TS_API_ERR_CONNECTION, "ws_send_failed");
        goto cleanup_ws;
    }

    // 短暂等待让服务器处理升级
    vTaskDelay(pdMS_TO_TICKS(proxy_test_remaining_ms(deadline) < 100 ? proxy_test_remaining_ms(deadline) : 100));

    // ========== Step 5: 发送 Socket.IO CONNECT 到默认 namespace ==========
    // Socket.IO 协议要求发送 40 (Engine.IO message + Socket.IO CONNECT) 来连接 namespace
    TS_LOGI(TAG, "Socket.IO sending CONNECT to default namespace");
    if (!proxy_test_send(ws_client, "40", deadline)) {
        ts_api_result_error(result, TS_API_ERR_CONNECTION, "ws_send_failed");
        goto cleanup_ws;
    }

    // 短暂等待连接确认
    vTaskDelay(pdMS_TO_TICKS(proxy_test_remaining_ms(deadline) < 200 ? proxy_test_remaining_ms(deadline) : 200));

    // ========== Step 6: 等待事件数据 ==========
    TS_LOGI(TAG, "Socket.IO waiting for event data...");
    bool data_ok = proxy_test_remaining_ms(deadline) && (xSemaphoreTake(ctx.data_sem, pdMS_TO_TICKS(proxy_test_remaining_ms(deadline))) == pdTRUE) && ctx.received && !ctx.failed && proxy_test_remaining_ms(deadline);

    esp_websocket_client_stop(ws_client);
    esp_websocket_client_destroy(ws_client);
    ws_client = NULL;

    cJSON_AddBoolToObject(result_data, "connected", true);
    cJSON_AddStringToObject(result_data, "sid", ctx.sid);

    if (data_ok && ctx.message) {
        TS_LOGI(TAG, "Socket.IO event received");

        if (ctx.event_name) {
            cJSON_AddStringToObject(result_data, "event", ctx.event_name);
        }

        // 解析数据
        cJSON *event_data = cJSON_Parse(ctx.message);
        if (event_data) {
            // 收集所有可用的 JSON 路径供 WebUI 展示
            cJSON *available_paths = cJSON_CreateArray();
            collect_json_paths(event_data, "", available_paths, 5);  // 最大深度 5
            cJSON_AddItemToObject(result_data, "available_paths", available_paths);

            // 如果指定了 json_path，提取值
            if (json_path && strlen(json_path) > 0) {
                cJSON *extracted = extract_json_value(event_data, json_path);
                if (extracted) {
                    cJSON_AddItemToObject(result_data, "value", cJSON_Duplicate(extracted, true));
                    cJSON_AddStringToObject(result_data, "path", json_path);
                } else {
                    cJSON_AddNullToObject(result_data, "value");
                    cJSON_AddStringToObject(result_data, "path_error", "Path not found");
                }
                // 同时返回完整数据
                cJSON_AddItemToObject(result_data, "data", event_data);
            } else {
                cJSON_AddItemToObject(result_data, "data", event_data);
            }
        } else {
            // 非 JSON 数据，返回原始字符串
            cJSON_AddStringToObject(result_data, "data", ctx.message);
        }

        result->code = TS_API_OK;
        result->message = strdup("Socket.IO test successful");
    } else {
        result->code = ctx.failed ? TS_API_ERR_INTERNAL : TS_API_ERR_TIMEOUT;
        result->message = strdup(ctx.error ? ctx.error : "sio_event_timeout");
        cJSON_AddStringToObject(result_data, "error",
            ctx.error ? ctx.error : (target_event ? "Target event not received" : "No event received"));
    }

    result->data = result_data;

cleanup_ws:
    if (ws_client) {
        esp_websocket_client_stop(ws_client);
        esp_websocket_client_destroy(ws_client);
    }

cleanup:
    ts_api_ws_test_transport_destroy(&transport);
    if (ctx.sid) free(ctx.sid);
    if (ctx.message) free(ctx.message);
    if (ctx.event_name) free(ctx.event_name);
    if (ctx.error) free(ctx.error);
    free(ctx.sample.data);
    if (ctx.connect_sem) vSemaphoreDelete(ctx.connect_sem);
    if (ctx.upgrade_sem) vSemaphoreDelete(ctx.upgrade_sem);
    if (ctx.data_sem) vSemaphoreDelete(ctx.data_sem);

    return ESP_OK;
}

/*===========================================================================*/
/*                    Source Export/Import APIs                               */
/*===========================================================================*/

/**
 * @brief Serialize source to JSON for export
 */
static cJSON *source_to_export_json(const ts_auto_source_t *source)
{
    char *encoded = ts_source_config_encode(source);
    cJSON *object = encoded ? cJSON_Parse(encoded) : NULL;
    free(encoded);
    return object;
}

/**
 * @brief automation.sources.export - Export data source as config pack
 */
static esp_err_t api_automation_sources_export(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_OK;
    }

    const cJSON *id = cJSON_GetObjectItem(params, "id");
    if (!id || !cJSON_IsString(id) || !id->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'id' parameter");
        return ESP_OK;
    }

    if (!ts_config_pack_can_export()) {
        ts_api_result_reason(result, TS_API_ERR_NO_PERMISSION, "pack_not_allowed", NULL);
        return ESP_OK;
    }

    // Get source
    ts_auto_source_t source_copy;
    if (ts_source_get_copy(id->valuestring, &source_copy) != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Source not found");
        return ESP_OK;
    }

    // Build export JSON
    cJSON *source_json = source_to_export_json(&source_copy);
    if (!source_json) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to serialize source");
        return ESP_ERR_NO_MEM;
    }
    cJSON *export_json = cJSON_CreateObject();
    cJSON_AddStringToObject(export_json, "type", "automation_source");
    cJSON_AddItemToObject(export_json, "source", source_json);

    char *json_str = cJSON_PrintUnformatted(export_json);
    cJSON_Delete(export_json);
    if (!json_str) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "no_memory");   /* the serialized text could not be built */
        return ESP_OK;
    }

    // Get recipient certificate
    const cJSON *recipient_cert = cJSON_GetObjectItem(params, "recipient_cert");
    char *cert_pem = NULL;
    size_t cert_len = 0;
    bool free_cert = false;

    if (recipient_cert && cJSON_IsString(recipient_cert) && recipient_cert->valuestring[0]) {
        cert_pem = recipient_cert->valuestring;
        cert_len = strlen(cert_pem);
    } else {
        cert_pem = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        if (!cert_pem) {
            cJSON_free(json_str);
            ts_api_result_error(result, TS_API_ERR_NO_MEM, "no_memory");
            return ESP_OK;
        }
        cert_len = 4096;
        esp_err_t ret = ts_cert_get_certificate(cert_pem, &cert_len);
        if (ret != ESP_OK) {
            free(cert_pem);
            cJSON_free(json_str);
            ts_api_result_reason(result, TS_API_ERR_INTERNAL, "device_cert_unavailable", esp_err_to_name(ret));
            return ESP_OK;
        }
        free_cert = true;
    }

    // Create encrypted config pack
    ts_config_pack_export_opts_t opts = {
        .recipient_cert_pem = cert_pem,
        .recipient_cert_len = cert_len,
        .description = "Automation data source configuration"
    };

    char *tscfg_output = NULL;
    size_t tscfg_len = 0;
    ts_config_pack_result_t pack_result = ts_config_pack_create(
        source_copy.id, json_str, strlen(json_str), &opts, &tscfg_output, &tscfg_len);

    cJSON_free(json_str);
    if (free_cert) free(cert_pem);

    if (pack_result != TS_CONFIG_PACK_OK) {
        pack_failure(result, pack_result);
        return ESP_OK;
    }

    // Return result
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "tscfg", tscfg_output);
    char filename[80];
    snprintf(filename, sizeof(filename), "source_%s.tscfg", source_copy.id);
    cJSON_AddStringToObject(data, "filename", filename);

    free(tscfg_output);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief automation.sources.import - Import data source from config pack
 */
static esp_err_t api_automation_sources_import(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_pack_import(TS_AUTO_CONFIG_SOURCE,params,result); }

/*===========================================================================*/
/*                    Rule Export/Import APIs                                 */
/*===========================================================================*/

/**
 * @brief Serialize rule to JSON for export
 */

/**
 * @brief automation.rules.export - Export rule as config pack
 */
static esp_err_t api_automation_rules_export(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_OK;
    }

    const cJSON *id = cJSON_GetObjectItem(params, "id");
    if (!id || !cJSON_IsString(id) || !id->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'id' parameter");
        return ESP_OK;
    }

    if (!ts_config_pack_can_export()) {
        ts_api_result_reason(result, TS_API_ERR_NO_PERMISSION, "pack_not_allowed", NULL);
        return ESP_OK;
    }

    ts_auto_rule_t snapshot;
    if (ts_rule_definition_get(id->valuestring, &snapshot) != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Rule not found");
        return ESP_OK;
    }

    char rule_id[TS_AUTO_NAME_MAX_LEN];
    strcpy(rule_id, snapshot.id);
    // Build export JSON
    cJSON *export_json = cJSON_CreateObject();
    cJSON_AddStringToObject(export_json, "type", "automation_rule");
    cJSON *encoded = ts_rule_encode(&snapshot);
    ts_rule_dispose(&snapshot);
    if (!encoded || !export_json || !cJSON_AddItemToObject(export_json, "rule", encoded)) {
        cJSON_Delete(encoded);
        cJSON_Delete(export_json);
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "no_memory");
        return ESP_OK;
    }

    char *json_str = cJSON_PrintUnformatted(export_json);
    cJSON_Delete(export_json);
    if (!json_str) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "no_memory");   /* the serialized text could not be built */
        return ESP_OK;
    }

    // Get recipient certificate
    const cJSON *recipient_cert = cJSON_GetObjectItem(params, "recipient_cert");
    char *cert_pem = NULL;
    size_t cert_len = 0;
    bool free_cert = false;

    if (recipient_cert && cJSON_IsString(recipient_cert) && recipient_cert->valuestring[0]) {
        cert_pem = recipient_cert->valuestring;
        cert_len = strlen(cert_pem);
    } else {
        cert_pem = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        if (!cert_pem) {
            cJSON_free(json_str);
            ts_api_result_error(result, TS_API_ERR_NO_MEM, "no_memory");
            return ESP_OK;
        }
        cert_len = 4096;
        esp_err_t ret = ts_cert_get_certificate(cert_pem, &cert_len);
        if (ret != ESP_OK) {
            free(cert_pem);
            cJSON_free(json_str);
            ts_api_result_reason(result, TS_API_ERR_INTERNAL, "device_cert_unavailable", esp_err_to_name(ret));
            return ESP_OK;
        }
        free_cert = true;
    }

    // Create encrypted config pack
    ts_config_pack_export_opts_t opts = {
        .recipient_cert_pem = cert_pem,
        .recipient_cert_len = cert_len,
        .description = "Automation rule configuration"
    };

    char *tscfg_output = NULL;
    size_t tscfg_len = 0;
    ts_config_pack_result_t pack_result = ts_config_pack_create(rule_id, json_str, strlen(json_str),
                                                                &opts, &tscfg_output, &tscfg_len);

    cJSON_free(json_str);
    if (free_cert) free(cert_pem);

    if (pack_result != TS_CONFIG_PACK_OK) {
        pack_failure(result, pack_result);
        return ESP_OK;
    }

    // Return result
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "tscfg", tscfg_output);
    char filename[80];
    snprintf(filename, sizeof(filename), "rule_%s.tscfg", rule_id);
    cJSON_AddStringToObject(data, "filename", filename);

    free(tscfg_output);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief automation.rules.import - Import rule from config pack
 */
static esp_err_t api_automation_rules_import(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *bytes=cJSON_GetObjectItemCaseSensitive(params,"tscfg");
    bool preview=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params,"preview"));
    const cJSON *revision=cJSON_GetObjectItemCaseSensitive(params,"expected_revision"),
                *generation=cJSON_GetObjectItemCaseSensitive(params,"expected_generation"),
                *credential=cJSON_GetObjectItemCaseSensitive(params,"credential_generation"),
                *digest=cJSON_GetObjectItemCaseSensitive(params,"package_digest");
    if(!cJSON_IsString(bytes)||!bytes->valuestring[0]) {
        ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"invalid_pack");return ESP_OK;
    }
    if(!preview&&(!cJSON_IsNumber(revision)||!cJSON_IsNumber(generation)||!cJSON_IsNumber(credential)||
        !cJSON_IsString(digest)||!isfinite(revision->valuedouble)||!isfinite(generation->valuedouble)||
        !isfinite(credential->valuedouble)||credential->valuedouble<0||credential->valuedouble>UINT32_MAX||
        credential->valuedouble!=(uint32_t)credential->valuedouble||
        revision->valuedouble<0||revision->valuedouble>UINT32_MAX||
        generation->valuedouble<0||generation->valuedouble>UINT32_MAX||
        revision->valuedouble!=(uint32_t)revision->valuedouble||
        generation->valuedouble!=(uint32_t)generation->valuedouble)) {
        ts_api_result_error(result,TS_API_ERR_INVALID_ARG,"preview_required");return ESP_OK;
    }
    cJSON *data=NULL;const char *reason=NULL;
    esp_err_t ret=ts_rule_import_pack(bytes->valuestring,strlen(bytes->valuestring),preview,
        cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(params,"overwrite")),
        preview?0:(uint32_t)revision->valuedouble,preview?0:(uint32_t)generation->valuedouble,
        preview?0:(uint32_t)credential->valuedouble,preview?NULL:digest->valuestring,
        cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(params,"active_version")),&data,&reason);
    if(ret==ESP_OK)ts_api_result_ok(result,data);
    else {
        ts_api_result_error(result,ret==ESP_ERR_NO_MEM?TS_API_ERR_NO_MEM:TS_API_ERR_INVALID_ARG,
                            reason?reason:"invalid_pack");
        result->data=data;
    }
    return ESP_OK;
}

/*===========================================================================*/
/*                    Action Template Export/Import APIs                      */
/*===========================================================================*/

/**
 * @brief Serialize action template to JSON for export
 */
static cJSON *action_template_to_export_json(const ts_action_template_t *tpl)
{
    char *encoded = ts_action_template_encode(tpl);
    cJSON *object = encoded ? cJSON_Parse(encoded) : NULL;
    free(encoded);
    return object;
}

/**
 * @brief automation.actions.export - Export action template as config pack
 */
static esp_err_t api_automation_actions_export(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_OK;
    }

    const cJSON *id = cJSON_GetObjectItem(params, "id");
    if (!id || !cJSON_IsString(id) || !id->valuestring[0]) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'id' parameter");
        return ESP_OK;
    }

    if (!ts_config_pack_can_export()) {
        ts_api_result_reason(result, TS_API_ERR_NO_PERMISSION, "pack_not_allowed", NULL);
        return ESP_OK;
    }

    ts_action_template_t tpl;
    esp_err_t ret = ts_action_template_get(id->valuestring, &tpl);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Action template not found");
        return ESP_OK;
    }

    // Build export JSON
    cJSON *template_json = action_template_to_export_json(&tpl);
    if (!template_json) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to serialize action template");
        return ESP_ERR_NO_MEM;
    }
    cJSON *export_json = cJSON_CreateObject();
    cJSON_AddStringToObject(export_json, "type", "action_template");
    cJSON_AddItemToObject(export_json, "template", template_json);

    char *json_str = cJSON_PrintUnformatted(export_json);
    cJSON_Delete(export_json);
    if (!json_str) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "no_memory");   /* the serialized text could not be built */
        return ESP_OK;
    }

    // Get recipient certificate
    const cJSON *recipient_cert = cJSON_GetObjectItem(params, "recipient_cert");
    char *cert_pem = NULL;
    size_t cert_len = 0;
    bool free_cert = false;

    if (recipient_cert && cJSON_IsString(recipient_cert) && recipient_cert->valuestring[0]) {
        cert_pem = recipient_cert->valuestring;
        cert_len = strlen(cert_pem);
    } else {
        cert_pem = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        if (!cert_pem) {
            cJSON_free(json_str);
            ts_api_result_error(result, TS_API_ERR_NO_MEM, "no_memory");
            return ESP_OK;
        }
        cert_len = 4096;
        ret = ts_cert_get_certificate(cert_pem, &cert_len);
        if (ret != ESP_OK) {
            free(cert_pem);
            cJSON_free(json_str);
            ts_api_result_reason(result, TS_API_ERR_INTERNAL, "device_cert_unavailable", esp_err_to_name(ret));
            return ESP_OK;
        }
        free_cert = true;
    }

    // Create encrypted config pack
    ts_config_pack_export_opts_t opts = {
        .recipient_cert_pem = cert_pem,
        .recipient_cert_len = cert_len,
        .description = "Automation action template"
    };

    char *tscfg_output = NULL;
    size_t tscfg_len = 0;
    ts_config_pack_result_t pack_result = ts_config_pack_create(
        tpl.id, json_str, strlen(json_str), &opts, &tscfg_output, &tscfg_len);

    cJSON_free(json_str);
    if (free_cert) free(cert_pem);

    if (pack_result != TS_CONFIG_PACK_OK) {
        pack_failure(result, pack_result);
        return ESP_OK;
    }

    // Return result
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "tscfg", tscfg_output);
    char filename[80];
    snprintf(filename, sizeof(filename), "action_%s.tscfg", tpl.id);
    cJSON_AddStringToObject(data, "filename", filename);

    free(tscfg_output);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief automation.actions.import - Import action template from config pack
 */
static esp_err_t api_automation_actions_import(const cJSON *params,ts_api_result_t *result)
{ return ts_api_auto_pack_import(TS_AUTO_CONFIG_ACTION,params,result); }

/*===========================================================================*/
/*                           API Registration                                 */
/*===========================================================================*/

esp_err_t ts_api_automation_register(void)
{
    TS_LOGI(TAG, "Registering automation APIs");

    const ts_api_endpoint_t configuration[]={
        {.name="automation.config.status", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,.description="Current automation storage and backup status",.category=TS_API_CAT_CONFIG,.handler=api_config_status,.requires_auth=true},
        {.name="automation.config.refresh", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,.description="Check SD card once without changing the user choice",.category=TS_API_CAT_CONFIG,.handler=api_config_refresh,.requires_auth=true},
        {.name="automation.config.backup", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,.description="Save the complete emergency configuration",.category=TS_API_CAT_CONFIG,.handler=api_config_backup,.requires_auth=true},
        {.name="automation.config.preview", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,.description="Preview a configuration restore or source choice",.category=TS_API_CAT_CONFIG,.handler=api_config_preview,.requires_auth=true},
        {.name="automation.config.apply", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,.description="Apply the confirmed restore or source choice",.category=TS_API_CAT_CONFIG,.handler=api_config_apply,.requires_auth=true},
        {.name="automation.config.enable", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,.description="Explicitly enable or pause the current configuration",.category=TS_API_CAT_CONFIG,.handler=api_config_enable,.requires_auth=true},
    };
    esp_err_t ret=ts_api_register_multiple(configuration,sizeof(configuration)/sizeof(configuration[0]));
    if(ret!=ESP_OK)return ret;

    // Status
    ts_api_endpoint_t ep_status = {
        .name = "automation.status", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "Get automation engine status",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_status,
        .requires_auth=true,
    };
    ts_api_register(&ep_status);

    // Control APIs
    ts_api_endpoint_t ep_start = {
        .name = "automation.start", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Start automation engine",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_start,
        .requires_auth=true,
    };
    ts_api_register(&ep_start);

    ts_api_endpoint_t ep_stop = {
        .name = "automation.stop", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Stop automation engine",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_stop,
        .requires_auth=true,
    };
    ts_api_register(&ep_stop);

    ts_api_endpoint_t ep_pause = {
        .name = "automation.pause", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Pause automation engine",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_pause,
        .requires_auth=true,
    };
    ts_api_register(&ep_pause);

    ts_api_endpoint_t ep_resume = {
        .name = "automation.resume", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Resume automation engine",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_resume,
        .requires_auth=true,
    };
    ts_api_register(&ep_resume);

    ts_api_endpoint_t ep_reload = {
        .name = "automation.reload", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Reload configuration",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_reload,
        .requires_auth=true,
    };
    ts_api_register(&ep_reload);

    // Variables APIs
    ts_api_endpoint_t ep_vars_list = {
        .name = "automation.variables.list", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "List all variables",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_variables_list,
        .requires_auth=true,
    };
    ts_api_register(&ep_vars_list);

    ts_api_endpoint_t ep_vars_get = {
        .name = "automation.variables.get", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "Get variable value",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_variables_get,
        .requires_auth=true,
    };
    ts_api_register(&ep_vars_get);

    ts_api_endpoint_t ep_vars_set = {
        .name = "automation.variables.set", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Set variable value",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_variables_set,
        .requires_auth=true,
    };
    ts_api_register(&ep_vars_set);
    ts_api_endpoint_t ep_vars_delete = ep_vars_set;
    ep_vars_delete.name = "automation.variables.delete";
    ep_vars_delete.external_access=TS_API_ACCESS_ROOT;
    ep_vars_delete.allowed_methods=TS_API_METHOD_WRITE;
    ep_vars_delete.requires_auth=true;
    ep_vars_delete.description = "Delete a saved user variable";
    ep_vars_delete.handler = api_automation_variables_delete;
    ts_api_register(&ep_vars_delete);

    // Rules APIs
    ts_api_endpoint_t ep_rules_list = {
        .name = "automation.rules.list", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "List all rules",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_list,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_list);

    ts_api_endpoint_t ep_rules_enable = {
        .name = "automation.rules.enable", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Enable a rule",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_enable,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_enable);

    ts_api_endpoint_t ep_rules_disable = {
        .name = "automation.rules.disable", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Disable a rule",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_disable,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_disable);

    ts_api_endpoint_t ep_rules_get = {
        .name = "automation.rules.get", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "Get rule details by ID",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_get,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_get);

    ts_api_endpoint_t ep_service = {.name = "automation.services.status", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
                                    .description = "Confirm managed remote process state",
                                    .category = TS_API_CAT_SYSTEM,
                                    .handler = api_service_status,
                                    .requires_auth=true};
    ts_api_register(&ep_service);
    ep_service.name = "automation.services.stop";
    ep_service.external_access=TS_API_ACCESS_ADMIN;
    ep_service.allowed_methods=TS_API_METHOD_WRITE;
    ep_service.requires_auth=true;
    ep_service.handler = api_service_stop;
    ep_service.description = "Stop verified managed remote process";
    ts_api_register(&ep_service);
    ep_service.name = "automation.services.list";
    ep_service.external_access=TS_API_ACCESS_READ;
    ep_service.allowed_methods=TS_API_METHOD_READ;
    ep_service.requires_auth=true;
    ep_service.description = "List independent remote service controls";
    ep_service.handler = api_service_list;
    ts_api_register(&ep_service);
    ts_api_endpoint_t ep_rules_trigger = {
        .name = "automation.rules.trigger", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Manually trigger a rule",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_trigger,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_trigger);

    ts_api_endpoint_t ep_rules_add = {
        .name = "automation.rules.add", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Add a new rule",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_add,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_add);
    ts_api_endpoint_t ep_rules_update = ep_rules_add;
    ep_rules_update.name = "automation.rules.update";
    ep_rules_update.external_access=TS_API_ACCESS_ROOT;
    ep_rules_update.allowed_methods=TS_API_METHOD_WRITE;
    ep_rules_update.requires_auth=true;
    ep_rules_update.description = "Update rule with expected revision";
    ep_rules_update.handler = api_automation_rules_update;
    ts_api_register(&ep_rules_update);

    ts_api_endpoint_t ep_rules_delete = {
        .name = "automation.rules.delete", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Delete a rule",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_delete,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_delete);

    // Sources API
    ts_api_endpoint_t ep_sources_list = {
        .name = "automation.sources.list", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "List all data sources",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_list,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_list);

    ts_api_endpoint_t ep_sources_add = {
        .name = "automation.sources.add", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Add a new data source",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_add,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_add);
    ts_api_endpoint_t ep_sources_update = ep_sources_add;
    ep_sources_update.name = "automation.sources.update";
    ep_sources_update.external_access=TS_API_ACCESS_ROOT;
    ep_sources_update.allowed_methods=TS_API_METHOD_WRITE;
    ep_sources_update.requires_auth=true;
    ep_sources_update.description = "Update an existing data source";
    ep_sources_update.handler = api_automation_sources_update;
    ts_api_register(&ep_sources_update);

    ts_api_endpoint_t ep_sources_delete = {
        .name = "automation.sources.delete", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Delete a data source",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_delete,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_delete);

    ts_api_endpoint_t ep_sources_add_mapping = {
        .name = "automation.sources.add_mapping", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Add a field mapping to an existing data source",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_add_mapping,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_add_mapping);

    ts_api_endpoint_t ep_sources_remove_mapping = {
        .name = "automation.sources.remove_mapping", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Remove a field mapping from a data source",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_remove_mapping,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_remove_mapping);

    ts_api_endpoint_t ep_sources_enable = {
        .name = "automation.sources.enable", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Enable a data source",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_enable,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_enable);

    ts_api_endpoint_t ep_sources_disable = {
        .name = "automation.sources.disable", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Disable a data source",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_disable,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_disable);

    // Test action APIs
    ts_api_endpoint_t ep_test_led = {
        .name = "automation.test.led", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Test LED action",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_test_led,
        .requires_auth=true,
    };
    ts_api_register(&ep_test_led);

    ts_api_endpoint_t ep_test_gpio = {
        .name = "automation.test.gpio", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Test GPIO action",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_test_gpio,
        .requires_auth=true,
    };
    ts_api_register(&ep_test_gpio);

    ts_api_endpoint_t ep_test_device = {
        .name = "automation.test.device", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Test device control action",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_test_device,
        .requires_auth=true,
    };
    ts_api_register(&ep_test_device);

    ts_api_endpoint_t ep_test_ssh = {
        .name = "automation.test.ssh", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Test SSH command execution",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_test_ssh,
        .requires_auth=true,
    };
    ts_api_register(&ep_test_ssh);

    // Action Template APIs
    ts_api_endpoint_t ep_actions_list = {
        .name = "automation.actions.list", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "List all action templates",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_actions_list,
        .requires_auth=true,
    };
    ts_api_register(&ep_actions_list);

    ts_api_endpoint_t ep_actions_get = {
        .name = "automation.actions.get", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "Get action template by ID",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_actions_get,
        .requires_auth=true,
    };
    ts_api_register(&ep_actions_get);

    ts_api_endpoint_t ep_actions_add = {
        .name = "automation.actions.add", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Create a new action template",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_actions_add,
        .requires_auth=true,
    };
    ts_api_register(&ep_actions_add);
    ts_api_endpoint_t ep_actions_update = {
        .name = "automation.actions.update", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Update an existing action template",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_actions_update,
        .requires_auth=true,
    };
    ts_api_register(&ep_actions_update);

    ts_api_endpoint_t ep_actions_delete = {
        .name = "automation.actions.delete", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Delete an action template",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_actions_delete,
        .requires_auth=true,
    };
    ts_api_register(&ep_actions_delete);

    ts_api_endpoint_t ep_actions_execute = {
        .name = "automation.actions.execute", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Execute an action template",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_actions_execute,
        .requires_auth=true,
    };
    ts_api_register(&ep_actions_execute);

    // Action Statistics APIs
    ts_api_endpoint_t ep_action_stats = {
        .name = "automation.action.stats", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_READ,
        .description = "Get action execution statistics",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_action_stats,
        .requires_auth=true,
    };
    ts_api_register(&ep_action_stats);

    ts_api_endpoint_t ep_action_stats_reset = {
        .name = "automation.action.stats.reset", .external_access=TS_API_ACCESS_ADMIN, .allowed_methods=TS_API_METHOD_WRITE,
        .description = "Reset action statistics",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_action_stats_reset,
        .requires_auth=true,
    };
    ts_api_register(&ep_action_stats_reset);

    // Proxy APIs for external connection test
    ts_api_endpoint_t ep_proxy_fetch = {
        .name = "automation.proxy.fetch", .external_access=TS_API_ACCESS_READ, .allowed_methods=TS_API_METHOD_POST, .session_handler=ts_api_automation_proxy_fetch_for_session,
        .description = "Proxy fetch from external REST API",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_proxy_fetch,
        .requires_auth=true,
    };
    ts_api_register(&ep_proxy_fetch);

    ts_api_endpoint_t ep_proxy_ws = {
        .name = "automation.proxy.websocket_test", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Test WebSocket connection",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_proxy_ws_test,
        .requires_auth=true,
    };
    ts_api_register(&ep_proxy_ws);

    ts_api_endpoint_t ep_proxy_sio = {
        .name = "automation.proxy.socketio_test", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Test Socket.IO connection (v4 protocol)",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_proxy_socketio_test,
        .requires_auth=true,
    };
    ts_api_register(&ep_proxy_sio);

    // Export/Import APIs - Sources
    ts_api_endpoint_t ep_sources_export = {
        .name = "automation.sources.export", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Export data source as config pack (.tscfg)",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_export,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_export);

    ts_api_endpoint_t ep_sources_import = {
        .name = "automation.sources.import", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Import data source from config pack",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_sources_import,
        .requires_auth=true,
    };
    ts_api_register(&ep_sources_import);

    // Export/Import APIs - Rules
    ts_api_endpoint_t ep_rules_export = {
        .name = "automation.rules.export", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Export rule as config pack (.tscfg)",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_export,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_export);

    ts_api_endpoint_t ep_rules_import = {
        .name = "automation.rules.import", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Import rule from config pack",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_rules_import,
        .requires_auth=true,
    };
    ts_api_register(&ep_rules_import);

    // Export/Import APIs - Action Templates
    ts_api_endpoint_t ep_actions_export = {
        .name = "automation.actions.export", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Export action template as config pack (.tscfg)",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_actions_export,
        .requires_auth=true,
    };
    ts_api_register(&ep_actions_export);

    ts_api_endpoint_t ep_actions_import = {
        .name = "automation.actions.import", .external_access=TS_API_ACCESS_ROOT, .allowed_methods=TS_API_METHOD_POST,
        .description = "Import action template from config pack",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_automation_actions_import,
        .requires_auth=true,
    };
    ts_api_register(&ep_actions_import);

    TS_LOGI(TAG, "Automation APIs registered");
    return ESP_OK;
}
