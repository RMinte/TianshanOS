"""Actual network diagnostic handlers; deterministic sockets/clock, no network."""
from pathlib import Path
import os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
def function(text,name):
 m=re.search(r'^(?:static )?[^\n;]+\b'+name+r'\([^;]+?\)\s*\{',text,re.M);assert m,name
 return text[m.start():text.index('\n}',m.end())+2]+'\n'
source=(root/'components/ts_api/src/ts_api_device.c').read_text()
code=r'''
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>
#include <stdatomic.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "cJSON.h"
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_ARG=0x102,ESP_ERR_INVALID_STATE=0x103,ESP_ERR_NO_MEM=0x101,TS_API_ERR_INVALID_ARG=1,TS_API_ERR_NO_MEM=6,TS_API_ERR_BUSY=10};
typedef struct {int code;char *message;cJSON *data;} ts_api_result_t;
static void ts_api_result_error(ts_api_result_t *r,int code,const char *msg){r->code=code;r->message=strdup(msg);}
static void ts_api_result_ok(ts_api_result_t *r,cJSON *data){r->code=0;r->data=data;}
#define TS_LOGI(...) ((void)0)
#define TS_LOGE(...) ((void)0)
static unsigned checks,failures,sockets,closes,sends,waits;static int scenario;
static int64_t now_us;
static void *json_fail(size_t size){(void)size;return NULL;}
static void check(bool v,const char *m){checks++;if(!v){failures++;fprintf(stderr,"FAIL %s scenario=%d\n",m,scenario);}}
static int64_t esp_timer_get_time(void){return now_us;}
static uint16_t inet_chksum(const void *p,size_t n){(void)p;(void)n;return 0;}
static uint8_t packet[128];static size_t packet_size;static struct sockaddr_in target;
static int fake_socket(int f,int t,int p){assert(f==AF_INET&&t==SOCK_RAW&&p==IPPROTO_ICMP);sockets++;return scenario==1?-1:5;}
static int fake_close(int fd){assert(fd==5);closes++;return 0;}
static int fake_fcntl(int fd,int cmd,...){assert(fd==5);(void)cmd;return scenario==2?-1:0;}
static int fake_setsockopt(int fd,int l,int o,const void *v,socklen_t n){(void)fd;(void)l;(void)o;(void)v;(void)n;return 0;}
static ssize_t fake_sendto(int fd,const void *p,size_t n,int flags,const struct sockaddr *to,socklen_t len){
 assert(fd==5);(void)flags;(void)len;sends++;memcpy(packet,p,n);packet_size=n;memcpy(&target,to,sizeof(target));
 if(scenario==3){errno=EIO;return -1;}if(scenario==5&&sends==1){errno=EAGAIN;return -1;}return (ssize_t)n;
}
static int fake_select(int n,fd_set *r,fd_set *w,fd_set *e,struct timeval *t){
 (void)n;(void)r;(void)w;(void)e;assert(t);waits++;
 if(scenario==4){now_us+=(int64_t)t->tv_sec*1000000+t->tv_usec;return 0;}
 if(scenario==6&&waits==1){errno=EINTR;return -1;}now_us+=1000;return 1;
}
static ssize_t fake_recvfrom(int fd,void *p,size_t n,int flags,struct sockaddr *from,socklen_t *len){
 assert(fd==5&&n>=packet_size+20);(void)flags;memset(p,0,20);((uint8_t*)p)[0]=0x45;memcpy((uint8_t*)p+20,packet,packet_size);((uint8_t*)p)[20]=0;
 memcpy(from,&target,sizeof(target));if(scenario==7)((struct sockaddr_in*)from)->sin_addr.s_addr^=1;*len=sizeof(target);return (ssize_t)packet_size+20;
}
#define socket fake_socket
#define close fake_close
#define fcntl fake_fcntl
#define setsockopt fake_setsockopt
#define select fake_select
#define sendto fake_sendto
#define recvfrom fake_recvfrom
'''
code=code.replace('#include <stdio.h>','#include <assert.h>\n#include <stdio.h>')
for name in ['ping_wait','device_ping_impl','api_device_ping']:
 if re.search(r'\b'+name+r'\([^;]+?\)\s*\{',source):code+=function(source,name)
# The production gate is a single nonblocking owner, not a second test policy.
if 's_ping_busy' in source:code=code.replace('static int64_t now_us;','static atomic_flag s_ping_busy=ATOMIC_FLAG_INIT;\nstatic int64_t now_us;')
code+=r'''
static void release(ts_api_result_t *r){cJSON_Delete(r->data);free(r->message);}
static ts_api_result_t invoke(const char *json){cJSON *p=cJSON_Parse(json);assert(p);ts_api_result_t r={0};api_device_ping(p,&r);cJSON_Delete(p);return r;}
int main(void){
 const char *invalid[]={"{\"timeout\":-1}","{\"timeout\":0}","{\"timeout\":0.5}","{\"timeout\":2147483648}","{\"timeout\":30001}","{\"timeout\":\"1000\"}","{\"host\":\"invalid\"}","{\"host\":123}"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){
  unsigned before=sockets;ts_api_result_t r=invoke(invalid[i]);check(r.code==TS_API_ERR_INVALID_ARG&&sockets==before,"invalid IP/timeout rejects before socket");release(&r);
 }
 for(scenario=0;scenario<8;scenario++){
  sockets=closes=sends=waits=0;now_us=0;ts_api_result_t r=invoke("{\"host\":\"192.0.2.8\",\"timeout\":1000}");
  check(sockets==1&&closes==(scenario!=1),"every owned socket closes once on success/fault");
  if(scenario==0||scenario==5||scenario==6)check(r.code==0&&cJSON_IsTrue(cJSON_GetObjectItem(r.data,"reachable")),"legitimate matching ICMP echo preserved");
  else check(r.code==0&&cJSON_IsFalse(cJSON_GetObjectItem(r.data,"reachable")),"socket/send/timeout failures return unreachable");
  check(now_us<=1000000,"shared send/read deadline never extends");release(&r);
 }
 scenario=0;sockets=0;atomic_flag_test_and_set(&s_ping_busy);ts_api_result_t busy=invoke("{}");check(busy.code==TS_API_ERR_BUSY&&!sockets,"busy ping refuses before socket");release(&busy);atomic_flag_clear(&s_ping_busy);busy=invoke("{}");check(busy.code==0,"ping resumes after owner release");release(&busy);
 cJSON *oom_params=cJSON_CreateObject();sockets=closes=0;cJSON_Hooks hooks={.malloc_fn=json_fail,.free_fn=free};cJSON_InitHooks(&hooks);ts_api_result_t oom={0};api_device_ping(oom_params,&oom);cJSON_InitHooks(NULL);check(oom.code==TS_API_ERR_NO_MEM&&sockets==1&&closes==1,"result OOM cannot leak ping socket or busy owner");release(&oom);cJSON_Delete(oom_params);
 printf("%u checks,%u failures: actual device.ping;synthetic socket/clock,no packets\n",checks,failures);return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='ts-network-diagnostics-') as td:
 d=Path(td);(d/'test.c').write_text(code);idf=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'));cjson=idf/'components/json/cJSON'
 subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(cjson),str(d/'test.c'),str(cjson/'cJSON.c'),'-lm','-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True,timeout=15)

# Reuse the existing real HTTP-owner fixture, without executing its unrelated
# main or introducing another transport simulation for this API entry point.
fixture=root/'tests/runtime/test_http_fetch.py'
ns={'__file__':str(fixture)}
exec(compile(fixture.read_text().rsplit('run_native(code,',1)[0],str(fixture),'exec'),ns)
http_code=ns['code'].split('int main(void)')[0]
http_code+=r'''
#include <stdatomic.h>
#define TS_API_ERR_INVALID_ARG 1
#define TS_API_ERR_NO_MEM 6
#define TS_API_ERR_BUSY 10
typedef struct {int code;char *message;cJSON *data;} ts_api_result_t;
static void ts_api_result_error(ts_api_result_t *r,int error,const char *message){r->code=error;r->message=strdup(message);}
static void ts_api_result_ok(ts_api_result_t *r,cJSON *data){r->code=0;r->data=data;}
static void *probe_json_fail(size_t size){(void)size;return NULL;}
static const char *esp_err_to_name(int error){(void)error;return "synthetic_transport_error";}
static atomic_flag s_ota_probe_busy=ATOMIC_FLAG_INIT;
'''
ota_source=(root/'components/ts_api/src/ts_api_ota.c').read_text()
for name in ['ota_test_connection_impl','api_ota_test_connection']:http_code+=function(ota_source,name)
http_code+=r'''
static void dispose(ts_api_result_t *r){cJSON_Delete(r->data);free(r->message);}
static ts_api_result_t probe(const char *url){cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"url",url);ts_api_result_t r={0};api_ota_test_connection(p,&r);cJSON_Delete(p);return r;}
int main(void){
 const char *bad[]={"ftp://fixture.invalid/file","https://fixture.invalid:0/","http://fixture.invalid/\r\nInjected:yes","relative/path"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){reset(NORMAL);ts_api_result_t r=probe(bad[i]);check(r.code==TS_API_ERR_INVALID_ARG&&!dns_calls&&!clients,"invalid diagnostic URL refused before DNS/client");dispose(&r);}
 char long_host[400];memset(long_host,'a',sizeof(long_host));memcpy(long_host,"http://",7);long_host[sizeof(long_host)-2]='/';long_host[sizeof(long_host)-1]=0;reset(NORMAL);ts_api_result_t r=probe(long_host);check(r.code==TS_API_ERR_INVALID_ARG&&!dns_calls,"long authority cannot overflow UI host field");dispose(&r);
 const char *urls[]={"http://fixture.invalid/path?x=1","https://fixture.invalid:8443/path?x=1"};
 for(unsigned i=0;i<2;i++){
  reset(NORMAL);r=probe(urls[i]);check(!r.code&&cJSON_IsTrue(cJSON_GetObjectItem(r.data,"dns_ok"))&&cJSON_IsTrue(cJSON_GetObjectItem(r.data,"tcp_ok"))&&cJSON_IsTrue(cJSON_GetObjectItem(r.data,"http_ok")),"actual OTA handler uses real HTTP owner;HTTP/HTTPS success preserves UI shape");check(dns_calls==1&&clients==1&&cleanups==1&&!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(r.data,"resolved_ip")),"192.0.2.10"),"one pinned DNS/owned client with actual resolved address");if(i)check(!strcmp(tls_name,"fixture.invalid"),"HTTPS retains actual original SNI/name verification");check(cJSON_IsNumber(cJSON_GetObjectItem(r.data,"dns_time_ms"))&&cJSON_IsNumber(cJSON_GetObjectItem(r.data,"tcp_time_ms"))&&cJSON_IsNumber(cJSON_GetObjectItem(r.data,"http_time_ms")),"all three UI timing fields stay present");dispose(&r);
 }
 const int faults[]={DNS_FAIL,WAIT,EXPIRE_EVENT};
 for(unsigned i=0;i<3;i++){reset(faults[i]);r=probe(urls[1]);check(!r.code&&cJSON_IsFalse(cJSON_GetObjectItem(r.data,"http_ok")),"DNS/whole deadline/late response cannot claim connectivity success");check(clients==cleanups,"fault releases real HTTP owner");if(faults[i]!=EXPIRE_EVENT)check(clock_us<=5000000,"actual diagnostic receives total five-second budget");dispose(&r);}
 reset(NORMAL);atomic_flag_test_and_set(&s_ota_probe_busy);r=probe(urls[0]);check(r.code==TS_API_ERR_BUSY&&!dns_calls&&!clients,"busy diagnostic has no network allocation");dispose(&r);atomic_flag_clear(&s_ota_probe_busy);r=probe(urls[0]);check(!r.code,"diagnostic available again after busy owner releases");dispose(&r);
 reset(NORMAL);cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"url",urls[0]);cJSON_Hooks hooks={.malloc_fn=probe_json_fail,.free_fn=free};cJSON_InitHooks(&hooks);r=(ts_api_result_t){0};api_ota_test_connection(p,&r);cJSON_InitHooks(NULL);check(r.code==TS_API_ERR_NO_MEM&&!dns_calls&&!clients,"mandatory response allocation fails before network");dispose(&r);cJSON_Delete(p);
 printf("%u checks: actual OTA diagnostic->shared HTTP/DNS/URL owner;synthetic transport/time,no network\n",checks);return 0;
}
'''
ns['run_native'](http_code,[ns['IDF']/'components/http_parser/http_parser.c',ns['IDF']/'components/json/cJSON/cJSON.c',root/'components/ts_http_fetch/src/ts_http_url.c'],ns['headers'])

# Same API result fixture; execute both existing scan APIs through their real
# WiFi owner. Only the radio SDK and its completion/timeout are substituted.
wifi_code=code.split('static bool ping_wait(')[0]
wifi_code+=r'''
#define TS_API_ERR_INTERNAL 7
#define TS_API_ERR_HARDWARE 5
#define TS_WIFI_MODE_OFF 0
#define TS_WIFI_MODE_STA 1
#define TS_WIFI_MODE_AP 2
#define TS_WIFI_MODE_APSTA 3
#define TS_LOGW(...) ((void)0)
static bool s_initialized=true;static int s_mode=TS_WIFI_MODE_STA;
static int scan_error,result_error;static bool scan_oom;static unsigned scans,record_calls,record_allocs,record_frees;
typedef struct {int unused;} wifi_scan_config_t;
typedef struct {char ssid[32];uint8_t bssid[6];int8_t rssi;uint8_t channel;int auth_mode;} ts_wifi_scan_result_t;
typedef struct {char ssid[33];uint8_t bssid[6];int8_t rssi;uint8_t primary;int authmode;} wifi_ap_record_t;
static int esp_wifi_scan_start(const wifi_scan_config_t *c,bool blocking){assert(c&&blocking);check(c->unused==0,"scan keeps bounded SDK default configuration");scans++;return scan_error;}
static int esp_wifi_scan_get_ap_records(uint16_t *count,wifi_ap_record_t *records){record_calls++;assert(*count==20);if(result_error)return result_error;memset(records,0,sizeof(*records));strcpy(records[0].ssid,"Synthetic WiFi");records[0].rssi=-30;records[0].primary=6;*count=1;return ESP_OK;}
static void *scan_alloc(size_t size){if(scan_oom)return NULL;record_allocs++;return malloc(size);}
static void scan_free(void *p){if(p)record_frees++;free(p);}
#define TS_MALLOC_PSRAM scan_alloc
#define free scan_free
'''
wifi_source=(root/'components/ts_net/src/ts_wifi.c').read_text()
for name in ['ts_wifi_scan_start','ts_wifi_scan_get_results']:wifi_code+=function(wifi_source,name)
wifi_code+=r'''
#undef free
static const char *auth_mode_str(int type){(void)type;return "WPA2";}
static const char *auth_mode_to_str(int type){return auth_mode_str(type);}
static void add_mac_to_json(cJSON *j,const char *key,const uint8_t *b){(void)b;cJSON_AddStringToObject(j,key,"00:00:00:00:00:00");}
'''
for path,name in [('ts_api_wifi.c','api_wifi_scan'),('ts_api_network.c','api_network_wifi_scan')]:wifi_code+=function((root/'components/ts_api/src'/path).read_text(),name)
wifi_code+=r'''
int main(void){
 for(unsigned api=0;api<2;api++)for(unsigned fault=0;fault<6;fault++){
  s_initialized=fault!=1;s_mode=fault==2?TS_WIFI_MODE_AP:TS_WIFI_MODE_STA;scan_error=fault==3?0x300c:0;result_error=fault==4?ESP_FAIL:0;scan_oom=fault==5;scans=record_calls=record_allocs=record_frees=0;
  ts_api_result_t r={0};esp_err_t ret=api?api_network_wifi_scan(NULL,&r):api_wifi_scan(NULL,&r);
  if(!fault){check(ret==ESP_OK&&!r.code&&cJSON_GetArraySize(cJSON_GetObjectItem(r.data,"networks"))==1,"both real scan APIs preserve authorized network list");check(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(r.data,"networks"),0),"ssid")),"Synthetic WiFi"),"SSID/channel result shape preserved");}
  else check(ret!=ESP_OK&&r.code&&!r.data,"mode/SDK blocking timeout/results/OOM faults cannot claim success");
  check(record_allocs==record_frees,"scan result buffer always released");if(fault==1||fault==2)check(!scans&&!record_calls,"invalid owner state cannot start radio scan");if(fault==3||fault==5)check(!record_calls,"scan timeout/OOM cannot consume result buffer");cJSON_Delete(r.data);free(r.message);
 }
 printf("%u checks: actual two WiFi scan APIs and owner;synthetic bounded radio SDK,no device\n",checks);return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='ts-wifi-diagnostics-') as td:
 d=Path(td);(d/'test.c').write_text(wifi_code);cjson=ns['IDF']/'components/json/cJSON'
 subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(cjson),str(d/'test.c'),str(cjson/'cJSON.c'),'-lm','-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True,timeout=15)
