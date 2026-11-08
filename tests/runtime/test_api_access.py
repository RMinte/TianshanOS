#!/usr/bin/env python3
"""Actual registry policy lookup: scalar snapshots, no JSON allocation, concurrent updates."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
fixture=ROOT/'tests/runtime/test_cert_admission.py';text=fixture.read_text();ns={'__name__':'access_fixture','__file__':str(fixture)}
exec(compile(text[:text.index('\nwith tempfile.TemporaryDirectory')],str(fixture),'exec'),ns)
code=ns['code'];code=code[:code.index('int main(void)')]
code+=r'''
#include <stdatomic.h>
static unsigned json_attempts;
static void *blocked_json(size_t size){(void)size;json_attempts++;return NULL;}
static atomic_uint race_errors;
static void *writer(void *unused){
 (void)unused;ts_api_endpoint_t ep={.name="fixture.racy",.handler=mutation,.requires_auth=true,.permission="cert.manage",.external_access=TS_API_ACCESS_ROOT,.allowed_methods=TS_API_METHOD_WRITE};
 for(unsigned n=0;n<5000;n++){if(ts_api_register(&ep)!=ESP_OK||ts_api_unregister(ep.name)!=ESP_OK)atomic_fetch_add(&race_errors,1);}return NULL;
}
static void *reader(void *unused){
 (void)unused;for(unsigned n=0;n<5000;n++){
  ts_api_access_t access={0};esp_err_t ret=ts_api_get_access("fixture.racy",&access);
  if(ret!=ESP_ERR_NOT_FOUND&&(ret!=ESP_OK||!access.requires_auth||access.minimum_level!=TS_PERM_ROOT))atomic_fetch_add(&race_errors,1);
 }return NULL;
}
int main(void){
 ts_api_access_t access={0};check(ts_api_get_access("cert.install",&access)==ESP_ERR_INVALID_STATE,"uninitialized registry cannot provide a policy");
 check(ts_api_init()==ESP_OK&&ts_api_cert_register()==ESP_OK,"real registry and certificate operations register");
 struct {const char *name;bool auth;unsigned level;const char *permission;ts_api_external_access_t role;uint8_t methods;} rows[]={
  {"fixture.public",false,TS_PERM_NONE,NULL,TS_API_ACCESS_PUBLIC,TS_API_METHOD_READ},{"fixture.read",true,TS_PERM_READ,NULL,TS_API_ACCESS_READ,TS_API_METHOD_READ},
  {"ui.widgets.set",true,TS_PERM_ADMIN,NULL,TS_API_ACCESS_ADMIN,TS_API_METHOD_WRITE},{"automation.rules.add",true,TS_PERM_ROOT,NULL,TS_API_ACCESS_ROOT,TS_API_METHOD_WRITE},
  {"automation.actions.execute",true,TS_PERM_ADMIN,NULL,TS_API_ACCESS_ADMIN,TS_API_METHOD_WRITE},{"automation.services.stop",true,TS_PERM_ADMIN,NULL,TS_API_ACCESS_ADMIN,TS_API_METHOD_WRITE},
  {"fixture.capability",false,TS_PERM_ROOT,"cert.manage",TS_API_ACCESS_ROOT,TS_API_METHOD_WRITE}
 };
 for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++){
  ts_api_endpoint_t ep={.name=rows[i].name,.handler=mutation,.requires_auth=rows[i].auth,.permission=rows[i].permission,.external_access=rows[i].role,.allowed_methods=rows[i].methods};check(ts_api_register(&ep)==ESP_OK,"test operation registers");
 }
 cJSON_Hooks hooks={blocked_json,free};cJSON_InitHooks(&hooks);
 const char *certs[]={"cert.generate_keypair","cert.generate_csr","cert.install","cert.install_ca","cert.delete","cert.status","cert.get_certificate"};
 for(unsigned n=0;n<sizeof(certs)/sizeof(*certs);n++){
  check(ts_api_get_access(certs[n],&access)==ESP_OK,"policy lookup works when JSON allocation is forbidden");
  check(access.requires_auth==(n<5)&&access.minimum_level==(n<5?TS_PERM_ADMIN:TS_PERM_NONE),"real certificate policy retains management and public separation");
 }
 for(unsigned n=0;n<sizeof(rows)/sizeof(*rows);n++){
  check(ts_api_get_access(rows[n].name,&access)==ESP_OK,"live fixture policy lookup succeeds");
  check(access.requires_auth==(rows[n].level>0)&&access.minimum_level==rows[n].level&&access.allowed_methods==rows[n].methods,"explicit roles/methods remain authoritative without prefix or compatibility flag inference");
 }
 check(ts_api_get_access(NULL,&access)==ESP_ERR_INVALID_ARG&&ts_api_get_access("cert.status",NULL)==ESP_ERR_INVALID_ARG,"invalid lookup arguments rejected");
 check(ts_api_get_access("missing",&access)==ESP_ERR_NOT_FOUND,"unknown policy explicitly unavailable");
 check(json_attempts==0,"all successful and failing lookups perform zero JSON allocations");cJSON_InitHooks(NULL);
 pthread_t a,b;check(!pthread_create(&a,NULL,writer,NULL)&&!pthread_create(&b,NULL,reader,NULL),"parallel registry/query workers start");
 pthread_join(a,NULL);pthread_join(b,NULL);check(atomic_load(&race_errors)==0,"concurrent register/unregister never yields a partial or stale permission snapshot");
 check(ts_api_unregister("fixture.capability")==ESP_OK&&ts_api_get_access("fixture.capability",&access)==ESP_ERR_NOT_FOUND,"unregistered policy immediately unavailable");
 check(ts_api_deinit()==ESP_OK,"real registry deinitializes");
 check(ts_api_get_access("cert.status",&access)==ESP_ERR_INVALID_STATE,"deinitialized registry rejects policy lookup");
 printf("%u checks, %u failures: real registry policy, zero JSON allocations, 5000 writer/reader iterations\n",checks,failures);return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='ts-api-access-') as tmp:
    c=Path(tmp,'test.c');binary=Path(tmp,'test');c.write_text(code)
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in ns['includes']],str(c),str(ns['IDF']/'components/json/cJSON/cJSON.c'),'-lpthread','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=30)
