#!/usr/bin/env python3
"""Actual SDK callbacks -> actual shared log hook -> synthetic Root token replay.

SDK DEBUG/purge logging is deliberately enabled in the fixture; it is not the
normal product log threshold. Socket/TLS and Admin HTTP retrieval are not run.
Other sink statements are replayed without claiming full protocol execution.
"""
from pathlib import Path
import argparse, os, re, subprocess, tempfile
from zipfile import ZipFile
ROOT = Path(__file__).resolve().parents[2]
IDF = Path(os.environ.get('IDF_PATH', '/Users/massif/esp/v5.5.2/esp-idf'))
p = argparse.ArgumentParser(); p.add_argument('--source-package'); args = p.parse_args()
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if 'current-source/'+path in z.namelist():
                return z.read('current-source/'+path).decode()
    return (ROOT/path).read_text()
def function(text, name):
    m = re.search(r'^(?:static )?[\w *]+\b'+name+r'\([^;]*?\)\s*\{', text, re.M)
    assert m, name
    return text[m.start():text.index('\n}',m.end())+2]+'\n'
with tempfile.TemporaryDirectory(prefix='ts-sdk-private-') as tmp:
    t = Path(tmp); tool = t/'prepare.py'; tool.write_text(source('tools/prepare_httpd_admission.py'))
    subprocess.run(['python3',str(tool),'--idf-dir',str(IDF),'--output',str(t/'sdk')],check=True)
    sdk = {n:(t/'sdk'/('httpd_'+n+'.c')).read_text() if (t/'sdk'/('httpd_'+n+'.c')).exists()
           else (IDF/'components/esp_http_server/src'/('httpd_'+n+'.c')).read_text()
           for n in ('parse','uri','txrx','ws')}
    log = (ROOT/'tests/runtime/test_log.c').read_text()
    code = log[:log.index('int main(void)')]
    names = ['log_output_buffer','parse_esp_log','ts_log_vprintf_hook','get_effective_level','notify_callbacks','log_output_console','ts_log_v','ts_log_enable_esp_capture']
    (t/'log_functions.inc').write_text('\n'.join(function(source('components/ts_core/ts_log/src/ts_log.c'),n) for n in names))
    code += r"""
#include "ts_security.h"
#include "ts_crypto.h"
#include "mbedtls/base64.h"
typedef pthread_mutex_t StaticSemaphore_t;
typedef pthread_mutex_t *SemaphoreHandle_t;
static int xSemaphoreTakeRecursive(SemaphoreHandle_t p,unsigned timeout){return xSemaphoreTake(p,timeout);}
static void xSemaphoreGiveRecursive(SemaphoreHandle_t p){xSemaphoreGive(p);}
#define MAX_SESSIONS 8
#define TAG "sdk"
#define TS_LOGI(...) ((void)0)
#define TS_LOGW(...) ((void)0)
static uint32_t random_state=123;
static void esp_fill_random(void *p,size_t n){for(size_t i=0;i<n;i++){random_state=random_state*1664525+1013904223;((uint8_t*)p)[i]=random_state>>24;}}
typedef int nvs_handle_t;
"""
    security = source('components/ts_security/src/ts_security.c')
    code += security[security.index('typedef struct {'):security.index('esp_err_t ts_security_init(')]
    code += function(security,'ts_security_random')
    code += security[security.index('static void ts_security_destroy_sessions_by_client_locked'):]
    crypto = source('components/ts_security/src/ts_crypto.c')
    code += function(crypto,'ts_crypto_base64_encode')+function(crypto,'ts_crypto_base64_decode')
    code += r"""
static unsigned checks,failures;
static void check(bool ok,const char *label){++checks;if(!ok){++failures;fprintf(stderr,"FAIL %s\n",label);}}
static void sdk_log(const char *tag,const char *format,...){char text[512];va_list a;va_start(a,format);vsnprintf(text,sizeof(text),format,a);va_end(a);emit("D (1) %s: %s\n",tag,text);}
#define ESP_LOGD sdk_log
#define ESP_LOGW sdk_log
#define ESP_LOGE sdk_log
#define LOG_FMT(x) x
#define ESP_LOG_BUFFER_HEX_LEVEL(tag,data,n,level) dump_hex(tag,data,n)
static void dump_hex(const char *tag,const void *data,size_t n){char out[256];size_t at=0;for(size_t i=0;i<n&&at+4<sizeof(out);i++)at+=snprintf(out+at,sizeof(out)-at,"%02x ",((const uint8_t*)data)[i]);sdk_log(tag,"%s",out);}
#define ESP_ERR_HTTPD_RESP_HDR 0x2000
#define ESP_ERR_HTTPD_INVALID_REQ 0x2001
#define NEWLIB_NANO_COMPAT_FORMAT "zu"
#define NEWLIB_NANO_COMPAT_CAST(x) ((size_t)(x))
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_414_URI_TOO_LONG 414
enum{PARSING_IDLE,PARSING_URL,PARSING_HDR_FIELD,PARSING_HDR_VALUE,PARSING_FAILED};
struct httpd_req_aux {size_t max_uri_len;unsigned resp_hdrs_count;struct {const char *field,*value;} resp_hdrs[4];};
struct httpd_data {struct {unsigned max_resp_headers;} config;};
typedef struct httpd_req {struct httpd_req_aux *aux;struct httpd_data *handle;char uri[256];int method;} httpd_req_t;
typedef struct{void *data;} http_parser;
typedef struct{int status,error;struct{const char *at;size_t length;}last;httpd_req_t *req;} parser_data_t;
static bool httpd_valid_req(httpd_req_t *r){return r&&r->aux&&r->handle;}
"""
    for file,name in [('parse','cb_header_value'),('parse','cb_url'),('txrx','httpd_resp_set_hdr'),('ws','httpd_ws_get_response_subprotocol')]:code += function(sdk[file],name)
    # Exercise each additional diagnostic statement that references private data.
    # These are exact generated SDK statements, not hand-written sanitizers.
    private = re.compile(r'\b(?:r->uri|req->uri|at|ws_upgrade_hdr_val|key|version_val|server_raw_text|server_key_encoded|subprotocol|supported_subprotocol|msg|dummy)\b')
    statements=[]
    for name,text in sdk.items():
        for m in re.finditer(r'ESP_LOG(?:D|W|E|BUFFER_HEX_LEVEL)\s*\([\s\S]*?\);',text):
            statement=m.group()
            if private.search(re.sub(r'"(?:\\.|[^"\\])*"', '""', statement)) and not ('NEWLIB_NANO_COMPAT' in statement):statements.append((name,statement))
    code += r"""
static void additional_sink_statements(const char *secret){
 struct httpd_req req_value={0},*req=&req_value,*r=req;strcpy(req->uri,secret);req->method=1;
 const char *at=secret,*ws_upgrade_hdr_val=secret,*key=secret,*version_val=secret,*server_raw_text=secret,*server_key_encoded=secret,*supported_subprotocol=secret,*msg=secret;
 char subprotocol[128];strcpy(subprotocol,secret);const void *dummy=secret;size_t length=strlen(secret);int recv_len=(int)length;const char *status="400 Bad Request";
"""
    # Actual generic-API request diagnostic: the URI can contain credentials
    # even when the SDK itself has stopped printing request bytes.
    web = source('components/ts_webui/src/ts_webui_api.c')
    request_log = re.search(r'TS_LOGD\(TAG, "API request:[\s\S]*?\);', web)
    assert request_log
    code += 'const char *uri=secret,*api_name=secret;\n#define TS_LOGD sdk_log\n'
    code += '\n'.join(statement for _,statement in statements)+'\n'+request_log.group()+'\n}\n'

    code += r"""
static ts_log_entry_t entries[128];static pthread_mutex_t ring=PTHREAD_MUTEX_INITIALIZER;
static void reset_logs(void){memset(entries,0,sizeof(entries));s_log_ctx.buffer.head=s_log_ctx.buffer.count=0;console_size=0;console_output[0]=0;}
static int capture_console(const char *fmt,va_list a){int n=vsnprintf(console_output+console_size,sizeof(console_output)-console_size,fmt,a);assert(n>=0&&(size_t)n<sizeof(console_output)-console_size);console_size+=n;return n;}
static bool leaked(const char *text){if(strstr(console_output,text))return true;for(size_t i=0;i<s_log_ctx.buffer.count;i++)if(strstr(entries[i].message,text))return true;return false;}
static bool replay(const char *candidate){uint32_t sid=0;return ts_security_validate_token(candidate,&sid)==ESP_OK&&ts_security_check_permission(sid,TS_PERM_ROOT);}
static bool recover_from_ring(void){
 char joined[2048]={0};size_t used=0;
 for(size_t i=0;i<s_log_ctx.buffer.count;i++){
  const char *message=entries[i].message;
  for(size_t j=0;j+24<=strlen(message);j++){char candidate[25];memcpy(candidate,message+j,24);candidate[24]=0;if(replay(candidate))return true;}
  const char *value=strstr(message,"processing value = ");if(value&&used<sizeof(joined)-1){value+=strlen("processing value = ");size_t n=strlen(value);assert(used+n<sizeof(joined));memcpy(joined+used,value,n);used+=n;joined[used]=0;}
 }
 for(size_t i=0;i+24<=used;i++){char candidate[25];memcpy(candidate,joined+i,24);candidate[24]=0;if(replay(candidate))return true;}
 return false;
}
int main(void){
 pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(&s_session_gate_storage,&attr);pthread_mutexattr_destroy(&attr);s_session_gate=&s_session_gate_storage;
 s_log_ctx.initialized=true;s_log_ctx.esp_log_capture_enabled=true;s_log_ctx.original_vprintf=capture_console;s_log_ctx.mutex=&ring;s_log_ctx.buffer.entries=entries;s_log_ctx.buffer.capacity=128;
 uint32_t sid=0;char token[64];assert(ts_security_create_session("root",TS_PERM_ROOT,&sid)==ESP_OK);assert(ts_security_generate_token(sid,token,sizeof(token))==ESP_OK&&strlen(token)==24);
 char value[128];snprintf(value,sizeof(value),"Bearer %s",token);char unchanged[128];strcpy(unchanged,value);
 parser_data_t data={.status=PARSING_HDR_FIELD};http_parser parser={.data=&data};
 check(cb_header_value(&parser,value,strlen(value))==ESP_OK&&data.last.at==value&&data.last.length==strlen(value),"SDK header parsing unchanged");
 check(!recover_from_ring(),"unfragmented captured SDK header cannot replay Root token");check(!leaked(token),"SDK header token absent from console and shared ring");check(!strcmp(value,unchanged)&&replay(token),"original header and issued Root token remain usable");
 for(unsigned split=1;split<strlen(value);++split){reset_logs();data=(parser_data_t){.status=PARSING_HDR_FIELD};check(cb_header_value(&parser,value,split)==ESP_OK&&cb_header_value(&parser,value+split,strlen(value)-split)==ESP_OK&&data.last.length==strlen(value),"split callback preserves header length");check(!recover_from_ring(),"captured split header cannot reconstruct Root credential");}
 reset_logs();char cookie[]="session=synthetic-cookie-secret";data=(parser_data_t){.status=PARSING_HDR_FIELD};check(cb_header_value(&parser,cookie,strlen(cookie))==ESP_OK&&!leaked("synthetic-cookie-secret"),"Cookie value absent from diagnostic sinks");
 reset_logs();struct httpd_req_aux aux={.max_uri_len=255};struct httpd_data hd={.config.max_resp_headers=4};httpd_req_t req={.aux=&aux,.handle=&hd};data=(parser_data_t){.status=PARSING_IDLE,.req=&req};
 check(cb_url(&parser,value,strlen(value))==ESP_OK&&data.last.length==strlen(value)&&!leaked(token),"SDK URL callback keeps bytes out of logs");
 reset_logs();check(httpd_resp_set_hdr(&req,"Set-Cookie",value)==ESP_OK&&aux.resp_hdrs_count==1&&aux.resp_hdrs[0].value==value&&!leaked(token),"response header retains exact bytes, omits diagnostic value");
 check(httpd_resp_set_hdr(&req,NULL,value)==ESP_ERR_INVALID_ARG,"bad response header remains rejected");aux.resp_hdrs_count=4;check(httpd_resp_set_hdr(&req,"Set-Cookie",value)==ESP_ERR_HTTPD_RESP_HDR,"response header limit retained");
 reset_logs();char offered[128];strcpy(offered,token);check(!httpd_ws_get_response_subprotocol(NULL,offered,sizeof(offered))&&!leaked(token),"unsupported WS protocol warning omits peer bytes");
 strcpy(offered,"normal, fallback");check(httpd_ws_get_response_subprotocol("normal",offered,sizeof(offered)),"normal WS subprotocol negotiation retained");
 reset_logs();additional_sink_statements(token);check(!leaked(token),"all additional private SDK sink statements omit contents");check(!recover_from_ring(),"additional SDK diagnostics cannot replay Root credential");
 char hex[128];size_t pos=0;for(size_t i=0;i<strlen(token);i++)pos+=snprintf(hex+pos,sizeof(hex)-pos,"%02x ",(unsigned char)token[i]);check(!leaked(hex),"purged body hex not captured even when purge logging compiled in");
 check(replay(token),"logging does not revoke legitimate token");assert(ts_security_destroy_session(sid)==ESP_OK);check(!replay(token),"real revocation still rejects credential");
 fprintf(stdout,"SDK diagnostic privacy: %u checks, %u failures; actual callbacks/log hook/Root verifier; synthetic credentials, forced DEBUG and purge; no device/TLS\n",checks,failures);return failures?1:0;
}
"""
    c=t/'test.c';c.write_text(code);mbed=IDF/'components/mbedtls/mbedtls'
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize='+os.environ.get('SDK_PRIVACY_SANITIZER','address,undefined'),'-fno-sanitize-recover=all',
        *['-I'+str(path) for path in [t,ROOT/'tests/runtime/storage_stubs',ROOT/'components/ts_security/include',ROOT/'components/ts_core/ts_log/src',mbed/'include']],str(c),str(mbed/'library/base64.c'),'-pthread','-o',str(t/'test')],check=True)
    subprocess.run([str(t/'test')],check=True,timeout=30)
