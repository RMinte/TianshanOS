"""Exercise production credentials, sessions and HTTP authorization offline."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

source = Path('components/ts_webui/src/ts_webui_api.c').read_text()
# Consumers of this shared protocol fixture inherit its actual URL dependencies.
url_idf = Path(os.environ.get('IDF_PATH', '/Users/massif/esp/v5.5.2/esp-idf'))
url_native_includes = [Path('components/ts_http_fetch/include').resolve(), url_idf/'components/http_parser']
url_native_sources = [Path('components/ts_http_fetch/src/ts_http_url.c').resolve(), url_idf/'components/http_parser/http_parser.c']



def extract(name, implementation=source):
    match = re.search(r'^(?:static )?[^\n;]+\b' + name + r'\([^;]+?\)\s*\{', implementation, re.M)
    assert match, name
    return implementation[match.start():implementation.index('\n}', match.start()) + 2] + '\n'


code = r'''
#define _POSIX_C_SOURCE 200809L
#define CONFIG_TS_WEBUI_AUTH_REQUIRED 1
#define API_PREFIX "/api/v1"
#define TS_HTTP_GET 0
#define TS_HTTP_POST 1
#define TS_HTTP_PUT 2
#define TS_HTTP_DELETE 3
#define ESP_ERR_NOT_SUPPORTED 0x106
#include <assert.h>
#include <math.h>
#include "ts_http_url.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
typedef int esp_err_t;
enum { ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NOT_FOUND=1, ESP_ERR_NOT_ALLOWED=2, ESP_ERR_INVALID_ARG=4, TS_API_OK=0, TS_API_ERR_NOT_FOUND=2, TS_API_ERR_NO_PERMISSION=3, TS_API_ERR_NOT_SUPPORTED=8, TS_API_ERR_INTERNAL=7, TS_API_ERR_INVALID_ARG=1, TS_API_ERR_AUTH=11, ESP_ERR_NO_MEM=0x101, TS_API_ERR_NO_MEM=6 };
typedef enum { TS_PERM_NONE=0, TS_PERM_READ=1, TS_PERM_WRITE=2, TS_PERM_ADMIN=3, TS_PERM_ROOT=4 } ts_perm_level_t;
typedef struct { int content_len;const char *query; } httpd_req_t;
typedef struct { httpd_req_t *req; const char *uri; int method; const char *body; size_t body_len; const char *auth; const char *path; } ts_http_request_t;
typedef struct { int code; char *message; cJSON *data; } ts_api_result_t;
typedef struct { uint32_t session_id; ts_perm_level_t level; } ts_session_t;
static bool session_live = true;
static esp_err_t ts_security_validate_session(uint32_t sid, ts_session_t *session) {
    if(sid==4 && session_live){if(session)*session=(ts_session_t){sid,TS_PERM_NONE};return ESP_OK;}
    if (!session_live || !sid || sid > 3) return ESP_ERR_NOT_FOUND;
    if (session) *session = (ts_session_t){sid,sid==1?TS_PERM_ROOT:sid==2?TS_PERM_ADMIN:TS_PERM_READ};
    return ESP_OK;
}
static void ts_api_result_init(ts_api_result_t *r) {memset(r,0,sizeof(*r));}
static void ts_api_result_error(ts_api_result_t *r,int code,const char *message) {free(r->message);cJSON_Delete(r->data);r->data=NULL;r->code=code;r->message=strdup(message);}
static bool fail_query_copy;
static bool fail_after_business,fail_next_json;
static void *proxy_json_malloc(size_t n){if(fail_next_json){fail_next_json=false;return NULL;}return malloc(n);}

static char *query_strdup(const char *s){return fail_query_copy?NULL:strdup(s);}
static char *query_strndup(const char *s,size_t n){return fail_query_copy?NULL:strndup(s,n);}
static int status, calls;static cJSON *received_params;static char received_name[64];
static char *response;
#define TS_LOGD(...) ((void)0)
#define TS_LOGI(...) ((void)0)
#define TS_LOGE(...) ((void)0)
#define TS_API_MALLOC(size) malloc(size)
static const char *esp_err_to_name(int e) {(void)e; return "error";}
static int64_t esp_timer_get_time(void) {return 0;}
static void ts_httpd_set_body_receive_budget(uint32_t milliseconds){(void)milliseconds;}
static size_t httpd_req_get_url_query_len(httpd_req_t *r) {return r->query?strlen(r->query):0;}
static esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *b,size_t n) {assert(r->query&&strlen(r->query)<n);strcpy(b,r->query);return ESP_OK;}
static esp_err_t ts_http_get_header(ts_http_request_t *r,const char *key,char *out,size_t n) {
    (void)key;if (!r->auth) return ESP_ERR_NOT_FOUND;snprintf(out,n,"Bearer %s",r->auth);return ESP_OK;
}
static esp_err_t ts_auth_validate_request(const char *header,uint32_t *session,ts_perm_level_t *level) {
    if(!session_live)return ESP_ERR_NOT_FOUND;
    if (!strcmp(header,"Bearer root")) {*session=1;*level=TS_PERM_ROOT;return ESP_OK;}
    if (!strcmp(header,"Bearer admin")) {*session=2;*level=TS_PERM_ADMIN;return ESP_OK;}
    if (!strcmp(header,"Bearer read")) {*session=3;*level=TS_PERM_READ;return ESP_OK;}
    return ESP_ERR_NOT_FOUND;
}
static esp_err_t ts_http_send_error(ts_http_request_t *r,int code,const char *message) {
    (void)r;(void)message;status=code;return ESP_OK;
}
static esp_err_t ts_http_send_json(ts_http_request_t *r,int code,const char *json) {
    (void)r;status=code;free(response);response=strdup(json);return ESP_OK;
}
static esp_err_t ts_http_get_query_param(ts_http_request_t *r,const char *key,char *out,size_t n) {
    (void)key;if(!r->path)return ESP_ERR_NOT_FOUND;snprintf(out,n,"%s",r->path);return ESP_OK;
}
static esp_err_t ts_http_send_private_file(ts_http_request_t *r,const char *path) {(void)r;(void)path;status=200;return ESP_OK;}
static bool ts_storage_exists(const char *path) {(void)path;return true;}
static bool ts_storage_is_dir(const char *path) {(void)path;return false;}
static esp_err_t ts_storage_check_write_path(const char *path,bool children) {
    (void)children;return strstr(path,"/config/automation/")||!strncmp(path,"/autocfg/",9)?ESP_ERR_NOT_ALLOWED:ESP_OK;
}
static esp_err_t ts_storage_external_read_requires_admin(const char *path,bool *root) {
    *root=!strncmp(path,"/spiffs",7)||!strncmp(path,"/www",4)||!strncmp(path,"/autocfg",8)||strstr(path,"/config/")||strstr(path,"/pki/");return ESP_OK;
}
static cJSON *ts_api_get_info(const char *name) {
    if (!strcmp(name,"missing")) return NULL;
    cJSON *info=cJSON_CreateObject();
    bool required=!strncmp(name,"storage.",8)&&strcmp(name,"storage.read")&&strcmp(name,"storage.list")&&strcmp(name,"storage.status")&&strcmp(name,"storage.info");
    required=required||!strcmp(name,"ssh.exec")||!strcmp(name,"ssh.cancel")||!strcmp(name,"automation.sources.export")||!strcmp(name,"automation.actions.add")||
        !strncmp(name,"automation.services.",20)||!strcmp(name,"automation.rules.trigger")||!strcmp(name,"automation.actions.execute");
    cJSON_AddBoolToObject(info,"requires_auth",required);return info;
}
static esp_err_t ts_api_call(const char *name,const cJSON *request,ts_api_result_t *result) {
    ++calls;strcpy(received_name,name);cJSON_Delete(received_params);received_params=cJSON_Duplicate(request,true);result->code=0;result->data=cJSON_CreateObject();
    if(!strcmp(name,"automation.sources.list")) {
        cJSON *items=cJSON_AddArrayToObject(result->data,"sources"),*row=cJSON_CreateObject();
        cJSON_AddStringToObject(row,"id","weather");cJSON_AddStringToObject(row,"rest_auth","secret");cJSON_AddStringToObject(row,"auth_header","alias-secret");cJSON_AddItemToArray(items,row);
    }
    const char *collection=!strcmp(name,"ssh.hosts.list")?"hosts":!strcmp(name,"ssh.commands.list")?"commands":
        !strcmp(name,"automation.rules.list")?"rules":!strcmp(name,"automation.actions.list")?"templates":
        !strcmp(name,"automation.variables.list")?"variables":NULL;
    if(collection)cJSON_AddArrayToObject(result->data,collection);
    if(!strcmp(name,"config.module.show")) {
        const cJSON *module=cJSON_GetObjectItem(request,"module");
        cJSON *wifi=cJSON_AddObjectToObject(result->data,cJSON_IsString(module)&&!strcmp(module->valuestring,"all")?"wifi":"config");
        cJSON_AddStringToObject(wifi,"ap.password","AP_SYNTHETIC_SECRET");cJSON_AddStringToObject(wifi,"ap.ssid","Privacy Lab");
    }
    if(!strcmp(name,"config.get"))
        cJSON_AddStringToObject(result->data,"value","synthetic-config-value");
    if(fail_after_business){result->message=strdup("Synthetic business accepted");fail_next_json=true;}
    return ESP_OK;
}
static void ts_api_result_free(ts_api_result_t *r) {free(r->message);cJSON_Delete(r->data);}
static bool is_binary_upload_complete(ts_http_request_t *r) {(void)r;return true;}
static cJSON *ts_config_pack_parse_import_request(const char *body,size_t len) {(void)len;return cJSON_Parse(body);}
static esp_err_t send_key_generate_result(ts_http_request_t *r,ts_api_result_t *result,const char *id,int64_t start) {
    (void)r;(void)result;(void)id;(void)start;return ESP_OK;
}
'''
code = code.replace('#include \"cJSON.h\"', '#include \"cJSON.h\"\n#include \"' + str(Path('components/ts_api/include/ts_api_policy.h').resolve()) + '\"', 1)
metadata = r"""
typedef struct { const char *name, *description; int category; void *handler; bool requires_auth; const char *permission; ts_api_external_access_t external_access; uint8_t allowed_methods; void *session_handler; } ts_api_endpoint_t;
#define TS_API_CAT_STORAGE 0
#define TS_API_CAT_SECURITY 1
#define TS_API_CAT_SYSTEM 2
#define TS_API_CAT_FAN 3
#define TS_API_CAT_HAL 4
#define TS_API_CAT_DEVICE 5
#define TS_API_CAT_POWER 6
#define TS_API_CAT_LED 7
#define TS_API_CAT_NETWORK 8
#define TS_API_CAT_CONFIG 9
"""
for filename, array in [('ts_api_storage.c','s_storage_endpoints'),('ts_api_ssh.c','ssh_endpoints'),('ts_api_sftp.c','sftp_endpoints'),('ts_api_ui.c','ui_endpoints'),('ts_api_cert.c','cert_apis'),('ts_api_fan.c','s_fan_endpoints'),('ts_api_gpio.c','s_gpio_endpoints'),('ts_api_power.c','s_power_endpoints'),('ts_api_device.c','device_endpoints'),('ts_api_led.c','led_endpoints'),('ts_api_log.c','endpoints'),('ts_api_system.c','system_apis'),('ts_api_service.c','s_service_endpoints'),('ts_api_dhcp.c','endpoints'),('ts_api_nat.c','endpoints'),('ts_api_time.c','endpoints'),('ts_api_config.c','config_apis'),('ts_api_auth.c','s_auth_endpoints'),('ts_api_network.c','network_endpoints'),('ts_api_wifi.c','endpoints')]:
    implementation = Path('components/ts_api/src', filename).read_text()
    start = implementation.index('static const ts_api_endpoint_t ' + array + '[]')
    table = implementation[start:start+re.search(r'\n[ \t]*};',implementation[start:]).end()]
    if filename in ('ts_api_dhcp.c','ts_api_nat.c','ts_api_time.c','ts_api_wifi.c'):
        table=re.sub(r'\bendpoints\b',filename.removesuffix('.c')+'_endpoints',table)
    for handler in set(re.findall(r'\.(?:handler|session_handler)\s*=\s*(\w+)', table)):
        metadata += '#define ' + handler + ' NULL\n'
    metadata += table + '\n'
# The recovery controls are local entries of the actual OTA registration table.
ota_source=Path('components/ts_api/src/ts_api_ota.c').read_text()
ota_start=ota_source.index('    ts_api_endpoint_t endpoints[] = {')
ota_table=ota_source[ota_start:ota_start+re.search(r'\n[ \t]*};',ota_source[ota_start:]).end()]
ota_table=ota_table.replace('ts_api_endpoint_t endpoints[]','static const ts_api_endpoint_t ota_endpoints[]',1)
for handler in set(re.findall(r'\.(?:handler|session_handler)\s*=\s*(\w+)',ota_table)):
    metadata+='#define '+handler+' NULL\n'
metadata+=ota_table+'\n'
# Read the remaining scalar declarations, including explicit fields after five
# dynamic .name assignments. Their actual registration/copy behavior is tested
# independently by test_complete_api_inventory.py.
remaining=[]
for filename in ['ts_api_monitor.c','ts_api_ui.c','ts_api_temp.c','ts_api_hosts.c','ts_api_automation.c','ts_api_lpmu_access.c']:
    text=Path('components/ts_api/src',filename).read_text();names=list(re.finditer(r'\.name\s*=\s*"([\w.]+)"',text))
    for i,m in enumerate(names):
        block=text[m.end():names[i+1].start() if i+1<len(names) else len(text)]
        access=re.search(r'\.external_access\s*=\s*(TS_API_ACCESS_\w+)',block);methods=re.search(r'\.allowed_methods\s*=\s*(TS_API_METHOD_\w+)',block)
        assert access and methods,m[1]
        remaining.append('{"'+m[1]+'",'+access[1]+','+methods[1]+'}')
assert len(remaining)==78
metadata+='static const struct {const char *name;unsigned mode,methods;} remaining_policies[]={'+','.join(remaining)+'};\n'
# Insert declarations before get_info; its metadata decisions come from real tables.
marker = 'static cJSON *ts_api_get_info'
index = code.index(marker)
code = code[:index] + metadata + code[index:]
marker = '    cJSON_AddBoolToObject(info,"requires_auth",required);return info;'
code = code.replace(marker, r"""
    for(unsigned i=0;i<sizeof(ota_endpoints)/sizeof(*ota_endpoints);i++)
        if(!strcmp(ota_endpoints[i].name,name)) {
            required=ota_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",ota_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",ota_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(s_storage_endpoints)/sizeof(s_storage_endpoints[0]);i++)
        if(!strcmp(s_storage_endpoints[i].name,name)) {
            required=s_storage_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",s_storage_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",s_storage_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(ssh_endpoints)/sizeof(ssh_endpoints[0]);i++)
        if(!strcmp(ssh_endpoints[i].name,name)) {
            required=ssh_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",ssh_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",ssh_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(sftp_endpoints)/sizeof(sftp_endpoints[0]);i++)
        if(!strcmp(sftp_endpoints[i].name,name)) {
            required=sftp_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",sftp_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",sftp_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(ui_endpoints)/sizeof(ui_endpoints[0]);i++)
        if(!strcmp(ui_endpoints[i].name,name))required=ui_endpoints[i].requires_auth;
    for(unsigned i=0;i<sizeof(cert_apis)/sizeof(cert_apis[0]);i++)
        if(!strcmp(cert_apis[i].name,name)) {
            required=cert_apis[i].requires_auth;
            if(cert_apis[i].permission)cJSON_AddStringToObject(info,"permission",cert_apis[i].permission);
            cJSON_AddNumberToObject(info,"external_access",cert_apis[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",cert_apis[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(s_fan_endpoints)/sizeof(s_fan_endpoints[0]);i++)
        if(!strcmp(s_fan_endpoints[i].name,name)) {
            required=s_fan_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",s_fan_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",s_fan_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(s_gpio_endpoints)/sizeof(s_gpio_endpoints[0]);i++)
        if(!strcmp(s_gpio_endpoints[i].name,name)) {
            required=s_gpio_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",s_gpio_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",s_gpio_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(s_power_endpoints)/sizeof(s_power_endpoints[0]);i++)
        if(!strcmp(s_power_endpoints[i].name,name)) {
            required=s_power_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",s_power_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",s_power_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(device_endpoints)/sizeof(device_endpoints[0]);i++)
        if(!strcmp(device_endpoints[i].name,name)) {
            required=device_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",device_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",device_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(led_endpoints)/sizeof(led_endpoints[0]);i++)
        if(!strcmp(led_endpoints[i].name,name)) {
            required=led_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",led_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",led_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(endpoints)/sizeof(endpoints[0]);i++)
        if(!strcmp(endpoints[i].name,name)) {
            required=endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(system_apis)/sizeof(system_apis[0]);i++)
        if(!strcmp(system_apis[i].name,name)) {
            required=system_apis[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",system_apis[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",system_apis[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(s_service_endpoints)/sizeof(s_service_endpoints[0]);i++)
        if(!strcmp(s_service_endpoints[i].name,name)) {
            required=s_service_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",s_service_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",s_service_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(ts_api_dhcp_endpoints)/sizeof(ts_api_dhcp_endpoints[0]);i++)
        if(!strcmp(ts_api_dhcp_endpoints[i].name,name)) {
            required=ts_api_dhcp_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",ts_api_dhcp_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",ts_api_dhcp_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(ts_api_nat_endpoints)/sizeof(ts_api_nat_endpoints[0]);i++)
        if(!strcmp(ts_api_nat_endpoints[i].name,name)) {
            required=ts_api_nat_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",ts_api_nat_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",ts_api_nat_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(ts_api_time_endpoints)/sizeof(ts_api_time_endpoints[0]);i++)
        if(!strcmp(ts_api_time_endpoints[i].name,name)) {
            required=ts_api_time_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",ts_api_time_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",ts_api_time_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(config_apis)/sizeof(config_apis[0]);i++)
        if(!strcmp(config_apis[i].name,name)) {
            required=config_apis[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",config_apis[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",config_apis[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(s_auth_endpoints)/sizeof(s_auth_endpoints[0]);i++)
        if(!strcmp(s_auth_endpoints[i].name,name)) {
            required=s_auth_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",s_auth_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",s_auth_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(network_endpoints)/sizeof(network_endpoints[0]);i++)
        if(!strcmp(network_endpoints[i].name,name)) {
            required=network_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",network_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",network_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(ts_api_wifi_endpoints)/sizeof(ts_api_wifi_endpoints[0]);i++)
        if(!strcmp(ts_api_wifi_endpoints[i].name,name)) {
            required=ts_api_wifi_endpoints[i].requires_auth;
            cJSON_AddNumberToObject(info,"external_access",ts_api_wifi_endpoints[i].external_access);
            cJSON_AddNumberToObject(info,"allowed_methods",ts_api_wifi_endpoints[i].allowed_methods);
        }
    for(unsigned i=0;i<sizeof(remaining_policies)/sizeof(*remaining_policies);i++)if(!strcmp(name,remaining_policies[i].name)) {
        required=remaining_policies[i].mode>=TS_API_ACCESS_READ&&remaining_policies[i].mode<=TS_API_ACCESS_ROOT;
        cJSON_AddNumberToObject(info,"external_access",remaining_policies[i].mode);
        cJSON_AddNumberToObject(info,"allowed_methods",remaining_policies[i].methods);
    }
    cJSON_AddBoolToObject(info,"requires_auth",required);return info;
""", 1)
code += '#include \"' + str(Path('components/ts_api/include/ts_api_params.h').resolve()) + '\"\n'
code += extract('ts_api_parse_query', Path('components/ts_api/src/ts_api.c').read_text())
code += extract('classify_external_access', Path('components/ts_api/src/ts_api.c').read_text())
code += r'''
/* Metadata adapter for the HTTP slice; the real live-registry lookup is covered
 * separately by test_api_access.py and the complete registry suites. */
static esp_err_t ts_api_get_access(const char *name,ts_api_access_t *access){
 cJSON *info=ts_api_get_info(name);if(!info)return ESP_ERR_NOT_FOUND;
 const cJSON *auth=cJSON_GetObjectItemCaseSensitive(info,"requires_auth"),*permission=cJSON_GetObjectItemCaseSensitive(info,"permission");
 if(!cJSON_IsBool(auth)){cJSON_Delete(info);return ESP_FAIL;}
 const cJSON *mode=cJSON_GetObjectItemCaseSensitive(info,"external_access"),*methods=cJSON_GetObjectItemCaseSensitive(info,"allowed_methods");
 classify_external_access(cJSON_IsNumber(mode)?mode->valueint:TS_API_ACCESS_INTERNAL,cJSON_IsNumber(methods)?methods->valueint:0,access);cJSON_Delete(info);return ESP_OK;
}
'''
code += 'static esp_err_t api_call_dispatch(const char *name,const cJSON *params,uint32_t sid,bool external,ts_api_result_t *result){\n (void)external;\n#ifdef CONFIG_TS_WEBUI_AUTH_REQUIRED\n const cJSON *path=cJSON_GetObjectItem(params,"path");\n if(!strcmp(name,"storage.read")&&cJSON_IsString(path)&&strstr(path->valuestring,"/config/")&&sid!=1&&sid!=2){\n  ts_api_result_error(result,TS_API_ERR_NO_PERMISSION,"Root permission required");return ESP_ERR_NOT_ALLOWED;\n }\n#endif\n return ts_api_call(name,params,result);\n}\n'
code += 'typedef esp_err_t (*ts_api_handler_t)(const cJSON *,ts_api_result_t *);\ntypedef esp_err_t (*ts_api_session_handler_t)(const cJSON *,uint32_t,ts_api_result_t *);\n'
code += '/* Registry/business boundary for this protocol fixture. Real snapshot and\n * concurrent replacement behavior are exercised by test_default_deny_policy. */\nstatic char protocol_operation[128];\nstatic esp_err_t protocol_context_handler(const cJSON *p,uint32_t sid,ts_api_result_t *r){return api_call_dispatch(protocol_operation,p,sid,true,r);}\nstatic esp_err_t copy_external_call(const char *name,ts_api_access_t *access,ts_api_handler_t *handler,ts_api_session_handler_t *session_handler){\n esp_err_t ret=ts_api_get_access(name,access);if(ret!=ESP_OK)return ret;\n snprintf(protocol_operation,sizeof(protocol_operation),"%s",name);*handler=NULL;*session_handler=protocol_context_handler;return ESP_OK;\n}\n'
# Reuse this real HTTP/core dispatch fixture for the OTA public/full view seam.
code += '#undef api_ota_partitions\n#undef api_ota_partitions_for_session\n'
code = code.replace('static char protocol_operation[128];', 'static esp_err_t protocol_ota_handler(const cJSON *,uint32_t,ts_api_result_t *);\nstatic char protocol_operation[128];', 1)
ota_header = Path('components/ts_ota/include/ts_ota.h').read_text()
ota_types_start = ota_header.rfind('typedef struct {', 0, ota_header.index('} ts_ota_version_info_t;'))
code += ota_header[ota_types_start:ota_header.index('} ts_ota_partition_info_t;')+len('} ts_ota_partition_info_t;')]
code += r'''
static unsigned ota_next_reads,ota_rollback_reads;
static bool ota_running_error;
static esp_err_t ts_ota_get_running_partition_info(ts_ota_partition_info_t *info) {
 if(ota_running_error)return ESP_FAIL;memset(info,0,sizeof(*info));strcpy(info->label,"ota_1");info->address=0x320000;info->size=0x300000;info->is_bootable=true;strcpy(info->version.version,"same-build");strcpy(info->version.project_name,"TianShanOS");return ESP_OK;
}
static esp_err_t ts_ota_get_next_partition_info(ts_ota_partition_info_t *info) {
 ++ota_next_reads;ts_ota_get_running_partition_info(info);strcpy(info->label,"ota_0");info->address=0x20000;return ESP_OK;
}
static bool ts_ota_can_rollback(void) {++ota_rollback_reads;return true;}
'''
code += extract('ts_security_check_permission', Path('components/ts_security/src/ts_security.c').read_text())
code += extract('ts_api_result_ok', Path('components/ts_api/src/ts_api.c').read_text())
for name in ['ota_partition_json','api_ota_partitions','api_ota_partitions_for_session']:
    code += extract(name, ota_source)
code += 'static esp_err_t protocol_ota_handler(const cJSON *p,uint32_t sid,ts_api_result_t *r){++calls;return api_ota_partitions_for_session(p,sid,r);}'
code = code.replace('*session_handler=protocol_context_handler;', '*session_handler=!strcmp(name,"ota.partitions")?protocol_ota_handler:protocol_context_handler;', 1)
code += extract('project_result_fields', Path('components/ts_api/src/ts_api.c').read_text())
code += extract('private_result_projection', Path('components/ts_api/src/ts_api.c').read_text())
code += extract('ts_api_call_for_session', Path('components/ts_api/src/ts_api.c').read_text())
# Keep the actual local proxy branch; the external network path is forbidden in this offline fixture.
proxy_root=Path(os.environ.get('SOURCE_BASELINE_ROOT','.'))
proxy = extract('ts_api_automation_proxy_fetch_for_session', (proxy_root/'components/ts_api/src/ts_api_automation.c').read_text())
network_start=proxy.index('    esp_http_client_method_t')
code += 'static unsigned outbound;static bool allow_outbound;\n#define strdup query_strdup\n#define strndup query_strndup\n'+proxy[:network_start]+'    assert(allow_outbound);++outbound;result->code=TS_API_OK;result->data=cJSON_CreateObject();return ESP_OK;\n}\n#undef strdup\n#undef strndup\n'

code += extract('api_automation_proxy_fetch', Path('components/ts_api/src/ts_api_automation.c').read_text())
code += extract('check_auth')
if 'static bool api_request_access(' in source:
    code += extract('api_request_access') + extract('api_admit')
code += extract('api_handler') + extract('file_download_handler')
code += r'''
static void api_method(const char *uri,const char *body,const char *auth,int expected_status,int method) {
    const char *query=strchr(uri,'?');httpd_req_t native={.query=query?query+1:NULL};ts_http_request_t req={.req=&native,.uri=uri,.method=method,.body=body,.body_len=body?strlen(body):0,.auth=auth};
    status=0;free(response);response=NULL;api_handler(&req,NULL);if(status!=expected_status)fprintf(stderr,"HTTP %s expected %d received %d: %s\n",uri,expected_status,status,response?response:"");assert(status==expected_status);
}
static void api(const char *uri,const char *body,const char *auth,int expected_status){api_method(uri,body,auth,expected_status,TS_HTTP_POST);}
static void file(const char *path,const char *auth,int expected_status) {
    ts_http_request_t req={.path=path,.auth=auth};status=0;file_download_handler(&req,NULL);assert(status==expected_status);
}
int main(void) {

    /* Actual OTA handler/HTTP admission: a rebooted bearer gets only running identity. */
    const char *ota_auths[]={NULL,"expired","read","root"};
    for(unsigned i=0;i<4;i++)for(unsigned get=0;get<2;get++){
      unsigned next=ota_next_reads,rollback=ota_rollback_reads;
      api_method("/api/v1/ota/partitions",get?NULL:"{\"session_id\":1,\"role\":\"root\",\"full\":true}",ota_auths[i],200,get?TS_HTTP_GET:TS_HTTP_POST);
      cJSON *reply=cJSON_Parse(response),*data=cJSON_GetObjectItem(reply,"data"),*running=cJSON_GetObjectItem(data,"running");assert(running);
      if(i<2){assert(cJSON_GetArraySize(data)==1&&cJSON_GetArraySize(running)==3&&!cJSON_GetObjectItem(running,"address")&&!cJSON_GetObjectItem(data,"next"));assert(ota_next_reads==next&&ota_rollback_reads==rollback);}
      else{assert(cJSON_GetObjectItem(running,"address")&&cJSON_GetObjectItem(data,"next")&&cJSON_GetObjectItem(data,"can_rollback"));assert(ota_next_reads==next+1&&ota_rollback_reads==rollback+1);}
      cJSON_Delete(reply);
    }
    ts_api_result_t ota_result={0};ts_api_call_for_session("ota.partitions",NULL,4,&ota_result);assert(ota_result.code==0&&cJSON_GetArraySize(ota_result.data)==1);ts_api_result_free(&ota_result);
    ts_api_result_init(&ota_result);api_ota_partitions(NULL,&ota_result);assert(cJSON_GetObjectItem(ota_result.data,"next"));ts_api_result_free(&ota_result);
    session_live=false;api_method("/api/v1/ota/partitions",NULL,"root",200,TS_HTTP_GET);
    unsigned before_unauthorized=calls;api_method("/api/v1/ota/progress",NULL,"root",401,TS_HTTP_GET);api("/api/v1/system/reboot","{}","root",401);assert(calls==before_unauthorized);session_live=true;
    ota_running_error=true;api_method("/api/v1/ota/partitions",NULL,NULL,200,TS_HTTP_GET);assert(strstr(response,"Partition information unavailable"));ota_running_error=false;
    puts("PASS actual OTA public/full handler and HTTP/core dispatch: no/expired/NONE vs READ/Root, GET/POST, forged identity, protected401 and running-read failure");
    /* Missing declaration has zero verbs even when the request has no valid
     * JSON body. Role/permission strings cannot make this an external API. */
    api("/api/v1/fixture/internal","not-json",NULL,405);
    api("/api/v1/fixture/internal","not-json","root",405);
    const char *privacy_auth[]={NULL,"read","admin","root"};
    const uint32_t privacy_sid[]={0,3,2,1};
    for(unsigned i=0;i<4;++i) {
        api("/api/v1/config/module/show","{\"module\":\"wifi\"}",privacy_auth[i],i?200:401);
        if(i)assert(strstr(response,"Privacy Lab")&&!!strstr(response,"AP_SYNTHETIC_SECRET")==(i>=2));
        api("/api/v1/config/module/show","{\"module\":\"all\"}",privacy_auth[i],i?200:401);
        if(i)assert(!!strstr(response,"AP_SYNTHETIC_SECRET")==(i>=2));else assert(!response||!strstr(response,"AP_SYNTHETIC_SECRET"));
        int submitted=calls;
        api("/api/v1/config/get","{\"key\":\"custom.credential\",\"session_id\":1}",privacy_auth[i],i>=2?200:401);
        assert(calls==submitted+(i>=2));
        api("/api/v1/config/get","{\"key\":\"system.language\"}",privacy_auth[i],200);
        const char *names[]={"config.module.show","config.get"};
        const char *urls[]={"http://localhost/api/v1/config/module/show?module=wifi","http://localhost/api/v1/config/get?key=custom.credential"};
        const char *bodies[]={"{\"module\":\"wifi\"}","{\"key\":\"custom.credential\"}"};
        for(unsigned n=0;n<2;++n) {
            cJSON *params=cJSON_Parse(bodies[n]),*proxy_params=cJSON_CreateObject();
            cJSON_AddStringToObject(proxy_params,"url",urls[n]);cJSON_AddNumberToObject(proxy_params,"session_id",1);
            ts_api_result_t direct={0},proxy_result={0};
            ts_api_call_for_session(names[n],params,privacy_sid[i],&direct);
            ts_api_automation_proxy_fetch_for_session(proxy_params,privacy_sid[i],&proxy_result);
            assert(direct.code==proxy_result.code);
            if(direct.code==TS_API_OK) {
                cJSON *body=cJSON_GetObjectItemCaseSensitive(proxy_result.data,"body");
                assert(cJSON_Compare(direct.data,cJSON_GetObjectItemCaseSensitive(body,"data"),true));
            }
            ts_api_result_free(&direct);ts_api_result_free(&proxy_result);cJSON_Delete(params);cJSON_Delete(proxy_params);
        }
    }
    puts("PASS configuration privacy across actual HTTP/core/local proxy: language bootstrap, authenticated admin raw reads, both module shapes and forged JSON");
    const char *hardware_ops[]={"fan.status","fan.config","gpio.list","gpio.info","fan.set","fan.mode","fan.enable","fan.limits","fan.curve","fan.save","gpio.set","gpio.toggle"};
    for(unsigned n=0;n<sizeof(hardware_ops)/sizeof(*hardware_ops);n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        bool control=n>=4,allowed=n==0||(identity>0&&(!control||identity>=2));
        int expected=!allowed?(identity?403:401):control&&get?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",hardware_ops[n]);int previous=calls;
        api_method(uri,"{\"role\":\"root\",\"session_id\":1}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",hardware_ops[n]);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};previous=calls;ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);
        assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS twelve actual fan/GPIO descriptor policies through HTTP and local proxy: minimal public/read metadata, Admin controls, GET no side effects");
    const char *power_public[]={"device.status","device.fan.status","device.power.status","device.usb.status","power.status","power.voltage","power.chip","power.protection.status"};
    const char *power_reads[]={"power.stats","power.protection.config"};
    const char *power_controls[]={"device.power","device.reset","device.fan.set","device.usb.set","device.ping","power.stats.reset","power.threshold.set","power.interval.set","power.debug","power.protection.set","power.monitor.start","power.monitor.stop"};
    for(unsigned n=0;n<22;n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        const char *name=n<8?power_public[n]:n<10?power_reads[n-8]:power_controls[n-10];bool control=n>=10,allowed=n<8||(identity>0&&(!control||identity>=2));
        int expected=!allowed?(identity?403:401):control&&get?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",name);int previous=calls;api_method(uri,"{\"session_id\":1,\"role\":\"root\"}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};previous=calls;ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS twenty-two power/device descriptor policies through real HTTP/local proxy; Admin controls and active ping, GET no effects, public home telemetry retained");
    const char *led_ops[]={"led.list","led.brightness","led.clear","led.set","led.fill","led.effect.list","led.effect.start","led.effect.stop","led.color.parse","led.color.hsv","led.filter.list","led.filter.start","led.filter.stop","led.image","led.qrcode","led.text","led.text.stop","led.save","led.boot.config","led.color_correction.get","led.color_correction.set","led.color_correction.reset","led.color_correction.export","led.color_correction.import"};
    const char *led_reads[]={"led.boot.config","led.color.hsv","led.color.parse","led.color_correction.get","led.effect.list","led.filter.list"};
    for(unsigned n=0;n<sizeof(led_ops)/sizeof(*led_ops);n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        const char *name=led_ops[n];bool public=!strcmp(name,"led.list"),read=false;for(unsigned i=0;i<sizeof(led_reads)/sizeof(*led_reads);i++)read|=!strcmp(name,led_reads[i]);
        bool control=!public&&!read,allowed=public||(identity>0&&(!control||identity>=2));int expected=!allowed?(identity?403:401):control&&get?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",name);int previous=calls;api_method(uri,"{\"session_id\":1,\"role\":\"root\",\"brightness\":0}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s?brightness=0",name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");ts_api_result_t result={0};previous=calls;
        ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS all twenty-four LED descriptor policies through HTTP/local proxy: Admin controls/calibration file export, GET no effects, public device summary");
    const char *log_ops[]={"log.list","log.stats","log.clear","log.setLevel","log.capture"};
    for(unsigned n=0;n<5;n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        bool allowed=identity>=2;int expected=!allowed?(identity?403:401):n>=2&&get?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",log_ops[n]);int previous=calls;api_method(uri,"{\"session_id\":1,\"role\":\"root\"}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",log_ops[n]);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");ts_api_result_t result={0};previous=calls;
        ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS all five log descriptor policies through HTTP/local proxy: Admin reads/control, GET control refuses before log access");
    const struct {const char *name;unsigned required,methods;} management_ops[]={{"system.info",0,3},{"system.memory",0,3},{"system.memory_detail",3,3},{"system.cpu",0,3},{"system.tasks",3,3},{"system.reboot",3,14},{"system.log.level",3,14},{"system.apis",1,3},{"service.list",0,3},{"service.status",0,3},{"service.start",3,14},{"service.stop",3,14},{"service.restart",3,14},{"dhcp.status",1,3},{"dhcp.clients",1,3},{"dhcp.start",3,14},{"dhcp.stop",3,14},{"nat.status",1,3},{"nat.enable",3,14},{"nat.disable",3,14},{"nat.save",3,14}};
    for(unsigned n=0;n<21;n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        unsigned level=identity==0?0:identity==1?1:identity==2?3:4;bool allowed=level>=management_ops[n].required;
        int expected=!allowed?(identity?403:401):get&&!(management_ops[n].methods&TS_API_METHOD_GET)?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",management_ops[n].name);int previous=calls;
        api_method(uri,"{\"role\":\"root\",\"session_id\":1}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",management_ops[n].name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};previous=calls;ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);
        assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS twenty-one system/service/DHCP/NAT real descriptor policies through HTTP/local proxy: minimal telemetry public, diagnostics and writes Admin, GET control has no effects");
    const struct {const char *name;unsigned required,methods;} auth_outer_ops[]={{"auth.login",0,2},{"auth.logout",0,2},{"auth.status",0,2},{"auth.change_password",0,2},{"auth.admin.set_password",4,2},{"auth.root.set_password",4,2},{"auth.admin.reset_password",4,2}};
    for(unsigned n=0;n<7;n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        unsigned level=identity==0?0:identity==1?1:identity==2?3:4;bool allowed=level>=auth_outer_ops[n].required;
        int expected=!allowed?(identity?403:401):get&&!(auth_outer_ops[n].methods&TS_API_METHOD_GET)?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",auth_outer_ops[n].name);int previous=calls;
        api_method(uri,"{\"role\":\"root\",\"session_id\":1}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",auth_outer_ops[n].name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};previous=calls;ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);
        assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS seven auth descriptor outer policies through HTTP/local proxy: password login/public entry and body-token workflows; account administration Root; all POST only, GET control has no effects");
    const struct {const char *name;unsigned required,methods;} network_config_ops[]={{"config.get",0,3},{"config.set",3,14},{"config.delete",3,14},{"config.list",3,3},{"config.save",3,14},{"config.module.list",1,3},{"config.module.show",1,3},{"config.module.set",3,14},{"config.module.save",3,14},{"config.module.reset",3,14},{"network.status",0,3},{"network.wifi.mode",3,14},{"network.wifi.scan",3,14},{"network.wifi.connect",3,14},{"network.wifi.disconnect",3,14},{"network.wifi.ap.config",3,14},{"network.wifi.ap.stations",1,3},{"network.eth.status",0,3},{"network.hostname",3,14},{"wifi.status",0,3},{"wifi.scan",3,14},{"wifi.connect",3,14},{"wifi.disconnect",3,14},{"dhcp.status",1,3},{"dhcp.clients",1,3},{"dhcp.start",3,14},{"dhcp.stop",3,14},{"nat.status",1,3},{"nat.enable",3,14},{"nat.disable",3,14},{"nat.save",3,14}};
    for(unsigned n=0;n<31;n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        unsigned level=identity==0?0:identity==1?1:identity==2?3:4;bool allowed=level>=network_config_ops[n].required;
        int expected=!allowed?(identity?403:401):get&&!(network_config_ops[n].methods&TS_API_METHOD_GET)?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",network_config_ops[n].name);int previous=calls;
        api_method(uri,"{\"key\":\"system.language\",\"role\":\"root\",\"session_id\":1}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s?key=system.language",network_config_ops[n].name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};previous=calls;ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);
        assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS thirty-one network/config real descriptor policies through HTTP/local proxy: minimal status/language public, original config/network writes Admin, GET control has no effects");
    const struct {const char *name;unsigned required,methods;} time_ops[]={{"time.info",0,3},{"time.sync",3,14},{"time.set_ntp",3,14},{"time.set_timezone",3,14},{"time.force_sync",3,14}};
    for(unsigned n=0;n<5;n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        unsigned level=identity==0?0:identity==1?1:identity==2?3:4;bool allowed=level>=time_ops[n].required;
        int expected=!allowed?(identity?403:401):get&&!(time_ops[n].methods&TS_API_METHOD_GET)?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",time_ops[n].name);int previous=calls;
        api_method(uri,"{\"role\":\"root\",\"session_id\":1}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",time_ops[n].name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};previous=calls;ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);
        assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS five time real descriptor policies through HTTP/local proxy: time info public, original writes Admin, GET control has no effects");
    const struct {const char *name;unsigned required,methods;} ssh_ops[]={{"ssh.exec_status",4,3},{"ssh.services.start",3,14},{"ssh.exec",4,14},{"ssh.exec_stream",4,14},{"ssh.cancel",4,14},{"ssh.test",3,14},{"ssh.copyid",3,14},{"ssh.revoke",3,14},{"ssh.keygen",3,14},{"ssh.hosts.list",1,3},{"ssh.hosts.add",3,14},{"ssh.hosts.update",3,14},{"ssh.hosts.remove",3,14},{"ssh.hosts.get",1,3},{"ssh.hosts.export",3,14},{"ssh.hosts.import",3,14},{"ssh.commands.list",1,3},{"ssh.commands.add",4,14},{"ssh.commands.update",4,14},{"ssh.commands.remove",4,14},{"ssh.commands.get",1,3},{"ssh.commands.export",4,14},{"ssh.commands.import",4,14},{"sftp.ls",3,14},{"sftp.get",3,14},{"sftp.put",3,14},{"sftp.rm",3,14},{"sftp.mkdir",3,14},{"sftp.stat",3,14}};
    for(unsigned n=0;n<29;n++)for(unsigned identity=0;identity<4;identity++)for(unsigned get=0;get<2;get++){
        unsigned level=identity==0?0:identity==1?1:identity==2?3:4;bool allowed=level>=ssh_ops[n].required;
        int expected=!allowed?(identity?403:401):get&&!(ssh_ops[n].methods&TS_API_METHOD_GET)?405:200;bool dispatch=expected==200;
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",ssh_ops[n].name);int previous=calls;
        api_method(uri,"{\"role\":\"root\",\"session_id\":1}",privacy_auth[identity],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+dispatch);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",ssh_ops[n].name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};previous=calls;ts_api_automation_proxy_fetch_for_session(input,privacy_sid[identity],&result);
        assert((result.code==TS_API_OK)==dispatch&&calls==previous+dispatch);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS twenty-nine SSH/SFTP real descriptor policies through HTTP/local proxy: read metadata requires login, original security/SFTP management Admin; command execution Root, GET control has no effects");
    int denied_before=calls;
    api("/api/v1/cert/install?bad=%Q2","{",NULL,401);assert(calls==denied_before);
    api("/api/v1/cert/install?bad=%Q2","{","read",403);assert(calls==denied_before);
    api("/api/v1/cert/install?bad=%Q2","{","root",400);assert(calls==denied_before);
    const char *cert_writes[]={"cert.generate_keypair","cert.generate_csr","cert.install","cert.install_ca","cert.delete"};
    for(unsigned n=0;n<5;++n)for(unsigned i=0;i<4;++i) {
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",cert_writes[n]);
        int previous=calls;bool allowed=i>=2;
        api(uri,"{\"session_id\":1,\"role\":\"root\",\"permission\":\"cert.manage\"}",privacy_auth[i],allowed?200:(i?403:401));
        assert(calls==previous+allowed);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s?session_id=1",cert_writes[n]);
        cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"url",url);cJSON_AddStringToObject(p,"method","POST");
        ts_api_result_t result={0};previous=calls;
        ts_api_automation_proxy_fetch_for_session(p,privacy_sid[i],&result);
        assert((result.code==TS_API_OK)==allowed&&calls==previous+allowed);
        ts_api_result_free(&result);cJSON_Delete(p);
    }
    for(unsigned n=0;n<5;n++){
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",cert_writes[n]);int previous=calls;
        api_method(uri,"{}","root",405,TS_HTTP_GET);assert(calls==previous);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",cert_writes[n]);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method","GET");
        ts_api_result_t result={0};ts_api_automation_proxy_fetch_for_session(input,1,&result);assert(result.code==TS_API_ERR_NOT_SUPPORTED&&calls==previous);ts_api_result_free(&result);cJSON_Delete(input);
    }
    api("/api/v1/cert/status",NULL,NULL,200);api("/api/v1/cert/get_certificate",NULL,NULL,200);
    puts("PASS PKI management metadata on actual HTTP/local proxy: anonymous/READ/WRITE denied before dispatch; original Admin/Root management and public status/PEM retained");
    int prior_source_calls=calls;api("/api/v1/automation/sources/list",NULL,NULL,401);assert(calls==prior_source_calls);
    api("/api/v1/automation/sources/list",NULL,"admin",200);assert(strstr(response,"weather")&&!strstr(response,"secret"));
    api("/api/v1/automation/sources/list",NULL,"root",200);assert(strstr(response,"secret")&&strstr(response,"alias-secret"));
    int before=calls;
    api("/api/v1/automation/sources/export","{}",NULL,401);assert(calls==before);
    api("/api/v1/automation/sources/export","{}","admin",403);assert(calls==before);
    api("/api/v1/automation/sources/export","{}","root",200);assert(calls==before+1);
    api("/api/v1/automation/actions/add","{}","admin",403);
    const char *home_operations[]={"services/list","services/status","services/stop","rules/trigger","actions/execute"};
    for(unsigned i=0;i<sizeof(home_operations)/sizeof(home_operations[0]);++i){
        char uri[96];snprintf(uri,sizeof(uri),"/api/v1/automation/%s",home_operations[i]);
        before=calls;api(uri,"{}",NULL,401);assert(calls==before);
        api(uri,"{}","admin",200);assert(calls==before+1);
        api(uri,"{}","root",200);assert(calls==before+2);
    }
    api("/api/v1/ui/widgets/set","{}",NULL,401);
    api("/api/v1/ui/widgets/set","{}","admin",200);
    api("/api/v1/ui/widgets/set","{}","root",200);
    api("/api/v1/ui/widgets/get",NULL,NULL,401);
    api("/api/v1/ui/widgets/get",NULL,"admin",200);
    api("/api/v1/storage/read","{\"path\":\"/sdcard/config/automation/state.json\"}",NULL,401);
    api("/api/v1/storage/read","{\"Path\":\"/sdcard/config/automation/state.json\"}",NULL,401);
    api("/api/v1/storage/read","{\"path\":\"/sdcard/config/automation/state.json\"}","root",200);
    api("/api/v1/storage/read","{\"path\":\"/sdcard/hello.txt\"}",NULL,401);
    api("/api/v1/storage/read","{\"path\":\"/sdcard/hello.txt\"}","admin",200);
    api("/api/v1/storage/read","{\"path\":\"/sdcard/config/wifi.json\"}","admin",200);
    file("/sdcard/config/automation/state.json",NULL,401);
    file("/sdcard/config/automation/state.json","admin",200);
    file("/sdcard/config/automation/state.json","root",200);
    file("/sdcard/hello.txt",NULL,401);
    file("/sdcard/hello.txt","admin",200);
    const char *writes[]={"storage.mount","storage.unmount","storage.delete","storage.mkdir","storage.rename"};
    for(unsigned i=0;i<sizeof(writes)/sizeof(writes[0]);++i) {
        char uri[96]; snprintf(uri,sizeof(uri),"/api/v1/%s",writes[i]);
        before=calls;api(uri,"{}",NULL,401);assert(calls==before);
        api(uri,"{}","admin",200);api(uri,"{}","root",200);assert(calls==before+2);
    }

    const unsigned storage_levels[]={0,1,3,4};
    const struct {const char *name;unsigned required;bool write;} storage_ops[]={
      {"storage.status",0,false},{"storage.mount",3,true},{"storage.unmount",3,true},{"storage.list",1,false},{"storage.delete",3,true},{"storage.mkdir",3,true},{"storage.rename",3,true},{"storage.info",1,false},{"storage.read",1,false}};
    for(unsigned n=0;n<9;n++)for(unsigned who=0;who<4;who++)for(unsigned get=0;get<2;get++){
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",storage_ops[n].name);int previous=calls;
        int expected=storage_levels[who]<storage_ops[n].required?(who?403:401):get&&storage_ops[n].write?405:200;bool allowed=expected==200;
        api_method(uri,"{\"path\":\"/sdcard/data/a\",\"role\":\"root\",\"session_id\":1}",privacy_auth[who],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+allowed);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",storage_ops[n].name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};ts_api_automation_proxy_fetch_for_session(input,privacy_sid[who],&result);assert(calls==previous+2*allowed);assert((result.code==0)==allowed);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS nine storage outer descriptors through HTTP/local proxy: data reads require login, ordinary writes Admin, mount controls original Admin, GET cannot mutate; private path callbacks independently tested");

    /* Actual scalar declarations from the remaining modules. The independent
     * role choices and runtime alias copying are verified in the 1409 checks. */
    const unsigned remaining_levels[]={0,1,3,4};
    for(unsigned n=0;n<sizeof(remaining_policies)/sizeof(*remaining_policies);n++){
        if(!strcmp(remaining_policies[n].name,"automation.proxy.fetch"))continue;
        unsigned minimum=remaining_policies[n].mode==TS_API_ACCESS_PUBLIC?0:remaining_policies[n].mode-TS_API_ACCESS_PUBLIC;
        for(unsigned who=0;who<4;who++)for(unsigned get=0;get<2;get++){
            char uri[128],url[180];snprintf(uri,sizeof(uri),"/api/v1/%s",remaining_policies[n].name);int previous=calls;
            bool method=(remaining_policies[n].methods&(get?TS_API_METHOD_GET:TS_API_METHOD_POST))!=0;
            int expected=remaining_levels[who]<minimum?(who?403:401):!method?405:200;bool allowed=expected==200;
            api_method(uri,"{\"role\":\"root\",\"session_id\":1}",privacy_auth[who],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+allowed);
            snprintf(url,sizeof(url),"http://localhost/api/v1/%s",remaining_policies[n].name);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
            ts_api_result_t r={0};ts_api_automation_proxy_fetch_for_session(input,privacy_sid[who],&r);assert(calls==previous+2*allowed&&(r.code==0)==allowed);ts_api_result_free(&r);cJSON_Delete(input);
        }
    }
    puts("PASS remaining77 outer registrations HTTP/local proxy x4 identities xGET/POST, including five aliases; broker78 separately exercises local inheritance and external Root gate");
    const char *private_exec[]={"ssh.exec_stream","ssh.exec_status","ssh.cancel"};
    for(unsigned n=0;n<3;n++)for(unsigned who=0;who<4;who++)for(unsigned get=0;get<2;get++){
        char uri[96],url[160];snprintf(uri,sizeof(uri),"/api/v1/%s",private_exec[n]);int previous=calls;
        int expected=who!=3?(who?403:401):get&&n!=1?405:200;bool allowed=expected==200;
        api_method(uri,"{\"owner_session_id\":1,\"session_id\":1,\"role\":\"root\"}",privacy_auth[who],expected,get?TS_HTTP_GET:TS_HTTP_POST);assert(calls==previous+allowed);
        snprintf(url,sizeof(url),"http://localhost/api/v1/%s",private_exec[n]);cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddStringToObject(input,"method",get?"GET":"POST");
        ts_api_result_t result={0};previous=calls;ts_api_automation_proxy_fetch_for_session(input,privacy_sid[who],&result);assert((result.code==TS_API_OK)==allowed&&calls==previous+allowed);ts_api_result_free(&result);cJSON_Delete(input);
    }
    puts("PASS private EXEC start/status/cancel coarse policies: Root and permitted methods through HTTP/local proxy; JSON owner fields cannot elevate");
    api("/api/v1/ui/widgets/set","{}","read",403);
    const char *targets[]={"system.memory","ssh.exec","automation.sources.export","automation.sources.list"};
    for(unsigned i=0;i<4;i++)for(uint32_t sid=0;sid<=2;sid++) {
        char url[160];snprintf(url,sizeof(url),"http://127.0.0.1/api/v1/%s",targets[i]);
        cJSON *params=cJSON_CreateObject();cJSON_AddStringToObject(params,"url",url);cJSON_AddStringToObject(params,"method","POST");
        cJSON_AddNumberToObject(params,"session_id",1);cJSON *headers=cJSON_AddObjectToObject(params,"headers");
        cJSON_AddStringToObject(headers,"Authorization","Bearer root");
        ts_api_result_t direct={0},proxy={0};
        ts_api_call_for_session(targets[i],NULL,sid,&direct);
        ts_api_automation_proxy_fetch_for_session(params,sid,&proxy);
        assert(direct.code==proxy.code);
        if(direct.code==TS_API_OK && !strcmp(targets[i],"automation.sources.list")) {
            char *d=cJSON_PrintUnformatted(direct.data),*p=cJSON_PrintUnformatted(proxy.data);
            assert(!!strstr(d,"secret")==!!strstr(p,"secret"));assert(!!strstr(p,"secret")== (sid==1));free(d);free(p);
        }
        ts_api_result_free(&direct);ts_api_result_free(&proxy);cJSON_Delete(params);
    }
    cJSON *params=cJSON_Parse("{\"url\":\"http://localhost/api/v1/ssh.exec\",\"session_id\":1}");
    cJSON_AddStringToObject(params,"method","POST");ts_api_result_t denied={0};before=calls;api_automation_proxy_fetch(params,&denied);
    assert(denied.code==TS_API_ERR_AUTH && calls==before);ts_api_result_free(&denied);cJSON_Delete(params);
    /* Legal legacy coercion is shared; malformed/ambiguous external parameters are rejected. */
    api("/api/v1/hosts/info?host=192.0.2.8&port=2222&label=A+B%252E&empty=",
        "{\"extra\":7}","read",200);
    assert(!strcmp(received_name,"hosts.info"));
    assert(cJSON_IsString(cJSON_GetObjectItem(received_params,"host"))&&!strcmp(cJSON_GetObjectItem(received_params,"host")->valuestring,"192.0.2.8"));
    assert(cJSON_GetObjectItem(received_params,"port")->valueint==2222&&cJSON_IsNumber(cJSON_GetObjectItem(received_params,"port")));
    assert(!strcmp(cJSON_GetObjectItem(received_params,"label")->valuestring,"A B%2E"));
    assert(!strcmp(cJSON_GetObjectItem(received_params,"empty")->valuestring,"")&&cJSON_GetObjectItem(received_params,"extra")->valueint==7);
    before=calls;api("/api/v1/hosts/info?bad=%Q2",NULL,"read",400);assert(calls==before);
    api("/api/v1/hosts/info?dup=first&dup=second",NULL,"read",400);assert(calls==before);
    api("/api/v1/hosts/info?host=query","{\"HOST\":\"body\"}","read",400);assert(calls==before);
    /* The trusted parser itself retains its historical grammar. */
    char legacy_query[]="bad=%Q2&h%6fst=key&bare&dup=first&dup=second";cJSON *legacy=cJSON_CreateObject();ts_api_parse_query(legacy,legacy_query);
    assert(!strcmp(cJSON_GetObjectItem(legacy,"bad")->valuestring,"%Q2")&&!strcmp(cJSON_GetObjectItem(legacy,"h%6fst")->valuestring,"key")&&!cJSON_GetObjectItem(legacy,"bare"));
    unsigned dups=0;for(cJSON *i=legacy->child;i;i=i->next)dups+=!strcmp(i->string,"dup");assert(dups==2);cJSON_Delete(legacy);
    const char *query_targets[]={"system.memory","hosts.info","ssh.exec","automation.sources.export","storage.read"};
    for(unsigned i=0;i<5;++i)for(uint32_t sid=0;sid<=3;++sid){
        const char *query="host=192.0.2.8&port=2222&label=A+B%252E&session_id=1&path=%2Fsdcard%2Fconfig%2Fautomation%2Fstate.json";
        cJSON *expected=cJSON_Parse("{\"host\":\"192.0.2.8\",\"port\":2222,\"label\":\"A B%2E\",\"session_id\":1,\"path\":\"/sdcard/config/automation/state.json\"}");
        ts_api_result_t direct={0},proxy_result={0};ts_api_call_for_session(query_targets[i],expected,sid,&direct);
        char url[320];snprintf(url,sizeof(url),"http://localhost/api/v1/%s?%s",query_targets[i],query);
        cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",url);cJSON_AddNumberToObject(input,"session_id",1);cJSON_AddStringToObject(input,"method","POST");
        int previous=calls;ts_api_automation_proxy_fetch_for_session(input,sid,&proxy_result);
        assert(proxy_result.code==direct.code);
        if(direct.code==TS_API_OK){
            assert(calls==previous+1&&!strcmp(received_name,query_targets[i]));
            assert(cJSON_IsString(cJSON_GetObjectItem(received_params,"host"))&&!strcmp(cJSON_GetObjectItem(received_params,"host")->valuestring,"192.0.2.8"));
            assert(cJSON_GetObjectItem(received_params,"port")->valueint==2222&&!strcmp(cJSON_GetObjectItem(received_params,"label")->valuestring,"A B%2E"));
        }else assert(calls==previous);
        ts_api_result_free(&direct);ts_api_result_free(&proxy_result);cJSON_Delete(input);cJSON_Delete(expected);
    }
    fail_query_copy=true;before=calls;
    cJSON *allocation_input=cJSON_Parse("{\"url\":\"http://localhost/api/v1/hosts/info?host=192.0.2.8\"}");ts_api_result_t allocation_result={0};
    assert(ts_api_automation_proxy_fetch_for_session(allocation_input,1,&allocation_result)==ESP_ERR_NO_MEM&&allocation_result.code==TS_API_ERR_NO_MEM&&calls==before);
    ts_api_result_free(&allocation_result);cJSON_Delete(allocation_input);fail_query_copy=false;
    const char *foreign_targets[]={"http://localhost.attacker/api/v1/system/info","http://127.0.0.10/api/v1/system/info","https://example.invalid/path?next=http://localhost/api/v1/system/info"};
    for(unsigned i=0;i<sizeof(foreign_targets)/sizeof(*foreign_targets);i++){
        cJSON *input=cJSON_CreateObject();cJSON_AddStringToObject(input,"url",foreign_targets[i]);ts_api_result_t denied={0};before=calls;
        ts_api_automation_proxy_fetch_for_session(input,3,&denied);
        assert(denied.code==TS_API_ERR_NO_PERMISSION&&calls==before); /* External targets cannot masquerade as the local namespace. */
        ts_api_result_free(&denied);cJSON_Delete(input);
    }
    cJSON *receipt_input=cJSON_Parse("{\"url\":\"http://localhost/api/v1/system/info\"}");ts_api_result_t unknown={0};
    cJSON_Hooks hooks={.malloc_fn=proxy_json_malloc,.free_fn=free};cJSON_InitHooks(&hooks);fail_after_business=true;before=calls;
    ts_api_automation_proxy_fetch_for_session(receipt_input,1,&unknown);fail_after_business=false;cJSON_InitHooks(NULL);
    assert(calls==before+1&&unknown.code==TS_API_ERR_INTERNAL&&!unknown.data&&unknown.message&&!strcmp(unknown.message,"result_unknown"));
    ts_api_result_free(&unknown);cJSON_Delete(receipt_input);
    puts("PASS exact local namespace vs foreign target,and actual broker never acknowledges success after response allocation failure following a substituted business effect");
    /* The verified HTTP caller, not query or nested JSON, supplies proxy identity. */
    before=calls;api("/api/v1/automation/proxy/fetch?session_id=1","{\"url\":\"http://localhost/api/v1/ssh/exec?session_id=1\",\"method\":\"POST\"}",NULL,401);assert(calls==before);

    api("/api/v1/automation/proxy/fetch","{\"url\":\"http://localhost/api/v1/hosts/info?host=192.0.2.8&port=2222\"}","read",200);
    assert(!strcmp(received_name,"hosts.info")&&!strcmp(cJSON_GetObjectItem(received_params,"host")->valuestring,"192.0.2.8"));
    session_live=false;
    before=calls;api("/api/v1/cert/install","{\"cert_pem\":\"synthetic\"}","root",401);assert(calls==before);
    api("/api/v1/config/module/show","{\"module\":\"wifi\"}","root",401);assert(calls==before);
    api("/api/v1/config/get","{\"key\":\"custom.credential\"}","root",401);
    api("/api/v1/system/memory",NULL,"root",200);api("/api/v1/ssh/exec","{}","root",401);
    before=calls;api("/api/v1/automation/sources/list",NULL,"root",401);assert(calls==before);session_live=true;

    unsigned outbound_before=outbound;allow_outbound=true;
    api("/api/v1/automation/proxy/fetch","{\"url\":\"https://fixture.invalid/data?secret=synthetic\",\"role\":\"root\",\"session_id\":1}",NULL,401);assert(outbound==outbound_before);
    for(unsigned who=1;who<3;who++){
        api("/api/v1/automation/proxy/fetch","{\"url\":\"https://fixture.invalid/data\",\"role\":\"root\",\"session_id\":1}",privacy_auth[who],403);assert(outbound==outbound_before);
    }
    api("/api/v1/automation/proxy/fetch","{\"url\":\"https://fixture.invalid/data\"}","root",200);assert(outbound==outbound_before+1);allow_outbound=false;
    puts("PASS external proxy Root gate before synthetic network sink; anonymous denied outside body, forged JSON cannot elevate; local destination policy retained");
    cJSON_Delete(received_params);free(response);puts("PASS shared HTTP/local proxy roles, public reads, protected paths, redaction, forged JSON and revoked sessions");
}
'''

idf = Path(os.environ.get('IDF_PATH', '/Users/massif/esp/v5.5.2/esp-idf'))
cjson = idf / 'components/json/cJSON'
with tempfile.TemporaryDirectory(prefix='ts-sensitive-auth-') as directory:
    build = Path(directory)
    (build / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-g', '-Wno-deprecated-declarations', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(cjson), *['-I'+str(p) for p in url_native_includes],
                    str(build / 'test.c'), str(cjson / 'cJSON.c'), *map(str,url_native_sources), '-o', str(build / 'test')], check=True)
    subprocess.run([str(build / 'test')], check=True)
    noauth = code.replace('#define CONFIG_TS_WEBUI_AUTH_REQUIRED 1', '')
    start = noauth.index('int main(void) {')
    noauth = noauth[:start] + r"""
int main(void) {
    api("/api/v1/fixture/internal","not-json",NULL,405);
    api("/api/v1/automation/sources/export","{}",NULL,200);
    api("/api/v1/ui/widgets/set","{}",NULL,200);
    api("/api/v1/ui/widgets/get",NULL,NULL,200);
    api("/api/v1/storage/read","{\"path\":\"/sdcard/config/automation/state.json\"}",NULL,200);
    api("/api/v1/automation/sources/list",NULL,NULL,200);assert(strstr(response,"secret"));
    cJSON_Delete(received_params);free(response);puts("PASS auth-disabled shared API preserves original public/internal behavior");
}
"""
    (build / 'noauth.c').write_text(noauth)
    subprocess.run(['cc', '-std=gnu11', '-g', '-Wno-deprecated-declarations', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I' + str(cjson), *['-I'+str(p) for p in url_native_includes],
                    str(build / 'noauth.c'), str(cjson / 'cJSON.c'), *map(str,url_native_sources), '-o', str(build / 'noauth')], check=True)
    subprocess.run([str(build / 'noauth')], check=True)


# Credential/session coverage lives in test_auth_persistence.py: one production
# owner and one adversarial NVS fixture, including logs and malformed tokens.
