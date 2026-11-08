#!/usr/bin/env python3
"""Production diagnostic WS/SIO callbacks with real cJSON; synthetic SDK events,
allocator faults and semaphore effects. No network, device or real credentials.
"""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
f='components/ts_api/src/ts_api_automation.c'
if args.source_package:
 with ZipFile(args.source_package) as z:t=z.read('current-source/'+f).decode()
else:t=(ROOT/f).read_text()
def function(name):
 m=re.search(r'^static [^\n;]+\b'+name+r'\([^;]+?\)\s*\{',t,re.M);assert m,name
 return t[m.start():t.index('\n}',m.end())+2]+'\n'
def struct(name):
 e=t.index('} '+name+';')+len('} '+name+';');a=t.rfind('typedef struct',0,e);return t[a:e]+'\n'
def diagnostic_budget_fixture():
    """Real entry points/callbacks. Substitute only SDK I/O, monotonic time and
    JSON-path presentation helpers (those have independent codec tests)."""
    if args.source_package: return ''  # Historical packages predate these helpers.
    support=r"""
#include <math.h>
#include "ts_api.h"
#include "ts_http_fetch.h"
#include "ts_http_url.h"
#define TS_LOGW capture
#define TS_STRDUP_PSRAM strdup
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef unsigned TickType_t;
typedef void (*ws_callback_t)(void *,esp_event_base_t,int32_t,void *);
typedef void *esp_websocket_client_handle_t;
typedef struct {const char *uri;unsigned buffer_size,network_timeout_ms;int port;void *ext_transport;bool disable_auto_reconnect;int (*crt_bundle_attach)(void *);} esp_websocket_client_config_t;
#define WEBSOCKET_EVENT_ANY -1
static int64_t fake_now,operation_deadline;
static unsigned created_sems,deleted_sems,http_calls,ws_inits,ws_starts,ws_stops,ws_destroys,sends,wait_index;
static unsigned connect_delay,probe_delay,data_delay,http_delay,send_delay;
static int expire_event_stage;
static const char *polling_payload;static char captured_ws_uri[512];
static unsigned transports_created,transports_destroyed;
typedef struct {void *websocket;int port;} ts_api_ws_test_transport_t;
static bool is_sio,partial_send,registration_failure,start_failure,init_failure,omit_data;
static ws_callback_t registered_callback;static void *registered_context;
static int64_t esp_timer_get_time(void){return fake_now;}
static int esp_crt_bundle_attach(void *v){(void)v;return 0;}
/* Constructor/destructor boundary only. The transport implementation and real
 * SDK dispatch are verified separately; this fixture proves both API consumers. */
static esp_err_t ts_api_ws_test_transport_init(ts_api_ws_test_transport_t *owner,const char *uri,int64_t deadline){
 check(deadline==operation_deadline,"API transports receive the original absolute operation deadline");struct http_parser_url parsed;check(ts_ws_url_parse(uri,&parsed),"actual external transport receives the validated WS URI");owner->websocket=(void*)2;owner->port=parsed.field_set&(1U<<UF_PORT)?parsed.port:parsed.field_data[UF_SCHEMA].len==3?443:80;transports_created++;return ESP_OK;
}
static void ts_api_ws_test_transport_destroy(ts_api_ws_test_transport_t *owner){if(owner->websocket){transports_destroyed++;owner->websocket=NULL;}}

static const char *esp_err_to_name(esp_err_t e){(void)e;return "synthetic_sdk_failure";}
static SemaphoreHandle_t xSemaphoreCreateBinary(void){check(created_sems<3,"bounded diagnostic semaphores");return ++created_sems;}
static void vSemaphoreDelete(SemaphoreHandle_t s){check(s>0&&s<=3,"only owned semaphore deleted");deleted_sems++;}
static void inject_text(const char *text){esp_websocket_event_data_t e={.fin=true,.op_code=1,.data_ptr=text,.data_len=(int)strlen(text),.payload_len=(int)strlen(text)};registered_callback(registered_context,0,WEBSOCKET_EVENT_DATA,&e);}
static bool xSemaphoreTake(SemaphoreHandle_t sem,TickType_t ticks){
    if(gives[sem]){gives[sem]--;return true;}
    unsigned stage=wait_index++,delay=stage==0?connect_delay:is_sio&&stage==1?probe_delay:data_delay;
    check(fake_now+(int64_t)ticks*1000<=operation_deadline,"each wait uses the remaining operation budget");
    if(ticks<delay||omit_data&&stage==(is_sio?2:1)){fake_now+=(int64_t)ticks*1000;return false;}
    fake_now+=(int64_t)delay*1000;
    if(stage==0)registered_callback(registered_context,0,WEBSOCKET_EVENT_CONNECTED,NULL);
    else if(is_sio&&stage==1)inject_text("3probe");
    else inject_text(is_sio?"42[\"status\",{\"value\":7}]":"{\"value\":7}");
    if ((int)stage==expire_event_stage) fake_now=operation_deadline;
    if(gives[sem]){gives[sem]--;return true;}return false;
}
static void vTaskDelay(TickType_t ticks){check(fake_now+(int64_t)ticks*1000<=operation_deadline,"protocol settle delay never resets budget");fake_now+=(int64_t)ticks*1000;}
static esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *cfg){
    snprintf(captured_ws_uri,sizeof(captured_ws_uri),"%s",cfg->uri);ws_inits++;check(cfg->ext_transport==(void*)2&&cfg->port>0,"actual API binds external bounded transport and explicit port");check(cfg->disable_auto_reconnect&&cfg->crt_bundle_attach==esp_crt_bundle_attach,"existing reconnect/TLS trust settings retained");check(cfg->network_timeout_ms>0&&cfg->network_timeout_ms<=30000,"no zero or wrapped SDK timeout");return init_failure?NULL:(void*)1;
}
static esp_err_t esp_websocket_register_events(void *client,int event,ws_callback_t cb,void *ctx){(void)client;(void)event;registered_callback=cb;registered_context=ctx;return registration_failure?ESP_FAIL:ESP_OK;}
static esp_err_t esp_websocket_client_start(void *client){(void)client;ws_starts++;return start_failure?ESP_FAIL:ESP_OK;}
static esp_err_t esp_websocket_client_stop(void *client){check(client!=NULL,"stop owned client");ws_stops++;return ESP_OK;}
static esp_err_t esp_websocket_client_destroy(void *client){check(client!=NULL,"destroy owned client");ws_destroys++;return ESP_OK;}
static int esp_websocket_client_send_text(void *client,const char *text,int length,TickType_t ticks){
    (void)client;(void)text;sends++;check(fake_now+(int64_t)ticks*1000<=operation_deadline,"send never receives a fresh fixed timeout");
    if(ticks<send_delay){fake_now+=(int64_t)ticks*1000;return -1;}fake_now+=(int64_t)send_delay*1000;return partial_send?length-1:length;
}
esp_err_t ts_http_fetch(const ts_http_fetch_config_t *req,ts_http_fetch_result_t *out){
    http_calls++;check(req->maximum_body==2047&&req->timeout_ms&&fake_now+(int64_t)req->timeout_ms*1000<=operation_deadline,"SIO polling consumes the same total budget");
    if(http_delay>=req->timeout_ms){fake_now+=(int64_t)req->timeout_ms*1000;return ESP_ERR_TIMEOUT;}
    fake_now+=(int64_t)http_delay*1000;out->status=200;out->body=strdup(polling_payload);return ESP_OK;
}
static cJSON *extract_json_value(cJSON *v,const char *path){return cJSON_GetObjectItem(v,path);}
static void collect_json_paths(cJSON *value,const char *prefix,cJSON *out,int depth){(void)value;(void)prefix;(void)out;(void)depth;}
"""
    api=(ROOT/'components/ts_api/src/ts_api.c').read_text()
    for name in ['ts_api_result_error','ts_api_result_reason','ts_api_result_free']:
        m=re.search(r'^(?:static )?[^\n;]+\b'+name+r'\([^;]+?\)\s*\{',api,re.M);assert m,name
        support+=api[m.start():api.index('\n}',m.end())+2]+'\n'
    source=(ROOT/'components/ts_automation/src/ts_source_manager.c').read_text()
    m=re.search(r'^bool ts_source_sio_url\([^;]+?\)\s*\{',source,re.M);assert m
    support+=source[m.start():source.index('\n}',m.end())+2]+'\n'
    for name in ['proxy_test_timeout','proxy_test_remaining_ms','proxy_test_send','api_automation_proxy_ws_test','api_automation_proxy_socketio_test']:support+=function(name)
    support+=r"""
static void reset_operation(bool sio,unsigned budget){
    memset(gives,0,sizeof(gives));created_sems=deleted_sems=http_calls=ws_inits=ws_starts=ws_stops=ws_destroys=sends=wait_index=0;
    transports_created=transports_destroyed=0;expire_event_stage=-1;
    polling_payload="0{\"sid\":\"synthetic_session\"}";captured_ws_uri[0]=0;fake_now=0;operation_deadline=(int64_t)budget*1000;is_sio=sio;
    connect_delay=100;probe_delay=100;data_delay=100;http_delay=100;send_delay=10;
    partial_send=registration_failure=start_failure=init_failure=omit_data=false;registered_callback=NULL;registered_context=NULL;
}
static void invoke_operation(cJSON *p,ts_api_result_t *r){if(is_sio)api_automation_proxy_socketio_test(p,r);else api_automation_proxy_ws_test(p,r);}
static cJSON *operation_params(unsigned timeout){
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,is_sio?"url":"uri",is_sio?"https://fixture.invalid":"wss://fixture.invalid/data");
    if(timeout)cJSON_AddNumberToObject(p,"timeout_ms",timeout);return p;
}
static void verify_operation_cleanup(void){
    check(transports_created==transports_destroyed,"every diagnostic external transport released after client join");check(created_sems==deleted_sems,"all diagnostic semaphores released");check(ws_destroys==ws_inits-(init_failure?1:0),"every initialized client destroyed exactly once");}
static void diagnostic_budget_tests(void){

    char url[512],small[8];
    check(ts_source_sio_url("HTTPS://[::1]:8443/prefix/","opaque&transport=polling/#?+%",url,sizeof(url))&&!strcmp(url,"wss://[::1]:8443/prefix/socket.io/?EIO=4&transport=websocket&sid=opaque%26transport%3Dpolling%2F%23%3F%2B%25"),"one validated base preserves TLS,IPv6,path and percent-encodes all SID query delimiters");
    check(ts_source_sio_url("http://user:synthetic@fixture.invalid/prefix/",NULL,url,sizeof(url))&&!strcmp(url,"http://user:synthetic@fixture.invalid/prefix/socket.io/?EIO=4&transport=polling"),"polling preserves existing path and URI credentials without double slash");
    check(!ts_source_sio_url("https://fixture.invalid","sid",small,sizeof(small))&&!small[0],"short output rejects without returning truncated connection target");
    const char *bad_base[]={"https://fixture.invalid/?transport=x","https://fixture.invalid/#fragment","ws://fixture.invalid","https://bad host","https://fixture.invalid:0"};
    for(unsigned i=0;i<sizeof(bad_base)/sizeof(*bad_base);i++)check(!ts_source_sio_url(bad_base[i],"sid",url,sizeof(url))&&!url[0],"ambiguous SIO base refused without partial output");
    const char *bad_sid[]={"","x\r\nForged:y","x y"};
    for(unsigned i=0;i<sizeof(bad_sid)/sizeof(*bad_sid);i++)check(!ts_source_sio_url("https://fixture.invalid",bad_sid[i],url,sizeof(url))&&!url[0],"empty/control/space SID refused");
    char sid[65];memset(sid,'a',64);sid[64]=0;check(!ts_source_sio_url("https://fixture.invalid",sid,url,sizeof(url)),"oversized SID never silently shortens remote session");sid[63]=0;check(ts_source_sio_url("https://fixture.invalid",sid,url,sizeof(url)),"full existing runtime SID capacity retained");
    for(unsigned mode=0;mode<2;mode++){
        const char *targets[]={mode?"https://fixture.invalid/?x=y":"http://fixture.invalid/ws",mode?"ws://fixture.invalid":"wss://bad host/ws"};
        for(unsigned i=0;i<2;i++){
            reset_operation(mode,1000);cJSON *p=operation_params(1000);cJSON_ReplaceItemInObject(p,mode?"url":"uri",cJSON_CreateString(targets[i]));ts_api_result_t r={0};invoke_operation(p,&r);
            check(r.code==TS_API_ERR_INVALID_ARG&&!created_sems&&!http_calls&&!ws_inits,"invalid diagnostic WS/SIO URL fails before allocations or network");ts_api_result_free(&r);cJSON_Delete(p);
        }
    }

    reset_operation(true,1000);polling_payload="0{\"sid\":\"opaque&transport=polling/#?+%\"}";
    cJSON *encoded_params=operation_params(1000);ts_api_result_t encoded_result={0};invoke_operation(encoded_params,&encoded_result);
    check(encoded_result.code==TS_API_OK&&strstr(captured_ws_uri,"sid=opaque%26transport%3Dpolling%2F%23%3F%2B%25"),"actual polling-to-WS diagnostic passes encoded opaque SID rather than injecting URL parameters");verify_operation_cleanup();ts_api_result_free(&encoded_result);cJSON_Delete(encoded_params);
    const char *invalid_open[]={"0{\"sid\":\"\"}","0{\"sid\":\"x\\r\\nX:Y\"}","0{\"sid\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}"};
    for(unsigned i=0;i<sizeof(invalid_open)/sizeof(*invalid_open);i++){
        reset_operation(true,1000);polling_payload=invalid_open[i];cJSON *p=operation_params(1000);ts_api_result_t r={0};invoke_operation(p,&r);
        check(r.code!=TS_API_OK&&!ws_inits,"actual invalid polling SID never reaches SDK WebSocket creation");verify_operation_cleanup();ts_api_result_free(&r);cJSON_Delete(p);
    }
    const char *bad[]={"-1","0","0.5","30001","2147483648","1e100","null","true","\"5000\""};
    for(unsigned sio=0;sio<2;sio++){
        for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
            reset_operation(sio,1000);cJSON *p=operation_params(0);cJSON_AddItemToObject(p,"timeout_ms",cJSON_Parse(bad[i]));ts_api_result_t r={0};invoke_operation(p,&r);
            check(r.code==TS_API_ERR_INVALID_ARG&&!created_sems&&!http_calls&&!ws_inits,"invalid timeout rejected before allocation or network");ts_api_result_free(&r);cJSON_Delete(p);
        }
        reset_operation(sio,1000);cJSON *p=operation_params(0);cJSON *nan_value=cJSON_AddNumberToObject(p,"timeout_ms",1);nan_value->valuedouble=NAN;ts_api_result_t r={0};invoke_operation(p,&r);check(r.code==TS_API_ERR_INVALID_ARG&&!ws_inits,"nonfinite timeout rejected before conversion");ts_api_result_free(&r);cJSON_Delete(p);
        for(unsigned kind=0;kind<3;kind++){
            reset_operation(sio,1000);p=operation_params(1000);const char *field=kind==0?"json_path":sio?"event":"subscribe";
            if(kind==1)cJSON_AddBoolToObject(p,field,true);else {size_t length=kind==0||sio?256:4097;char *text=malloc(length+1);memset(text,'a',length);text[length]=0;cJSON_AddStringToObject(p,field,text);free(text);}
            r=(ts_api_result_t){0};invoke_operation(p,&r);check(r.code==TS_API_ERR_INVALID_ARG&&!created_sems&&!http_calls&&!ws_inits,"wrong type or oversized diagnostic selector rejected before effects");ts_api_result_free(&r);cJSON_Delete(p);
        }
        reset_operation(sio,1000);p=operation_params(1000);json_budget=0;cJSON_Hooks oom_hooks={json_malloc,free};cJSON_InitHooks(&oom_hooks);r=(ts_api_result_t){0};invoke_operation(p,&r);json_budget=-1;cJSON_InitHooks(NULL);
        check(r.code==TS_API_ERR_NO_MEM&&!http_calls&&!ws_inits,"response allocation failure has no network effects");verify_operation_cleanup();ts_api_result_free(&r);cJSON_Delete(p);
        // Original UI values, default timeout and normal protocol stay usable.
        for(unsigned mode=0;mode<3;mode++){
            reset_operation(sio,mode==2?30000:mode?5000:sio?15000:10000);p=operation_params(mode==2?30000:mode?5000:0);if(!sio)cJSON_AddStringToObject(p,"subscribe","{\"subscribe\":true}");r=(ts_api_result_t){0};invoke_operation(p,&r);
            check(r.code==TS_API_OK&&cJSON_IsTrue(cJSON_GetObjectItem(r.data,"connected")),"original UI/default handshake and sample succeed");
            check(ws_starts==1&&ws_stops==1&&ws_destroys==1&&fake_now<operation_deadline,"one start, joined stop and destroy");verify_operation_cleanup();ts_api_result_free(&r);cJSON_Delete(p);
        }
        // A queued callback may be ready when the deadline expires. It must
        // not turn an expired connect/probe/sample into a successful result.
        for(unsigned stage=0;stage<(sio?3:2);stage++){
            reset_operation(sio,1000);expire_event_stage=(int)stage;p=operation_params(1000);r=(ts_api_result_t){0};invoke_operation(p,&r);
            check(r.code!=TS_API_OK && fake_now==operation_deadline,"queued event at deadline cannot succeed or renew operation");verify_operation_cleanup();ts_api_result_free(&r);cJSON_Delete(p);
        }
        for(unsigned fault=0;fault<7;fault++){
            reset_operation(sio,1000);p=operation_params(1000);if(!sio)cJSON_AddStringToObject(p,"subscribe","subscribe");
            if(fault==0){connect_delay=800;data_delay=800;}
            if(fault==1)partial_send=true;
            if(fault==2)registration_failure=true;
            if(fault==3)start_failure=true;
            if(fault==4)init_failure=true;
            if(fault==5)omit_data=true;
            if(fault==6){if(sio)http_delay=1000;else send_delay=1000;}
            r=(ts_api_result_t){0};invoke_operation(p,&r);check(r.code!=TS_API_OK,"timeout/partial send/register/start/init/no data failures are truthful");
            check(fake_now<=operation_deadline,"whole handler waits/send/poll never multiply the timeout");
            if(registration_failure)check(!ws_starts,"event registration failure cannot start a client");
            if(sio&&fault==6)check(!ws_inits,"exhausted polling budget cannot start a WebSocket");
            verify_operation_cleanup();ts_api_result_free(&r);cJSON_Delete(p);
        }
    }
}
"""
    return support

modern='proxy_ws_sample_append(' in t
code=r'''
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include "cJSON.h"
typedef int SemaphoreHandle_t;
typedef int esp_event_base_t;
enum {WEBSOCKET_EVENT_CONNECTED,WEBSOCKET_EVENT_CLOSED,WEBSOCKET_EVENT_DISCONNECTED,WEBSOCKET_EVENT_DATA,WEBSOCKET_EVENT_ERROR};
typedef struct {bool fin;uint8_t op_code;const char *data_ptr;int data_len,payload_len,payload_offset;} esp_websocket_event_data_t;
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static unsigned checks,failures,allocations,max_allocation,gives[4];
static bool fail_alloc;static int json_budget=-1;
static char logs[8192];
static void capture(const char *tag,const char *fmt,...){(void)tag;size_t n=strlen(logs);va_list ap;va_start(ap,fmt);vsnprintf(logs+n,sizeof(logs)-n,fmt,ap);va_end(ap);}
#define TAG "fixture"
#define TS_LOGI capture
#define TS_LOGD capture
#define TS_LOGE capture
static void xSemaphoreGive(SemaphoreHandle_t s){if(s>=0&&s<4)gives[s]++;}
static void *heap_caps_realloc(void *p,size_t n,int caps){(void)caps;allocations++;if(n>max_allocation)max_allocation=(unsigned)n;if(fail_alloc||n>65537)return NULL;return realloc(p,n);}
static void *heap_caps_malloc(size_t n,int caps){return heap_caps_realloc(NULL,n,caps);}
static void *json_malloc(size_t n){if(json_budget==0)return NULL;if(json_budget>0)json_budget--;return malloc(n);}
static void check(bool ok,const char *why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
'''
if modern:code+='#define PROXY_WS_SAMPLE_MAX (64U*1024U)\n'+struct('proxy_ws_sample_t')+function('proxy_ws_sample_append')
code+=struct('ws_test_ctx_t')+function('ws_test_handler')+struct('sio_test_ctx_t')+function('sio_try_parse_event')+function('sio_parse_message')+function('sio_ws_handler')
if modern:code+=r'''
#define WS_DATA(c) ((c).sample.data)
#define WS_LENGTH(c) ((c).sample.length)
#define WS_FAILED(c) ((c).failed)
#define SIO_FAILED(c) ((c).failed)
#define SIO_CAP(c) ((c).sample.capacity)
#define SIO_RX_FREE(c) free((c).sample.data)
'''
else:code+=r'''
#define WS_DATA(c) ((c).message)
#define WS_LENGTH(c) ((c).message_len)
#define WS_FAILED(c) ((c).error!=NULL)
#define SIO_FAILED(c) ((c).error!=NULL)
#define SIO_CAP(c) ((c).fragment_cap)
#define SIO_RX_FREE(c) free((c).fragment_buf)
'''
code+=diagnostic_budget_fixture() if not args.source_package else 'static void diagnostic_budget_tests(void){}\n'
code+=r'''
static void ws_event(ws_test_ctx_t *c,int opcode,bool fin,const char *p,int n,int total,int offset){esp_websocket_event_data_t e={fin,(uint8_t)opcode,p,n,total,offset};ws_test_handler(c,0,WEBSOCKET_EVENT_DATA,&e);}
static void sio_event(sio_test_ctx_t *c,int opcode,bool fin,const char *p,int n,int total,int offset){esp_websocket_event_data_t e={fin,(uint8_t)opcode,p,n,total,offset};sio_ws_handler(c,0,WEBSOCKET_EVENT_DATA,&e);}
static void ws_free(ws_test_ctx_t *c){free(WS_DATA(*c));free(c->error);memset(c,0,sizeof(*c));}
static void sio_free(sio_test_ctx_t *c){free(c->sid);free(c->message);free(c->event_name);free(c->error);SIO_RX_FREE(*c);memset(c,0,sizeof(*c));}
int main(void){
 char text[128]="{\"v\":\"中文 123\"}",event[128]="42[\"status\",{\"value\":12345}]";int n=(int)strlen(text),en=(int)strlen(event);memset(text+n,'x',sizeof(text)-(size_t)n);memset(event+en,'x',sizeof(event)-(size_t)en);
 // Every SDK cut, including cuts in UTF-8 or at the first byte of a number.
 for(int cut=1;cut<n;cut++){
  ws_test_ctx_t c={.data_sem=1};ws_event(&c,1,true,text,cut,n,0);check(!c.received,"WS must not publish an SDK prefix");ws_event(&c,1,true,text+cut,n-cut,n,cut);check(c.received&&WS_LENGTH(c)==(size_t)n&&!memcmp(WS_DATA(c),text,n),"WS exact SDK chunks");ws_free(&c);
 }
 for(int cut=1;cut<en;cut++){
  sio_test_ctx_t c={.data_sem=1};sio_event(&c,1,true,event,cut,en,0);check(!c.received,"SIO must not parse an SDK prefix");sio_event(&c,1,true,event+cut,en-cut,en,cut);check(c.received&&c.message&&!strcmp(c.message,"{\"value\":12345}")&&c.event_name&&!strcmp(c.event_name,"status"),"SIO exact SDK chunks including numeric continuation");sio_free(&c);
  c=(sio_test_ctx_t){.data_sem=1};sio_event(&c,1,false,event,cut,cut,0);sio_event(&c,9,true,"ping",4,4,0);check(!c.received,"SIO ignores interleaved control frames");sio_event(&c,0,true,event+cut,en-cut,en-cut,0);check(c.received&&c.message&&!strcmp(c.message,"{\"value\":12345}"),"SIO RFC continuation assembled before parsing");sio_free(&c);
 }
 for(int cut=1;cut<n;cut++){
  ws_test_ctx_t c={.data_sem=1};ws_event(&c,1,false,text,cut,cut,0);ws_event(&c,10,true,"p",1,1,0);ws_event(&c,0,true,text+cut,n-cut,n-cut,0);check(c.received&&WS_LENGTH(c)==(size_t)n&&!memcmp(WS_DATA(c),text,n),"WS RFC continuation with control");ws_free(&c);
 }
 for(unsigned size=8191;size<=65536;size=(size==8191?8192:size==8192?65535:65536)){
  char *large=malloc(size+1);memset(large,'x',size);large[size]=0;ws_test_ctx_t c={.data_sem=1};
  for(unsigned at=0;at<size;){unsigned got=size-at>1024?1024:size-at;ws_event(&c,1,true,large+at,(int)got,(int)size,(int)at);at+=got;}
  check(c.received&&!WS_FAILED(c)&&WS_LENGTH(c)==size&&!memcmp(WS_DATA(c),large,size),"WS normal 8 KiB/64 KiB exact sample, no silent truncation");ws_free(&c);free(large);if(size==65536)break;
 }

 // Exact 64 KiB Socket.IO sample remains available, including all payload bytes.
 {const char *prefix="42[\"status\",{\"value\":\"",*suffix="\"}]";size_t size=65536,head=strlen(prefix),tail=strlen(suffix);char *large=malloc(size+1);memcpy(large,prefix,head);memset(large+head,'z',size-head-tail);memcpy(large+size-tail,suffix,tail+1);sio_test_ctx_t sample={.data_sem=1};for(size_t at=0;at<size;){size_t count=size-at>1024?1024:size-at;sio_event(&sample,1,true,large+at,(int)count,(int)size,(int)at);at+=count;}check(sample.received&&sample.message&&strlen(sample.message)==size-head-tail+12,"SIO 64 KiB exact message retains full payload");sio_free(&sample);free(large);}
 {ws_test_ctx_t empty={.data_sem=1};ws_event(&empty,1,false,text,n,n,0);ws_event(&empty,0,true,NULL,0,0,0);check(empty.received&&WS_LENGTH(empty)==(size_t)n&&!memcmp(WS_DATA(empty),text,n),"empty continuation FIN completes buffered WS sample");ws_free(&empty);}
 // An oversized declared message is rejected before any allocation/read-copy.
 ws_test_ctx_t w={.data_sem=1};unsigned before=allocations;ws_event(&w,1,true,"x",1,65537,0);check(WS_FAILED(w)&&!w.received&&allocations==before,"WS oversized frame rejected before allocation");ws_free(&w);
 sio_test_ctx_t c={.data_sem=1,.upgrade_sem=2};before=allocations;sio_event(&c,1,true,"4",1,INT_MAX,0);check(SIO_FAILED(c)&&!c.received&&allocations==before,"SIO oversized frame rejected before allocation");sio_free(&c);
 // Never allow a sequence of incomplete frames to grow the diagnostic heap.
 c=(sio_test_ctx_t){.data_sem=1};char block[4096];memset(block,'x',sizeof(block));memcpy(block,"42[",3);sio_event(&c,1,false,block,4096,4096,0);memset(block,'x',sizeof(block));
 for(int i=0;i<20;i++)sio_event(&c,0,false,block,4096,4096,0);
 check(SIO_FAILED(c)&&!c.received&&SIO_CAP(c)<=65537&&max_allocation<=65537,"SIO aggregate continuation budget bounded before realloc");before=allocations;sio_event(&c,0,true,"1]",2,2,0);check(allocations==before&&!c.received,"failed SIO ignores all later payload");sio_free(&c);
 // Offset, declared length, opcode and FIN consistency are SDK callback contracts.
 for(unsigned kind=0;kind<7;kind++){
  w=(ws_test_ctx_t){.data_sem=1};c=(sio_test_ctx_t){.data_sem=1};
  ws_event(&w,1,true,text,2,n,0);sio_event(&c,1,true,event,2,en,0);
  int offset=kind==0?3:kind==1?0:2,total=kind==2?n+1:n,stotal=kind==2?en+1:en,opcode=kind==3?2:1;
  int got=kind==4?n:kind==5?-1:1;bool fin=kind!=6;
  ws_event(&w,opcode,fin,text+2,got,total,offset);sio_event(&c,opcode,fin,event+2,kind==4?en:got,stotal,offset);
  check(WS_FAILED(w)&&!w.received,"WS invalid metadata fails instead of mixing messages");check(SIO_FAILED(c)&&!c.received,"SIO invalid metadata fails instead of mixing messages");ws_free(&w);sio_free(&c);
 }
 w=(ws_test_ctx_t){.data_sem=1};ws_event(&w,0,true,"x",1,1,0);check(WS_FAILED(w)&&!w.received,"orphan continuation fails");ws_free(&w);
 w=(ws_test_ctx_t){.data_sem=1};char nul[3]={'a',0,'b'};ws_event(&w,1,true,nul,3,3,0);check(WS_FAILED(w)&&!w.received,"embedded NUL cannot publish a truncated text result");ws_free(&w);
 // Existing allocation remains owned on realloc failure and all errors wake waiters.
 w=(ws_test_ctx_t){.data_sem=1};ws_event(&w,1,false,block,4096,4096,0);fail_alloc=true;before=gives[1];ws_event(&w,0,true,block,4096,4096,0);fail_alloc=false;check(WS_FAILED(w)&&!w.received&&gives[1]>before,"WS realloc failure truthful and wakes waiter");ws_free(&w);
 c=(sio_test_ctx_t){.data_sem=1,.upgrade_sem=2};fail_alloc=true;before=gives[1];sio_event(&c,1,true,event,en,en,0);fail_alloc=false;check(SIO_FAILED(c)&&!c.received&&gives[1]>before,"SIO realloc failure truthful and wakes waiter");sio_free(&c);
 // JSON payload print failure cannot signal a successful event with no payload.
 for(int budget=0;budget<20;budget++){
  c=(sio_test_ctx_t){.data_sem=1};cJSON_Hooks hooks={json_malloc,free};json_budget=budget;cJSON_InitHooks(&hooks);sio_event(&c,1,true,event,en,en,0);json_budget=-1;cJSON_InitHooks(NULL);
  check(!c.received||(c.message&&c.event_name&&!strcmp(c.message,"{\"value\":12345}")),"SIO event OOM never reports partial success");sio_free(&c);
 }
 // Engine.IO setup, filtering, complete-message reuse and secret-free diagnostics.
 logs[0]=0;c=(sio_test_ctx_t){.data_sem=1,.upgrade_sem=2,.target_event="private_event_SYNTHETIC"};const char *open="0{\"sid\":\"private_sid_SYNTHETIC\"}";sio_event(&c,1,true,open,(int)strlen(open),(int)strlen(open),0);check(c.sid&&!strcmp(c.sid,"private_sid_SYNTHETIC"),"Engine.IO open retained");sio_event(&c,1,true,"3probe",6,6,0);check(c.upgraded,"probe retained");sio_event(&c,1,true,event,en,en,0);check(!c.received,"nonmatching event remains filtered");const char *selected="42[\"private_event_SYNTHETIC\",{\"v\":1}]";sio_event(&c,1,true,selected,(int)strlen(selected),(int)strlen(selected),0);check(c.received&&c.message&&!strcmp(c.message,"{\"v\":1}"),"matching event full payload retained");check(!strstr(logs,"private_sid_SYNTHETIC")&&!strstr(logs,"private_event_SYNTHETIC"),"shared logs omit remote session/event payload");sio_free(&c);
 diagnostic_budget_tests();
 printf("%u checks, %u failures: actual diagnostic WS/SIO callbacks and allocator boundaries; synthetic SDK events, no network/device\n",checks,failures);return failures?1:0;
}
'''
IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'));cjson=IDF/'components/json/cJSON'
with tempfile.TemporaryDirectory(prefix='ts-proxy-samples-') as tmp:
 c=Path(tmp)/'test.c';c.write_text(code);exe=Path(tmp)/'test'
 subprocess.run(['cc','-std=gnu11','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(cjson),'-I'+str(ROOT/'tests/runtime/storage_stubs'),'-I'+str(ROOT/'tests/runtime/http_fetch_stubs'),'-I'+str(ROOT/'components/ts_api/include'),'-I'+str(ROOT/'components/ts_http_fetch/include'),'-I'+str(IDF/'components/http_parser'),str(c),str(cjson/'cJSON.c'),str(ROOT/'components/ts_http_fetch/src/ts_http_url.c'),str(IDF/'components/http_parser/http_parser.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
