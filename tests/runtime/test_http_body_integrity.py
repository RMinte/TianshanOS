#!/usr/bin/env python3
"""Real HTTP/HTTPS wrappers, body receiver and response status; synthetic SDK I/O."""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--source-ref');p.add_argument('--source-package');p.add_argument('--case',choices=['all','bodies','statuses'],default='all');args=p.parse_args()
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
        return subprocess.check_output(['git','show','35e081ac8:'+path],cwd=ROOT,text=True)
    return subprocess.check_output(['git','show',args.source_ref+':'+path],cwd=ROOT,text=True) if args.source_ref else (ROOT/path).read_text()
def function(text,name,required=True):
    m=re.search(r'^(?:static )?esp_err_t '+name+r'\([^;]*?\)\s*\{',text,re.M)
    if not m:assert not required,name;return ''
    return text[m.start():text.index('\n}',m.start())+2]+'\n'
http=source('components/ts_net/src/ts_http_server.c');https=source('components/ts_net/src/ts_https_server.c')
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int esp_err_t;
static void ts_httpd_set_body_receive_budget(uint32_t milliseconds){(void)milliseconds;}
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define HTTP_GET 0
#define HTTPD_SOCK_ERR_TIMEOUT -7
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define TS_LOGW(...) ((void)0)
#define TS_LOGE(...) ((void)0)
typedef struct {void *user_ctx;const char *uri;int method;size_t content_len;} httpd_req_t;
typedef struct {httpd_req_t *req;const char *uri;int method;char *body;size_t body_len;} ts_http_request_t;
typedef struct {int (*handler)(ts_http_request_t*,void*);void *user_data;bool (*admit)(ts_http_request_t*,void*);size_t max_body_bytes;uint32_t body_timeout_ms;} ts_http_route_t;
static bool s_stopping,oom;static unsigned calls,reads,allocations,status;static int failure;static size_t position,fail_after;
static unsigned char payload[73791];static char wire_status[64];static char response_body[200];
static void *allocate(size_t n){allocations++;return oom?NULL:malloc(n);}
#define TS_MALLOC_PSRAM allocate
static bool ts_http_authorize_route(ts_http_request_t *r,const ts_http_route_t *d){(void)r;(void)d;return true;}
static int httpd_resp_set_status(httpd_req_t *r,const char *s){(void)r;snprintf(wire_status,sizeof(wire_status),"%s",s);return 0;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value){(void)r;assert(!strcmp(key,"Cache-Control")&&!strcmp(value,"no-store"));return ESP_OK;}
static int httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return 0;}
static int httpd_resp_send(httpd_req_t *r,const char *b,size_t n){(void)r;assert(n<sizeof(response_body));memcpy(response_body,b,n);response_body[n]=0;status=(unsigned)atoi(wire_status);return 0;}
static int httpd_resp_send_err(httpd_req_t *r,int s,const char *m){(void)r;(void)m;status=s;return 0;}
static int httpd_req_recv(httpd_req_t *r,char *b,size_t n){
 (void)r;reads++;if(failure&&position>=fail_after){int ret=failure;failure=-1;return ret==-2?0:ret;}
 size_t got=n>1024?1024:n;if(failure&&got>fail_after-position)got=fail_after-position;
 assert(position+got<=sizeof(payload));memcpy(b,payload+position,got);position+=got;return (int)got;
}
static int business(ts_http_request_t *r,void *d){
 (void)d;calls++;assert(r->body_len==r->req->content_len);
 if(r->body_len){assert(r->body[r->body_len]=='\0');assert(!memcmp(r->body,payload,r->body_len));}return ESP_OK;
}
'''
code+=function(http,'ts_http_send_response')+function(http,'ts_http_send_json')+function(http,'ts_http_send_error')
code+=function(http,'ts_http_admit_body')+function(http,'ts_http_receive_body',False)+function(http,'http_handler_wrapper')+function(https,'https_handler_wrapper')
code+=r'''
static void reset(void){oom=false;calls=reads=allocations=status=0;position=0;failure=0;fail_after=0;wire_status[0]=response_body[0]=0;}
static void bodies(void){
 for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(unsigned char)(i%251);
 int(*wrappers[])(httpd_req_t*)={http_handler_wrapper,https_handler_wrapper};
 for(unsigned w=0;w<2;w++){
  ts_http_route_t route={business,NULL};httpd_req_t req={.user_ctx=&route,.uri="/api/v1/fixture",.method=1};
  const size_t sizes[]={0,1,7,1024,1025,sizeof(payload)};
  for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++){reset();req.content_len=sizes[i];assert(wrappers[w](&req)==ESP_OK&&calls==1&&position==sizes[i]&&allocations==(sizes[i]>0));}
  const int errors[]={-1,0,HTTPD_SOCK_ERR_TIMEOUT};
  for(unsigned i=0;i<sizeof(errors)/sizeof(*errors);i++)for(unsigned partial=0;partial<2;partial++){
   reset();req.content_len=100;failure=errors[i]?errors[i]:-2;fail_after=partial?3:0;
   assert(wrappers[w](&req)==ESP_FAIL&&calls==0&&status==(errors[i]==HTTPD_SOCK_ERR_TIMEOUT?408:400));assert(reads==partial+1);
  }
  reset();req.content_len=100;oom=true;assert(wrappers[w](&req)==ESP_FAIL&&calls==0&&reads==0&&allocations==1&&status==500);
  reset();req.content_len=SIZE_MAX;assert(wrappers[w](&req)==ESP_FAIL&&calls==0&&reads==0&&allocations==0&&status==413);
  printf("PASS %s: full/empty/binary/multi-read; EOF/error/timeout at start or mid-body; OOM/length overflow: no business or reused unread connection\n",w?"HTTPS":"HTTP");
 }
}
static void statuses(void){
 httpd_req_t raw={0};ts_http_request_t req={.req=&raw};const int codes[]={200,201,202,204,400,401,403,404,405,408,409,413,422,429,500,503,599};
 for(unsigned i=0;i<sizeof(codes)/sizeof(*codes);i++){reset();assert(ts_http_send_error(&req,codes[i],"fixture")==ESP_OK);assert(status==(unsigned)codes[i]&&strstr(response_body,"fixture"));}
 reset();assert(ts_http_send_error(&req,99,"fixture")==ESP_OK&&status==500);reset();assert(ts_http_send_error(&req,600,"fixture")==ESP_OK&&status==500);
 puts("PASS real response builder: 17 valid status codes reach the SDK header unchanged; invalid codes become 500, never silent 200");
}
int main(void){RUN_CASES;return 0;}
'''
code=code.replace('RUN_CASES',{'all':'statuses();bodies();','bodies':'bodies();','statuses':'statuses();'}[args.case])
with tempfile.TemporaryDirectory(prefix='ts-http-body-') as tmp:
    c=Path(tmp)/'probe.c';exe=Path(tmp)/'probe';c.write_text(code)
    subprocess.run(['cc','-std=c11','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
