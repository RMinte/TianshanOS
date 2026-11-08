#!/usr/bin/env python3
"""Actual route registration, HTTP/HTTPS wrappers and generic API admission.

Policy values come from real declarations via the existing HTTP fixture adapter;
header/session/SDK I/O and business effects are synthetic. Full real registry,
body-token handlers and browser flows are separate regression suites.
"""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
def source(path):
 if args.source_package:
  with ZipFile(args.source_package) as z:
   if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
  return subprocess.check_output(['git','show','35e081ac8:'+path],cwd=os.environ.get('SOURCE_REPOSITORY',ROOT),text=True)
 return (ROOT/path).read_text()
def function(text,name):
 m=re.search(r'^(?:static )?[^\n;]+\b'+name+r'\([^;]+?\)\s*\{',text,re.M);assert m,name
 return text[m.start():text.index('\n}',m.start())+2]+'\n'
fixture=ROOT/'tests/runtime/test_sensitive_auth.py';text=fixture.read_text();ns={'__name__':'prebody_fixture','__file__':str(fixture)}
exec(compile(text[:text.index('\nidf = Path(')],str(fixture),'exec'),ns)
code=ns['code'];code=code[:code.index('int main(void) {')]
# The real auth owner is separately tested; inject role change at receive here.
code=code.replace('static bool session_live = true;', 'static bool session_live = true;static bool downgrade_after_read,root_downgraded;')
code=code.replace('*session=1;*level=TS_PERM_ROOT;', '*session=1;*level=root_downgraded?TS_PERM_READ:TS_PERM_ROOT;')

web=source('components/ts_webui/src/ts_webui_api.c')
if args.source_package:
 for name in ('check_auth','api_request_access','api_admit','api_handler'):
  current=ns['extract'](name)
  replacement=function(web,name) if re.search(r'\b'+name+r'\([^;]*?\)\s*\{',web) else ''
  code=code.replace(current,replacement,1)
with tempfile.TemporaryDirectory(prefix='ts-prebody-sdk-') as sdk_dir:
 namespace={'__name__':'prebody_sdk_generator'}
 exec(compile(source('tools/prepare_httpd_admission.py'),'prepare.py','exec'),namespace)
 namespace['prepare'](Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf')),Path(sdk_dir))
 sdk=(Path(sdk_dir)/'httpd_txrx.c').read_text()
 budget=re.search(r'/\* TS_RX_BUDGET_BEGIN[\s\S]*?/\* TS_RX_BUDGET_END \*/',sdk)
 stub='static void ts_httpd_set_body_receive_budget(uint32_t milliseconds){(void)milliseconds;}'
 if budget:
  code=code.replace(stub,'static uint32_t esp_log_timestamp(void){return 0;}\n'+budget.group()+'\nstatic uint32_t selected_body_budget(void){return s_receive_budget;}\n')
 else:
  code=code.replace(stub,stub+'\nstatic void ts_httpd_receive_begin(void){}\nstatic void ts_httpd_receive_end(void){}\nstatic uint32_t selected_body_budget(void){return 0;}\n')
code=code.replace('typedef struct { int content_len;const char *query; } httpd_req_t;',
 'typedef struct{size_t content_len;const char *query,*uri,*auth;int method;void *user_ctx;} httpd_req_t;')
code=code.replace('.body=body,.body_len=body?strlen(body):0', '.body=(char*)body,.body_len=body?strlen(body):0')
code=code.replace('int method; const char *body; size_t body_len;', 'int method; char *body; size_t body_len;')
code=code.replace('(void)key;if (!r->auth) return ESP_ERR_NOT_FOUND;snprintf(out,n,"Bearer %s",r->auth);',
 '(void)key;const char *auth=r->auth?r->auth:r->req?r->req->auth:NULL;if (!auth) return ESP_ERR_NOT_FOUND;snprintf(out,n,"Bearer %s",auth);')
header=(ROOT/'components/ts_net/include/ts_http_server.h').read_text();start=header.index('typedef esp_err_t (*ts_http_handler_t)');end=header.index('} ts_http_route_t;',start)+len('} ts_http_route_t;');code+='typedef int ts_http_method_t;\n'+header[start:end]+'\n'
code+=r'''
#define HTTP_GET TS_HTTP_GET
#define HTTP_OPTIONS 6
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_SOCK_ERR_TIMEOUT -7
static bool s_stopping,body_oom,revoke_after_read;
static unsigned receives,body_allocations,checks,failures;
static size_t body_position;static const char *body_wire;static size_t body_wire_len;
static void check(bool ok,const char *label){++checks;if(!ok){++failures;fprintf(stderr,"FAIL %s\n",label);}}
static int httpd_resp_set_status(httpd_req_t *r,const char *v){(void)r;status=atoi(v);return ESP_OK;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *v){(void)r;(void)v;return ESP_OK;}
static int httpd_resp_send_err(httpd_req_t *r,int value,const char *v){(void)r;(void)v;status=value;return ESP_OK;}
static void *body_allocate(size_t n){++body_allocations;return body_oom||n>2U*1024U*1024U?NULL:malloc(n);}
#define TS_MALLOC_PSRAM body_allocate
static int httpd_req_recv(httpd_req_t *r,char *out,size_t n){
 (void)r;++receives;assert(body_position+n<=body_wire_len);if(n>1024)n=1024;if(body_wire)memcpy(out,body_wire+body_position,n);else memset(out,' ',n);body_position+=n;if(revoke_after_read)session_live=false;if(downgrade_after_read)root_downgraded=true;return (int)n;
}
'''
http=source('components/ts_net/src/ts_http_server.c');https=source('components/ts_net/src/ts_https_server.c')
for name in ('ts_http_authorize_route','ts_http_admit_body','ts_http_receive_body','http_handler_wrapper'):
 if name+'(' in http:code+=function(http,name)
code+=function(https,'https_handler_wrapper')
code+=r'''
static ts_http_route_t registered[12];static unsigned registered_count;
static esp_err_t ts_http_server_register_route(const ts_http_route_t *r){assert(registered_count<12);registered[registered_count++]=*r;return ESP_OK;}
'''
for name in ('login_handler','logout_handler','options_handler','file_upload_handler','ota_firmware_upload_handler','ota_www_upload_handler'):
 code+='static esp_err_t '+name+'(ts_http_request_t *r,void *u){(void)u;calls++;assert(r->body_len==r->req->content_len);if(body_wire&&r->body_len)assert(!memcmp(r->body,body_wire,r->body_len));status=200;return ESP_OK;}\n'
code+=function(web,'ts_webui_api_init')
code+=r'''
static void reset(void){ts_httpd_receive_end();ts_httpd_receive_begin();body_oom=revoke_after_read=downgrade_after_read=root_downgraded=false;session_live=true;receives=body_allocations=0;body_position=0;status=calls=0;free(response);response=NULL;}
static void request(unsigned transport,const char *uri,const char *auth,int method,const char *body,size_t n,int expected,bool admitted){
 reset();body_wire=body;body_wire_len=n;ts_http_route_t *route=NULL;for(unsigned i=0;i<registered_count;i++)if(registered[i].handler==api_handler&&registered[i].method==method)route=&registered[i];assert(route);
 httpd_req_t req={.user_ctx=route,.uri=uri,.method=method,.auth=auth,.content_len=n};const char *q=strchr(uri,'?');req.query=q?q+1:NULL;
 int result=transport?https_handler_wrapper(&req):http_handler_wrapper(&req);
 if(status!=expected)fprintf(stderr,"HTTP %s auth=%s transport=%u expected=%d actual=%d allocations=%u reads=%u calls=%d\n",uri,auth?auth:"none",transport,expected,status,body_allocations,receives,calls);
 check(status==expected,"response status retained");
 if(!admitted){check(receives==0&&body_allocations==0,"rejected request has zero body receive/allocation");check(calls==0&&result==ESP_FAIL,"rejected unread request closes before business");}
 else{check(body_position==n&&body_allocations==(n>0),"authorized body received exactly once");check(calls==1&&result==ESP_OK,"normal registered business executed once");check(selected_body_budget()==(!strncmp(uri,"/api/v1/auth",12)?10000:30000),"actual SDK owner selects the correct body deadline once");}
}
static void special_request(unsigned transport,ts_http_route_t *route,const char *auth,const char *body,size_t length,bool allowed){
 reset();body_wire=body;body_wire_len=length;httpd_req_t req={.user_ctx=route,.uri=route->uri,.method=route->method,.auth=auth,.content_len=length};
 int ret=transport?https_handler_wrapper(&req):http_handler_wrapper(&req);
 check(status==(allowed?200:413),"dedicated auth/upload route preserves success or denies over-limit bytes");
 check(allowed?(calls==1&&body_position==length):(calls==0&&!body_allocations&&!receives&&ret==ESP_FAIL),"dedicated route refuses allocation/read before effects or forwards exact authorized bytes");
 if(allowed)check(selected_body_budget()==300000,"large upload keeps its explicit300s allowance");
}
int main(void){
 check(ts_webui_api_init()==ESP_OK&&registered_count==11,"actual route registration retained");
 unsigned generic=0;for(unsigned i=0;i<registered_count;i++)if(registered[i].handler==api_handler){++generic;check(registered[i].admit!=NULL,"every generic method owns pre-body policy");}check(generic==4,"all four generic HTTP methods registered");
 for(unsigned i=0;i<registered_count;i++){
  if(registered[i].handler==login_handler)check(registered[i].max_body_bytes==16384&&registered[i].body_timeout_ms==10000,"dedicated login has small explicit byte/time limits");
  if(registered[i].handler==file_upload_handler||registered[i].handler==ota_firmware_upload_handler||registered[i].handler==ota_www_upload_handler)check(registered[i].max_body_bytes==16U*1024U*1024U&&registered[i].body_timeout_ms==300000,"file and OTA routes keep explicit large bounds");
 }
 const char *payload="{\"fixture\":\"kept\"}";
 for(unsigned w=0;w<2;w++){
  request(w,"/api/v1/ui/widgets/set?max_body_bytes=999999999","admin",TS_HTTP_POST,NULL,1048577,413,false);
  request(w,"/api/v1/auth.status",NULL,TS_HTTP_POST,NULL,16385,413,false);
  request(w,"/api/v1/auth.login",NULL,TS_HTTP_POST,NULL,16385,413,false);
  char *at_limit=malloc(1048577);assert(at_limit);memset(at_limit,' ',1048576);at_limit[0]='{';at_limit[1]='}';at_limit[1048576]=0;request(w,"/api/v1/ui/widgets/set","admin",TS_HTTP_POST,at_limit,1048576,200,true);free(at_limit);
  char *binary=malloc(1572864);assert(binary);for(size_t b=0;b<1572864;b++)binary[b]=(char)(b%251);
  for(unsigned i=0;i<registered_count;i++){
   if(registered[i].handler==login_handler)special_request(w,&registered[i],NULL,NULL,16385,false);
   if(registered[i].handler==file_upload_handler||registered[i].handler==ota_firmware_upload_handler||registered[i].handler==ota_www_upload_handler){special_request(w,&registered[i],"root",NULL,16U*1024U*1024U+1,false);special_request(w,&registered[i],"root",binary,1572864,true);}
  }
  for(unsigned i=0;i<registered_count;i++){
   ts_http_route_t *special=&registered[i];
   if(special->handler!=file_upload_handler&&special->handler!=ota_firmware_upload_handler&&special->handler!=ota_www_upload_handler)continue;
   for(unsigned downgrade=0;downgrade<2;downgrade++){
    reset();revoke_after_read=!downgrade;downgrade_after_read=downgrade;
    httpd_req_t late={.user_ctx=special,.uri=special->uri,.method=TS_HTTP_POST,.auth="root",.content_len=1572864};body_wire=binary;body_wire_len=1572864;
    int effect=w?https_handler_wrapper(&late):http_handler_wrapper(&late);
    check(effect==ESP_FAIL&&status==(downgrade?403:401)&&calls==0&&body_position==1572864,"dedicated file/APP/WWW route rechecks revocation or below-admin downgrade after full large receive, before any handler");
   }
   for(unsigned role=0;role<3;role++){
    const char *auth[]={NULL,"admin","read"};reset();httpd_req_t denied={.user_ctx=special,.uri=special->uri,.method=TS_HTTP_POST,.auth=auth[role],.content_len=1572864};body_wire=binary;body_wire_len=1572864;
    int effect=w?https_handler_wrapper(&denied):http_handler_wrapper(&denied);
    bool allowed=role==1&&special->required_level==TS_PERM_ADMIN;
    check(allowed?(effect==ESP_OK&&status==200&&calls==1&&body_position==1572864):(effect==ESP_FAIL&&status==(role?403:401)&&!receives&&!body_allocations&&!calls),"file Admin access stays usable; other wrong initial identities refused before body allocation");
   }
  }
  free(binary);

  request(w,"/api/v1/time/force_sync",NULL,TS_HTTP_POST,NULL,1048576,401,false);
  request(w,"/api/v1/time/force_sync","admin",TS_HTTP_POST,payload,strlen(payload),200,true);
  request(w,"/api/v1/log/clear","read",TS_HTTP_POST,NULL,1048576,403,false);
  request(w,"/api/v1/storage/list",NULL,TS_HTTP_GET,NULL,1048576,401,false);
  request(w,"/api/v1/time/force_sync","root",TS_HTTP_GET,NULL,1048576,405,false);
  request(w,"/api/v1/missing","root",TS_HTTP_POST,NULL,1048576,404,false);
  request(w,"/api/v1/fixture/internal","root",TS_HTTP_POST,NULL,1048576,405,false);
  request(w,"/api/v1/system/info",NULL,TS_HTTP_GET,NULL,0,200,true);
  request(w,"/api/v1/system/info?fixture=1",NULL,TS_HTTP_GET,NULL,0,200,true);
  request(w,"/api/v1/time/force_sync","root",TS_HTTP_POST,payload,strlen(payload),200,true);
  check(cJSON_IsString(cJSON_GetObjectItem(received_params,"fixture"))&&!strcmp(cJSON_GetObjectItem(received_params,"fixture")->valuestring,"kept"),"authorized JSON fields preserved");
  request(w,"/api/v1/log/clear","admin",TS_HTTP_POST,payload,strlen(payload),200,true);
  const char *token="{\"token\":\"body-token-fixture\"}";
  for(unsigned i=0;i<3;i++){const char *names[]={"/api/v1/auth/logout","/api/v1/auth/status","/api/v1/auth/change_password"};request(w,names[i],NULL,TS_HTTP_POST,token,strlen(token),200,true);check(cJSON_IsString(cJSON_GetObjectItem(received_params,"token")),"body-token endpoint forwarded to existing token validator");}
  reset();ts_http_route_t *route=NULL;for(unsigned i=0;i<registered_count;i++)if(registered[i].handler==api_handler&&registered[i].method==TS_HTTP_POST)route=&registered[i];assert(route);
  httpd_req_t req={.user_ctx=route,.uri="/api/v1/time/force_sync",.method=TS_HTTP_POST,.auth="root",.content_len=strlen(payload)};body_wire=payload;body_wire_len=strlen(payload);revoke_after_read=true;
  int result=w?https_handler_wrapper(&req):http_handler_wrapper(&req);check(result==ESP_FAIL&&status==401&&calls==0&&body_position==strlen(payload),"revocation during receive rechecked before business");
  reset();body_oom=true;result=w?https_handler_wrapper(&req):http_handler_wrapper(&req);check(result==ESP_FAIL&&status==500&&calls==0&&!receives,"authorized allocation failure has no body read/effect");
  char large[100001];memset(large,' ',sizeof(large)-1);large[0]='{';large[1]='}';large[sizeof(large)-1]=0;request(w,"/api/v1/ui/widgets/set","admin",TS_HTTP_POST,large,strlen(large),200,true);
 }
 reset();ts_http_request_t bad={.uri="/not-api/time/force_sync",.method=TS_HTTP_POST,.auth="root"};if(registered[6].admit){check(!registered[6].admit(&bad,NULL)&&status==404,"wrong API prefix cannot alias registered policy");}
 cJSON_Delete(received_params);free(response);fprintf(stdout,"%u checks, %u failures: actual API registration/policy owner/HTTP+HTTPS pre-body ordering; synthetic SDK auth and effects; no device\n",checks,failures);return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='ts-prebody-') as tmp:
 t=Path(tmp);(t/'test.c').write_text(code);idf=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'))
 subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(idf/'components/json/cJSON'),'-I'+str(ROOT/'components/ts_security/include'),*['-I'+str(p) for p in ns['url_native_includes']],str(t/'test.c'),str(idf/'components/json/cJSON/cJSON.c'),*map(str,ns['url_native_sources']),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True,timeout=30)
