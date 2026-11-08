"""Actual SDK Basic-auth producer and header callback before/after build adaptation; synthetic secrets only."""
from pathlib import Path
import base64,hashlib,importlib.util,tempfile,shutil,re,os
from test_console_lifecycle import RTOS,IDF,body,run_native
from host_paths import build_dir,sdk_library_dir
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('prepared_http',root/'tools/prepare_http_transport_polling.py')
preparation=importlib.util.module_from_spec(spec);spec.loader.exec_module(preparation)
work=build_dir('http-client-privacy')
original_path=IDF/'components/esp_http_client/esp_http_client.c'
transport_path=IDF/'components/tcp_transport/transport_ssl.c'
original=original_path.read_bytes();transport=transport_path.read_bytes()
websocket_path=Path(os.environ.get('TS_WS_COMPONENT_SOURCE', str(root/'managed_components/espressif__esp_websocket_client/esp_websocket_client.c')))
ws_path=IDF/'components/tcp_transport/transport_ws.c'
websocket_original=websocket_path.read_bytes();ws_original=ws_path.read_bytes()
metadata=preparation.prepare(IDF,work,websocket_path);prepared=work/'esp_http_client.c'
assert metadata['private_diagnostic_sites']==10 and metadata['websocket_private_diagnostic_sites']==11
first=prepared.read_bytes();assert preparation.prepare(IDF,work,websocket_path)==metadata and prepared.read_bytes()==first
assert original_path.read_bytes()==original and transport_path.read_bytes()==transport
# The integration must fail closed if any reviewed privacy anchor drifts.
with tempfile.TemporaryDirectory(prefix='ts-sdk-http-anchor-') as directory:
    fake=Path(directory)
    for src in (original_path,transport_path,ws_path):
        out=fake/src.relative_to(IDF);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,out)
    candidate=fake/original_path.relative_to(IDF)
    candidate.write_text(candidate.read_text().replace('HEADER=%s:%s','CHANGED_HEADER=%s:%s'))
    try:preparation.prepare(fake,fake/'out',websocket_path)
    except ValueError:pass
    else:raise AssertionError('changed SDK private diagnostic anchor must not compile unnoticed')
    assert not (fake/'out/esp_http_client.c').exists()

code='#define _DARWIN_C_SOURCE\n#define _GNU_SOURCE\n'+RTOS+r'''
#include <stdarg.h>
#include "mbedtls/base64.h"
#undef ESP_LOGD
#undef ESP_LOGE
static char diagnostics[8192];
static void private_log(const char *fmt,...){size_t used=strlen(diagnostics);va_list args;va_start(args,fmt);vsnprintf(diagnostics+used,sizeof(diagnostics)-used,fmt,args);va_end(args);}
#define ESP_LOGD(tag,...) private_log(__VA_ARGS__)
#define ESP_LOGE(tag,...) private_log(__VA_ARGS__)
#define TAG "http-privacy"
#define esp_crypto_base64_encode mbedtls_base64_encode
#define ESP_RETURN_ON_FALSE(condition,result,tag,...) do{if(!(condition))return(result);}while(0)
#define ESP_GOTO_ON_FALSE(condition,result,label,tag,...) do{if(!(condition)){ret=(result);goto label;}}while(0)
#define HTTP_EVENT_ON_HEADER 3
static unsigned header_callbacks,event_callbacks,authorization_writes;
static bool fail_authorization;
static char delivered_key[100],delivered_value[256],authorization[256];
typedef struct client {
 struct {const char *username,*password;} connection_info;
 char *current_header_key,*current_header_value;
 struct {char *header_key,*header_value;} event;
} *esp_http_client_handle_t;
static esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,const char *key,const char *value){(void)client;assert(!strcmp(key,"Authorization"));authorization_writes++;snprintf(authorization,sizeof(authorization),"%s",value);return fail_authorization?ESP_FAIL:ESP_OK;}
static void http_dispatch_event(esp_http_client_handle_t client,int event,void *data,int length){assert(event==HTTP_EVENT_ON_HEADER&&!data&&!length);header_callbacks++;snprintf(delivered_key,sizeof(delivered_key),"%s",client->event.header_key);snprintf(delivered_value,sizeof(delivered_value),"%s",client->event.header_value);}
static void http_dispatch_event_to_event_loop(int event,void *data,size_t length){assert(event==HTTP_EVENT_ON_HEADER&&data&&length==sizeof(esp_http_client_handle_t));event_callbacks++;}
'''
code+=body(IDF/'components/esp_http_client/lib/http_auth.c','http_auth_basic')
for path,suffix in ((original_path,'original'),(prepared,'prepared')):
    for name in ('esp_http_client_prepare_basic_auth','http_on_header_event'):
        code+=body(path,name).replace(name+'(',name+'_'+suffix+'(',1)
code+=r'''
static unsigned checks;
static void check(bool ok,const char *why){checks++;if(!ok){fprintf(stderr,"FAIL %s\n",why);abort();}}
static void reset(void){diagnostics[0]=authorization[0]=delivered_key[0]=delivered_value[0]=0;header_callbacks=event_callbacks=authorization_writes=0;fail_authorization=false;}
int main(void){
 struct client client={.connection_info={"synthetic-user","synthetic-password"}};
 reset();check(esp_http_client_prepare_basic_auth_original(&client)==ESP_OK&&authorization_writes==1,"original SDK creates real Basic credential");
 check(strstr(diagnostics,EXPECTED_BASIC)!=NULL,"original SDK Debug record contains directly reversible synthetic Basic credential");
 reset();check(esp_http_client_prepare_basic_auth_prepared(&client)==ESP_OK&&authorization_writes==1&&!strcmp(authorization,EXPECTED_BASIC),"prepared SDK sends identical Basic authorization through actual SDK base64 producer");
 check(!strstr(diagnostics,"synthetic-user")&&!strstr(diagnostics,"synthetic-password")&&!strstr(diagnostics,EXPECTED_BASIC)&&strstr(diagnostics,"prepared"),"prepared diagnostics retain state and never authentication bytes");
 fail_authorization=true;check(esp_http_client_prepare_basic_auth_prepared(&client)==ESP_FAIL,"header-setting failure semantics unchanged");
 const char *keys[]={"Set-Cookie","Authorization","X-Private-Fixture"};
 for(unsigned i=0;i<3;i++){
  reset();client.current_header_key=strdup(keys[i]);client.current_header_value=strdup("synthetic-private-response-value");
  check(http_on_header_event_original(&client)==0&&header_callbacks==1&&event_callbacks==1&&strstr(diagnostics,"synthetic-private-response-value"),"original SDK response-header Debug path discloses synthetic private field");
  check(!client.current_header_key&&!client.current_header_value,"original SDK callback releases owned header strings");
  reset();client.current_header_key=strdup(keys[i]);client.current_header_value=strdup("synthetic-private-response-value");
  check(http_on_header_event_prepared(&client)==0&&header_callbacks==1&&event_callbacks==1&&!strcmp(delivered_key,keys[i])&&!strcmp(delivered_value,"synthetic-private-response-value"),"same complete private headers still delivered once to actual application and event loop boundaries");
  check(!client.current_header_key&&!client.current_header_value&&!strstr(diagnostics,"synthetic-private-response-value")&&!strstr(diagnostics,keys[i]),"prepared global Debug receives neither field name/value while cleanup remains complete");
 }
 printf("%u checks: actual original/prepared SDK Basic auth + header events, real SDK base64 crypto, preserved credentials/callbacks/error/cleanup with private global logs; no network or real secrets\n",checks);
}
'''
expected='Basic '+base64.b64encode(b'synthetic-user:synthetic-password').decode()
code='#define EXPECTED_BASIC "'+expected+'"\n'+code
sdk=sdk_library_dir(IDF)
run_native(code,[sdk/'library/libmbedcrypto.a'],(),[IDF/'components/mbedtls/mbedtls/include'])
assert hashlib.sha256(original_path.read_bytes()).hexdigest()==metadata['http_sdk_sha256']
print('PASS 10 exact SDK diagnostic sites adapted, source drift rejects and shared SDK byte hashes unchanged')

# Replay exact reviewed sink statements without claiming a socket handshake.
# The error formatter itself runs in full and preserves its application event.
private = re.compile(r'\b(?:host|ws->sub_protocol|ws->auth|ws->headers|ws->buffer|server_key|client_key|expected_server_key|client->errormsg_buffer|client->config->scheme|uri|client->config->host)\b')
def private_statements(text):
    return [m.group() for m in re.finditer(r'ESP_LOG(?:D|E)\s*\([\s\S]*?\);', text)
            if private.search(re.sub(r'"(?:\\.|[^"\\])*"', '""', m.group()))]
original_sinks=private_statements(ws_original.decode())+private_statements(websocket_original.decode())
assert len(original_sinks)==11, original_sinks
prepared_sinks=[]
# Find each replacement at the same source line, not a duplicated test policy.
for original_text,prepared_text in [(ws_original.decode(),(work/'transport_ws.c').read_text()),(websocket_original.decode(),(work/'esp_websocket_client.c').read_text())]:
    for statement in private_statements(original_text):
        before=original_text[:original_text.index(statement)]
        # Match the enclosing function's statement ordinal. Formatting adaptation
        # adds lines, so a source-line offset is not a stable identifier.
        prefix=list(re.finditer(r'ESP_LOG(?:D|E)\s*\([\s\S]*?\);',before))
        prepared_sinks.append(list(re.finditer(r'ESP_LOG(?:D|E)\s*\([\s\S]*?\);',prepared_text))[len(prefix)].group())
with tempfile.TemporaryDirectory(prefix='ts-sdk-ws-anchor-') as directory:
    fake=Path(directory)
    for src in (original_path,transport_path,ws_path):
        out=fake/src.relative_to(IDF);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,out)
    managed=fake/'managed.c';managed.write_bytes(websocket_original)
    for i,statement in enumerate(original_sinks):
        is_transport=statement in ws_original.decode()
        target=fake/ws_path.relative_to(IDF) if is_transport else managed
        clean=ws_original.decode() if is_transport else websocket_original.decode()
        target.write_text(clean.replace(statement,statement.replace('ESP_LOG','DRIFT_LOG',1),1))
        output=fake/('out-'+str(i))
        try:preparation.prepare(fake,output,managed)
        except ValueError:pass
        else:raise AssertionError('changed WS diagnostic anchor accepted: '+statement)
        assert not output.exists(), 'partial generated sources published after rejected drift'
        target.write_text(clean)

ws_code=RTOS+r'''
#undef ESP_LOGD
#undef ESP_LOGE
#define TAG "ws-privacy"
#define ESP_LOGD(tag,...) private_log(__VA_ARGS__)
#define ESP_LOGE(tag,...) private_log(__VA_ARGS__)
#define WEBSOCKET_EVENT_ERROR 9
#define WS_OVER_TCP_SCHEME "ws"
#define WS_OVER_TLS_SCHEME "wss"
static char diagnostics[8192],event_text[4096];static unsigned events,checks;static bool fail_alloc;
static void private_log(const char *fmt,...){size_t used=strlen(diagnostics);va_list a;va_start(a,fmt);vsnprintf(diagnostics+used,sizeof(diagnostics)-used,fmt,a);va_end(a);}
struct config{const char *scheme,*host;int port;};
typedef struct ws_client{size_t errormsg_size;char *errormsg_buffer;struct config *config;} *esp_websocket_client_handle_t;
static void *error_alloc(size_t n){return fail_alloc?NULL:malloc(n);}
static int esp_websocket_client_dispatch_event(esp_websocket_client_handle_t client,int event,const char *data,int len){(void)client;assert(event==WEBSOCKET_EVENT_ERROR&&len>=0&&(size_t)len==strlen(data));events++;snprintf(event_text,sizeof(event_text),"%s",data);return ESP_OK;}
#define malloc error_alloc
'''
ws_code+=body(work/'esp_websocket_client.c','esp_websocket_client_error')+'\n#undef malloc\n'
for suffix,statements in [('original',original_sinks),('prepared',prepared_sinks)]:
    ws_code+='static void sinks_'+suffix+'(const char *secret){\n'+r'''
 const char *host=secret,*server_key=secret,*client_key=secret,*expected_server_key=secret,*uri=secret;int port=443,needed_size=(int)strlen(secret);
 struct {const char *sub_protocol,*auth,*headers,*buffer;} storage={secret,secret,secret,secret},*ws=&storage;
 struct config config={secret,secret,443};struct ws_client instance={.errormsg_buffer=(char*)secret,.config=&config},*client=&instance;
'''+'\n'.join(statements)+'\n}\n'
ws_code+=r'''
static void check(bool ok,const char *why){checks++;if(!ok){fprintf(stderr,"FAIL %s\n",why);abort();}}
static void reset(void){diagnostics[0]=event_text[0]=0;events=0;}
int main(void){
 const char *secret="synthetic-ws-private-user:password/query-token";
 reset();sinks_original(secret);check(strstr(diagnostics,secret)!=NULL,"original SDK diagnostic statements disclose synthetic authentication/URI contents");
 reset();sinks_prepared(secret);check(!strstr(diagnostics,secret)&&diagnostics[0],"all eleven adapted diagnostics preserve state/count without private bytes");
 struct ws_client client={0};char expected[4096];
 snprintf(expected,sizeof(expected),"uri=%s count=%d fractional=%.2f length=%zu",secret,731,4.25,strlen(secret));
 reset();check(esp_websocket_client_error(&client,"uri=%s count=%d fractional=%.2f length=%zu",secret,731,4.25,strlen(secret))==ESP_OK,"actual formatted SDK error succeeds");
 check(events==1&&!strcmp(event_text,expected),"va_copy retains complete mixed arguments exactly once in application event");
 check(!strstr(diagnostics,secret)&&strstr(diagnostics,"WebSocket client error"),"error event contents do not enter global error log");
 size_t original_size=client.errormsg_size;char *original_buffer=client.errormsg_buffer;
 reset();check(esp_websocket_client_error(&client,"code=%d",7)==ESP_OK&&events==1&&!strcmp(event_text,"code=7")&&client.errormsg_buffer==original_buffer&&client.errormsg_size==original_size,"existing error storage reused without truncation");
 reset();fail_alloc=true;check(esp_websocket_client_error(&client,"%04096d",1)==ESP_ERR_NO_MEM&&!events&&!client.errormsg_buffer&&!client.errormsg_size,"allocation failure emits no partial or stale error event");fail_alloc=false;
 reset();check(esp_websocket_client_error(&client,"retry=%s:%u",secret,17u)==ESP_OK&&events==1&&strstr(event_text,secret)&&!strstr(diagnostics,secret),"normal application callback recovers after allocation failure with private bytes intact");
 free(client.errormsg_buffer);
 printf("%u checks: actual prepared WebSocket error formatter/event, exact eleven original/prepared sink statements, preserved mixed varargs/storage/error paths; no network or real secrets\n",checks);
}
'''
run_native(ws_code)
assert websocket_path.read_bytes()==websocket_original and ws_path.read_bytes()==ws_original
print('PASS eleven WS diagnostic anchors individually reject drift before any output; shared SDK and managed dependency unchanged')
