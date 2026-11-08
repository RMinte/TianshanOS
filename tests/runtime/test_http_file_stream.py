"""Production file sender and HTTP wrapper, real stdio incl. injected read EIO."""
from test_console_lifecycle import body, run_native
import os
from pathlib import Path

def sender_body(path,name):
    override=os.environ.get("TS_JSON_CACHE_BEFORE")
    return body(Path(override)/path if override and (Path(override)/path).exists() else path,name)

code = r'''
#define _DARWIN_C_SOURCE
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "esp_err.h"
#define ESP_LOGW(...) ((void)0)
#define TS_LOGW(...) ((void)0)
#define TS_LOGE(...) ((void)0)
#define TS_MALLOC_PSRAM malloc
#define HTTP_GET 0
#define HTTPD_SOCK_ERR_TIMEOUT -7
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_200 "200 OK"
#define HTTPD_400 "400 Bad Request"
#define HTTPD_404 "404 Not Found"
#define HTTPD_500 "500 Internal Server Error"
#define TAG "fixture"
static void ts_httpd_set_body_receive_budget(uint32_t milliseconds){(void)milliseconds;}
typedef struct {void *user_ctx;const char *uri;int method;size_t content_len;} httpd_req_t;
typedef struct {httpd_req_t *req;const char *uri;int method;char *body;size_t body_len;} ts_http_request_t;
typedef struct {int (*handler)(ts_http_request_t*,void*);void *user_data;bool (*admit)(ts_http_request_t*,void*);size_t max_body_bytes;uint32_t body_timeout_ms;} ts_http_route_t;
static bool s_stopping,accept_gzip,fail_type;
static unsigned header_calls,fail_header;
static char mime[100],disposition[900],csp[120],nosniff[30];
static const char *disposition_pointer;
static bool ts_http_authorize_route(ts_http_request_t *r,const ts_http_route_t *d){(void)r;(void)d;return true;}
static size_t fault_after=SIZE_MAX,position,received;
static int opened,closed,ends,chunks,fail_chunk,fail_end,json_errors;
static char payload[100000],wire[100000];
static bool is_compressible(const char *ext){return ext&&(!strcmp(ext,".js")||!strcmp(ext,".html")||!strcmp(ext,".css"));}
static bool client_accepts_gzip(httpd_req_t *req){(void)req;return accept_gzip;}
static bool ts_storage_exists(const char *p){struct stat st;return !stat(p,&st);}
static ssize_t ts_storage_size(const char *p){struct stat st;return stat(p,&st)?-1:st.st_size;}
static ssize_t ts_storage_read_file(const char *p,void *b,size_t n){FILE *f=fopen(p,"rb");assert(f);size_t r=fread(b,1,n,f);assert(!fclose(f));return r;}
static int reader(void *cookie,char *buf,int n){
 FILE *f=cookie;if(position>=fault_after){errno=EIO;return -1;}
 if((size_t)n>fault_after-position)n=(int)(fault_after-position);
 int got=(int)fread(buf,1,(size_t)n,f);position+=got;return got;
}
static int closer(void *cookie){++closed;return fclose(cookie);}
#ifndef __APPLE__
static ssize_t cookie_reader(void *p,char *b,size_t n){return reader(p,b,(int)n);}
#endif
static FILE *open_stream(const char *p,const char *m){
 FILE *f=fopen(p,m);if(!f)return NULL;++opened;position=0;
#ifdef __APPLE__
 return funopen(f,reader,NULL,NULL,closer);
#else
 cookie_io_functions_t io={.read=cookie_reader,.close=closer};return fopencookie(f,"r",io);
#endif
}
static int httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;snprintf(mime,sizeof(mime),"%s",s);return fail_type?ESP_FAIL:ESP_OK;}
static char encoding[20],cache[100];
static int httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *s){
 (void)r;if(++header_calls==fail_header)return ESP_FAIL;
 if(!strcmp(k,"Content-Encoding"))strcpy(encoding,s);if(!strcmp(k,"Cache-Control"))strcpy(cache,s);
 if(!strcmp(k,"Content-Disposition")){strcpy(disposition,s);disposition_pointer=s;}if(!strcmp(k,"Content-Security-Policy"))strcpy(csp,s);if(!strcmp(k,"X-Content-Type-Options"))strcpy(nosniff,s);
 return ESP_OK;
}
static int httpd_resp_send(httpd_req_t *r,const char *p,size_t n){(void)r;if(disposition_pointer)assert(!strcmp(disposition_pointer,disposition));if(n)memcpy(wire+received,p,n);received+=n;return ESP_OK;}
static int httpd_resp_send_chunk(httpd_req_t *r,const char *p,size_t n){
 if(!n){++ends;return fail_end?ESP_FAIL:ESP_OK;}
 if(++chunks==fail_chunk)return ESP_FAIL;return httpd_resp_send(r,p,n);
}
static int ts_http_send_error(ts_http_request_t *r,int status,const char *s){(void)r;(void)status;(void)s;++json_errors;return ESP_FAIL;}
static int httpd_resp_set_status(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
static int httpd_resp_send_err(httpd_req_t *r,int c,const char *s){(void)r;(void)c;(void)s;return ESP_OK;}
static int httpd_req_recv(httpd_req_t *r,void *p,size_t n){(void)r;(void)p;(void)n;assert(false);return -1;}
#define fopen open_stream
'''
for name in ['send_file', 'ts_http_send_file', 'ts_http_send_private_file']:
    code += body('components/ts_net/src/ts_http_server.c', name)
code += '\n#undef fopen\n'
code += body('components/ts_net/src/ts_http_server.c', 'ts_http_admit_body')
code += body('components/ts_net/src/ts_http_server.c', 'ts_http_receive_body')
code += body('components/ts_net/src/ts_http_server.c', 'http_handler_wrapper')
code += sender_body('components/ts_net/src/ts_http_server.c','ts_http_send_response')
code += sender_body('components/ts_net/src/ts_http_server.c','ts_http_send_json')
code += '\ntypedef ts_http_request_t ts_https_req_t;\n'
code += sender_body('components/ts_https/src/ts_https.c','ts_https_send_json')
code += r'''
static int route_file(ts_http_request_t *r,void *p){return ts_http_send_file(r,p);}
static int route_private_file(ts_http_request_t *r,void *p){return ts_http_send_private_file(r,p);}
static void reset(void){opened=closed=ends=chunks=fail_chunk=fail_end=json_errors=0;disposition_pointer=NULL;received=header_calls=fail_header=0;fail_type=false;encoding[0]=cache[0]=mime[0]=disposition[0]=csp[0]=nosniff[0]=0;fault_after=SIZE_MAX;}
static void make(const char *p,size_t n){FILE *f=fopen(p,"wb");assert(f);assert(fwrite(payload,1,n,f)==n);assert(!fclose(f));}
static int request(const char *p){ts_http_route_t route={route_file,(void*)p};httpd_req_t req={.user_ctx=&route,.uri=p,.method=HTTP_GET};return http_handler_wrapper(&req);}
static int private_request(const char *p){ts_http_route_t route={route_private_file,(void*)p};httpd_req_t req={.user_ctx=&route,.uri="/api/v1/file/download",.method=HTTP_GET};return http_handler_wrapper(&req);}
int main(void){
 const char *json="{\"private_key\":\"synthetic-json-canary\",\"code\":0}";
 httpd_req_t raw={0};ts_http_request_t json_req={.req=&raw};
 int statuses[]={200,201,400,401,403,404,409,413,429,500,503};
 for(unsigned i=0;i<sizeof(statuses)/sizeof(*statuses);++i){
  reset();assert(ts_http_send_json(&json_req,statuses[i],json)==ESP_OK&&!strcmp(cache,"no-store")&&header_calls==1&&received==strlen(json)&&!memcmp(wire,json,received));
  reset();assert(ts_https_send_json(&json_req,statuses[i],json)==ESP_OK&&!strcmp(cache,"no-store")&&header_calls==1&&received==strlen(json)&&!memcmp(wire,json,received));
 }
 reset();fail_header=1;assert(ts_http_send_json(&json_req,200,json)==ESP_FAIL&&!received);
 reset();fail_header=1;assert(ts_https_send_json(&json_req,200,json)==ESP_FAIL&&!received);
 reset();assert(ts_http_send_json(NULL,200,json)==ESP_ERR_INVALID_ARG&&!header_calls&&!received);
 assert(ts_https_send_json(NULL,200,json)==ESP_ERR_INVALID_ARG&&!header_calls&&!received);
 assert(ts_https_send_json(&json_req,200,NULL)==ESP_ERR_INVALID_ARG&&!header_calls&&!received);
 reset();assert(ts_http_send_json(&json_req,200,NULL)==ESP_OK&&!strcmp(cache,"no-store")&&!received);
 puts("PASS actual HTTP/HTTPS JSON senders: eleven success/error statuses no-store and exact body; cache header failure emits no private body; original empty/null contracts preserved");
 for(size_t i=0;i<sizeof(payload);i++)payload[i]=(char)(i%251);
 size_t sizes[]={0,12,32767,32768,32769,65536,73791};
 for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++){
  reset();make("file+v1.bin",sizes[i]);assert(request("file+v1.bin")==ESP_OK);
  assert(received==sizes[i]&&!memcmp(wire,payload,received)&&!json_errors);
  assert(ends==(sizes[i]>=32768));assert(opened==closed);
  reset();assert(private_request("file+v1.bin")==ESP_OK);
  assert(!strcmp(cache,"no-store")&&!strcmp(nosniff,"nosniff")&&strstr(csp,"sandbox")&&strstr(csp,"default-src 'none'")&&strstr(disposition,"attachment;")&&received==sizes[i]&&!memcmp(wire,payload,received)&&!json_errors);
  assert(ends==(sizes[i]>=32768)&&opened==closed);
 }
 make("file+v1.bin",65536);
 size_t boundaries[]={0,1,8192,17000,65535};
 for(unsigned i=0;i<sizeof(boundaries)/sizeof(*boundaries);i++){
  reset();fault_after=boundaries[i];assert(request("file+v1.bin")==ESP_FAIL);
  assert(!ends&&!json_errors&&opened==1&&closed==1&&received<=boundaries[i]);
  reset();fault_after=boundaries[i];assert(private_request("file+v1.bin")==ESP_FAIL);
  assert(!strcmp(cache,"no-store")&&!ends&&!json_errors&&opened==closed);
 }
 reset();fail_chunk=2;assert(request("file+v1.bin")==ESP_FAIL&&!ends&&!json_errors&&opened==closed);
 reset();fail_end=1;assert(request("file+v1.bin")==ESP_FAIL&&ends==1&&!json_errors&&opened==closed);
 const char *assets[]={"./index.html","./app.js","./style.css"};
 for(unsigned i=0;i<3;i++){
  reset();make(assets[i],40000);assert(request(assets[i])==ESP_OK&&received==40000&&!memcmp(wire,payload,received));
  assert(strstr(cache,i?"max-age":"no-cache"));
  char gz[80];snprintf(gz,sizeof(gz),"%s.gz",assets[i]);make(gz,33333);
  reset();accept_gzip=true;assert(request(assets[i])==ESP_OK&&!strcmp(encoding,"gzip")&&received==33333&&!memcmp(wire,payload,received));accept_gzip=false;
  reset();assert(private_request(assets[i])==ESP_OK&&!strcmp(cache,"no-store")&&received==40000&&!memcmp(wire,payload,received));
  reset();accept_gzip=true;assert(private_request(assets[i])==ESP_OK&&!strcmp(cache,"no-store")&&!encoding[0]&&received==40000&&!memcmp(wire,payload,received));accept_gzip=false;
 }

 /* Private downloads return the named file, not a different compressed sibling. */
 reset();make("./index.html",40000);char different[4]={'b','a','d','!'};FILE *neighbor=fopen("./index.html.gz","wb");assert(neighbor&&fwrite(different,1,4,neighbor)==4&&!fclose(neighbor));accept_gzip=true;
 assert(private_request("./index.html")==ESP_OK&&received==40000&&!memcmp(wire,payload,received)&&!encoding[0]);accept_gzip=false;
 const char *names[]={"quote\"\r\n例.html","plus+name.svg","space name.txt"};
 for(unsigned i=0;i<3;i++){reset();make(names[i],12);assert(private_request(names[i])==ESP_OK&&received==12&&!strchr(disposition,'\r')&&!strchr(disposition,'\n'));assert(strstr(disposition,"filename*=UTF-8''"));if(i==0)assert(strstr(disposition,"%22%0D%0A%E4%BE%8B.html"));if(i==1)assert(strstr(disposition,"plus%2Bname.svg"));if(i==2)assert(strstr(disposition,"space%20name.txt"));}
 for(unsigned fail=1;fail<=4;fail++){reset();fail_header=fail;assert(private_request("file+v1.bin")==ESP_FAIL&&!received&&!opened&&!json_errors);}
 reset();fail_type=true;assert(private_request("file+v1.bin")==ESP_FAIL&&!received&&!opened&&!json_errors);
 reset();char oversized[257];memset(oversized,'x',256);oversized[256]=0;assert(private_request(oversized)==ESP_ERR_INVALID_SIZE&&!header_calls&&!received&&!opened);
 reset();make("no-extension",12);assert(private_request("no-extension")==ESP_OK&&!strcmp(cache,"no-store")&&received==12);
 reset();private_request("missing.json");assert(json_errors==1&&!strcmp(cache,"no-store"));
 puts("PASS private download boundary: attachment/UTF-8 filename/no-store/nosniff/sandbox, header failures before bytes, exact named file even with conflicting gzip neighbor, empty/small/chunked/extensionless/error; public asset cache and gzip unchanged");
 puts("PASS F07 actual sender + HTTP wrapper: threshold/whole/partial chunks, real stdio EIO, send/terminator failures, closed streams, index/JS/CSS/gzip/plus-name content");
}
'''
run_native(code)
