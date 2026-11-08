"""Actual diagnostic transport + SDK transport dispatch/WS handshake and frames.
Only DNS, raw TCP/TLS I/O and scheduler/clock are synthetic. No network/device.
"""
from pathlib import Path
import importlib.util,os,tempfile
from test_console_lifecycle import IDF,run_native,body
from host_paths import sdk_library_dir
root=Path(__file__).resolve().parents[2]
sdk=sdk_library_dir(IDF)
websocket_source=Path(os.environ.get('TS_WS_COMPONENT_SOURCE',str(root/'managed_components/espressif__esp_websocket_client/esp_websocket_client.c')))
spec=importlib.util.spec_from_file_location('prepare',root/'tools/prepare_http_transport_polling.py');prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)
with tempfile.TemporaryDirectory(prefix='ts-ws-diag-prepared-') as directory:
 prepared=Path(directory);prepare.prepare(IDF,prepared,websocket_source)
 headers={name:(IDF/'components/tcp_transport/include'/name).read_text() for name in ['esp_transport.h','esp_transport_tcp.h','esp_transport_ssl.h','esp_transport_ws.h']}
 headers.update({'esp_transport_internal.h':(IDF/'components/tcp_transport/private_include/esp_transport_internal.h').read_text(),
  'esp_tls_crypto.h':(IDF/'components/esp-tls/esp-tls-crypto/esp_tls_crypto.h').read_text(),
  'esp_tls.h':'''#pragma once
#include <stddef.h>
#include "esp_err.h"
struct esp_tls_last_error{int last_error,esp_tls_error_code,esp_tls_flags;};
typedef struct esp_tls_last_error esp_tls_last_error_t;
typedef struct {const unsigned char *key;size_t key_size;const char *hint;} psk_hint_key_t;
typedef int esp_tls_addr_family_t;
typedef int esp_tls_proto_ver_t;
struct ifreq;
#define ESP_TLS_ERR_TYPE_SYSTEM 3
#define ESP_ERR_ESP_TLS_TCP_CLOSED_FIN 0x7001
#define ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT 0x7002
#define ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST 0x7003
static inline int esp_tls_get_and_clear_error_type(void *h,int t,int *out){(void)h;(void)t;*out=0;return 0;}
''',
  'esp_log.h':'#pragma once\n#define ESP_LOGD(...) ((void)0)\n#define ESP_LOGV(...) ((void)0)\n#define ESP_STATIC_ANALYZER_CHECK(condition,value) do{if(condition)return(value);}while(0)\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n',
  'esp_timer.h':'#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
  'esp_crt_bundle.h':'#pragma once\nint esp_crt_bundle_attach(void *);\n',
  'freertos/FreeRTOS.h':'#pragma once\n#define pdMS_TO_TICKS(n) (n)\n',
  'freertos/task.h':'#pragma once\nvoid vTaskDelay(unsigned);\n',
  'ts_dns.h':'#pragma once\n#include <stdint.h>\n#include <stdbool.h>\n#include "esp_err.h"\ntypedef struct {char text[64];} ip_addr_t;\nesp_err_t ts_dns_resolve(const char*,int64_t,bool(*)(void*),void*,ip_addr_t*);\nchar *ipaddr_ntoa_r(const ip_addr_t*,char*,int);\n',
  'ts_http_url.h':(root/'components/ts_http_fetch/include/ts_http_url.h').read_text(),
  'http_parser.h':(IDF/'components/http_parser/http_parser.h').read_text()})
 code=r'''
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include "esp_transport.h"
#include "esp_transport_ssl.h"
#include "esp_tls_crypto.h"
#include "ts_dns.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
static unsigned checks,allocations;static int allocation_budget=-1;static void *live[128];static unsigned live_count;
static void check(bool v,const char *m){checks++;if(!v){fprintf(stderr,"FAIL %s\n",m);abort();}}
static void *tracked_malloc(size_t size){if(allocation_budget==0)return NULL;if(allocation_budget>0)allocation_budget--;void *p=malloc(size);if(p){assert(live_count<128);live[live_count++]=p;allocations++;}return p;}
static void tracked_free(void *p){if(!p)return;unsigned i;for(i=0;i<live_count&&live[i]!=p;i++){}assert(i<live_count);live[i]=live[--live_count];free(p);}
static void *tracked_calloc(size_t n,size_t size){void *p=tracked_malloc(n*size);if(p)memset(p,0,n*size);return p;}
static char *tracked_strdup(const char *s){size_t n=strlen(s)+1;char *p=tracked_malloc(n);if(p)memcpy(p,s,n);return p;}
int esp_crypto_base64_encode(unsigned char *dst,size_t cap,size_t *written,const unsigned char *src,size_t len){return mbedtls_base64_encode(dst,cap,written,src,len);}
int esp_crypto_sha1(const unsigned char *in,size_t len,unsigned char out[20]){return mbedtls_sha1(in,len,out);}
static ssize_t fixture_random(void *out,size_t len,unsigned flags){(void)flags;for(size_t i=0;i<len;i++)((unsigned char*)out)[i]=(unsigned char)(3+i);return (ssize_t)len;}
static int64_t now_us;static unsigned dns_calls,async_calls;static bool dns_fail,dns_stall,connect_stall,read_stall,write_stall,bad_accept,slow_header,io_retry;
static const char *actual_sni;static bool actual_bundle;static char actual_target[64],upgrade_request[2048];
int64_t esp_timer_get_time(void){return now_us;}
void vTaskDelay(unsigned ticks){now_us+=(int64_t)ticks*1000;}
int esp_crt_bundle_attach(void *conf){(void)conf;return ESP_OK;}
esp_err_t ts_dns_resolve(const char *host,int64_t deadline,bool(*current)(void*),void *ctx,ip_addr_t *out){
 (void)current;(void)ctx;dns_calls++;check(!strcmp(host,"node.fixture")||!strcmp(host,"192.0.2.8"),"resolver sees original selected host");
 if(dns_fail)return ESP_ERR_NOT_FOUND;if(dns_stall){now_us=deadline;return ESP_ERR_TIMEOUT;}now_us+=2000;strcpy(out->text,"192.0.2.8");return ESP_OK;
}
char *ipaddr_ntoa_r(const ip_addr_t *ip,char *out,int cap){snprintf(out,(size_t)cap,"%s",ip->text);return out;}
#define malloc tracked_malloc
#define calloc tracked_calloc
#define free tracked_free
#define strdup tracked_strdup
'''
 code+='#define TAG sdk_dispatch_tag\n#include "'+str(IDF/'components/tcp_transport/transport.c')+'"\n#undef TAG\n'
 code+=body(IDF/'components/tcp_transport/transport_internal.c','esp_transport_utils_ms_to_timeval')
 code+='#define CONFIG_WS_BUFFER_SIZE 1024\n#define getrandom fixture_random\n#define TAG sdk_ws_tag\n#include "'+str(prepared/'transport_ws.c')+'"\n#undef TAG\n#undef getrandom\n'
 code+=r'''
typedef struct {bool connected;int64_t connect_at,read_at;char wire[1024];size_t wire_len,cursor;} raw_t;
static raw_t *raw;
static int raw_close(esp_transport_handle_t t){raw_t *r=esp_transport_get_context_data(t);r->connected=false;return 0;}
static int raw_destroy(esp_transport_handle_t t){tracked_free(esp_transport_get_context_data(t));raw=NULL;return 0;}
static int raw_async(esp_transport_handle_t t,const char *host,int port,int timeout){
 raw_t *r=esp_transport_get_context_data(t);async_calls++;check(timeout==0,"connect uses nonblocking SDK API");check(port==8080||port==8443,"original port retained");snprintf(actual_target,sizeof(actual_target),"%s",host);check(!strcmp(host,"192.0.2.8"),"socket connects only to pinned numeric address");
 if(connect_stall||now_us<r->connect_at)return 0;r->connected=true;return 1;
}
static int raw_poll_read(esp_transport_handle_t t,int timeout){raw_t *r=esp_transport_get_context_data(t);check(timeout==0,"read readiness never blocks SDK");return !read_stall&&r->cursor<r->wire_len&&now_us>=r->read_at;}
static int raw_poll_write(esp_transport_handle_t t,int timeout){(void)t;check(timeout==0,"write readiness never blocks SDK");return !write_stall;}
static int raw_read(esp_transport_handle_t t,char *out,int length,int timeout){
 raw_t *r=esp_transport_get_context_data(t);check(timeout==0&&length>0,"bounded WS parser uses nonblocking raw read");if(io_retry){io_retry=false;errno=EAGAIN;return -1;}
 if(r->cursor==r->wire_len){errno=EAGAIN;return -1;}out[0]=r->wire[r->cursor++];r->read_at=now_us+(slow_header?10000:0);return 1;
}
static int raw_write(esp_transport_handle_t t,const char *text,int length,int timeout){
 raw_t *r=esp_transport_get_context_data(t);check(timeout==0&&length>0,"upgrade/write never blocks SDK");
 if(length>=4&&!strncmp(text,"GET ",4)){
  assert((size_t)length<sizeof(upgrade_request));memcpy(upgrade_request,text,(size_t)length);upgrade_request[length]=0;
  const char *key=strstr(upgrade_request,"Sec-WebSocket-Key: ");assert(key);key+=19;const char *end=strstr(key,"\r\n");assert(end);char material[128];size_t n=(size_t)(end-key);memcpy(material,key,n);strcpy(material+n,"258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
  unsigned char digest[20],accept[64];size_t encoded;assert(esp_crypto_sha1((unsigned char*)material,strlen(material),digest)==0&&esp_crypto_base64_encode(accept,sizeof(accept),&encoded,digest,sizeof(digest))==0);accept[encoded]=0;if(bad_accept)accept[0]^=1;
  int count=snprintf(r->wire,sizeof(r->wire),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);assert(count>0);r->wire_len=(size_t)count;r->cursor=0;
  const char payload[]="{\"temperature\":23}";r->wire[r->wire_len++]=(char)0x81;r->wire[r->wire_len++]=sizeof(payload)-1;memcpy(r->wire+r->wire_len,payload,sizeof(payload)-1);r->wire_len+=sizeof(payload)-1;
 }
 return length;
}
static esp_transport_handle_t raw_init(void){esp_transport_handle_t t=esp_transport_init();if(!t)return NULL;raw=calloc(1,sizeof(*raw));if(!raw){esp_transport_destroy(t);return NULL;}raw->connect_at=now_us+10000;esp_transport_set_context_data(t,raw);esp_transport_set_func(t,NULL,raw_read,raw_write,raw_close,raw_poll_read,raw_poll_write,raw_destroy);esp_transport_set_async_connect_func(t,raw_async);return t;}
esp_transport_handle_t esp_transport_tcp_init(void){return raw_init();}
esp_transport_handle_t esp_transport_ssl_init(void){return raw_init();}
void esp_transport_ssl_set_common_name(esp_transport_handle_t t,const char *name){(void)t;actual_sni=name;}
void esp_transport_ssl_crt_bundle_attach(esp_transport_handle_t t,esp_err_t(*attach)(void*)){(void)t;actual_bundle=attach==esp_crt_bundle_attach;}
'''
 code+='#include "'+str(root/'components/ts_api/src/ts_api_ws_test_transport.c')+'"\n'
 code+=r'''
static void reset(void){check(!live_count,"previous test releases every transport/string");now_us=0;dns_calls=async_calls=0;dns_fail=dns_stall=connect_stall=read_stall=write_stall=bad_accept=slow_header=io_retry=false;actual_sni=NULL;actual_bundle=false;upgrade_request[0]=actual_target[0]=0;allocation_budget=-1;}
int main(void){
 for(unsigned secure=0;secure<2;secure++){
  reset();ts_api_ws_test_transport_t owner;const char *uri=secure?"wss://user:synthetic@node.fixture:8443/feed?x=1":"ws://node.fixture:8080/feed?x=1";
  check(ts_api_ws_test_transport_init(&owner,uri,500000)==ESP_OK,"real constructor succeeds");
  if(secure)check(actual_bundle&&!strcmp(actual_sni,"node.fixture"),"TLS keeps bundle and original name before numeric connect");
  check(esp_transport_connect(owner.websocket,owner.hostname,owner.port,500)==0,"real SDK WS handshake and actual SHA/Base64 succeed");
  check(strstr(upgrade_request,"GET /feed?x=1 HTTP/1.1\r\n")&&strstr(upgrade_request,secure?"Host: node.fixture:8443\r\n":"Host: node.fixture:8080\r\n"),"path/query and original Host retained");
  if(secure)check(strstr(upgrade_request,"Authorization: Basic dXNlcjpzeW50aGV0aWM=\r\n"),"existing URI Basic authentication preserved");
  char output[64];size_t used=0;while(used<18){int n=esp_transport_read(owner.websocket,output+used,(int)(sizeof(output)-used),500);check(n>0,"real SDK frame parser reads complete payload");used+=(size_t)n;}output[used]=0;check(!strcmp(output,"{\"temperature\":23}"),"actual WS payload unchanged");check(dns_calls==1&&now_us<500000,"single resolution/shared budget");esp_transport_close(owner.websocket);ts_api_ws_test_transport_destroy(&owner);check(!live_count&&!raw,"all SDK handles and credentials freed");
 }
 for(unsigned fault=0;fault<7;fault++){
  reset();dns_fail=fault==0;dns_stall=fault==1;connect_stall=fault==2;read_stall=fault==3;write_stall=fault==4;bad_accept=fault==5;slow_header=fault==6;
  ts_api_ws_test_transport_t owner;check(ts_api_ws_test_transport_init(&owner,"wss://node.fixture:8443/feed",100000)==ESP_OK,"fault fixture allocates original owner");
  check(esp_transport_connect(owner.websocket,owner.hostname,owner.port,100)<0,"DNS/connect/read/write/accept/drip failures reject handshake");check(now_us<=100000,"slow whole handshake cannot multiply timeout");ts_api_ws_test_transport_destroy(&owner);check(!live_count&&!raw,"every failure releases all SDK transports");
 }
 reset();ts_api_ws_test_transport_t owner;check(ts_api_ws_test_transport_init(&owner,"ws://node.fixture:8080/",100000)==ESP_OK,"retry fixture init");io_retry=true;check(esp_transport_connect(owner.websocket,owner.hostname,owner.port,100)==0,"nonblocking EAGAIN read retries without treating it as EOF");ts_api_ws_test_transport_destroy(&owner);
 for(int budget=0;budget<32;budget++){reset();allocation_budget=budget;esp_err_t ret=ts_api_ws_test_transport_init(&owner,"wss://user:synthetic@node.fixture:8443/feed?q=1",100000);allocation_budget=-1;if(ret==ESP_OK)ts_api_ws_test_transport_destroy(&owner);check(!live_count&&!raw,"constructor allocation sweep cannot leak any handle/string");}
 reset();check(ts_api_ws_test_transport_init(&owner,"ftp://node.fixture/",100000)==ESP_ERR_INVALID_ARG&&!live_count&&!dns_calls,"invalid target rejects before transport/DNS");check(ts_api_ws_test_transport_init(&owner,"ws://node.fixture/",0)==ESP_ERR_TIMEOUT&&!live_count,"expired operation cannot allocate a client");
 printf("%u checks: actual diagnostic owner+SDK transport/WS handshake/frame/crypto;synthetic raw TCP/TLS,DNS,clock only;no network\n",checks);return 0;
}
'''
 run_native(code,[IDF/'components/http_parser/http_parser.c',root/'components/ts_http_fetch/src/ts_http_url.c',sdk/'library/libmbedcrypto.a'],headers,[root/'components/ts_api/src',IDF/'components/mbedtls/mbedtls/include'])
