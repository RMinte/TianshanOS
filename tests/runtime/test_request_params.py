#!/usr/bin/env python3
"""Actual HTTP/login/local-proxy admission; effects, allocation and HTTP I/O synthetic."""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,subprocess,tempfile,re
ROOT=Path(__file__).resolve().parents[2]
os.chdir(ROOT)
parser=argparse.ArgumentParser();parser.add_argument('--source-package');args=parser.parse_args()
fixture=ROOT/'tests/runtime/test_sensitive_auth.py'
text=fixture.read_text();ns={'__name__':'params_fixture','__file__':str(fixture)}
exec(compile(text[:text.index("\nidf = Path(")],str(fixture),'exec'),ns)
code=ns['code'];code=code[:code.index('int main(void) {')]
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
        return subprocess.check_output(['git','show','35e081ac8:'+path],text=True)
    return (ROOT/path).read_text()
def function(text,name):
    m=re.search(r'^(?:static )?[^\n;]+\b'+name+r'\([^;]+?\)\s*\{',text,re.M);assert m,name
    return text[m.start():text.index('\n}',m.start())+2]+'\n'
if args.source_package:
    code=code.replace(ns['extract']('api_handler'),function(source('components/ts_webui/src/ts_webui_api.c'),'api_handler'),1)
    before=ns['extract']('ts_api_automation_proxy_fetch_for_session',(ROOT/'components/ts_api/src/ts_api_automation.c').read_text())
    after=function(source('components/ts_api/src/ts_api_automation.c'),'ts_api_automation_proxy_fetch_for_session')
    code=code.replace(before[:before.index('    // 外部 HTTP 请求')],after[:after.index('    // 外部 HTTP 请求')],1)
code=code.replace('#define TS_API_MALLOC(size) malloc(size)',r'''
static bool fail_query_alloc;static int json_budget=-1;
static void *query_alloc(size_t size){return fail_query_alloc?NULL:malloc(size);}
static void *json_alloc(size_t size){if(json_budget==0)return NULL;if(json_budget>0)--json_budget;return malloc(size);}
#define TS_API_MALLOC(size) query_alloc(size)
''',1)
# Stop fault injection at the effect boundary; outgoing response OOM is a separate contract.
code=code.replace('    ++calls;strcpy(received_name,name);','    json_budget=-1;++calls;strcpy(received_name,name);',1)
code+=r'''
#define ESP_ERR_INVALID_STATE 0x103
static unsigned login_attempts;
static esp_err_t ts_auth_login(const char *user,const char *password,uint32_t *sid,char *token,size_t size){
 (void)user;(void)password;login_attempts++;*sid=1;snprintf(token,size,"fixture-token");return ESP_OK;
}
static bool ts_auth_password_changed(const char *user){(void)user;return true;}
'''
code+=function(source('components/ts_webui/src/ts_webui_api.c'),'login_handler')
code+=r'''
static unsigned checks,failures;
static void expect(bool ok,const char *label){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);}}
static void admit(const char *uri,const char *body,size_t length,int expected_status,bool dispatch){
 const char *query=strchr(uri,'?');httpd_req_t native={.query=query?query+1:NULL};
 ts_http_request_t req={.req=&native,.uri=uri,.method=1,.body=body,.body_len=length,.auth="root"};
 int before=calls;status=0;free(response);response=NULL;api_handler(&req,NULL);
 expect(status==expected_status,"HTTP status follows admission result");expect(calls==before+dispatch,"invalid request has zero business effects");
}
int main(void){
 const char *bad[]={"{","[]","null","true","3","\"string\"","{\"x\":1}tail","{\"x\":1}{\"y\":2}",
  "{\"path\":\"a\",\"path\":\"b\"}","{\"path\":\"a\",\"PATH\":\"b\"}","{\"p\\u0061th\":\"a\",\"path\":\"b\"}",
  "{\"data\":{\"x\":1,\"x\":2}}","{\"path\":\"a\\u0000b\"}","{\"x\\u0000y\":1}","{\"x\":1e999}","{\"x\":\"raw\nline\"}"};
 for(unsigned n=0;n<sizeof(bad)/sizeof(*bad);n++)admit("/api/v1/hosts/info",bad[n],strlen(bad[n]),400,false);
 char nul[]="{\"x\":1}\0{\"y\":2}";admit("/api/v1/hosts/info",nul,sizeof(nul)-1,400,false);
 admit("/api/v1/hosts/info",NULL,4,400,false);
 const char *good[]={"{}"," {\"empty\":\"\",\"flag\":false,\"n\":0} \r\n", "{\"data\":{\"A\":1,\"a\":2},\"literal\":\"\\\\u0000\",\"text\":\"中文 + quotes\\\"\"}"};
 for(unsigned n=0;n<sizeof(good)/sizeof(*good);n++)admit("/api/v1/hosts/info",good[n],strlen(good[n]),200,true);
 const char *queries[]={"x=1&x=2","x=1&X=2","bad=%Q2","bad=%00","bad=%0","bare","=value"};
 for(unsigned n=0;n<sizeof(queries)/sizeof(*queries);n++){
  char uri[256];snprintf(uri,sizeof(uri),"/api/v1/hosts/info?%s",queries[n]);admit(uri,NULL,0,400,false);
  cJSON *p=cJSON_CreateObject();char url[256];snprintf(url,sizeof(url),"http://localhost/api/v1/hosts/info?%s",queries[n]);cJSON_AddStringToObject(p,"url",url);
  ts_api_result_t result={0};int before=calls;ts_api_automation_proxy_fetch_for_session(p,1,&result);
  expect(result.code==TS_API_ERR_INVALID_ARG&&calls==before,"local proxy cannot bypass query admission");ts_api_result_free(&result);cJSON_Delete(p);
 }
 admit("/api/v1/hosts/info?x=query","{\"X\":\"body\"}",12,400,false);
 admit("/api/v1/hosts/info?port=2222&label=A+B%252E&empty=","{\"extra\":7}",11,200,true);
 expect(cJSON_GetObjectItem(received_params,"port")->valueint==2222&&!strcmp(cJSON_GetObjectItem(received_params,"label")->valuestring,"A B%2E"),"once-only decoding, numeric coercion and disjoint body merge preserved");
 char deep[256];unsigned pos=0;for(unsigned i=0;i<33;i++){memcpy(deep+pos,"{\"x\":",5);pos+=5;}deep[pos++]='0';for(unsigned i=0;i<33;i++)deep[pos++]='}';deep[pos]=0;
 admit("/api/v1/hosts/info",deep,pos,400,false);
 char *long_body=malloc(32768);memset(long_body,'x',32768);memcpy(long_body,"{\"command\":\"",12);memcpy(long_body+32764,"\"}",3);
 admit("/api/v1/hosts/info",long_body,32766,200,true);free(long_body);
 fail_query_alloc=true;admit("/api/v1/hosts/info?host=fixture",NULL,0,500,false);fail_query_alloc=false;
 cJSON_Hooks hooks={.malloc_fn=json_alloc,.free_fn=free};
 for(int budget=0;budget<100;budget++){
  const char *body="{\"command\":\"full fixture\",\"data\":{\"name\":\"value\"},\"n\":0}";
  cJSON *expected=cJSON_Parse(body);int before=calls;httpd_req_t native={0};ts_http_request_t req={.req=&native,.uri="/api/v1/hosts/info",.method=1,.body=body,.body_len=strlen(body),.auth="root"};
  json_budget=budget;cJSON_InitHooks(&hooks);api_handler(&req,NULL);json_budget=-1;cJSON_InitHooks(NULL);
  expect(calls==before||(calls==before+1&&cJSON_Compare(expected,received_params,true)),"OOM never dispatches partial JSON parameters");cJSON_Delete(expected);
 }
 for(unsigned n=0;n<sizeof(bad)/sizeof(*bad);n++){
  unsigned before=login_attempts;ts_http_request_t req={.body=bad[n],.body_len=strlen(bad[n])};status=0;login_handler(&req,NULL);
  expect(status==400&&login_attempts==before,"bad login JSON rejected before password verification");
 }
 const char *bad_logins[]={"{\"username\":\"root\",\"USERNAME\":\"admin\",\"password\":\"Secret\"}","{\"username\":\"root\",\"password\":\"Secret\",\"password\":\"Other\"}","{\"username\":\"root\",\"password\":\"Secret\"}tail","{\"username\":\"root\",\"password\":\"Secret\\u0000tail\"}"};
 for(unsigned n=0;n<sizeof(bad_logins)/sizeof(*bad_logins);n++){
  unsigned before=login_attempts;ts_http_request_t req={.body=bad_logins[n],.body_len=strlen(bad_logins[n])};status=0;login_handler(&req,NULL);
  expect(status==400&&login_attempts==before,"ambiguous credential request never reaches verification");
 }
 const char *login="{\"username\":\"root\",\"password\":\"Synthetic Secret\"}";unsigned before=login_attempts;
 ts_http_request_t req={.body=login,.body_len=strlen(login)};login_handler(&req,NULL);expect(status==200&&login_attempts==before+1,"normal login stays usable");
 cJSON_Delete(received_params);free(response);
 printf("%u checks, %u failures: actual HTTP/login/local proxy with synthetic effects and 100 JSON allocation budgets\n",checks,failures);
 return failures?1:0;
}
'''
IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'));cjson=IDF/'components/json/cJSON'
with tempfile.TemporaryDirectory(prefix='ts-request-params-') as tmp:
    c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(cjson),*['-I'+str(p) for p in ns['url_native_includes']],str(c),str(cjson/'cJSON.c'),*map(str,ns['url_native_sources']),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
