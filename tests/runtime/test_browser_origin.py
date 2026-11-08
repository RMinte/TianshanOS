#!/usr/bin/env python3
"""Actual generated SDK dispatch/header reader, Host/Origin policy, HTTP/HTTPS
wrappers, body receiver, CORS and options handler. Synthetic socket/TLS/auth/IO.
No device, DNS lookup or external service. --probe-output builds a local browser bridge.
"""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile,json
ROOT=Path(__file__).resolve().parents[2];IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'))
p=argparse.ArgumentParser();p.add_argument('--source-package');p.add_argument('--probe-output');p.add_argument('--allow-origin',default='');args=p.parse_args()
def source(path):
 if args.source_package:
  with ZipFile(args.source_package) as z:
   if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
  q=subprocess.run(['git','show','35e081ac8:'+path],cwd=os.environ.get('SOURCE_REPOSITORY',str(ROOT)),capture_output=True,text=True);return q.stdout if q.returncode==0 else None
 return (ROOT/path).read_text() if (ROOT/path).exists() else None
def function(text,name):
 m=re.search(r'^(?:static )?[^\n;]+\b'+name+r'\([^;]+?\)\s*\{',text,re.M);assert m,name
 return text[m.start():text.index('\n}',m.start())+2]+'\n'
policy=source('components/ts_net/src/ts_http_origin.c')
code=r"""
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#define CONFIG_HTTPD_WS_SUPPORT 1
#define CONFIG_HTTPD_MAX_URI_LEN 256
#define CONFIG_TS_WEBUI_AUTH_REQUIRED 1
#define CONFIG_TS_WEBUI_CORS_ENABLE 1
#define CONFIG_LWIP_LOCAL_HOSTNAME "rm01"
#define TS_HTTP_ORIGIN_MAX_BYTES 288
#define TS_NET_HOSTNAME_MAX_LEN 32
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_ALLOWED 0x106
#define ESP_ERR_HTTPD_RESULT_TRUNC 0x7007
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTP_OPTIONS 2
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_403_FORBIDDEN 403
#define HTTPD_404_NOT_FOUND 404
#define HTTPD_405_METHOD_NOT_ALLOWED 405
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_SOCK_ERR_TIMEOUT -2
#define UF_PATH 0
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define LOG_FMT(x) x
static bool s_stopping;
typedef int esp_err_t,httpd_err_code_t;
static void ts_httpd_set_body_receive_budget(uint32_t milliseconds){(void)milliseconds;}
typedef struct httpd_req httpd_req_t;
typedef esp_err_t (*sdk_handler_t)(httpd_req_t *);
struct sock_db {int fd;bool ws_handshake_done,ws_control_frames;sdk_handler_t ws_handler;void *ws_user_ctx;};
struct http_parser_url {unsigned field_set;struct {size_t off,len;} field_data[1];};
struct httpd_req_aux {char *scratch;size_t scratch_cur_size;unsigned req_hdrs_count;struct http_parser_url url_parse_res;bool ws_handshake_detect;struct sock_db *sd;};
struct httpd_req {void *handle,*user_ctx,*aux;const char *uri;int method;size_t content_len;};
struct httpd_data {httpd_req_t hd_req;struct httpd_req_aux hd_req_aux;};
typedef struct {bool is_websocket,handle_ws_control_frames;int method;void *user_ctx;sdk_handler_t handler;const char *supported_subprotocol;} httpd_uri_t;
typedef struct {httpd_req_t *req;const char *uri;int method;char *body;size_t body_len;char cors_origin[TS_HTTP_ORIGIN_MAX_BYTES];} ts_http_request_t;
typedef struct {esp_err_t (*handler)(ts_http_request_t *,void *);void *user_data;bool requires_auth;unsigned required_level;bool (*admit)(ts_http_request_t*,void*);size_t max_body_bytes;uint32_t body_timeout_ms;} ts_http_route_t;
static unsigned checks,failures,lookups,upgrades,peers,businesses,body_reads,allocations,auth_checks;
static int status;static bool tls,socket_error,authorized;
static struct sockaddr_storage socket_address;
static const char *acao,*vary,*acam,*acah;static char captured_acao[288],captured_vary[32],captured_acam[96],captured_acah[96];
static const char *redirect_location;static char saved_redirect[1400];static bool redirect_no_store,redirect_no_referrer;
static char raw_headers[2048];static unsigned raw_count;static size_t raw_used;
static void check(bool b,const char *why){++checks;if(!b){++failures;fprintf(stderr,"FAIL %s\n",why);}}
static bool httpd_valid_req(httpd_req_t *r){return r&&r->aux;}
static void *httpd_get_global_transport_ctx(void *handle){(void)handle;return tls?(void*)1:NULL;}
static int httpd_req_to_sockfd(httpd_req_t *r){(void)r;return 17;}
static int test_getsockname(int fd,struct sockaddr *out,socklen_t *len){assert(fd==17);if(socket_error)return -1;memcpy(out,&socket_address,sizeof(socket_address));*len=sizeof(socket_address);return 0;}
#define getsockname test_getsockname
static esp_err_t ts_net_copy_hostname(char *out,size_t n){snprintf(out,n,"ControlUnit");return ESP_OK;}
static esp_err_t ts_net_manager_copy_hostname(char *out,size_t n){snprintf(out,n,"rack-node");return ESP_OK;}
static void capture(void){if(redirect_location)snprintf(saved_redirect,sizeof(saved_redirect),"%s",redirect_location);if(acao)snprintf(captured_acao,sizeof(captured_acao),"%s",acao);if(vary)snprintf(captured_vary,sizeof(captured_vary),"%s",vary);if(acam)snprintf(captured_acam,sizeof(captured_acam),"%s",acam);if(acah)snprintf(captured_acah,sizeof(captured_acah),"%s",acah);}
static int httpd_resp_set_hdr(httpd_req_t *r,const char *name,const char *value){(void)r;if(!strcmp(name,"Location"))redirect_location=value;if(!strcmp(name,"Cache-Control")&&!strcmp(value,"no-store"))redirect_no_store=true;if(!strcmp(name,"Referrer-Policy")&&!strcmp(value,"no-referrer"))redirect_no_referrer=true;if(!strcmp(name,"Access-Control-Allow-Origin"))acao=value;if(!strcmp(name,"Vary"))vary=value;if(!strcmp(name,"Access-Control-Allow-Methods"))acam=value;if(!strcmp(name,"Access-Control-Allow-Headers"))acah=value;return ESP_OK;}
static int httpd_resp_set_status(httpd_req_t *r,const char *text){(void)r;status=atoi(text);return ESP_OK;}
static int httpd_resp_sendstr(httpd_req_t *r,const char *text){(void)r;(void)text;if(!status)status=200;capture();return ESP_OK;}
static int httpd_resp_send_err(httpd_req_t *r,int code,const char *text){(void)r;(void)text;status=code;capture();return ESP_OK;}
static int httpd_req_handle_err(httpd_req_t *r,int code){return httpd_resp_send_err(r,code,"fixture");}
static void *allocate(size_t n){++allocations;return malloc(n);}
#define TS_MALLOC_PSRAM allocate
static int httpd_req_recv(httpd_req_t *r,char *out,size_t n){(void)r;memset(out,'x',n);++body_reads;return (int)n;}
static bool ts_http_authorize_route(ts_http_request_t *r,const ts_http_route_t *route){++auth_checks;if(!route->requires_auth||authorized)return true;httpd_resp_send_err(r->req,401,"Unauthorized");return false;}
static esp_err_t ts_http_send_error(ts_http_request_t *r,int code,const char *why){return httpd_resp_send_err(r->req,code,why);}
static esp_err_t ts_http_send_response(ts_http_request_t *r,int code,const char *type,const char *body){(void)type;(void)body;status=code;capture();return ESP_OK;}
"""
with tempfile.TemporaryDirectory(prefix='ts-origin-sdk-source-') as tmp:
 namespace={'__name__':'origin_sdk_generator'};exec(compile(source('tools/prepare_httpd_admission.py'),'prepare_httpd_admission.py','exec'),namespace);namespace['prepare'](IDF,Path(tmp))
 sdk_uri=(Path(tmp)/'httpd_uri.c').read_text() if (Path(tmp)/'httpd_uri.c').exists() else (IDF/'components/esp_http_server/src/httpd_uri.c').read_text()
 sdk_parse=(Path(tmp)/'httpd_parse.c').read_text() if (Path(tmp)/'httpd_parse.c').exists() else None
 if policy:
  code+=function(sdk_parse,'ts_httpd_get_single_header')
  code+=re.sub(r'^#include[^\n]*\n','',policy,flags=re.M)+'\n'
http=source('components/ts_net/src/ts_http_server.c')
for name in ['ts_http_set_cors','ts_http_admit_body','ts_http_receive_body','http_handler_wrapper']:code+=function(http,name)
code+=function(source('components/ts_net/src/ts_https_server.c'),'https_handler_wrapper')
code+=function(source('components/ts_webui/src/ts_webui_api.c'),'options_handler')
code+=r"""
static esp_err_t business(ts_http_request_t *r,void *ctx){(void)ctx;++businesses;assert(r->body_len==r->req->content_len);if(r->body_len)assert(r->body[r->body_len]==0);ts_http_set_cors(r,"*");return httpd_resp_sendstr(r->req,"{\"ok\":true}");}
static esp_err_t peer(httpd_req_t *r){(void)r;++peers;return ESP_OK;}
static esp_err_t httpd_ws_respond_server_handshake(httpd_req_t *r,const char *protocol){(void)r;(void)protocol;++upgrades;status=101;return ESP_OK;}
static httpd_uri_t selected;
static httpd_uri_t *httpd_find_uri_handler(struct httpd_data *h,const char *u,size_t n,int method,httpd_err_code_t *error){(void)h;(void)u;(void)n;(void)method;(void)error;++lookups;return &selected;}
"""+function(sdk_uri,'httpd_uri')
code+='#define HAS_POLICY '+str(int(bool(policy)))+'\n'
code+=r"""
static void header(const char *name,const char *value){if(!value)return;size_t n=strlen(name)+strlen(value)+3;assert(raw_used+n<sizeof(raw_headers));int got=snprintf(raw_headers+raw_used,sizeof(raw_headers)-raw_used,"%s: %s",name,value);raw_used+=(size_t)got+1;raw_count++;}
static void reset(bool secure,const char *local_ip,unsigned port){
 redirect_location=NULL;saved_redirect[0]=0;redirect_no_store=redirect_no_referrer=false;tls=secure;socket_error=false;authorized=true;s_stopping=false;status=0;lookups=upgrades=peers=businesses=body_reads=allocations=auth_checks=0;acao=vary=acam=acah=NULL;captured_acao[0]=captured_vary[0]=captured_acam[0]=captured_acah[0]=0;raw_used=raw_count=0;memset(raw_headers,0,sizeof(raw_headers));memset(&socket_address,0,sizeof(socket_address));
 if(strchr(local_ip,':')){struct sockaddr_in6 *a=(void*)&socket_address;a->sin6_family=AF_INET6;a->sin6_port=htons(port);assert(inet_pton(AF_INET6,local_ip,&a->sin6_addr)==1);}
 else {struct sockaddr_in *a=(void*)&socket_address;a->sin_family=AF_INET;a->sin_port=htons(port);assert(inet_pton(AF_INET,local_ip,&a->sin_addr)==1);}
}
static int dispatch(const char *host,const char *origin,unsigned kind,bool duplicate_host,bool duplicate_origin,bool needs_auth){
 header("Host",host);header("Origin",origin);header("X-Unrelated","metadata");if(duplicate_host)header("hOsT",host);if(duplicate_origin)header("ORIGIN",origin);
 struct sock_db sd={.fd=17};struct httpd_data hd={0};hd.hd_req_aux=(struct httpd_req_aux){.scratch=raw_headers,.scratch_cur_size=raw_used,.req_hdrs_count=raw_count,.url_parse_res={.field_set=1,.field_data={{0,8}}},.ws_handshake_detect=kind==2,.sd=&sd};
 hd.hd_req=(httpd_req_t){.handle=&hd,.aux=&hd.hd_req_aux,.uri="/fixture",.method=(kind==2||kind==4)?HTTP_GET:kind==3?HTTP_OPTIONS:HTTP_POST,.content_len=(kind==2||kind==3||kind==4)?0:7};
 ts_http_route_t route={.handler=kind==3?options_handler:business,.requires_auth=needs_auth};selected=(httpd_uri_t){.is_websocket=(kind==2||kind==4),.method=hd.hd_req.method,.user_ctx=&route,.handler=(kind==2||kind==4)?peer:kind==1?https_handler_wrapper:http_handler_wrapper};
 return httpd_uri(&hd);
}
static void request(const char *host,const char *origin,bool allowed,unsigned kind){
 reset(kind==1,"192.168.1.4",kind==1?443:80);int ret=dispatch(host,origin,kind,false,false,false);
 if(allowed){check(ret==ESP_OK,"known device/explicit authority and admitted origin remain usable");check(lookups==1,"admitted request enters actual SDK route lookup");if(kind==2)check(upgrades==1&&peers==1&&!businesses,"allowed WS upgrades before its peer handler");else if(kind==3)check(status==204&&!businesses&&!body_reads&&!allocations,"valid preflight is metadata only");else check(businesses==1&&auth_checks==2&&body_reads==1&&allocations==1,"HTTP/HTTPS executes one business after route auth before and after the single body read");
  if(kind!=2)check(origin?!strcmp(captured_acao,origin)&&!strcmp(captured_vary,"Origin"):!captured_acao[0],"CORS echoes only verified original origin; CLI has no wildcard");}
 else {check(ret!=ESP_OK&&status>=400,"untrusted/malformed request is rejected");check(!lookups&&!upgrades&&!peers&&!businesses&&!auth_checks&&!body_reads&&!allocations,"rejected before route lookup, WS101, auth/body allocation or any business");check(!captured_acao[0],"denied origins get no ACAO");}
}
#if HAS_POLICY
static void strict_headers(void){
 reset(false,"192.168.1.4",80);header("Host","\t rm01.local\t ");header("Origin","");
 struct httpd_req_aux aux={.scratch=raw_headers,.scratch_cur_size=raw_used,.req_hdrs_count=raw_count};httpd_req_t req={.aux=&aux};char out[32];
 check(ts_httpd_get_single_header(&req,"HOST",out,sizeof(out))==ESP_OK&&!strcmp(out,"rm01.local"),"strict SDK reader trims only surrounding OWS");
 check(ts_httpd_get_single_header(&req,"origin",out,sizeof(out))==ESP_OK&&!out[0],"empty Origin remains present rather than CLI absence");
 check(ts_httpd_get_single_header(&req,"Missing",out,sizeof(out))==ESP_ERR_NOT_FOUND,"absent header is distinguishable");
 check(ts_httpd_get_single_header(&req,"Host",out,1)==ESP_ERR_HTTPD_RESULT_TRUNC&&!out[0],"short output never returns a truncated identity");
 header("hOsT","rm01.local");aux.scratch_cur_size=raw_used;aux.req_hdrs_count=raw_count;
 check(ts_httpd_get_single_header(&req,"Host",out,sizeof(out))==ESP_ERR_INVALID_ARG,"identical mixed-case duplicate remains ambiguous");
 aux.req_hdrs_count=1;aux.scratch_cur_size=5;check(ts_httpd_get_single_header(&req,"Host",out,sizeof(out))==ESP_ERR_INVALID_ARG,"unterminated SDK scratch is refused within allocation bounds");
 aux.scratch_cur_size=0;check(ts_httpd_get_single_header(&req,"Host",out,sizeof(out))==ESP_ERR_INVALID_ARG,"empty scratch cannot claim parsed header");
 aux.scratch=NULL;check(ts_httpd_get_single_header(&req,"Host",out,sizeof(out))==ESP_ERR_INVALID_ARG,"missing SDK scratch fails closed");
 aux.req_hdrs_count=0;check(ts_httpd_get_single_header(&req,"Origin",out,sizeof(out))==ESP_ERR_NOT_FOUND,"zero parsed headers is ordinary absence");
 check(ts_httpd_get_single_header(NULL,"Host",out,sizeof(out))==ESP_ERR_INVALID_ARG,"invalid SDK request fails closed");
 check(ts_httpd_get_single_header(&req,NULL,out,sizeof(out))==ESP_ERR_INVALID_ARG,"invalid header argument fails closed");
}
#endif
static void ordinary_ws_get(void){
 for(unsigned tls=0;tls<2;tls++)for(unsigned attempt=0;attempt<32;attempt++){
  reset(tls,"192.168.1.4",tls?443:80);
  int ret=dispatch(tls?"192.168.1.4:443":"192.168.1.4",NULL,4,false,false,false);
  check(ret!=ESP_OK&&status==400,"ordinary GET to WS-only route closes with400");
  check(lookups==1&&!upgrades&&!peers&&!businesses&&!auth_checks&&!body_reads&&!allocations,"missing Upgrade never reaches application peer owner or body allocation");
 }
}

static int unit(void){
 if(CONFIG_CASE==0)for(unsigned kind=0;kind<4;kind++){request("public.example","https://public.example",true,kind);request("public.example:8443","https://public.example:8443",true,kind);request("public.example","https://foreign.example",false,kind);}
 const char *known_hosts[]={"192.168.1.4","192.168.1.4:80","rm01","RM01.LOCAL","controlunit.local","rack-node.local"};
 const char *origins[]={NULL,"http://192.168.1.4","https://192.168.1.4","http://192.168.1.4:8080","http://evil.test","null","","http://192.168.1.4/","http://user@192.168.1.4","http://192.168.1.4 http://evil.test","http://192.168.1.4:0","http://192.168.1.4:65536"};
 if(CONFIG_CASE==2){for(unsigned kind=0;kind<4;kind++)request("192.168.1.4",NULL,false,kind);goto done;}
 if(CONFIG_CASE==1){
  for(unsigned kind=0;kind<4;kind++){request(kind==1?"192.168.1.4:443":"192.168.1.4","https://dashboard.example",true,kind);request("proxy.example:9000","http://proxy.example:9000",true,kind);request("dashboard.example","https://dashboard.example",true,kind);request("dashboard.example",NULL,true,kind);request("unknown.example","https://dashboard.example",true,kind);request("proxy.example:9001","http://proxy.example:9000",true,kind);}goto done;
 }
 for(unsigned kind=0;kind<4;kind++){
  for(unsigned h=0;h<sizeof(known_hosts)/sizeof(known_hosts[0]);h++)for(unsigned o=0;o<sizeof(origins)/sizeof(origins[0]);o++){
   bool allowed=(o==0);
   if(h<2&&o==1)allowed=true;
   if(h==0&&o==2)allowed=true;
   request(known_hosts[h],origins[o],allowed,kind);
  }
  const char *bad_hosts[]={NULL,"","192.168.1.4:0","192.168.1.4:65536","user@rm01.local","rm01.local/path","rm01.local,evil.test","rm01%2elocal","rm01..local","[::1","2001:db8::1","192.168.1.4:garbage"};
  for(unsigned h=0;h<sizeof(bad_hosts)/sizeof(bad_hosts[0]);h++)request(bad_hosts[h],NULL,false,kind);
  for(unsigned d=0;d<2;d++){
   reset(kind==1,"192.168.1.4",kind==1?443:80);int rc=dispatch("192.168.1.4",kind==1?"https://192.168.1.4":"http://192.168.1.4",kind,d==0,d==1,false);check(rc!=ESP_OK&&status==400&&!lookups&&!upgrades&&!businesses,"duplicate Host/Origin never select the first identity");
  }
 }
 request("rm01.local","http://rm01.local",true,0);request("rm01.local.","http://rm01.local.",true,0);request("rm01.local.","http://rm01.local",false,0);
 for(unsigned kind=0;kind<4;kind++){
  reset(kind==1,"2001:db8::1",kind==1?443:80);int rc=dispatch("[2001:db8::1]",kind==1?"https://[2001:db8:0::1]":"http://[2001:db8:0::1]",kind,false,false,false);check(rc==ESP_OK,"canonical IPv6 device authority is retained");
  reset(kind==1,"::ffff:192.168.1.4",kind==1?443:80);rc=dispatch("192.168.1.4",kind==1?"https://192.168.1.4":"http://192.168.1.4",kind,false,false,false);check(rc==ESP_OK,"IPv4 authority retained on mapped IPv6 socket");
  reset(false,"192.168.1.4",8080);rc=dispatch("192.168.1.4:8080","http://192.168.1.4:8080",kind==2?2:0,false,false,false);check(rc==ESP_OK,"non-default actual listener port works");
 }
 reset(false,"192.168.1.4",80);socket_error=true;check(dispatch("192.168.1.4",NULL,0,false,false,false)==ESP_OK&&lookups,"proxy authority is independent of backend address");
 reset(false,"192.168.1.4",80);authorized=false;check(dispatch("192.168.1.4","http://192.168.1.4",0,false,false,true)!=ESP_OK&&status==401&&auth_checks==1&&!body_reads&&!allocations&&!businesses,"allowed origin never bypasses actual route authentication boundary");
 reset(false,"192.168.1.4",80);header("Unrelated","x");
 done:
#if HAS_POLICY
 if(CONFIG_CASE==0){strict_headers();}
 if(CONFIG_CASE!=2)ordinary_ws_get();
#endif
 printf("%u checks, %u failures: actual SDK URI dispatch/header reader, Host/Origin/CORS and HTTP/HTTPS/OPTIONS/WS ordering; synthetic socket/TLS/auth/IO\n",checks,failures);return failures?1:0;
}
int main(int argc,char **argv){
 if(argc==1)return unit();
 assert(argc==10&&!strcmp(argv[1],"--probe"));unsigned kind=(unsigned)atoi(argv[7]);reset(!strcmp(argv[2],"https"),argv[3],(unsigned)atoi(argv[4]));authorized=atoi(argv[9])!=0;
 int ret=dispatch(strcmp(argv[5],"ABSENT")?argv[5]:NULL,strcmp(argv[6],"ABSENT")?argv[6]:NULL,kind,false,false,atoi(argv[8])!=0);
 printf("{\"ret\":%d,\"status\":%d,\"lookup\":%u,\"upgrade\":%u,\"business\":%u,\"auth\":%u,\"reads\":%u,\"allocations\":%u,\"acao\":\"%s\",\"vary\":\"%s\",\"acam\":\"%s\",\"acah\":\"%s\"}\n",ret,status,lookups,upgrades,businesses,auth_checks,body_reads,allocations,captured_acao,captured_vary,captured_acam,captured_acah);return 0;
}
"""
def configuration_checks():
 namespace={'__name__':'origin_config_fixture'};exec(compile(source('tools/prepare_httpd_admission.py'),'prepare_httpd_admission.py','exec'),namespace)
 valid=['','http://rm01.local','https://dashboard.example,http://proxy.example:9000','http://[2001:db8::1]:8080','http://node_1.local.']
 invalid=['*','null','http://user:secret@example','https://example/','https://example?','https://example#','https://example:0','https://example:65536','https://example:','https://example,,http://other','http://bad..name','http://[fe80::1%25eth0]','http://0177.0.0.1','http://a,http://b,http://c,http://d,http://e']
 for value in valid:namespace['validate_origins'](value)
 for value in invalid:
  try:namespace['validate_origins'](value)
  except ValueError:pass
  else:raise AssertionError('invalid origin configuration accepted')
 with tempfile.TemporaryDirectory(prefix='ts-origin-sdk-drift-') as tmp:
  src=Path(tmp)/'components/esp_http_server/src';src.mkdir(parents=True)
  for name in ['httpd_sess.c','httpd_main.c','httpd_uri.c','httpd_parse.c']:(src/name).write_bytes((IDF/'components/esp_http_server/src'/name).read_bytes())
  f=src/'httpd_uri.c';f.write_text(f.read_text().replace('    httpd_req_t            *req = &hd->hd_req;','    /* changed request ownership */'))
  try:namespace['prepare'](Path(tmp),Path(tmp)/'output')
  except ValueError:pass
  else:raise AssertionError('SDK URI source drift silently omitted admission')
 print('PASS 19 build-origin configuration cases; actual SDK dispatch anchor drift fails closed; installed SDK unchanged')
def build(path,configuration,case):
 c=Path(str(path)+'.c');c.write_text(code.replace('CONFIG_CASE',str(case)))
 subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize='+os.environ.get('ORIGIN_TEST_SANITIZER','address,undefined'),'-fno-sanitize-recover=all','-DTS_HTTP_TRUSTED_ORIGINS='+json.dumps(configuration),str(c),'-o',str(path)],check=True)
if args.probe_output:
 build(Path(args.probe_output),args.allow_origin,0);print(args.probe_output)
else:
 with tempfile.TemporaryDirectory(prefix='ts-browser-origin-') as tmp:
  for case,conf in [(0,''),(1,'https://dashboard.example,http://proxy.example:9000'),(2,'*')]:
   exe=Path(tmp)/str(case);build(exe,conf,case);subprocess.run([str(exe)],check=True,timeout=20)
  if policy:
   configuration_checks()
