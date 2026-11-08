#!/usr/bin/env python3
"""Production external policy and config handlers; synthetic store/session only."""
from pathlib import Path
import argparse,os,re,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[2]
IDF=Path(os.environ.get("IDF_PATH","/Users/massif/esp/v5.5.2/esp-idf"))
parser=argparse.ArgumentParser()
parser.add_argument("--source-ref")
parser.add_argument("--core-source",type=Path)
args=parser.parse_args()
def function(path,name,optional=False):
    source=args.core_source.read_text() if path=="components/ts_api/src/ts_api.c" and args.core_source else subprocess.check_output(["git","show",f"{args.source_ref}:{path}"],cwd=ROOT,text=True) if args.source_ref else (ROOT/path).read_text()
    match=re.search(r"^(?:static )?[^\n;]+\b"+name+r"\([^;]+?\)\s*\{",source,re.M)
    if not match and optional:return ""
    assert match,name
    return source[match.start():source.index("\n}",match.start())+2]+"\n"

code=r'''
#define _DEFAULT_SOURCE
#define CONFIG_TS_WEBUI_AUTH_REQUIRED 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "ts_api.h"
#include "ts_security.h"
#include "ts_config_module.h"
#define TS_LOGD(...) ((void)0)
#define TS_LOGW(...) ((void)0)
#define TS_LOGE(...) ((void)0)
#define TS_STRDUP_PSRAM strdup
#define ESP_ERR_NOT_ALLOWED 0x10d
static unsigned reads,writes,calls,checks,failures;
static bool session_live=true,session_downgrade, revoke_during_read, downgrade_during_read;
static const char *row_response;
static bool effect_response, allocation_stop_after_effect;
static void *deny_allocation(size_t size){(void)size;return NULL;}
static char password[80]="SYNTHETIC_AP_PASSWORD";
static void check(bool value,const char *label){checks++;if(!value){failures++;fprintf(stderr,"FAIL %s\n",label);}}
esp_err_t ts_security_validate_session(uint32_t sid,ts_session_t *out){
 if(!session_live||sid<1||sid>5)return ESP_ERR_NOT_FOUND;
 memset(out,0,sizeof(*out));out->session_id=sid;out->level=sid==1?(session_downgrade?TS_PERM_READ:TS_PERM_ROOT):sid==2?TS_PERM_ADMIN:sid==3?TS_PERM_READ:sid==5?TS_PERM_WRITE:TS_PERM_NONE;return ESP_OK;
}
esp_err_t ts_storage_check_write_path(const char *p,bool children){(void)p;(void)children;return ESP_OK;}
bool ts_config_module_is_registered(ts_config_module_t m){return m==TS_CONFIG_MODULE_WIFI||m==TS_CONFIG_MODULE_SYSTEM;}
bool ts_config_module_is_dirty(ts_config_module_t m){(void)m;return false;}
esp_err_t ts_config_module_get_bool(ts_config_module_t m,const char *k,bool *v){(void)m;(void)k;reads++;*v=false;return ESP_OK;}
esp_err_t ts_config_module_get_uint(ts_config_module_t m,const char *k,uint32_t *v){(void)m;(void)k;reads++;*v=6;return ESP_OK;}
esp_err_t ts_config_module_get_string(ts_config_module_t m,const char *k,char *v,size_t n){
 reads++;if(revoke_during_read)session_live=false;if(downgrade_during_read)session_downgrade=true;const char *value=NULL;
 if(m==TS_CONFIG_MODULE_WIFI){
  if(!strcmp(k,"mode"))value="ap";
  if(!strcmp(k,"ap.ssid"))value="Lab + 中文";
  if(!strcmp(k,"ap.password"))value=password;
 }
 if(m==TS_CONFIG_MODULE_SYSTEM&&!strcmp(k,"timezone"))value="UTC";
 if(!value)return ESP_ERR_NOT_FOUND;snprintf(v,n,"%s",value);return ESP_OK;
}
esp_err_t ts_config_module_set_bool(ts_config_module_t m,const char *k,bool v){(void)m;(void)k;(void)v;assert(false);return ESP_FAIL;}
esp_err_t ts_config_module_set_uint(ts_config_module_t m,const char *k,uint32_t v){(void)m;(void)k;(void)v;assert(false);return ESP_FAIL;}
esp_err_t ts_config_module_set_int(ts_config_module_t m,const char *k,int32_t v){(void)m;(void)k;(void)v;assert(false);return ESP_FAIL;}
esp_err_t ts_config_module_set_string(ts_config_module_t m,const char *k,const char *v){
 assert(m==TS_CONFIG_MODULE_WIFI&&!strcmp(k,"ap.password"));writes++;snprintf(password,sizeof(password),"%s",v);return ESP_OK;
}
esp_err_t ts_config_get_int64(const char *k,int64_t *v,int64_t d){(void)k;(void)v;(void)d;reads++;return ESP_ERR_NOT_FOUND;}
esp_err_t ts_config_get_bool(const char *k,bool *v,bool d){(void)k;(void)v;(void)d;reads++;return ESP_ERR_NOT_FOUND;}
esp_err_t ts_config_get_double(const char *k,double *v,double d){(void)k;(void)v;(void)d;reads++;return ESP_ERR_NOT_FOUND;}
esp_err_t ts_config_get_string(const char *k,char *v,size_t n,const char *d){
 (void)d;reads++;if(revoke_during_read)session_live=false;if(downgrade_during_read)session_downgrade=true;snprintf(v,n,"%s",!strcmp(k,"system.language")?"zh-CN":"SYNTHETIC_ARBITRARY_CREDENTIAL");return ESP_OK;
}
cJSON *ts_api_get_info(const char *name){
 cJSON *info=cJSON_CreateObject();cJSON_AddBoolToObject(info,"requires_auth",!strcmp(name,"config.module.set"));return info;
}
'''
for name in ["ts_api_result_init","ts_api_result_ok","ts_api_result_error","ts_api_result_free"]:
    code+=function("components/ts_api/src/ts_api.c",name)
for name in ["parse_module_name","module_to_name","add_module_config_to_json","api_config_module_show","api_config_get","api_config_module_set"]:
    code+=function("components/ts_api/src/ts_api_config.c",name)
code+='static char s_ota_server_url[256]=\"https://synthetic-user:synthetic-pass@example.invalid/?token=PRIVATE-CANARY\";\n'
code+=function('components/ts_api/src/ts_api_ota.c','api_ota_server_get')
code+=r'''
esp_err_t ts_api_call(const char *name,const cJSON *p,ts_api_result_t *result){
 calls++;
 if(!strcmp(name,"config.module.show"))return api_config_module_show(p,result);
 if(!strcmp(name,"config.get"))return api_config_get(p,result);
 if(!strcmp(name,"config.module.set"))return api_config_module_set(p,result);
 assert(false);return ESP_FAIL;
}
'''
# Coarse policy comes from the actual source descriptor table. This fixture
# controls the store/session boundary; the full real registry is tested separately.
configuration_source=subprocess.check_output(["git","show",f"{args.source_ref}:components/ts_api/src/ts_api_config.c"],cwd=ROOT,text=True) if args.source_ref else (ROOT/"components/ts_api/src/ts_api_config.c").read_text()
start=configuration_source.index("static const ts_api_endpoint_t config_apis[]")
end=configuration_source.index("\n    };",start)+7
metadata=configuration_source[start:end]
metadata=re.sub(r"\.handler\s*=\s*\w+", ".handler = NULL", metadata)
code+=metadata+"\n"
code+=r'''
esp_err_t ts_api_get_access(const char *name,ts_api_access_t *access){
 if(row_response){*access=(ts_api_access_t){.external_access=effect_response?TS_API_ACCESS_ADMIN:TS_API_ACCESS_READ,.minimum_level=effect_response?TS_PERM_ADMIN:TS_PERM_READ,.requires_auth=true,.allowed_methods=effect_response?TS_API_METHOD_WRITE:TS_API_METHOD_READ};return ESP_OK;}
 for(unsigned i=0;i<sizeof(config_apis)/sizeof(*config_apis);i++)if(!strcmp(name,config_apis[i].name)){
  const ts_api_endpoint_t *e=&config_apis[i];ts_api_external_access_t mode=e->external_access;
  *access=(ts_api_access_t){.external_access=mode,.allowed_methods=e->allowed_methods?e->allowed_methods:TS_API_METHOD_ALL};
  access->requires_auth=mode==TS_API_ACCESS_LEGACY?e->requires_auth:mode>=TS_API_ACCESS_READ&&mode<=TS_API_ACCESS_ROOT;
  access->minimum_level=mode==TS_API_ACCESS_LEGACY?TS_PERM_READ:access->requires_auth?mode-TS_API_ACCESS_PUBLIC:TS_PERM_NONE;return ESP_OK;
 }
 return ESP_ERR_NOT_FOUND;
}
'''
code+=r'''
static esp_err_t private_row_handler(const cJSON *params,ts_api_result_t *result){
 (void)params;++calls;cJSON *data=cJSON_Parse(row_response);
 if(!data){ts_api_result_error(result,TS_API_ERR_NO_MEM,"no_memory");return ESP_ERR_NO_MEM;}
 ts_api_result_ok(result,data);if(allocation_stop_after_effect){cJSON_Hooks hooks={deny_allocation,free};cJSON_InitHooks(&hooks);}if(revoke_during_read)session_live=false;if(downgrade_during_read)session_downgrade=true;
 return ESP_OK;
}
'''
code+='static esp_err_t api_call_dispatch(const char *name,const cJSON *p,uint32_t sid,bool external,ts_api_result_t *r){(void)sid;(void)external;return ts_api_call(name,p,r); }\n'
code+='static esp_err_t privacy_show(const cJSON *p,ts_api_result_t *r){calls++;return api_config_module_show(p,r);}\nstatic esp_err_t privacy_get(const cJSON *p,ts_api_result_t *r){calls++;return api_config_get(p,r);}\nstatic esp_err_t privacy_set(const cJSON *p,ts_api_result_t *r){calls++;return api_config_module_set(p,r);}\n/* The registry snapshot is the boundary in this store-focused fixture; actual\n * atomic registry replacement is covered in test_default_deny_policy.py. */\nstatic esp_err_t copy_external_call(const char *name,ts_api_access_t *access,ts_api_handler_t *handler,ts_api_session_handler_t *session_handler){\n esp_err_t ret=ts_api_get_access(name,access);if(ret!=ESP_OK)return ret;*session_handler=NULL;\n *handler=row_response?private_row_handler:!strcmp(name,"config.module.show")?privacy_show:!strcmp(name,"config.get")?privacy_get:privacy_set;return ESP_OK;\n}\n'
code+=function("components/ts_api/src/ts_api.c","project_config_fields",optional=True)
code+=function("components/ts_api/src/ts_api.c","config_read_projection",optional=True)
code+=function("components/ts_api/src/ts_api.c","project_result_fields",optional=True)
code+=function("components/ts_api/src/ts_api.c","private_result_projection",optional=True)
code+=function("components/ts_api/src/ts_api.c","ts_api_call_for_session")
code+=r'''
static void module(uint32_t sid,const char *name,bool secret){
 cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"module",name);
 ts_api_result_t result={0};unsigned before=writes,read_before=reads,call_before=calls;
 esp_err_t ret=ts_api_call_for_session("config.module.show",p,sid,&result);
 if(!session_live||sid<1||sid>5){
  check(ret!=ESP_OK,"anonymous/invalid/revoked module read refused");check(reads==read_before&&calls==call_before&&!result.data,"module denial before handler/store/secret");
  ts_api_result_free(&result);cJSON_Delete(p);return;
 }
 check(ret==ESP_OK,"authenticated module show succeeds");
 cJSON *wifi=cJSON_GetObjectItemCaseSensitive(result.data,!strcasecmp(name,"all")?"wifi":"config");
 check(cJSON_IsObject(wifi),"original single/all response shape");
 check(!!cJSON_GetObjectItemCaseSensitive(wifi,"ap.password")==secret,"AP password visible to authenticated administrators");
 check(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(wifi,"ap.ssid")),"SSID remains available");
 check(cJSON_GetObjectItemCaseSensitive(wifi,"ap.channel")->valueint==6,"channel remains available");
 check(writes==before,"projection never writes settings");
 ts_api_result_free(&result);cJSON_Delete(p);
}

static int json_allocations,json_fail_at;
static void *fail_json_allocation(size_t size){return ++json_allocations==json_fail_at?NULL:malloc(size);}
static void projection_checks(void){
 const char *names[]={"ssh.hosts.list","ssh.hosts.get","ssh.commands.list","ssh.commands.get","automation.sources.list","automation.rules.list","automation.rules.get","automation.actions.list","automation.actions.get","ota.server.get"};
 const char *collections[]={"hosts",NULL,"commands",NULL,"sources","rules",NULL,"templates",NULL,NULL};
 const char *row="{\"configured\":true,\"id\":\"public-id\",\"name\":\"Public name\",\"label\":\"Public label\",\"enabled\":true,\"host\":\"192.0.2.4\",\"port\":22,\"type\":\"rest\",\"runtime_active\":true,\"revision\":7,\"conditions_count\":2,\"actions_count\":3,\"trigger_count\":4,\"automatic_start\":{\"state\":\"waiting\",\"reason\":\"automatic_start_timeout\",\"retry_after_ms\":123,\"unknown\":\"PRIVATE-CANARY\"},\"ssh_commands\":[\"command-id\",{\"command\":\"PRIVATE-CANARY\"}],\"runtime_instance\":\"0000000000000001\",\"poll_interval_ms\":1000,\"connected\":true,\"mapping_count\":2,\"async\":false,\"use_count\":3,\"username\":\"PRIVATE-CANARY\",\"keyid\":\"PRIVATE-CANARY\",\"command\":\"PRIVATE-CANARY\",\"expectPattern\":\"PRIVATE-CANARY\",\"conditions\":[{\"expr\":\"PRIVATE-CANARY\"}],\"actions\":[{\"http\":{\"body\":\"PRIVATE-CANARY\"}}],\"rest_url\":\"https://example.invalid/?token=PRIVATE-CANARY\",\"auth_header\":\"PRIVATE-CANARY\",\"rest_auth\":\"PRIVATE-CANARY\",\"last_value\":\"PRIVATE-CANARY\",\"mappings\":[{\"secret\":\"PRIVATE-CANARY\"}],\"cli\":{\"command\":\"PRIVATE-CANARY\"},\"value\":\"PRIVATE-CANARY\",\"new_future_field\":{\"value\":\"PRIVATE-CANARY\"}}";
 cJSON *params=cJSON_Parse("{\"role\":\"root\",\"session_id\":1,\"include_private\":true,\"show_hidden\":true,\"fields\":\"*\"}");
 for(unsigned n=0;n<sizeof names/sizeof *names;++n){
  char raw[4096];if(collections[n])snprintf(raw,sizeof raw,"{\"%s\":[%s],\"count\":1,\"active_version\":\"version-one\",\"loaded\":true,\"new_secret\":\"PRIVATE-CANARY\"}",collections[n],row);else snprintf(raw,sizeof raw,"%s",row);
  row_response=raw;
  for(uint32_t sid=0;sid<=5;++sid){
   ts_api_result_t result={0};unsigned before=calls;bool allowed=sid>0&&sid!=4;bool full=sid==1||(sid==2&&strcmp(names[n],"automation.sources.list"));
   esp_err_t ret=ts_api_call_for_session(names[n],params,sid,&result);
   check((ret==ESP_OK)==allowed,"configuration metadata enforces live C identity, including NONE and WRITE");
   if(!allowed)check(calls==before&&!result.data,"denied metadata never calls producer or returns rows");
   else {
    char *json=cJSON_PrintUnformatted(result.data);check(json!=NULL,"metadata serializes");
    check((strstr(json,"PRIVATE-CANARY")!=NULL)==full,"all raw/nested/URL/unknown fields visible to original authenticated administrators");
    check(!strcmp(names[n],"ota.server.get") ? cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result.data,"configured")) : strstr(json,"Public name")!=NULL,"approved names/labels or configured state preserved");
    check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result.data,"restricted"))==(!full),"explicit projection marker");
    if(!full&&collections[n])check(cJSON_GetObjectItemCaseSensitive(result.data,"count")->valueint==1&&cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(result.data,collections[n])),"list shape/count survives projection");
    cJSON *item=collections[n]?cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(result.data,collections[n]),0):result.data;
    if(!full&&!strcmp(names[n],"automation.rules.list")){
     cJSON *start=cJSON_GetObjectItem(item,"automatic_start");check(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(start,"state")),"waiting")&&cJSON_GetObjectItem(start,"retry_after_ms")->valueint==123,"automatic restart status remains operational");
     check(cJSON_GetArraySize(cJSON_GetObjectItem(item,"ssh_commands"))==1,"service command ID references retained without arbitrary objects");
    }
    if(!full&&!strcmp(names[n],"ssh.hosts.list"))check(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(item,"host")),"192.0.2.4"),"approved host address telemetry retained");
    free(json);
   }
   ts_api_result_free(&result);
  }
  for(int downgrade=0;downgrade<2;++downgrade){
   revoke_during_read=!downgrade;downgrade_during_read=downgrade;ts_api_result_t result={0};
   check(ts_api_call_for_session(names[n],params,1,&result)!=ESP_OK&&!result.data,"Root response discarded when revoked or downgraded during producer read");
   ts_api_result_free(&result);session_live=true;session_downgrade=revoke_during_read=downgrade_during_read=false;
  }
  for(int point=1;point<=256;++point){
   ts_api_result_t result={0};json_allocations=0;json_fail_at=point;cJSON_Hooks hooks={fail_json_allocation,free};cJSON_InitHooks(&hooks);
   esp_err_t ret=ts_api_call_for_session(names[n],params,3,&result);cJSON_InitHooks(NULL);
   if(ret!=ESP_OK)check(result.code!=TS_API_OK&&!result.data,"allocation failures expose no partial raw data");
   else {char *text=cJSON_PrintUnformatted(result.data);check(text&&!strstr(text,"PRIVATE-CANARY"),"every successful allocation outcome is projected");free(text);}
   ts_api_result_free(&result);
  }
 }

 effect_response=true;
 const char *effect="{\"async\":false,\"status\":\"done\",\"duration_ms\":42,\"output\":\"PRIVATE-CANARY\",\"detail\":\"PRIVATE-CANARY\",\"unknown\":\"PRIVATE-CANARY\"}";
 row_response=effect;
 for(unsigned sid=0;sid<=5;++sid){
  ts_api_result_t result={0};unsigned before=calls;bool allowed=sid==1||sid==2;
  esp_err_t ret=ts_api_call_for_session("automation.actions.execute",params,sid,&result);
  check((ret==ESP_OK)==allowed&&calls==before+allowed,"existing action capability stays Admin/Root with one execution");
  if(allowed){char *json=cJSON_PrintUnformatted(result.data);check((strstr(json,"PRIVATE-CANARY")!=NULL)==(sid==1||sid==2),"private action output kept for original authenticated admin");check(cJSON_GetObjectItem(result.data,"duration_ms")->valueint==42,"duration and successful write outcome preserved");free(json);}
  ts_api_result_free(&result);
 }
 for(unsigned changed=0;changed<3;++changed){
  ts_api_result_t result={0};unsigned before=calls;revoke_during_read=changed==0;downgrade_during_read=changed==1;allocation_stop_after_effect=changed==2;
  unsigned sid=changed==2?2:1;esp_err_t ret=ts_api_call_for_session("automation.actions.execute",params,sid,&result);cJSON_InitHooks(NULL);
  check(ret==ESP_OK&&result.code==TS_API_OK&&calls==before+1,"post-effect revoke/downgrade/OOM never creates a false failed write");
  char *json=cJSON_PrintUnformatted(result.data);check(json&&((strstr(json,"PRIVATE-CANARY")!=NULL)==(changed==2))&&cJSON_GetObjectItem(result.data,"duration_ms")->valueint==42,"live admin output preserved; revoked or downgraded acknowledgement safely redacted");free(json);ts_api_result_free(&result);
  session_live=true;session_downgrade=revoke_during_read=downgrade_during_read=allocation_stop_after_effect=false;
 }
 effect_response=false;row_response=NULL;cJSON_Delete(params);
}
int main(void){
 projection_checks();
 const char *private_reads[]={"config.get","config.module.show"};
 for(unsigned operation=0;operation<2;++operation)for(unsigned changed=0;changed<2;++changed){
  cJSON *request=cJSON_Parse(operation?"{\"module\":\"wifi\"}":"{\"key\":\"custom.credential\"}");ts_api_result_t response={0};
  revoke_during_read=changed==0;downgrade_during_read=changed==1;
  check(ts_api_call_for_session(private_reads[operation],request,1,&response)!=ESP_OK&&!response.data,"actual raw config/module read drops secret after in-flight Root revoke/downgrade");
  ts_api_result_free(&response);cJSON_Delete(request);session_live=true;session_downgrade=revoke_during_read=downgrade_during_read=false;
 }

 ts_api_result_t ota={0};
 check(api_ota_server_get(NULL,&ota)==ESP_OK&&cJSON_IsTrue(cJSON_GetObjectItem(ota.data,"configured"))&&!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(ota.data,"url")),s_ota_server_url),"actual trusted getter keeps exact configured URL");ts_api_result_free(&ota);
 s_ota_server_url[0]=0;check(api_ota_server_get(NULL,&ota)==ESP_OK&&cJSON_IsFalse(cJSON_GetObjectItem(ota.data,"configured"))&&!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(ota.data,"url")),""),"actual empty configuration distinguishable from redaction");ts_api_result_free(&ota);

 for(uint32_t sid=0;sid<=3;sid++){
  module(sid,"wifi",sid==1||sid==2);module(sid,"WiFi",sid==1||sid==2);module(sid,"all",sid==1||sid==2);
  const char *keys[]={"system.language","custom.credential","ordinary.hostname"};
  for(unsigned i=0;i<3;i++){
   cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"KeY",keys[i]);
   cJSON_AddNumberToObject(p,"session_id",1);cJSON_AddStringToObject(p,"role","root");
   ts_api_result_t result={0};unsigned before=reads,submitted=calls;
   bool allowed=i==0||sid==1||sid==2;
   esp_err_t ret=ts_api_call_for_session("config.get",p,sid,&result);
   check(allowed?ret==ESP_OK:ret!=ESP_OK,"generic raw config role policy");
   if(!allowed){check(reads==before&&calls==submitted,"raw denial before store/handler");check(!result.data,"denial exposes no value");}
   else check(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(result.data,"value")),i==0?"zh-CN":"SYNTHETIC_ARBITRARY_CREDENTIAL"),"allowed config result preserved");
   ts_api_result_free(&result);cJSON_Delete(p);
  }
 }
 session_live=false;module(1,"wifi",false);module(99,"all",false);
 cJSON *p=cJSON_Parse("{\"key\":\"custom.credential\"}");ts_api_result_t result={0};unsigned before=reads;
 check(ts_api_call_for_session("config.get",p,1,&result)!=ESP_OK&&reads==before,"revoked root cannot read raw config");
 ts_api_result_free(&result);cJSON_Delete(p);session_live=true;
 p=cJSON_Parse("{\"module\":\"wifi\",\"key\":\"ap.password\",\"value\":\"NEW_SYNTHETIC_PASSWORD\"}");
 check(ts_api_call_for_session("config.module.set",p,3,&result)!=ESP_OK&&writes==0,"READ cannot change generic module settings");ts_api_result_free(&result);
 check(ts_api_call_for_session("config.module.set",p,2,&result)==ESP_OK,"Original admin configuration update restored");
 ts_api_result_free(&result);cJSON_Delete(p);
 check(writes==1&&!strcmp(password,"NEW_SYNTHETIC_PASSWORD"),"explicit supplied password retained, no mask writeback");
 module(2,"wifi",true);module(1,"wifi",true);
 p=cJSON_Parse("{\"module\":\"wifi\"}");before=writes;
 check(ts_api_call("config.module.show",p,&result)==ESP_OK,"trusted internal caller unchanged");
 check(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(result.data,"config"),"ap.password")),"internal result retains original secret");
 check(writes==before,"all read paths leave stored credential unchanged");
 ts_api_result_free(&result);cJSON_Delete(p);
 printf("%u checks, %u failures: actual policy/config handlers, synthetic store/session; no real secrets\n",checks,failures);
 return failures?1:0;
}
'''
includes=[ROOT/"tests/runtime/rule_pack/stubs"]
includes += [ROOT/("components/"+p+"/include") for p in ["ts_api","ts_security","ts_core/ts_config"]]
includes += [IDF/"components/json/cJSON"]
with tempfile.TemporaryDirectory(prefix="ts-config-privacy-") as tmp:
    c=Path(tmp)/"test.c";exe=Path(tmp)/"test";c.write_text(code)
    subprocess.run(["cc","-std=gnu11","-g","-fsanitize=address,undefined","-fno-sanitize-recover=all",
        "-Wno-deprecated-declarations",*["-I"+str(p) for p in includes],str(c),
        str(IDF/"components/json/cJSON/cJSON.c"),"-lm","-o",str(exe)],check=True,cwd=ROOT)
    subprocess.run([str(exe)],check=True,cwd=ROOT)
