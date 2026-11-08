#!/usr/bin/env python3
"""Actual action execution API/error helpers plus production diagnostic statements.
The SDK action executor and network logging inputs are synthetic. No network/device.
"""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args();IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'))
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
        return subprocess.check_output(['git','show','35e081ac8:'+path],cwd=os.environ.get('SOURCE_REPOSITORY',str(ROOT)),text=True)
    return (ROOT/path).read_text()
def function(path,name):
    t=source(path);m=re.search(r'^(?:static )?[^\n;]+\b'+name+r'\([^;]+?\)\s*\{',t,re.M);assert m,name;return t[m.start():t.index('\n}',m.start())+2]+'\n'
auto='components/ts_api/src/ts_api_automation.c';http='components/ts_webui/src/ts_webui_api.c';api='components/ts_api/src/ts_api.c'
header=source('components/ts_automation/include/ts_action_manager.h');status_start=header.rfind('typedef enum',0,header.index('TS_ACTION_STATUS_PENDING'));status_end=header.index('} ts_action_status_t;',status_start)+len('} ts_action_status_t;');result_end=header.index('} ts_action_result_t;')+len('} ts_action_result_t;');result_start=header.rfind('typedef struct',0,result_end)
code=r"""
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include "ts_api.h"
#define TAG "bounded_api_log_test"
#define TS_STRDUP_PSRAM strdup
static unsigned checks,failures,executions;
static const char *secret="SYNTHETIC_PRIVATE_API_OUTPUT_AND_BEARER";
static char diagnostics[16384];
static void capture(const char *tag,const char *format,...){(void)tag;size_t n=strlen(diagnostics);va_list ap;va_start(ap,format);vsnprintf(diagnostics+n,sizeof(diagnostics)-n,format,ap);va_end(ap);}
#define TS_LOGI capture
#define TS_LOGD capture
#define TS_LOGW capture
#define TS_LOGE capture
#define ESP_LOGI capture
#define ESP_LOGW capture
#define ESP_LOGD capture
#define ESP_LOGE capture
static void check(bool value,const char *why){++checks;if(!value){++failures;fprintf(stderr,"FAIL %s\n",why);}}
static const char *esp_err_to_name(esp_err_t code){(void)code;return "synthetic_error";}
"""+header[status_start:status_end]+'\n'+header[result_start:result_end]+'\n'
code+=r"""
static ts_action_status_t returned_status;
static esp_err_t returned_rc;
static bool returned_reason;
esp_err_t ts_action_template_execute_versioned(const char *id,const char *version,ts_action_result_t *out,const char **reason){
 assert(!strcmp(id,"fixture")&&!strcmp(version,"11111111111111111111111111111111"));*reason=NULL;++executions;out->status=returned_status;out->duration_ms=42;snprintf(out->output,sizeof(out->output),"%s",secret);out->reason=returned_reason?"synthetic_failure":NULL;return returned_rc;
}
"""
for name in ['ts_api_result_init','ts_api_result_ok','ts_api_result_error','ts_api_result_reason','ts_api_result_free']:code+=function(api,name)
for name in ['action_execute_failure','api_automation_actions_execute']:code+=function(auto,name)
# Execute the exact diagnostic statements with bounded sensitive inputs. This
# is a sink-format test, not a claim to run WS/Socket.IO transport handlers.
action_manager='components/ts_automation/src/ts_action_manager.c';ssh_api='components/ts_api/src/ts_api_ssh.c';ws_worker='components/ts_webui/src/ts_webui_ws.c'
selectors=[('local',auto,['Local API call detected','Local API dispatch requested']),('fetch',auto,['Proxy fetch']),('ws_uri',auto,['WS test: uri=','WS test timeout=']),('sio_payload',auto,['Socket.IO recv [','Socket.IO received ']),('sio_test',auto,['Socket.IO test:','Socket.IO test timeout=']),('sio_poll',auto,['Socket.IO polling response']),('sio_sid',auto,['Socket.IO session:','Socket.IO session established']),('sio_ws_uri',auto,['Socket.IO WS URL:','Socket.IO WS connection prepared']),('sio_event',auto,['Socket.IO received event:','Socket.IO event received']),('sio_type',auto,['Socket.IO unknown message type']),('http_error',http,['API call result:']),('action_key',action_manager,['Loaded SSH key']),('ssh_key',ssh_api,['Failed to load key','Failed to load SSH key']),('worker_key',ws_worker,['Failed to load key','Failed to load SSH key'])]
code+=r"""
static void diagnostic_sinks(void){
 const char *keyid=secret;size_t key_len=128;struct {const char *keyid;} key_params={secret},*params=&key_params;
 const char *api_name=secret,*url=secret,*uri=secret,*base_url=secret,*target_event=secret,*ws_url=secret,*poll_response=secret,*buf=secret,*method_str="POST";int timeout_ms=15000;char type='@';
 struct {int data_len;} incoming={.data_len=128},*data=&incoming;
 struct {const char *sid,*event_name;} ctx={.sid=secret,.event_name=secret};
 struct {struct {size_t length;} sample;} sample_value={.sample={128}},*sample_ctx=&sample_value;
 ts_api_result_t result={.code=TS_API_ERR_INTERNAL,.message=(char *)secret};esp_err_t ret=ESP_FAIL;
"""
for label,path,prefixes in selectors:
    t=source(path);matches=[]
    for m in re.finditer(r'(?:TS|ESP)_LOG[IWDE]\(TAG,\s*"([^"\n]+)"',t):
        if any(m[1].startswith(prefix) for prefix in prefixes):matches.append(t[m.start():t.index(';',m.start())+1])
    if not matches:
        # Shared HTTP receive code emits no protocol diagnostics at all.
        assert label in {'fetch','sio_poll','sio_sid'},label
        continue
    statement=matches[0].replace("ctx->sample.length","sample_ctx->sample.length").replace("keyid->valuestring","keyid") # Actual sink, synthetic pointer owner.
    # The general HTTP operation name is a known route identifier, not payload.
    pre='api_name="automation.actions.execute";' if label=='http_error' else 'api_name=secret;'
    code+='diagnostics[0]=0;'+pre+'\n'+statement+'\ncheck(!strstr(diagnostics,secret),"'+label+' contains no private input");\n'
    if label=='sio_type':code+='check(!strchr(diagnostics,\'@\'),"unknown frame byte is not emitted");\n'
code+=r"""
}
int main(void){
 cJSON *params=cJSON_Parse("{\"id\":\"fixture\",\"active_version\":\"11111111111111111111111111111111\"}");
 const ts_action_status_t statuses[]={TS_ACTION_STATUS_SUCCESS,TS_ACTION_STATUS_FAILED,TS_ACTION_STATUS_TIMEOUT,TS_ACTION_STATUS_CANCELLED,TS_ACTION_STATUS_QUEUED};
 for(unsigned i=0;i<5;i++)for(unsigned why=0;why<2;why++){
  returned_status=statuses[i];returned_rc=(returned_status==TS_ACTION_STATUS_SUCCESS||returned_status==TS_ACTION_STATUS_QUEUED)?ESP_OK:ESP_FAIL;returned_reason=why!=0;diagnostics[0]=0;ts_api_result_t r={0};unsigned before=executions;
  check(api_automation_actions_execute(params,&r)==ESP_OK&&executions==before+1,"actual action API invokes saved/versioned executor exactly once");
  check(!strstr(diagnostics,secret),"private action output never enters global diagnostics");
  if(returned_status==TS_ACTION_STATUS_QUEUED)check(r.code==TS_API_OK&&cJSON_IsTrue(cJSON_GetObjectItem(r.data,"async")),"queued status contract unchanged");
  else {const char *out=cJSON_GetStringValue(cJSON_GetObjectItem(r.data,"output"));check(out&&!strcmp(out,secret),"exact result output remains available to the invoking caller");}
  if(returned_status==TS_ACTION_STATUS_SUCCESS)check(cJSON_GetObjectItem(r.data,"duration_ms")->valueint==42,"successful duration unchanged");
  ts_api_result_free(&r);
 }
 cJSON_Delete(params);diagnostic_sinks();printf("%u checks, %u failures: actual action API preserves caller results; bounded private-output/URL/handshake/error diagnostic sink tests, no device/network\n",checks,failures);return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix='ts-api-log-privacy-') as tmp:
    c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
    inc=[ROOT/'tests/runtime/storage_stubs',ROOT/'components/ts_api/include',IDF/'components/json/cJSON']
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in inc],str(c),str(IDF/'components/json/cJSON/cJSON.c'),'-o',str(exe)],check=True,cwd=ROOT)
    subprocess.run([str(exe)],check=True,cwd=ROOT)
