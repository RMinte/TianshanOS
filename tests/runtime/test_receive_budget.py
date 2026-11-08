#!/usr/bin/env python3
"""Actual generated SDK receive/session functions; deterministic clock/socket I/O.

No external network, device, real credentials or wall-clock slow-request traffic.
"""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'))
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
def source(path):
 if args.source_package:
  with ZipFile(args.source_package) as z:
   if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
  return subprocess.check_output(['git','show','35e081ac8:'+path],cwd=os.environ.get('SOURCE_REPOSITORY',ROOT),text=True)
 return (ROOT/path).read_text()
def function(text,name):
 m=re.search(r'^(?:static )?[\w *]+\b'+name+r'\([^;]*?\)\s*\{',text,re.M);assert m,name
 return text[m.start():text.index('\n}',m.end())+2]+'\n'
with tempfile.TemporaryDirectory(prefix='ts-rx-budget-') as tmp:
 t=Path(tmp);namespace={'__name__':'rx_fixture_generator'};exec(compile(source('tools/prepare_httpd_admission.py'),'prepare.py','exec'),namespace);namespace['prepare'](IDF,t)
 txrx=(t/'httpd_txrx.c').read_text();sess=(t/'httpd_sess.c').read_text();parse=(t/'httpd_parse.c').read_text()
 block=re.search(r'/\* TS_RX_BUDGET_BEGIN[\s\S]*?/\* TS_RX_BUDGET_END \*/',txrx)
 code=r"""
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_SIZE 0x104
#define HTTPD_SOCK_ERR_TIMEOUT -7
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define LOG_FMT(x) x
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef int esp_err_t;
typedef enum {HTTPD_RECV_OPT_NONE,HTTPD_RECV_OPT_BLOCKING,HTTPD_RECV_OPT_HALT_AFTER_PENDING} httpd_recv_opt_t;
static _Thread_local uint32_t fake_clock,step;
static _Thread_local unsigned reads;
static _Thread_local int result_bytes;
static uint32_t esp_log_timestamp(void){return fake_clock;}
struct sock_db {void *handle;int fd;char pending_data[128];size_t pending_len;uint64_t lru_counter;int(*recv_fn)(void*,int,char*,size_t,int);};
struct httpd_req_aux {struct sock_db *sd;};
typedef struct {struct httpd_req_aux *aux;} httpd_req_t;
struct httpd_data {uint64_t lru_counter;};
static int fake_recv(void *handle,int fd,char *out,size_t n,int flags){(void)handle;(void)fd;(void)flags;++reads;fake_clock+=step;if(reads>64)return -99;if(result_bytes<=0)return result_bytes;size_t got=(size_t)result_bytes<n?(size_t)result_bytes:n;memset(out,'x',got);return (int)got;}
"""
 if block:code+=block.group()+'\n'
 else:code+='static void ts_httpd_receive_begin(void){}\nstatic void ts_httpd_receive_end(void){}\nstatic void ts_httpd_set_body_receive_budget(uint32_t ms){(void)ms;}\n'
 for name in ['httpd_recv_pending','httpd_recv_with_opt','httpd_recv']:code+=function(txrx,name)
 if 'ts_httpd_decode_content_length(' in parse:code+=function(parse,'ts_httpd_decode_content_length')
 else:
  # Execute the exact old assignment with a 32-bit request field. A driver shim
  # adds the new test's return channel; it does not add rejection behavior.
  m=re.search(r'r->content_len = \(\(int\)parser->content_length[\s\S]*?;',parse);assert m
  code+='static bool ts_httpd_decode_content_length(uint64_t wire,bool present,bool chunked,size_t *length){(void)present;(void)chunked;struct{uint32_t content_len;}req,*r=&req;struct{uint64_t content_length;}p={wire},*parser=&p;'+m.group()+'*length=r->content_len;return true;}\n'
 ws=(t/'httpd_ws.c').read_text()
 if 'uint64_t wide_len =' in ws:
  match=re.search(r'uint64_t wide_len =[\s\S]*?frame->len = \(size_t\)wide_len;',ws)
 else:match=re.search(r'frame->len = \(\(\(uint64_t\)length_bytes\[0\][\s\S]*?;',ws)
 assert match
 # Execute the exact SDK narrowing block with the ESP32's32-bit frame field.
 # Pointers/other library code remain native64-bit; this is an ABI model.
 code+='\n#undef SIZE_MAX\n#define SIZE_MAX UINT32_MAX\nstatic int decode_ws_32(const uint8_t length_bytes[8],uint32_t *out){struct{uint32_t len;}value={0},*frame=&value;\n'+match.group()+'\n*out=frame->len;return ESP_OK;}\n#undef SIZE_MAX\n#define SIZE_MAX __SIZE_MAX__\n'
 code+=r"""
static atomic_uint checks,failures;
static void check(bool ok,const char *label){atomic_fetch_add(&checks,1);if(!ok){atomic_fetch_add(&failures,1);fprintf(stderr,"FAIL SDK receive %s\n",label);}}
static void reset(void){ts_httpd_receive_end();fake_clock=0;step=0;reads=0;result_bytes=1;}
static void one_body(httpd_req_t *req,uint32_t budget,uint32_t increment){
 reset();ts_httpd_receive_begin();ts_httpd_set_body_receive_budget(budget);step=increment;char data[32];int ret=0;unsigned attempts=0;
 do{ret=httpd_recv(req,data,1);++attempts;}while(ret>0&&attempts<40);
 check(ret==HTTPD_SOCK_ERR_TIMEOUT&&attempts<=2,"body total budget expires despite successful partial receives");ts_httpd_receive_end();
}
static int lifecycle_mode;
static httpd_req_t *lifecycle_req;
static int httpd_req_new(struct httpd_data *hd,struct sock_db *s){(void)hd;(void)s;char b[4];if(lifecycle_mode==1){fake_clock+=15000;return httpd_recv(lifecycle_req,b,1)==HTTPD_SOCK_ERR_TIMEOUT?ESP_FAIL:ESP_OK;}if(lifecycle_mode==2)return ESP_FAIL;return ESP_OK;}
static int httpd_req_delete(struct httpd_data *hd){(void)hd;return lifecycle_mode==3?ESP_FAIL:ESP_OK;}
"""
 if 'httpd_sess_process_impl(' in sess:code+=function(sess,'httpd_sess_process_impl')
 code+=function(sess,'httpd_sess_process')
 code+=r"""
static void *worker(void *arg){uintptr_t kind=(uintptr_t)arg;struct sock_db sd={.recv_fn=fake_recv};struct httpd_req_aux aux={&sd};httpd_req_t req={&aux};char b[4];for(unsigned i=0;i<1000;i++){reset();ts_httpd_receive_begin();ts_httpd_set_body_receive_budget(kind?10:100);fake_clock=20;check((httpd_recv(&req,b,1)==HTTPD_SOCK_ERR_TIMEOUT)==(kind!=0),"independent server-task deadlines cannot leak across listeners");ts_httpd_receive_end();}return NULL;}
int main(void){
 struct sock_db sd={.recv_fn=fake_recv};struct httpd_req_aux aux={&sd};httpd_req_t req={&aux};char data[64];
 reset();fake_clock=500000;check(httpd_recv(&req,data,4)==1,"outside request lifetime has no stale timeout");
 reset();ts_httpd_receive_begin();step=4000;unsigned n=0;int ret=0;do{ret=httpd_recv(&req,data,1);++n;}while(ret>0&&n<20);check(ret==HTTPD_SOCK_ERR_TIMEOUT&&n==4,"slow header fragments share one15s budget");check(reads==4,"deadline checked after a late successful socket read");ts_httpd_receive_end();
 one_body(&req,10000,6000);one_body(&req,30000,16000);one_body(&req,300000,160000);
 reset();ts_httpd_receive_begin();ts_httpd_set_body_receive_budget(10);fake_clock=9;ts_httpd_set_body_receive_budget(300000);step=1;check(httpd_recv(&req,data,1)==HTTPD_SOCK_ERR_TIMEOUT,"second admission cannot extend an auth budget");ts_httpd_receive_end();
 reset();ts_httpd_receive_begin();ts_httpd_set_body_receive_budget(UINT32_MAX);fake_clock=300000;check(httpd_recv(&req,data,1)==HTTPD_SOCK_ERR_TIMEOUT&&!reads,"SDK clamps maximum body budget before another receive");ts_httpd_receive_end();
 reset();fake_clock=UINT32_MAX-100;ts_httpd_receive_begin();fake_clock+=15000;check(httpd_recv(&req,data,1)==HTTPD_SOCK_ERR_TIMEOUT,"monotonic32-bit millisecond wrap remains bounded");ts_httpd_receive_end();
 reset();ts_httpd_receive_begin();ts_httpd_set_body_receive_budget(10);step=6;check(httpd_recv_with_opt(&req,data,8,HTTPD_RECV_OPT_BLOCKING)==HTTPD_SOCK_ERR_TIMEOUT&&reads==2,"blocking SDK receive checks deadline inside every partial-read loop");ts_httpd_receive_end();
 reset();ts_httpd_receive_begin();result_bytes=0;check(httpd_recv_with_opt(&req,data,8,HTTPD_RECV_OPT_BLOCKING)==0&&reads==1,"EOF exits blocking read instead of spinning");ts_httpd_receive_end();
 reset();sd.pending_len=3;memcpy(sd.pending_data+sizeof(sd.pending_data)-3,"abc",3);ts_httpd_receive_begin();check(httpd_recv_with_opt(&req,data,3,HTTPD_RECV_OPT_HALT_AFTER_PENDING)==3&&!memcmp(data,"abc",3)&&!reads,"pending data remains exact and needs no socket read");ts_httpd_receive_end();
 reset();sd.pending_len=3;memcpy(sd.pending_data+sizeof(sd.pending_data)-3,"abc",3);ts_httpd_receive_begin();fake_clock=15000;check(httpd_recv_with_opt(&req,data,3,HTTPD_RECV_OPT_HALT_AFTER_PENDING)==HTTPD_SOCK_ERR_TIMEOUT&&sd.pending_len==3&&!reads,"expired request cannot consume pending bytes");ts_httpd_receive_end();sd.pending_len=0;
 reset();ts_httpd_receive_begin();result_bytes=16;step=1;check(httpd_recv_with_opt(&req,data,64,HTTPD_RECV_OPT_BLOCKING)==64&&reads==4,"normal fragmented body bytes are unchanged");ts_httpd_receive_end();
 struct httpd_data hd={0};lifecycle_req=&req;
 for(lifecycle_mode=0;lifecycle_mode<4;lifecycle_mode++){reset();int r=httpd_sess_process(&hd,&sd);check(lifecycle_mode==0?r==ESP_OK:r!=ESP_OK,"actual SDK session wrapper preserves normal/error paths");fake_clock=UINT32_MAX;check(httpd_recv(&req,data,1)>0,"every session exit clears receive lifetime");}
 for(unsigned i=0;i<8;i++){uint64_t values[]={0,1,1024,16U*1024U*1024U,16U*1024U*1024U+1,UINT32_MAX,(uint64_t)UINT32_MAX+1,UINT64_MAX};size_t value=123;bool ok=ts_httpd_decode_content_length(values[i],i!=7,false,&value);bool allowed=i<4||i==7;check(ok==allowed&&(!ok||value==(i==7?0:values[i])),"wire length bound and exact64-bit absence sentinel cannot truncate");}
 size_t explicit_length=123;check(!ts_httpd_decode_content_length(UINT64_MAX,true,false,&explicit_length),"explicit maximum wire length is not an absent-header sentinel");check(!ts_httpd_decode_content_length(UINT64_MAX,false,true,&explicit_length),"unsupported chunked body is refused before handler dispatch");check(!ts_httpd_decode_content_length(16,true,true,&explicit_length),"chunked and length combination cannot select an alternate length");
 for(unsigned i=0;i<10;i++){uint64_t values[]={0,125,126,65535,65536,UINT32_MAX,(uint64_t)UINT32_MAX+1,(uint64_t)UINT32_MAX+65,(uint64_t)INT64_MAX+1,UINT64_MAX};uint8_t encoded[8];for(unsigned b=0;b<8;b++)encoded[b]=(uint8_t)(values[i]>>(56-b*8));uint32_t decoded=123;int ret=decode_ws_32(encoded,&decoded);bool allowed=i<6;check(allowed?(ret==ESP_OK&&decoded==values[i]):ret==ESP_ERR_INVALID_SIZE,"actual SDK64-bit WS length cannot wrap into a small32-bit frame");}
 pthread_t threads[2];for(uintptr_t i=0;i<2;i++)assert(!pthread_create(&threads[i],NULL,worker,(void*)i));for(unsigned i=0;i<2;i++)pthread_join(threads[i],NULL);
 printf("%u checks, %u failures: actual SDK receive/session/length code; deterministic socket/clock +2000 per-task operations, no network/device\n",atomic_load(&checks),atomic_load(&failures));return atomic_load(&failures)?1:0;
}
"""
 (t/'test.c').write_text(code)
 subprocess.run(['cc','-std=gnu11','-g','-fsanitize='+os.environ.get('RECEIVE_TEST_SANITIZER','address,undefined'),'-fno-sanitize-recover=all',str(t/'test.c'),'-pthread','-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True,timeout=30)
