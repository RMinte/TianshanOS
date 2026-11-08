"""Actual shared HTTP owner and URL/DNS snapshot; SDK events and clock substituted."""
from pathlib import Path
from test_console_lifecycle import IDF, run_native

root=Path(__file__).resolve().parents[2]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_http_fetch.h"
#include "ts_http_url.h"
#include "ts_dns.h"
static int scenario;
enum {NORMAL,PARTIAL,TOO_BIG,UNKNOWN_BIG,WAIT,CANCEL,DNS_FAIL,DNS_LATE,AUTH_RETRY,REDIRECT,HEADER_ACK,EXPIRE_EVENT};
static int64_t clock_us;
static bool actor=true,fail_init,fail_header,fail_body,fail_heap;
static unsigned dns_calls,clients,cleanups,closes,performs,checks;
static char numeric_url[2300],host_header[300],tls_name[260],authorization[100],body_sent[100];
static const char *expected_ca;
static void check(bool value,const char *message){checks++;if(!value){fprintf(stderr,"FAIL %s scenario=%d\n",message,scenario);abort();}}
static void *body_malloc(size_t n){return fail_body?NULL:malloc(n);}
static void *body_heap(size_t n,unsigned caps){(void)caps;return fail_heap?NULL:body_malloc(n);}
static bool caller(void *context){assert(context==&actor);return actor;}
int64_t esp_timer_get_time(void){return clock_us;}
void vTaskDelay(unsigned ticks){clock_us+=(int64_t)ticks*1000;}
static int esp_crt_bundle_attach(void *p){(void)p;return ESP_OK;}
static char *ipaddr_ntoa_r(const ip_addr_t *ip,char *out,int size){snprintf(out,(size_t)size,"%s",ip->v6?"2001:db8::10":"192.0.2.10");return out;}
esp_err_t ts_dns_resolve(const char *host,int64_t deadline,bool(*current)(void*),void *context,ip_addr_t *out){
 ++dns_calls;assert(!current||current(context));out->v6=strchr(host,':')!=NULL;
 if(scenario==DNS_FAIL)return ESP_ERR_NOT_FOUND;
 if(scenario==DNS_LATE){clock_us=deadline;return ESP_ERR_TIMEOUT;}
 return ESP_OK;
}
struct fixture_client {esp_http_client_config_t config;int status;int64_t declared;bool complete,closed;};
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config){
 check(config->is_async&&config->disable_auto_redirect&&!config->skip_cert_common_name_check,"async, no redirect forwarding and mandatory names");
 check(config->cert_pem==expected_ca&&(expected_ca?!config->crt_bundle_attach:config->crt_bundle_attach!=NULL),"explicit issuer or SDK bundle");
 snprintf(numeric_url,sizeof(numeric_url),"%s",config->url);snprintf(tls_name,sizeof(tls_name),"%s",config->common_name?config->common_name:"");
 if(fail_init)return NULL;
 struct fixture_client *client=calloc(1,sizeof(*client));client->config=*config;client->status=200;client->declared=3;clients++;return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,const char *name,const char *value){(void)client;
 if(fail_header)return ESP_FAIL;
 if(!strcmp(name,"Host"))snprintf(host_header,sizeof(host_header),"%s",value);
 if(!strcmp(name,"Authorization"))snprintf(authorization,sizeof(authorization),"%s",value);
 return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client,int timeout){(void)client;check(timeout==0,"SDK polls have no per-operation blocking timeout");return ESP_OK;}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,const char *body,int length){(void)client;assert(length==(int)strlen(body));snprintf(body_sent,sizeof(body_sent),"%s",body);return ESP_OK;}
esp_err_t esp_http_client_close(esp_http_client_handle_t client){closes++;client->closed=true;return ESP_OK;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client){cleanups++;free(client);return ESP_OK;}
int esp_http_client_get_status_code(esp_http_client_handle_t client){return client->status;}
int64_t esp_http_client_get_content_length(esp_http_client_handle_t client){return client->declared;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client){return client->complete;}
static void event(esp_http_client_handle_t client,int kind,const char *data,int length){esp_http_client_event_t e={.client=client,.event_id=kind,.user_data=client->config.user_data,.data=(void*)data,.data_len=length};client->config.event_handler(&e);}
esp_err_t esp_http_client_perform(esp_http_client_handle_t client){
 performs++;
 if(scenario==WAIT)return ESP_ERR_HTTP_EAGAIN;
 event(client,HTTP_EVENT_ON_CONNECTED,NULL,0);
 event(client,HTTP_EVENT_HEADERS_SENT,NULL,0);
 if(scenario==CANCEL)actor=false;
 if(scenario==EXPIRE_EVENT)clock_us+=10000000;
 if(scenario==REDIRECT){client->status=302;client->declared=0;client->complete=true;event(client,HTTP_EVENT_REDIRECT,NULL,0);event(client,HTTP_EVENT_ON_FINISH,NULL,0);return ESP_OK;}
 if(scenario==AUTH_RETRY){client->status=401;event(client,HTTP_EVENT_ON_DATA,"bad",3);event(client,HTTP_EVENT_ON_FINISH,NULL,0);check(!client->closed,"header-only mode cannot suppress authentication retries");event(client,HTTP_EVENT_HEADERS_SENT,NULL,0);client->status=200;}
 if(scenario==TOO_BIG)client->declared=9999;
 if(scenario==UNKNOWN_BIG){client->declared=-1;event(client,HTTP_EVENT_ON_DATA,"1234567890",10);event(client,HTTP_EVENT_ON_DATA,"1234567890",10);}
 else event(client,HTTP_EVENT_ON_DATA,"abc",3);
 client->complete=scenario!=PARTIAL;
 if(client->complete&&!client->closed)event(client,HTTP_EVENT_ON_FINISH,NULL,0);
 return client->closed?ESP_FAIL:ESP_OK;
}
#define malloc body_malloc
#define heap_caps_malloc body_heap
'''
for name in ['ts_http_target.c','ts_http_fetch.c']:
    code+='#include "'+str(root/'components/ts_http_fetch/src'/name)+'"\n'
code+=r'''
#undef malloc
static ts_http_fetch_config_t request;
static ts_http_fetch_result_t reply;
static void reset(int chosen){scenario=chosen;clock_us=0;actor=true;fail_init=fail_header=fail_body=fail_heap=false;dns_calls=clients=cleanups=closes=performs=0;expected_ca=NULL;numeric_url[0]=host_header[0]=tls_name[0]=authorization[0]=body_sent[0]=0;request=(ts_http_fetch_config_t){.url="https://sensor.fixture:8443/read?x=1",.method=HTTP_METHOD_GET,.timeout_ms=20,.maximum_body=16,.current=caller,.context=&actor};memset(&reply,0,sizeof(reply));}
static esp_err_t fetch(void){esp_err_t result=ts_http_fetch(&request,&reply);check(clients==cleanups,"every initialized client is cleaned");if(result!=ESP_OK)check(!reply.body&&!reply.length,"failed/truncated samples cannot become partial success");return result;}
int main(void){
 reset(NORMAL);check(fetch()==ESP_OK&&reply.length==3&&!strcmp(reply.body,"abc"),"normal complete body");check(reply.dns_ok&&reply.connected&&!strcmp(reply.resolved_ip,"192.0.2.10"),"diagnostic phases come from actual resolver and SDK connection event");check(!strcmp(numeric_url,"https://192.0.2.10:8443/read?x=1")&&!strcmp(host_header,"sensor.fixture:8443")&&!strcmp(tls_name,"sensor.fixture"),"resolved address is pinned while Host/SNI retain original authority");free(reply.body);
 reset(NORMAL);request.url="http://[::1]:8080/read";request.method=HTTP_METHOD_POST;request.body="{}";request.authorization="Bearer synthetic";check(fetch()==ESP_OK&&!tls_name[0]&&!strcmp(host_header,"[::1]:8080")&&!strcmp(body_sent,"{}")&&!strcmp(authorization,"Bearer synthetic"),"HTTP LAN/IPv6 POST and credentials preserved");free(reply.body);
 reset(NORMAL);expected_ca="synthetic private issuer";request.cert_pem=expected_ca;check(fetch()==ESP_OK,"private CA does not disable hostname verification");free(reply.body);
 for(int i=PARTIAL;i<=DNS_LATE;i++){reset(i);check(fetch()!=ESP_OK,"partial,overflow,deadline,cancel,DNS failures reject");if(i==WAIT)check(clock_us==20000&&performs==20,"one total deadline includes all polls");}
 reset(AUTH_RETRY);check(fetch()==ESP_OK&&reply.length==3&&!strcmp(reply.body,"abc"),"auth retry returns only final body");free(reply.body);
 reset(AUTH_RETRY);request.headers_only=true;check(fetch()==ESP_OK&&reply.status==200&&!reply.body&&closes,"webhook header acknowledgment survives401 challenge and closes after200");
 reset(HEADER_ACK);request.headers_only=true;check(fetch()==ESP_OK&&reply.status==200&&!reply.body&&closes,"webhook needs no body allocation/drain");
 reset(EXPIRE_EVENT);check(fetch()==ESP_ERR_TIMEOUT,"callback data cannot arrive after total deadline");
 reset(REDIRECT);check(fetch()==ESP_OK&&reply.status==302&&clients==1,"redirect is returned without another target or credential forwarding");free(reply.body);
 reset(NORMAL);fail_init=true;check(fetch()==ESP_ERR_NO_MEM,"client allocation failure releases body and target");
 reset(NORMAL);fail_header=true;check(fetch()==ESP_FAIL&&!performs,"request setup failure precedes transport effects");
 reset(NORMAL);fail_body=true;check(fetch()==ESP_ERR_NO_MEM&&!dns_calls&&!clients,"body allocation fails before DNS/transport");
 reset(NORMAL);fail_heap=true;check(fetch()==ESP_OK,"internal RAM fallback retained if PSRAM allocation unavailable");free(reply.body);
 const char *bad[]={"file:///sdcard/a","https://bad host/read","https://sensor.fixture:0/a","https://sensor.fixture/\\x"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){reset(NORMAL);request.url=bad[i];check(fetch()==ESP_ERR_INVALID_ARG&&!dns_calls&&!clients,"invalid target rejects before network");}
 const char *headers[]={"{\"Host\":\"evil\"}","{\"X\\r\\nForged\":\"x\"}","{\"X\":\"value\\r\\nForged: x\"}","{\"X\":3}"};
 for(unsigned i=0;i<sizeof(headers)/sizeof(*headers);i++){reset(NORMAL);cJSON *h=cJSON_Parse(headers[i]);request.headers=h;check(fetch()==ESP_ERR_INVALID_ARG&&!dns_calls,"header injection/reserved authority/type reject before DNS");cJSON_Delete(h);}
 reset(NORMAL);request.timeout_ms=30001;check(fetch()==ESP_ERR_INVALID_ARG&&!dns_calls,"caller cannot extend hard budget");
 struct http_parser_url parts;const char *local="http://localhost:8080/api/v1/system/info?x=1#ignored";
 check(ts_http_url_parse(local,&parts)&&ts_http_url_is_local_api(local,&parts),"exact local namespace retained");
 const char *foreign[]={"http://localhost.attacker/api/v1/system/info","http://127.0.0.10/api/v1/system/info","http://example.invalid/path?next=http://localhost/api/v1/system/info"};
 for(unsigned i=0;i<sizeof(foreign)/sizeof(*foreign);i++)check(ts_http_url_parse(foreign[i],&parts)&&!ts_http_url_is_local_api(foreign[i],&parts),"userinfo/path/substrings cannot select the local API shortcut");

 const char *ws_good[]={"ws://sensor.fixture/read?x=1","WSS://sensor.fixture:8443/read","wss://[::1]:8443/ws","wss://user:synthetic@fixture.invalid/a"};
 for(unsigned i=0;i<sizeof(ws_good)/sizeof(*ws_good);i++)check(ts_ws_url_parse(ws_good[i],&parts),"WS/WSS uses exact bounded SDK authority parsing,including IPv6 and original URI credentials");
 const char *ws_bad[]={"http://fixture.invalid/ws","wss://bad host/ws","wss://fixture.invalid:0/ws","wss://fixture.invalid/a#fragment","file:///ws","wss://fixture.invalid/\\x","wss://fixture.invalid/a\r\nX:Y"};
 for(unsigned i=0;i<sizeof(ws_bad)/sizeof(*ws_bad);i++)check(!ts_ws_url_parse(ws_bad[i],&parts),"invalid WS scheme,ambiguous authority,port,fragment or control bytes refused before SDK");
 char huge[2051];memset(huge,'x',sizeof(huge));memcpy(huge,"wss://fixture.invalid/",22);huge[sizeof(huge)-1]=0;check(!ts_ws_url_parse(huge,&parts),"WS URL hard bound before parser");
 printf("%u checks: actual shared HTTP owner and SDK URL parser;Host/SNI/CA,DNS pin,total deadline,body/header bounds,auth/redirect/status and every client cleanup;SDK events/clock/DNS substituted\n",checks);
}
'''
headers={name:(root/'components/ts_http_fetch/include'/name).read_text() for name in ['ts_http_fetch.h','ts_http_url.h']}
headers['esp_err.h']=Path('tests/runtime/rule_pack/stubs/esp_err.h').read_text()+'\n#define ESP_ERR_INVALID_RESPONSE 0x108\n'
headers.update({'esp_http_client.h':Path('tests/runtime/http_fetch_stubs/esp_http_client.h').read_text(),
 'ts_dns.h':'#pragma once\n#include "esp_err.h"\ntypedef struct {bool v6;} ip_addr_t;\n#define IP_IS_V6(ip) ((ip)->v6)\nesp_err_t ts_dns_resolve(const char*,int64_t,bool(*)(void*),void*,ip_addr_t*);\n',
 'esp_crt_bundle.h':'#pragma once\n','esp_heap_caps.h':'#pragma once\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\n',
 'esp_timer.h':'#pragma once\nint64_t esp_timer_get_time(void);\n',
 'freertos/FreeRTOS.h':'#pragma once\n','freertos/task.h':'#pragma once\nvoid vTaskDelay(unsigned);\n',
 'http_parser.h':(IDF/'components/http_parser/http_parser.h').read_text()})
run_native(code,[IDF/'components/http_parser/http_parser.c',IDF/'components/json/cJSON/cJSON.c',root/'components/ts_http_fetch/src/ts_http_url.c'],headers)
