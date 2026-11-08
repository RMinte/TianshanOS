"""Actual REST/poll/publication paths with local HTTP HAL and a controlled clock."""
import argparse
import re
from pathlib import Path
from test_console_lifecycle import RTOS, IDF, body, run_native

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source-root',type=Path,default=Path('.'))
args=parser.parse_args()
MANAGER='components/ts_automation/src/ts_source_manager.c'
RUNTIME='components/ts_automation/src/ts_source_runtime.c'
ACTION='components/ts_automation/src/ts_action_manager.c'
def source(path):
    chosen=args.source_root/path
    return chosen if chosen.exists() else Path(path)
def function(path,name):return body(source(path),name)
rtos=re.sub(r'static int64_t esp_timer_get_time\(void\) \{[^\n]+\}',
            'static int64_t clock_ms=100000; static int64_t esp_timer_get_time(void) {return clock_ms*1000;}',RTOS)
code=rtos+r'''
#undef ESP_LOGD
#undef ESP_LOGI
#undef ESP_LOGW
#undef ESP_LOGE
static char source_diagnostics[32000];
static void source_diagnostic(const char *fmt,...){size_t used=strlen(source_diagnostics);va_list args;va_start(args,fmt);vsnprintf(source_diagnostics+used,sizeof(source_diagnostics)-used,fmt,args);va_end(args);}
#define ESP_LOGD(tag,...) source_diagnostic(__VA_ARGS__)
#define ESP_LOGI(tag,...) source_diagnostic(__VA_ARGS__)
#define ESP_LOGW(tag,...) source_diagnostic(__VA_ARGS__)
#define ESP_LOGE(tag,...) source_diagnostic(__VA_ARGS__)

#include "ts_source_runtime.h"
#include "ts_api.h"
#include "cJSON.h"
#include "ts_http_fetch.h"
#include "ts_http_url.h"
#define TAG "local-source"
#define CONFIG_TS_AUTOMATION_MAX_SOURCES 4
#define ESP_ERR_INVALID_RESPONSE 0x108
#define TS_STRDUP_PSRAM strdup
static ts_source_runtime_t ts_test_runtime;
#define ts_source_runtime ts_test_runtime
static struct {atomic_bool activation_pending;} s_sio_ctx;
static int activation_calls,variable_reads,http_calls,http_status=200;
static bool transport_test_active;static int transport_published,transport_creates,transport_updates;
static int64_t request_duration;static void (*during_read)(void);
static const char *http_payload="{\"temperature\":21}";
static int http_offset;static char actual_api_name[64];static cJSON *actual_params;
static bool fail_query_copy;
static esp_err_t api_hosts_info(const cJSON *,ts_api_result_t *);
static char *query_copy(const char *s) {if(fail_query_copy)return NULL;return strdup(s);}
static ts_http_fetch_config_t captured_fetch;
static int performed,cleaned,selected_method;static bool client_init_failure;static int perform_result=ESP_OK;
static char post_body[512],content_type[64];
esp_err_t ts_http_fetch(const ts_http_fetch_config_t *request,ts_http_fetch_result_t *reply){
 assert(request->url);captured_fetch=*request;++http_calls;memset(reply,0,sizeof(*reply));
 if(client_init_failure)return ESP_ERR_NO_MEM;
 ++performed;++cleaned;selected_method=request->method;clock_ms+=request_duration;
 snprintf(post_body,sizeof(post_body),"%s",request->body?request->body:"");
 snprintf(content_type,sizeof(content_type),"%s",request->content_type?request->content_type:"");
 if(during_read){void(*hook)(void)=during_read;during_read=NULL;hook();}
 if(perform_result!=ESP_OK)return perform_result;
 reply->status=http_status;reply->content_length=(int64_t)strlen(http_payload);
 if(!request->headers_only){reply->body=strdup(http_payload);reply->length=strlen(http_payload);}
 return ESP_OK;
}
static void ts_variable_registration_begin(void){}
static void ts_variable_registration_end(void){}
static int process_source_mappings(ts_auto_source_t *s,cJSON *j){(void)s;(void)j;assert(false);return 0;}
static int auto_discover_json_fields(ts_auto_source_t *s,cJSON *j,const char *p,int d,bool c){(void)s;(void)p;(void)d;assert(transport_test_active);const cJSON *v=cJSON_GetObjectItem(j,"temperature");assert(cJSON_IsNumber(v));transport_published=v->valueint;if(c)transport_creates++;else transport_updates++;return 1;}
static cJSON *ts_jsonpath_get(cJSON *j,const char *p){return cJSON_Duplicate(cJSON_GetObjectItem(j,p),true);}
static esp_err_t read_variable_source(ts_auto_source_t *s,ts_auto_value_t *v){(void)s;++variable_reads;*v=(ts_auto_value_t){.type=TS_AUTO_VAL_INT,.int_val=7};return ESP_OK;}
void ts_source_config_activate(void){++activation_calls;}
void ts_source_transport_changed(void){}
esp_err_t ts_api_call(const char *name,const cJSON *p,ts_api_result_t *r){strcpy(actual_api_name,name);cJSON_Delete(actual_params);actual_params=cJSON_Duplicate(p,true);return api_hosts_info(p,r);}
'''
known=Path('components/ts_security/include/ts_known_hosts.h').read_text()
a=known.index('typedef enum {');b=known.index('} ts_known_host_t;')+len('} ts_known_host_t;')
code+=known[a:b]+'\n'
code+=r'''
static esp_err_t ts_known_hosts_get(const char *host,int port,ts_known_host_t *out){assert(!strcmp(host,"192.0.2.8")&&port==2222);*out=(ts_known_host_t){.port=port,.type=TS_HOST_KEY_ED25519};strcpy(out->host,host);strcpy(out->fingerprint,"synthetic-local-fingerprint");return ESP_OK;}
static const char *ts_host_key_type_str(ts_host_key_type_t type){assert(type==TS_HOST_KEY_ED25519);return "ed25519";}
'''
for name in ['ts_api_result_ok','ts_api_result_error']:
    code+=body('components/ts_api/src/ts_api.c',name)
code+=body('components/ts_api/src/ts_api_hosts.c','api_hosts_info')
for name in ['find_source_index','current_source_index','extract_json_value','cjson_to_value']:
    code+=function(MANAGER,name)
code+=body('components/ts_api/src/ts_api.c','ts_api_parse_query')
code+='#define strdup query_copy\n'+function(MANAGER,'read_rest_source')+'\n#undef strdup\n'
code+=function(ACTION,'execute_webhook')
manager=source(MANAGER).read_text();end=manager.index('} sio_connection_t;')+len('} sio_connection_t;');start=manager.rfind('typedef struct {',0,end)
code+='\ntypedef void *esp_websocket_client_handle_t;\n#define SIO_SID_MAX_LEN 64\n#define SIO_EVENT_PREFIX "42"\n'+manager[start:end]+'\n'
code+=r"""
#define SIO_MSG_BUF_SIZE 8192
#define SIO_PROBE_MSG "2probe"
#define SIO_UPGRADE_MSG "5"
#define SIO_PING_MSG "2"
#define SIO_PONG_MSG "3"
typedef const char *esp_event_base_t;
typedef struct {char *data_ptr;int data_len,payload_len,payload_offset;bool fin;uint8_t op_code;} esp_websocket_event_data_t;
typedef struct {const char *uri;int buffer_size,reconnect_timeout_ms,network_timeout_ms,ping_interval_sec;bool enable_close_reconnect,disable_auto_reconnect;int (*crt_bundle_attach)(void *);} esp_websocket_client_config_t;
enum {WEBSOCKET_EVENT_ANY,WEBSOCKET_EVENT_CONNECTED,WEBSOCKET_EVENT_DISCONNECTED,WEBSOCKET_EVENT_DATA,WEBSOCKET_EVENT_ERROR,WEBSOCKET_EVENT_CLOSED};
static void sio_websocket_event_handler(void *,esp_event_base_t,int32_t,void *);
static sio_connection_t *transport_conn;
static unsigned transport_clients,transport_stops,transport_phase,transport_waits;
static char transport_urls[2][512];static bool transport_sdk_reconnect_disabled[2];
static int esp_crt_bundle_attach(void *p){(void)p;return 0;}
static int esp_websocket_client_send_text(void *c,const char *s,int n,int wait){(void)c;(void)s;(void)wait;return n;}
static void *heap_caps_realloc(void *p,size_t n,unsigned caps){(void)caps;return realloc(p,n);}
static void *esp_websocket_client_init(const esp_websocket_client_config_t *c){assert(transport_clients<2);strcpy(transport_urls[transport_clients],c->uri);transport_sdk_reconnect_disabled[transport_clients]=c->disable_auto_reconnect;return (void *)(uintptr_t)++transport_clients;}
static int esp_websocket_register_events(void *client,int event,void(*fn)(void *,esp_event_base_t,int32_t,void *),void *arg){assert(client&&event==WEBSOCKET_EVENT_ANY&&fn==sio_websocket_event_handler&&arg==transport_conn);return ESP_OK;}
static int esp_websocket_client_start(void *client){assert(client);return ESP_OK;}
static int esp_websocket_client_stop(void *client){assert(client);transport_stops++;return ESP_OK;}
static int esp_websocket_client_destroy(void *client){assert(client);return ESP_OK;}
static int xTaskNotifyGive(TaskHandle_t task){assert(task);return 1;}
static void sio_release_connection(sio_connection_t *c,bool activate){assert(c==transport_conn&&!activate);c->allocated=false;}
#define vTaskDeleteWithCaps(task) return
static unsigned ulTaskNotifyTake(int clear,unsigned wait){
 (void)clear;clock_ms+=wait;if(wait!=200)return 0;assert(++transport_waits<12);
 if(transport_phase==1){
  sio_websocket_event_handler(transport_conn,NULL,WEBSOCKET_EVENT_DISCONNECTED,NULL);
  http_payload="0{\"sid\":\"second-session\"}";transport_phase=2;return 0;
 }
 if(transport_phase==3||(transport_phase==2&&transport_clients<2)){transport_conn->should_stop=true;return 0;}
 sio_websocket_event_handler(transport_conn,NULL,WEBSOCKET_EVENT_CONNECTED,NULL);
 char probe[]="3probe";esp_websocket_event_data_t e={.data_ptr=probe,.data_len=6,.payload_len=6,.fin=true,.op_code=1};
 sio_websocket_event_handler(transport_conn,NULL,WEBSOCKET_EVENT_DATA,&e);
 char first[]="42[\"telemetry\",{\"temperature\":41}]",second[]="42[\"telemetry\",{\"temperature\":42}]";
 e.data_ptr=transport_phase==0?first:second;e.data_len=e.payload_len=(int)strlen(e.data_ptr);
 sio_websocket_event_handler(transport_conn,NULL,WEBSOCKET_EVENT_DATA,&e);
 transport_phase=transport_phase==0?1:3;return 0;
}
"""
for name in ['current_connection_index','sio_handle_event_message','ts_source_sio_url','sio_http_handshake']:
    code+=function(MANAGER,name)
for name in ['ws_handle_message','sio_websocket_event_handler','sio_connection_task']:
    code+=function(MANAGER,name)
for name in ['poll_instance','ts_source_poll_all']:
    code+=function(MANAGER,name)
code+=re.search(r'^esp_err_t ts_source_poll\(.*$',source(MANAGER).read_text(),re.M).group(0)+'\n'
runtime=source(RUNTIME).read_text();a=runtime.index('struct ts_source_config_view {');b=runtime.index('\n};',a)+3
code+='typedef struct ts_source_config_view ts_source_config_view_t;\n'+runtime[a:b]+'\n'
code+=function('components/ts_automation/src/ts_action_codec.c','ts_source_config_same')
for name in ['ts_source_config_discard','ts_source_config_publish']:
    code+=function(RUNTIME,name)
code+=r'''
static ts_auto_source_t *sample;
static void reset(void){memset(ts_source_runtime.sources,0,4*sizeof(*sample));ts_source_runtime.count=1;ts_source_runtime.running=true;sample=ts_source_runtime.sources;*sample=(ts_auto_source_t){.type=TS_AUTO_SRC_REST,.enabled=true,.poll_interval_ms=60000,.instance=1};strcpy(sample->id,"temperature");strcpy(sample->rest.url,"http://fixture.invalid/read");strcpy(sample->rest.path,"temperature");clock_ms=100000;http_calls=variable_reads=0;http_status=200;request_duration=0;http_payload="{\"temperature\":21}";}
static void publish(bool retained){ts_source_config_view_t *v=calloc(1,sizeof(*v));v->count=1;v->keep_runtime=retained;v->sources=calloc(1,sizeof(*v->sources));*v->sources=*sample;if(!retained){v->sources[0].instance=sample->instance+1;v->sources[0].last_update_ms=0;v->sources[0].last_attempt_ms=0;v->sources[0].last_value=(ts_auto_value_t){0};strcpy(v->sources[0].rest.url,"http://fixture.invalid/new");}ts_source_config_publish(v);}
static void unchanged(int64_t attempt,int64_t success,int value){assert(sample->last_attempt_ms==attempt&&sample->last_update_ms==success&&sample->last_value.int_val==value);}
int main(void){ts_source_runtime.sources=calloc(4,sizeof(*sample));ts_source_runtime.capacity=4;ts_source_runtime.initialized=true;ts_source_runtime.mutex=xSemaphoreCreateMutex();
 reset();http_status=503;assert(ts_source_poll_all()==0&&http_calls==1);clock_ms+=100;assert(ts_source_poll_all()==0&&http_calls==1);
 reset();assert(ts_source_poll_all()==1&&http_calls==1);unchanged(100000,100000,21);
 http_status=503;for(clock_ms=100100;clock_ms<160000;clock_ms+=100)assert(ts_source_poll_all()==0&&http_calls==1);assert(ts_source_poll_all()==0&&http_calls==2);unchanged(160000,100000,21);
 publish(true);unchanged(160000,100000,21);for(clock_ms=160100;clock_ms<220000;clock_ms+=100)assert(ts_source_poll_all()==0&&http_calls==2);
 http_status=200;http_payload="invalid JSON";assert(ts_source_poll_all()==0&&http_calls==3);unchanged(220000,100000,21);
 clock_ms=279900;assert(ts_source_poll_all()==0&&http_calls==3);clock_ms=280000;http_payload="{\"temperature\":32}";assert(ts_source_poll_all()==1&&http_calls==4);unchanged(280000,280000,32);
 /* Slow successes retain the existing completion-to-next-attempt interval. */
 clock_ms=340000;request_duration=9000;assert(ts_source_poll_all()==1&&clock_ms==349000);unchanged(349000,349000,32);clock_ms=400000;assert(ts_source_poll_all()==0&&http_calls==5);clock_ms=409000;assert(ts_source_poll_all()==1&&http_calls==6);
 request_duration=0;clock_ms=418100;assert(ts_source_poll(sample->id)==ESP_OK&&http_calls==7);unchanged(418100,418100,32);
 sample->poll_interval_ms=0;assert(ts_source_poll_all()==1&&ts_source_poll_all()==1&&http_calls==9);
 reset();sample->type=TS_AUTO_SRC_VARIABLE;assert(ts_source_poll_all()==1&&variable_reads==1&&!http_calls);clock_ms+=100;assert(ts_source_poll_all()==0&&variable_reads==1);clock_ms=160000;assert(ts_source_poll_all()==1&&variable_reads==2);
 reset();assert(ts_source_poll_all()==1);clock_ms+=100;publish(false);assert(sample->instance==2&&!sample->last_attempt_ms);assert(ts_source_poll_all()==1&&http_calls==2);
 /* Late old response passes through actual REST and publication owner guards. */
 clock_ms+=60000;http_payload="{\"temperature\":90}";
 /* Explicit replacement while the HTTP HAL read is in flight. */
 extern void replace_source(void);during_read=replace_source;assert(ts_source_poll_all()==0&&sample->instance==3);unchanged(0,0,0);
 reset();strcpy(sample->rest.url,"http://127.0.0.1/api/v1/hosts/info?host=192.0.2.8&label=A+B%252E&n=12&port=2222");strcpy(sample->rest.path,"port");assert(ts_source_poll_all()==1&&!http_calls&&sample->last_value.int_val==2222);assert(!strcmp(actual_api_name,"hosts.info"));assert(!strcmp(cJSON_GetObjectItem(actual_params,"host")->valuestring,"192.0.2.8"));assert(!strcmp(cJSON_GetObjectItem(actual_params,"label")->valuestring,"A B%2E"));assert(cJSON_IsNumber(cJSON_GetObjectItem(actual_params,"n"))&&cJSON_GetObjectItem(actual_params,"n")->valueint==12);
 fail_query_copy=true;clock_ms+=60000;assert(ts_source_poll_all()==0&&sample->last_update_ms==100000&&sample->last_attempt_ms==160000);fail_query_copy=false;

 /* Protocol contents still reach the actual source owner, not global logs. */
 reset();source_diagnostics[0]=0;strcpy(sample->rest.url,"https://fixture.invalid/read?token=synthetic-rest-query-secret");
 http_payload="{\"temperature\":73,\"token\":\"synthetic-rest-body-secret\"}";
 assert(ts_source_poll_all()==1&&sample->last_value.int_val==73&&!strstr(source_diagnostics,"synthetic-rest-query-secret")&&!strstr(source_diagnostics,"synthetic-rest-body-secret"));
 source_diagnostics[0]=0;http_payload="0{\"sid\":\"synthetic-sio-session-secret\",\"upgrades\":[\"websocket\"]}";
 char sid[64]={0};assert(sio_http_handshake("https://fixture.invalid",sid,sizeof(sid))==ESP_OK&&!strcmp(sid,"synthetic-sio-session-secret")&&!strstr(source_diagnostics,"synthetic-sio-session-secret"));
 reset();sample->type=TS_AUTO_SRC_SOCKETIO;strcpy(sample->socketio.event,"private_event");sample->auto_discover=true;
 sio_connection_t conn={.source_instance=sample->instance,.type=sample->type,.pending_mutex=xSemaphoreCreateMutex()};strcpy(conn.source_id,sample->id);
 source_diagnostics[0]=0;sio_handle_event_message(&conn,"42[\"private_event\",{\"token\":\"synthetic-sio-response-secret\",\"value\":42}]");
 assert(sample->connected&&sample->last_value.type==TS_AUTO_VAL_STRING&&strstr(sample->last_value.str_val,"synthetic-sio-response-secret")&&conn.pending_json&&strstr(conn.pending_json,"synthetic-sio-response-secret")&&!strstr(source_diagnostics,"synthetic-sio-response-secret"));
 free(conn.pending_json);vSemaphoreDelete(conn.pending_mutex);
 /* Verify actual consumers configure the shared transport; its TLS factory is exercised separately. */
 reset();strcpy(sample->rest.url,"https://fixture.invalid/read");assert(ts_source_poll_all()==1);
 assert(captured_fetch.timeout_ms==10000&&captured_fetch.maximum_body==4095&&!captured_fetch.cert_pem&&!captured_fetch.headers_only);
 const char *schemes[]={"http://fixture.invalid/hook","https://fixture.invalid/hook"};
 const char *methods[]={"GET","POST","PUT"};int expected_methods[]={HTTP_METHOD_GET,HTTP_METHOD_POST,HTTP_METHOD_PUT};
 ts_auto_action_t action={.type=TS_AUTO_ACT_WEBHOOK};strcpy(action.webhook.body_template,"{\"value\":\"synthetic-body\"}");
 for(unsigned u=0;u<2;u++)for(unsigned m=0;m<3;m++){
  strcpy(action.webhook.url,schemes[u]);strcpy(action.webhook.method,methods[m]);http_status=204;perform_result=ESP_OK;content_type[0]=post_body[0]=0;int before=cleaned;
  assert(execute_webhook(&action)==ESP_OK&&captured_fetch.headers_only&&captured_fetch.timeout_ms==5000&&selected_method==expected_methods[m]&&cleaned==before+1&&!strcmp(post_body,action.webhook.body_template));
  if(m)assert(!strcmp(content_type,"application/json"));
 }
 int before=cleaned;perform_result=ESP_FAIL;assert(execute_webhook(&action)==ESP_FAIL&&cleaned==before+1);perform_result=ESP_OK;
 http_status=401;assert(execute_webhook(&action)==ESP_FAIL);http_status=200;
 before=performed;client_init_failure=true;assert(execute_webhook(&action)==ESP_ERR_NO_MEM&&performed==before);client_init_failure=false;
 action.type=TS_AUTO_ACT_LOG;before=http_calls;assert(execute_webhook(&action)==ESP_ERR_INVALID_ARG&&http_calls==before);
 puts("PASS actual REST/Webhook shared transport consumers: HTTP/HTTPS GET/POST/PUT/body/typed deadlines/caps/status/failure retained;real shared SDK TLS factory has separate owner suite");
 puts("PASS actual REST/SIO diagnostic privacy: exact query/body read, SID returned, response summary and full pending JSON retained, no synthetic protocol canaries in global logs");
 /* Real SIO owner task: first session publishes, network disconnects, then
  * a new handshake/SID must precede resumed event publication. */
 reset();sample->type=TS_AUTO_SRC_SOCKETIO;sample->auto_discover=true;
 strcpy(sample->socketio.url,"http://fixture.invalid");strcpy(sample->socketio.event,"telemetry");sample->socketio.reconnect_ms=5000;
 sio_connection_t reconnect={.source_instance=sample->instance,.type=sample->type,.pending_mutex=xSemaphoreCreateMutex(),.allocated=true,.task_handle=(TaskHandle_t)1};
 strcpy(reconnect.source_id,sample->id);transport_conn=&reconnect;transport_test_active=true;
 http_payload="0{\"sid\":\"first-session\"}";http_status=200;transport_clients=transport_stops=transport_phase=transport_waits=0;
 sio_connection_task(&reconnect);transport_test_active=false;
 assert(transport_clients==2&&transport_stops==2&&http_calls==2);
 assert(strstr(transport_urls[0],"first-session")&&strstr(transport_urls[1],"second-session"));
 assert(transport_sdk_reconnect_disabled[0]&&transport_sdk_reconnect_disabled[1]);
 assert(transport_published==42&&transport_creates==1&&transport_updates==1&&!reconnect.allocated&&!reconnect.pending_mutex);
 puts("PASS real SIO task disconnect -> new handshake/SID -> temperature publish; one owner, no old-SID SDK retry, no fan/TTL changes");
 cJSON_Delete(actual_params);free(ts_source_runtime.sources);vSemaphoreDelete(ts_source_runtime.mutex);puts("PASS actual REST/poll/publication: failure intervals, success freshness, slow completion cadence, manual/zero/VARIABLE, retained/new/stale instances, actual local API query receiver and allocation failure");}
void replace_source(void){publish(false);}
'''
headers={p.name:p.read_text() for p in [Path('components/ts_automation/include/ts_automation_types.h'),Path('components/ts_automation/include/ts_source_manager.h'),Path('components/ts_automation/src/ts_source_runtime.h')]}
headers.update({'freertos/FreeRTOS.h':'#pragma once\n','freertos/semphr.h':'#pragma once\n'})
headers.update({name:(Path('components/ts_http_fetch/include')/name).read_text() for name in ['ts_http_fetch.h','ts_http_url.h']})
headers['esp_http_client.h']=Path('tests/runtime/http_fetch_stubs/esp_http_client.h').read_text()
headers['http_parser.h']=(IDF/'components/http_parser/http_parser.h').read_text()

run_native(code,[IDF/'components/json/cJSON/cJSON.c',IDF/'components/http_parser/http_parser.c',Path('components/ts_http_fetch/src/ts_http_url.c')],headers)
