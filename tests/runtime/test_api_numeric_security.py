#!/usr/bin/env python3
"""Actual API handlers, SDK cJSON, synthetic hardware/storage/clock boundaries."""
from pathlib import Path
import argparse, os, re, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--source-ref", help="Read production functions from a git revision for before/after evidence")
args = parser.parse_args()
IDF = Path(os.environ.get("IDF_PATH", "/Users/massif/esp/v5.5.2/esp-idf"))
def body(path, name, optional=False):
    source = subprocess.check_output(["git", "show", f"{args.source_ref}:{path}"], cwd=ROOT, text=True) if args.source_ref else (ROOT/path).read_text()
    m = re.search(r"^(?:static )?[^\n;]+\b"+name+r"\([^;]+?\)\s*\{",source,re.M)
    if not m and optional:return ""
    assert m,name
    return source[m.start():source.index("\n}",m.start())+2]+"\n"

code=r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "ts_api.h"
#include "ts_security.h"
#include "ts_variable.h"
#define CONFIG_TS_WEBUI_AUTH_REQUIRED 1
#include "ts_fan.h"
#include "ts_led.h"
#include "ts_temp_source.h"
#include "ts_time_sync.h"
#include "ts_config_pack.h"
#include "ts_storage.h"
#define TS_LOGI(...) ((void)0)
#define TS_LOGW(...) ((void)0)
#define TS_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define TS_CONFIG_MODULE_FAN 5
#define ESP_ERR_NOT_ALLOWED 0x10d
#define TS_STRDUP_PSRAM strdup
static unsigned mutations, saves, pack_creates, security_sets;
static bool get_fail, alloc_fail;
static unsigned min_seen,max_seen,brightness_seen;
static int64_t integer_seen,time_seen;
static double double_seen;
static int16_t temp_seen;
static ts_fan_config_t config={.min_duty=20,.max_duty=100,.hysteresis=30,.min_interval=2000};
esp_err_t ts_fan_get_config(ts_fan_id_t id,ts_fan_config_t *out) {(void)id;if(get_fail)return ESP_FAIL;*out=config;return ESP_OK;}
esp_err_t ts_fan_set_limits(ts_fan_id_t id,uint8_t min,uint8_t max){(void)id;mutations++;min_seen=min;max_seen=max;return ESP_OK;}
esp_err_t ts_fan_set_curve(ts_fan_id_t id,const ts_fan_curve_point_t *c,uint8_t n){(void)id;(void)c;(void)n;mutations++;return ESP_OK;}
esp_err_t ts_fan_set_hysteresis(ts_fan_id_t id,int16_t h,uint32_t t){(void)id;assert(h>=0&&h<=200&&t>=100&&t<=60000);mutations++;return ESP_OK;}
esp_err_t ts_fan_save_full_config(ts_fan_id_t id){(void)id;saves++;return ESP_OK;}
static bool ts_config_module_is_registered(int m){(void)m;return true;}
static int ts_config_module_persist(int m){(void)m;saves++;return ESP_OK;}
static const char *resolve_device_name(const char *n){return n;}
ts_led_device_t ts_led_device_get(const char *n){(void)n;return (ts_led_device_t)1;}
esp_err_t ts_led_device_set_brightness(ts_led_device_t d,uint8_t n){(void)d;mutations++;brightness_seen=n;return ESP_OK;}
uint8_t ts_led_device_get_brightness(ts_led_device_t d){(void)d;return brightness_seen;}
esp_err_t ts_temp_set_manual(int16_t n){mutations++;temp_seen=n;return ESP_OK;}
esp_err_t ts_temp_set_manual_mode(bool n){(void)n;mutations++;return ESP_OK;}
bool ts_temp_is_manual_mode(void){return true;}
int16_t ts_temp_get_effective(ts_temp_data_t *d){memset(d,0,sizeof(*d));return 0;}
const char *ts_temp_source_type_to_str(ts_temp_source_type_t s){(void)s;return "manual";}
esp_err_t ts_time_sync_set_time(int64_t n,ts_time_source_t s){(void)s;mutations++;time_seen=n;return ESP_OK;}
static int ts_config_set_bool(const char *k,bool n){(void)k;(void)n;mutations++;return ESP_OK;}
static int ts_config_set_int64(const char *k,int64_t n){(void)k;mutations++;integer_seen=n;return ESP_OK;}
static int ts_config_set_double(const char *k,double n){(void)k;mutations++;double_seen=n;return ESP_OK;}
static int ts_config_set_string(const char *k,const char *n){(void)k;(void)n;mutations++;return ESP_OK;}
bool ts_config_pack_can_export(void){return true;}
ts_config_pack_result_t ts_config_pack_create(const char *n,const char *j,size_t len,const ts_config_pack_export_opts_t *o,char **out,size_t *size){
 (void)n;(void)o;assert(len==strlen(j));pack_creates++;*out=strdup("{}");*size=2;return TS_CONFIG_PACK_OK;
}
static void pack_failure(ts_api_result_t *r,ts_config_pack_result_t e){(void)e;ts_api_result_error(r,TS_API_ERR_INTERNAL,"fixture");}
esp_err_t ts_storage_check_write_path(const char *p,bool b){(void)p;(void)b;assert(false);return ESP_FAIL;}
esp_err_t ts_storage_check_external_write_path(const char *p,bool b){return ts_storage_check_write_path(p,b);}
esp_err_t ts_storage_mkdir_p(const char *p){(void)p;assert(false);return ESP_FAIL;}
esp_err_t ts_storage_write_file(const char *p,const void *d,size_t n){(void)p;(void)d;(void)n;assert(false);return ESP_FAIL;}
esp_err_t ts_config_pack_save(const char *p,const char *d,size_t n){(void)p;(void)d;(void)n;assert(false);return ESP_FAIL;}
static char *export_strdup(const char *s){return alloc_fail?NULL:strdup(s);}
static char *export_print(const cJSON *j){return alloc_fail?NULL:cJSON_PrintUnformatted(j);}
'''

code+=r'''
static ts_temp_bound_var_t shared_bindings[TS_TEMP_MAX_BOUND_VARS];
static uint8_t shared_binding_count;
static unsigned variable_reads;
static bool caller_live=true;
esp_err_t ts_security_validate_session(uint32_t sid,ts_session_t *session){
 if(!caller_live||sid<1||sid>5)return ESP_ERR_NOT_FOUND;
 session->level=sid==1?TS_PERM_ROOT:sid==2?TS_PERM_ADMIN:sid==3?TS_PERM_READ:sid==4?TS_PERM_NONE:TS_PERM_WRITE;return ESP_OK;
}
static int64_t esp_timer_get_time(void){return 3000000;}
esp_err_t ts_temp_bind_variables(const ts_temp_bound_var_t *vars,uint8_t count){assert(count<=TS_TEMP_MAX_BOUND_VARS);++mutations;memcpy(shared_bindings,vars,count*sizeof(*vars));shared_binding_count=count;return ESP_OK;}
esp_err_t ts_temp_bind_variable(const char *name){ts_temp_bound_var_t value={.weight=1};snprintf(value.name,sizeof value.name,"%s",name);return ts_temp_bind_variables(&value,1);}
esp_err_t ts_temp_unbind_variable(void){++mutations;shared_binding_count=0;return ESP_OK;}
esp_err_t ts_temp_get_bound_variables(ts_temp_bound_var_t *vars,uint8_t *count){memcpy(vars,shared_bindings,shared_binding_count*sizeof(*vars));*count=shared_binding_count;return ESP_OK;}
esp_err_t ts_temp_get_bound_variable(char *name,size_t capacity){if(!shared_binding_count)return ESP_ERR_NOT_FOUND;snprintf(name,capacity,"%s",shared_bindings[0].name);return ESP_OK;}
esp_err_t ts_variable_get_info(const char *name,ts_variable_info_t *info){++variable_reads;memset(info,0,sizeof *info);snprintf(info->name,sizeof info->name,"%s",name);info->value.type=TS_AUTO_VAL_FLOAT;info->value.float_val=45;info->last_update_ms=2900;return ESP_OK;}
esp_err_t ts_temp_get_status(ts_temp_status_t *status){memset(status,0,sizeof *status);status->current_temp=450;status->current_valid=true;status->active_source=TS_TEMP_SOURCE_VARIABLE;return ESP_OK;}
'''
for f in ["ts_api_result_ok","ts_api_result_error","ts_api_result_free"]:
    code+=body("components/ts_api/src/ts_api.c",f)
for file,names in [
    ("ts_api_fan.c",["api_fan_limits","api_fan_curve"]),
    ("ts_api_led.c",["api_led_brightness"]),
    ("ts_api_temp.c",["api_temp_manual"]),
    ("ts_api_time.c",["api_time_sync"]),
    ("ts_api_config.c",["api_config_set"])]:
    for name in names: code+=body("components/ts_api/src/"+file,name)
code+=body("components/ts_api/src/ts_api_temp.c","variable_info_to_float")
code+=body("components/ts_api/src/ts_api_temp.c","api_temp_bind")
binding_session=body("components/ts_api/src/ts_api_temp.c","api_temp_bind_for_session",optional=True)
code+=binding_session
code+="#define strdup export_strdup\n#define cJSON_PrintUnformatted export_print\n"
code+=body("components/ts_api/src/ts_api_config_pack.c","api_config_pack_export")
code+="#undef strdup\n#undef cJSON_PrintUnformatted\n"
code+=r'''
static void binding_policy_checks(void);
static unsigned failures,checks;
static void check(bool value,const char *what){checks++;if(!value){failures++;fprintf(stderr,"FAIL %s\n",what);}}
typedef esp_err_t (*handler)(const cJSON *,ts_api_result_t *);
static void request(handler h,cJSON *p,bool valid,const char *label){
 unsigned before=mutations;ts_api_result_t result={0};int ret=h(p,&result);
 check(valid?ret==ESP_OK:ret!=ESP_OK,label);
 if(!valid)check(mutations==before,"rejected request has no hardware/clock/config mutations");
 ts_api_result_free(&result);cJSON_Delete(p);
}
static void json(handler h,const char *text,bool valid){request(h,cJSON_Parse(text),valid,text);}
/* Set nonfinite doubles directly: SDK CreateNumber itself casts NaN to int. */
static cJSON *number(const char *base,const char *key,double n){cJSON *p=cJSON_Parse(base),*v=cJSON_CreateNumber(isfinite(n)?n:0);v->valuedouble=n;cJSON_AddItemToObject(p,key,v);return p;}
int main(void){
 binding_policy_checks();
 double bad[]={-256,256,300,1e30,NAN,INFINITY,-INFINITY,1.5};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){
  request(api_fan_limits,number("{\"id\":0}","min_duty",bad[i]),false,"fan min boundary");
  request(api_fan_limits,number("{\"id\":0}","max_duty",bad[i]),false,"fan max boundary");
  request(api_led_brightness,number("{\"device\":\"matrix\"}","brightness",bad[i]),false,"LED boundary");
 }
 json(api_fan_limits,"{\"id\":0,\"min_duty\":25,\"max_duty\":80}",true);
 check(min_seen==25&&max_seen==80,"normal UI fan limits retained");
 json(api_fan_limits,"{\"id\":0,\"min_duty\":0,\"max_duty\":100}",true);
 json(api_fan_limits,"{\"id\":0,\"min_duty\":90,\"max_duty\":80}",false);
 json(api_fan_limits,"{\"id\":0.5,\"min_duty\":25}",false);
 json(api_fan_limits,"{\"id\":0,\"min_duty\":\"25\"}",false);
 get_fail=true;json(api_fan_limits,"{\"id\":0,\"min_duty\":25}",false);get_fail=false;
 json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30.5,\"duty\":25},{\"temp\":80,\"duty\":100}],\"hysteresis\":3.5,\"min_interval\":2000}",true);
 json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30,\"duty\":25}],\"hysteresis\":21}",false);
 json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30,\"duty\":25}],\"min_interval\":-1}",false);
 json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":1e30,\"duty\":25}]}",false);
 json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30,\"duty\":300}]}",false);
 get_fail=true;json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30,\"duty\":25}],\"hysteresis\":3}",false);get_fail=false;
 config.min_interval=0;json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30,\"duty\":25}],\"hysteresis\":3}",false);config.min_interval=2000;
 json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30,\"duty\":25}]}",true);
 json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30,\"duty\":25}],\"min_interval\":100}",true);
 json(api_fan_curve,"{\"id\":0,\"curve\":[{\"temp\":30,\"duty\":25}],\"min_interval\":60000}",true);
 for(int n=0;n<=255;n+=255){request(api_led_brightness,number("{\"device\":\"matrix\"}","brightness",n),true,"UI brightness endpoint");check(brightness_seen==(unsigned)n,"brightness exact");}
 json(api_led_brightness,"{\"device\":\"matrix\"}",true);
 double extreme[]={1e100,-1e100,NAN,INFINITY,-INFINITY};
 for(unsigned i=0;i<sizeof extreme/sizeof *extreme;i++){
  request(api_temp_manual,number("{\"enable\":false}","temperature",extreme[i]),false,"manual temp before mode mutation");
  request(api_time_sync,number("{}","timestamp_ms",extreme[i]),false,"timestamp boundary");
 }
 json(api_temp_manual,"{\"temperature\":45.5}",true);check(temp_seen==455,"UI temperature alias retained");
 json(api_temp_manual,"{\"temperature_c\":-50}",true);
 json(api_temp_manual,"{\"temperature_01c\":1500}",true);
 json(api_temp_manual,"{\"temperature_01c\":65536}",false);
 json(api_time_sync,"{\"timestamp_ms\":1793678400000}",true);check(time_seen==1793678400000LL,"Date.now style integer unchanged");
 check(security_sets==0,"ordinary browser clock never authorizes security time");
 request(api_time_sync,number("{}","timestamp_ms",0x1p63),false,"int64 rounded upper limit");
 request(api_config_set,number("{\"key\":\"fixture\"}","value",NAN),false,"nonfinite config");
 request(api_config_set,number("{\"key\":\"fixture\"}","value",INFINITY),false,"infinite config");
 request(api_config_set,number("{\"key\":\"fixture\"}","value",0x1p63),true,"large finite config is double");
 check(double_seen==0x1p63,"out of int64 does not cast");
 json(api_config_set,"{\"key\":\"fixture\",\"value\":42}",true);check(integer_seen==42,"normal integer config");
 for(unsigned i=0;i<2;i++){
  cJSON *p=cJSON_Parse(i?"{\"name\":\"fixture\",\"content\":{},\"recipient_cert\":\"synthetic\"}":"{\"name\":\"fixture\",\"content\":\"{}\",\"recipient_cert\":\"synthetic\"}");
  unsigned before=pack_creates;alloc_fail=true;request(api_config_pack_export,p,false,"export allocation failure");alloc_fail=false;check(pack_creates==before,"OOM does not create package");
 }
 json(api_config_pack_export,"{\"name\":\"fixture\",\"content\":{},\"recipient_cert\":\"synthetic\"}",true);
 json(api_config_pack_export,"{\"name\":\"fixture\",\"content\":\"{}\",\"recipient_cert\":\"synthetic\"}",true);
 printf("%u checks, %u failures: production handlers with synthetic boundaries\n",checks,failures);
 return failures?1:0;
}
'''

if binding_session:
    code+=r'''
static void binding_policy_checks(void){
 const char *writes[]={"{\"variables\":[{\"name\":\"private.value\",\"weight\":1}],\"role\":\"root\",\"session_id\":1}","{\"variable\":\"private.value\"}","{\"variable\":null}","{\"variables\":[]}","{\"VARIABLes\":[{\"name\":\"private.value\"}]}","{\"variables\":\"malformed\"}"};
 assert(ts_temp_bind_variable("approved.temperature")==ESP_OK);
 for(uint32_t sid=0;sid<=5;++sid){
  for(unsigned n=0;n<sizeof writes/sizeof *writes;++n){
   if(sid==1)continue;cJSON *params=cJSON_Parse(writes[n]);ts_api_result_t result={0};unsigned before=mutations,reads_before=variable_reads;
   check(api_temp_bind_for_session(params,sid,&result)!=ESP_OK&&result.code!=TS_API_OK,"non-Root cannot bind, unbind or name a private read target");
   check(mutations==before&&variable_reads==reads_before&&!strcmp(shared_bindings[0].name,"approved.temperature"),"binding denial precedes mutation and arbitrary variable access");ts_api_result_free(&result);cJSON_Delete(params);
  }
  ts_api_result_t result={0};unsigned before=mutations;bool allowed=sid==1||sid==2;
  check((api_temp_bind_for_session(NULL,sid,&result)==ESP_OK)==allowed,"Admin and Root retain existing configured temperature query");
  if(allowed)check(cJSON_GetArraySize(cJSON_GetObjectItem(result.data,"bound_variables"))==1&&cJSON_GetObjectItem(result.data,"weighted_temp_c")->valuedouble==45,"current approved temperature, weights and freshness unchanged");
  check(mutations==before,"query never mutates binding");ts_api_result_free(&result);
 }
 cJSON *params=cJSON_Parse("{\"variables\":[{\"name\":\"new.approved.temperature\",\"weight\":1}]}");ts_api_result_t result={0};
 check(api_temp_bind_for_session(params,1,&result)==ESP_OK&&!strcmp(shared_bindings[0].name,"new.approved.temperature"),"Root may deliberately select the shared temperature source");ts_api_result_free(&result);
 caller_live=false;unsigned before=mutations;check(api_temp_bind_for_session(params,1,&result)!=ESP_OK&&mutations==before,"revoked Root denied before hardware binding");caller_live=true;ts_api_result_free(&result);cJSON_Delete(params);
 params=cJSON_Parse("{\"variable\":null}");check(api_temp_bind_for_session(params,1,&result)==ESP_OK&&!shared_binding_count,"Root unbind retained");ts_api_result_free(&result);cJSON_Delete(params);
 params=cJSON_Parse("{\"variable\":\"uart.temperature\"}");check(api_temp_bind(params,&result)==ESP_OK&&!strcmp(shared_bindings[0].name,"uart.temperature"),"trusted internal C/UART binding retains its explicit capability");ts_api_result_free(&result);cJSON_Delete(params);
}
'''
else:
    code+='static void binding_policy_checks(void){}\\n'
includes=[ROOT/"tests/runtime/rule_pack/stubs"]
includes += [ROOT/("components/"+x+"/include") for x in ["ts_api","ts_drivers","ts_led","ts_net","ts_config_pack","ts_storage","ts_security","ts_automation","ts_core/ts_config"]]
includes += [IDF/"components/json/cJSON"]
with tempfile.TemporaryDirectory(prefix="ts-api-numeric-") as tmp:
    c=Path(tmp)/"probe.c";exe=Path(tmp)/"probe";c.write_text(code)
    subprocess.run(["cc","-std=gnu11","-g","-fsanitize=address,undefined,float-cast-overflow",
        "-fno-sanitize-recover=all","-Wno-deprecated-declarations",*["-I"+str(p) for p in includes],str(c),
        str(IDF/"components/json/cJSON/cJSON.c"),"-lm","-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
