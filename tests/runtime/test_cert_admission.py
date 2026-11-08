#!/usr/bin/env python3
"""Actual registry/dispatcher/cert registration; PKI effect boundary is a counter."""
from pathlib import Path
import argparse,os,re,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[2]
IDF=Path(os.environ.get("IDF_PATH","/Users/massif/esp/v5.5.2/esp-idf"))
parser=argparse.ArgumentParser()
parser.add_argument("--source-ref")
args=parser.parse_args()
def source(path):
    return subprocess.check_output(["git","show",f"{args.source_ref}:{path}"],cwd=ROOT,text=True) if args.source_ref else (ROOT/path).read_text()
def function(s,name):
    m=re.search(r"^(?:static )?[^\n;]+\b"+name+r"\([^;]+?\)\s*\{",s,re.M);assert m,name
    return s[m.start():s.index("\n}",m.start())+2]+"\n"
api=source("components/ts_api/src/ts_api.c")
api=api[:api.index("\nesp_err_t ts_api_register_all(void)")]
api=re.sub(r"^#include[^\n]*\n","",api,flags=re.M)
code=r'''
#define _DEFAULT_SOURCE
#define CONFIG_TS_WEBUI_AUTH_REQUIRED 1
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "ts_api.h"
#include "ts_security.h"
#include "freertos/semphr.h"
#define TS_LOGD(...) ((void)0)
#define TS_LOGI(...) ((void)0)
#define TS_LOGW(...) ((void)0)
#define TS_LOGE(...) ((void)0)
static bool fail_permission_copy;
static char *registry_strdup(const char *s){return fail_permission_copy&&!strcmp(s,"cert.manage")?NULL:strdup(s);}
#define TS_STRDUP_PSRAM registry_strdup
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
static unsigned mutations,reads,checks,failures;
static int allocation_budget=-1;
static void *metadata_malloc(size_t n){if(allocation_budget==0)return NULL;if(allocation_budget>0)--allocation_budget;return malloc(n);}
static void check(bool value,const char *label){checks++;if(!value){failures++;fprintf(stderr,"FAIL %s\n",label);}}
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps){(void)caps;return calloc(n,size);}
static cJSON *ts_config_pack_parse_import_request(const char *s,size_t n){(void)n;return cJSON_Parse(s);}
esp_err_t ts_security_validate_session(uint32_t sid,ts_session_t *out){
 if(sid==6){memset(out,0,sizeof(*out));out->session_id=sid;out->level=TS_PERM_NONE;return ESP_OK;}
 if(sid<1||sid>4)return ESP_ERR_NOT_FOUND;
 memset(out,0,sizeof(*out));out->session_id=sid;out->level=(ts_perm_level_t)sid;return ESP_OK;
}
esp_err_t ts_storage_check_write_path(const char *s,bool children){(void)s;(void)children;return ESP_OK;}
'''
code+=api
code+=r'''
static esp_err_t mutation(const cJSON *p,ts_api_result_t *r){(void)p;++mutations;ts_api_result_ok(r,cJSON_CreateObject());return ESP_OK;}
static esp_err_t readonly(const cJSON *p,ts_api_result_t *r){(void)p;++reads;ts_api_result_ok(r,cJSON_CreateObject());return ESP_OK;}
#define api_cert_status readonly
#define api_cert_get_certificate readonly
#define api_cert_generate_keypair mutation
#define api_cert_generate_csr mutation
#define api_cert_install mutation
#define api_cert_install_ca mutation
#define api_cert_delete mutation
'''
code+=function(source("components/ts_api/src/ts_api_cert.c"),"ts_api_cert_register")
code+=r'''
int main(void){
 check(ts_api_init()==ESP_OK,"actual registry init");
 check(ts_api_cert_register()==ESP_OK,"actual certificate registration");
 cJSON *list=ts_api_list(TS_API_CAT_SECURITY);
 check(cJSON_GetArraySize(list)==7,"all certificate registrations covered");cJSON_Delete(list);
 ts_api_endpoint_t copy_test={.name="fixture.failed_permission",.description="fixture",.handler=mutation,.permission="cert.manage",.external_access=TS_API_ACCESS_ROOT,.allowed_methods=TS_API_METHOD_WRITE};
 fail_permission_copy=true;
 check(ts_api_register(&copy_test)==ESP_ERR_NO_MEM,"permission-copy failure rejects registration");
 cJSON *failed_entry=ts_api_get_info(copy_test.name);
 check(failed_entry==NULL,"failed policy cannot leave an admitted entry");
 fail_permission_copy=false;
 /* No malformed slot or duplicate remains after a failed registration. */
 if(failed_entry){cJSON_Delete(failed_entry);ts_api_unregister(copy_test.name);}
 check(ts_api_register(&copy_test)==ESP_OK,"failed slot reusable");
 check(ts_api_unregister(copy_test.name)==ESP_OK,"test operation cleanup");
 const char *writes[]={"cert.generate_keypair","cert.generate_csr","cert.install","cert.install_ca","cert.delete"};
 const uint32_t identities[]={0,1,2,3,4,5,99};
 cJSON *params=cJSON_Parse("{\"session_id\":4,\"role\":\"root\",\"permission\":\"cert.manage\"}");
 for(unsigned n=0;n<5;n++){
  cJSON *info=ts_api_get_info(writes[n]);
  check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(info,"requires_auth")),"mutation declares authentication");
  const char *permission=cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(info,"permission"));
  check(permission&&!strcmp(permission,"cert.manage"),"mutation declares management capability");cJSON_Delete(info);
  for(unsigned i=0;i<sizeof identities/sizeof(*identities);i++){
   uint32_t sid=identities[i];bool allowed=sid==3||sid==4;unsigned before=mutations;
   ts_api_result_t result={0};esp_err_t ret=ts_api_call_for_session(writes[n],params,sid,&result);
   check(allowed?ret==ESP_OK:ret!=ESP_OK,"mutations require authenticated admin or Root session");
   check(mutations==before+allowed,"denial before PKI effect boundary");
   check(allowed?result.code==TS_API_OK:result.code==TS_API_ERR_AUTH,"explicit auth failure");
   ts_api_result_free(&result);
  }
 }
 cJSON_Hooks hooks={.malloc_fn=metadata_malloc,.free_fn=free};cJSON_InitHooks(&hooks);
 for(int budget=0;budget<33;budget++){
  ts_api_result_t r={0};unsigned before=mutations;allocation_budget=budget;
  esp_err_t ret=ts_api_call_for_session("cert.install",params,1,&r);
  allocation_budget=-1;
  check(ret!=ESP_OK&&mutations==before,"metadata OOM cannot downgrade PKI to READ");
  ts_api_result_free(&r);
 }
 cJSON_InitHooks(NULL);
 const char *public_reads[]={"cert.status","cert.get_certificate"};
 for(unsigned n=0;n<2;n++)for(unsigned i=0;i<sizeof identities/sizeof(*identities);i++){
  ts_api_result_t result={0};unsigned before=reads;
  check(ts_api_call_for_session(public_reads[n],NULL,identities[i],&result)==ESP_OK,"public certificate/status reads unchanged");
  check(reads==before+1,"read dispatched normally");ts_api_result_free(&result);
 }
 /* Names/permission strings never expose an internal operation. */
 ts_api_endpoint_t tagged={.name="fixture.cert.manage",.handler=mutation,.requires_auth=false,.permission="cert.manage"};
 check(ts_api_register(&tagged)==ESP_OK,"register tagged operation");
 ts_api_result_t result={0};unsigned before=mutations;
 check(ts_api_call_for_session(tagged.name,params,0,&result)!=ESP_OK&&mutations==before,"capability cannot become public by false flag");ts_api_result_free(&result);
 check(ts_api_call_for_session(tagged.name,params,2,&result)!=ESP_OK&&mutations==before,"write-level is not management");ts_api_result_free(&result);
 check(ts_api_call_for_session(tagged.name,params,4,&result)==ESP_ERR_NOT_SUPPORTED&&mutations==before,"Root cannot expose an internal-only tag");ts_api_result_free(&result);
 check(ts_api_call(tagged.name,params,&result)==ESP_OK&&mutations==before+1,"trusted internal tag still callable");ts_api_result_free(&result);
 ts_api_endpoint_t weak={.name="fixture.weakened.cert",.handler=mutation,.permission="cert.manage",.external_access=TS_API_ACCESS_PUBLIC,.allowed_methods=TS_API_METHOD_READ};
 check(ts_api_register(&weak)==ESP_ERR_INVALID_ARG,"contradictory management role refused at registration");
 weak.external_access=TS_API_ACCESS_ADMIN;
 check(ts_api_register(&weak)==ESP_ERR_INVALID_ARG,"GET admin management still refused at registration");
 weak.allowed_methods=TS_API_METHOD_WRITE;
 check(ts_api_register(&weak)==ESP_OK,"original admin certificate management restored");
 before=mutations;check(ts_api_call_for_session(weak.name,params,3,&result)==ESP_OK&&mutations==before+1,"authenticated admin positive");ts_api_result_free(&result);
 check(ts_api_unregister(weak.name)==ESP_OK,"admin fixture removed");
 weak.allowed_methods=TS_API_METHOD_READ;
 weak.external_access=TS_API_ACCESS_ROOT;
 check(ts_api_register(&weak)==ESP_ERR_INVALID_ARG,"GET management method refused at registration");
 weak.allowed_methods=TS_API_METHOD_WRITE;
 check(ts_api_register(&weak)==ESP_OK,"explicit Root/write management declaration accepted");
 before=mutations;check(ts_api_call_for_session(weak.name,params,4,&result)==ESP_OK&&mutations==before+1,"explicit Root positive");ts_api_result_free(&result);
 before=mutations;
 check(ts_api_call("cert.install",params,&result)==ESP_OK&&mutations==before+1,"trusted internal caller unchanged");
 ts_api_result_free(&result);cJSON_Delete(params);
 check(ts_api_deinit()==ESP_OK,"registry cleanup");
 printf("%u checks, %u failures: real registry/dispatcher/cert metadata, synthetic sessions and PKI effects\n",checks,failures);
 return failures?1:0;
}
'''
includes=[ROOT/"tests/runtime/storage_stubs",ROOT/"components/ts_api/include",
    ROOT/"components/ts_security/include",ROOT/"components/ts_core/ts_config/include",IDF/"components/json/cJSON"]
with tempfile.TemporaryDirectory(prefix="ts-cert-admission-") as tmp:
    c=Path(tmp)/"test.c";binary=Path(tmp)/"test";c.write_text(code)
    subprocess.run(["cc","-std=gnu11","-g","-Wno-deprecated-declarations",
        "-fsanitize=address,undefined","-fno-sanitize-recover=all",*["-I"+str(p) for p in includes],
        str(c),str(IDF/"components/json/cJSON/cJSON.c"),"-lpthread","-o",str(binary)],check=True,cwd=ROOT)
    subprocess.run([str(binary)],check=True,cwd=ROOT)
