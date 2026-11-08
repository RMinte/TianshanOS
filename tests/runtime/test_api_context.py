#!/usr/bin/env python3
"""Actual registry and SSH descriptors prove verified C context dispatch, including concurrency."""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,sys,re,subprocess,tempfile,importlib.util
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
        return subprocess.check_output(['git','show','35e081ac8:'+path],cwd=os.environ.get('SOURCE_REPOSITORY',str(ROOT)),text=True)
    return (ROOT/path).read_text()
f=ROOT/'tests/runtime/test_cert_admission.py';text=f.read_text();ns={'__file__':str(f),'__name__':'context_fixture'}
a=text.index('api=source(');b=text.index('\nwith tempfile.TemporaryDirectory');argv=sys.argv;sys.argv=[sys.argv[0]]
try:exec(compile(text[:a],str(f),'exec'),ns)
finally:sys.argv=argv
ns['source']=source;exec(compile(text[a:b],str(f),'exec'),ns);code=ns['code'];code=code[:code.index('int main(void)')]
code=code.replace('esp_err_t ts_security_validate_session(', 'static bool context_session_live=true;\nesp_err_t ts_security_validate_session(',1).replace('if(sid<1||sid>4)','if(!context_session_live || sid<1 || (sid>4 && sid!=8))',1).replace('out->level=(ts_perm_level_t)sid','out->level=sid==8?TS_PERM_ROOT:(ts_perm_level_t)sid',1)
code+=r"""
#include <pthread.h>
#include <stdatomic.h>
static atomic_uint context_calls,legacy_calls,context_mismatch;
static esp_err_t legacy_handler(const cJSON *p,ts_api_result_t *r){(void)p;atomic_fetch_add(&legacy_calls,1);ts_api_result_ok(r,NULL);return ESP_OK;}
static esp_err_t context_handler(const cJSON *p,uint32_t sid,ts_api_result_t *r){
 const cJSON *expected=cJSON_GetObjectItemCaseSensitive(p,"expected");
 if(!cJSON_IsNumber(expected)||expected->valuedouble!=sid)atomic_fetch_add(&context_mismatch,1);
 atomic_fetch_add(&context_calls,1);ts_api_result_ok(r,NULL);return ESP_OK;
}
"""
sp=importlib.util.spec_from_file_location('h',ROOT/'audit/api-security-20261103/tools/extract_inventory.py');h=importlib.util.module_from_spec(sp);sp.loader.exec_module(h)
s=source('components/ts_api/src/ts_api_ssh.c');m=re.search(r'static const ts_api_endpoint_t ssh_endpoints\[\]\s*=\s*\{',s);assert m
part=s[m.start():h.endbrace(h.masked(s),s.index('{',m.start()))+1]
for handler in set(re.findall(r'\.handler\s*=\s*(\w+)',part)):code+='#define '+handler+' legacy_handler\n'
for handler in set(re.findall(r'\.session_handler\s*=\s*(\w+)',part)):code+='#define '+handler+' context_handler\n'
code+=part+'\n'
code+=r"""
static const char *names[]={"ssh.exec_stream","ssh.exec_status","ssh.cancel"};
static void *parallel(void *arg){
 uint32_t sid=(uint32_t)(uintptr_t)arg;cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"expected",sid);cJSON_AddNumberToObject(p,"owner_session_id",99);
 for(unsigned i=0;i<1000;i++){ts_api_result_t r={0};assert(ts_api_call_for_session(names[0],p,sid,&r)==ESP_OK);ts_api_result_free(&r);}cJSON_Delete(p);return NULL;
}
int main(void){
 check(ts_api_init()==ESP_OK,"registry initializes");check(ts_api_register_multiple(ssh_endpoints,sizeof(ssh_endpoints)/sizeof(*ssh_endpoints))==ESP_OK,"real SSH descriptor table registers");
 const unsigned ids[]={0,1,2,3,4,5,8,99};
 cJSON *p=cJSON_Parse("{\"session_id\":99,\"caller_session_id\":99,\"owner_session_id\":99,\"role\":\"root\",\"expected\":4}");
 for(unsigned n=0;n<3;n++){
  ts_api_access_t access={0};check(ts_api_get_access(names[n],&access)==ESP_OK&&access.minimum_level==TS_PERM_ROOT&&access.requires_auth,"three private endpoints explicitly Root");
  check(access.allowed_methods==(n==1?TS_API_METHOD_READ:TS_API_METHOD_WRITE),"private method contract");
  for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);i++){
   unsigned sid=ids[i],before=atomic_load(&context_calls),old=atomic_load(&legacy_calls);bool allowed=sid==4||sid==8;cJSON_SetNumberValue(cJSON_GetObjectItem(p,"expected"),sid);ts_api_result_t r={0};
   check((ts_api_call_for_session(names[n],p,sid,&r)==ESP_OK)==allowed,"invalid/expired and lower roles do not enter session callback");
   check(atomic_load(&context_calls)==before+allowed&&atomic_load(&legacy_calls)==old,"external dispatch uses contextual path only");ts_api_result_free(&r);
  }
  unsigned before=atomic_load(&context_calls),old=atomic_load(&legacy_calls);ts_api_result_t r={0};check(ts_api_call(names[n],p,&r)==ESP_OK&&atomic_load(&legacy_calls)==old+1&&atomic_load(&context_calls)==before,"trusted internal dispatch retains legacy handler explicitly");ts_api_result_free(&r);
 }
 context_session_live=false;unsigned before=atomic_load(&context_calls);ts_api_result_t r={0};check(ts_api_call_for_session(names[0],p,4,&r)!=ESP_OK&&atomic_load(&context_calls)==before,"revoked owner cannot invoke callback");ts_api_result_free(&r);context_session_live=true;
 check(atomic_load(&context_mismatch)==0,"JSON identity fields never replace C caller identity");
 if(!failures){pthread_t a,b;before=atomic_load(&context_calls);check(!pthread_create(&a,NULL,parallel,(void*)(uintptr_t)4)&&!pthread_create(&b,NULL,parallel,(void*)(uintptr_t)8),"two independent verified sessions start");pthread_join(a,NULL);pthread_join(b,NULL);check(atomic_load(&context_calls)==before+2000&&atomic_load(&context_mismatch)==0,"2000 concurrent dispatches preserve per-call identities without shared current_session");}
 cJSON_Delete(p);check(ts_api_deinit()==ESP_OK,"cleanup");printf("%u checks, %u failures: real registry/SSH descriptors/C dispatch, 8 identities and 2000 concurrent contextual calls\n",checks,failures);return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix='ts-api-context-') as tmp:
    c=Path(tmp)/'probe.c';exe=Path(tmp)/'probe';c.write_text(code)
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in ns['includes']],str(c),str(ns['IDF']/'components/json/cJSON/cJSON.c'),'-lpthread','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
