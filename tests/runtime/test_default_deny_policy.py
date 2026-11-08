#!/usr/bin/env python3
"""Real management registrations/registry/dispatcher; synthetic business boundary."""
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
f=ROOT/'tests/runtime/test_cert_admission.py';text=f.read_text();ns={'__file__':str(f),'__name__':'auth_policy_fixture'}
a=text.index('api=source(');b=text.index('\nwith tempfile.TemporaryDirectory');argv=sys.argv;sys.argv=[sys.argv[0]]
try:exec(compile(text[:a],str(f),'exec'),ns)
finally:sys.argv=argv
ns['source']=source;exec(compile(text[a:b],str(f),'exec'),ns);code=ns['code'];code=code[:code.index('int main(void)')]
sp=importlib.util.spec_from_file_location('h',ROOT/'audit/api-security-20261103/tools/extract_inventory.py');h=importlib.util.module_from_spec(sp);sp.loader.exec_module(h)
# Trigger one trusted registry replacement immediately after the policy/handler
# snapshot unlock. Old code performs a second lookup and invokes the new Root
# handler under the earlier public policy; new code invokes its copied handler.
lookup='copy_external_call' if 'static esp_err_t copy_external_call(' in code else 'ts_api_get_access'
lookup_body=ns['function'](code,lookup)
code=code.replace(lookup_body,lookup_body.replace('    xSemaphoreGive(s_api.mutex);','    xSemaphoreGive(s_api.mutex);native_after_snapshot();'))
code='static void native_after_snapshot(void);\n'+code
code+=r"""
static bool replace_after_snapshot;
static unsigned public_snapshot_calls,root_replacement_calls;
static esp_err_t public_snapshot_handler(const cJSON *p,ts_api_result_t *r){(void)p;++public_snapshot_calls;ts_api_result_ok(r,NULL);return ESP_OK;}
static esp_err_t root_replacement_handler(const cJSON *p,ts_api_result_t *r){(void)p;++root_replacement_calls;ts_api_result_ok(r,NULL);return ESP_OK;}
static void native_after_snapshot(void){
 if(!replace_after_snapshot)return;replace_after_snapshot=false;
 assert(ts_api_unregister("fixture.snapshot")==ESP_OK);
 ts_api_endpoint_t replacement={.name="fixture.snapshot",.handler=root_replacement_handler,.external_access=TS_API_ACCESS_ROOT,.allowed_methods=TS_API_METHOD_WRITE};
 assert(ts_api_register(&replacement)==ESP_OK);
}
"""
code+=r"""
int main(void){
 check(ts_api_init()==ESP_OK,"real registry initializes");
 const unsigned identities[]={0,1,2,3,4,5,99};
 ts_api_endpoint_t internals[]={
 {.name="fixture.internal",.handler=mutation},
 {.name="automation.hidden_internal",.handler=mutation,.requires_auth=true},
 {.name="fixture.cert.manage",.handler=mutation,.permission="cert.manage"},
 {.name="system.unexposed",.handler=mutation,.requires_auth=true,.permission="admin"}};
 cJSON *params=cJSON_Parse("{\"session_id\":4,\"role\":\"root\",\"permission\":\"cert.manage\",\"minimum_level\":0}");
 for(unsigned n=0;n<4;n++){
  check(ts_api_register(&internals[n])==ESP_OK,"internal-only operation registers for trusted C use");
  ts_api_access_t access={0};check(ts_api_get_access(internals[n].name,&access)==ESP_OK,"internal metadata query succeeds");
  check(access.external_access==TS_API_ACCESS_INTERNAL&&!access.allowed_methods&&!access.requires_auth&&!access.minimum_level,"no name/legacy flag/permission creates external role or verbs");
  for(unsigned i=0;i<7;i++){
   ts_api_result_t r={0};unsigned before=mutations;
   check(ts_api_call_for_session(internals[n].name,params,identities[i],&r)==ESP_ERR_NOT_SUPPORTED&&r.code==TS_API_ERR_NOT_SUPPORTED,"all external identities including Root are denied before dispatch");
   check(mutations==before,"unexposed operation never reaches handler");ts_api_result_free(&r);
  }
  ts_api_result_t r={0};unsigned before=mutations;check(ts_api_call(internals[n].name,params,&r)==ESP_OK&&mutations==before+1,"trusted internal original handler contract remains");ts_api_result_free(&r);
 }
 ts_api_endpoint_t ambiguous={.name="fixture.missing_role",.handler=mutation,.allowed_methods=TS_API_METHOD_READ};
 check(ts_api_register(&ambiguous)==ESP_ERR_INVALID_ARG,"nonzero verbs without explicit external role rejected");
 ts_api_endpoint_t pub={.name="automation.explicit_public",.handler=mutation,.requires_auth=true,.permission="admin",.external_access=TS_API_ACCESS_PUBLIC,.allowed_methods=TS_API_METHOD_READ};
 check(ts_api_register(&pub)==ESP_OK,"explicit public declaration accepted without name/legacy inference");ts_api_access_t a={0};check(ts_api_get_access(pub.name,&a)==ESP_OK&&!a.requires_auth&&!a.minimum_level&&a.allowed_methods==TS_API_METHOD_READ,"explicit declaration is role and method truth");
 ts_api_result_t r={0};unsigned before=mutations;check(ts_api_call_for_session(pub.name,params,0,&r)==ESP_OK&&mutations==before+1,"explicit public positive");ts_api_result_free(&r);
 ts_api_endpoint_t root={.name="fixture.explicit_root",.handler=mutation,.requires_auth=false,.external_access=TS_API_ACCESS_ROOT,.allowed_methods=TS_API_METHOD_WRITE};
 check(ts_api_register(&root)==ESP_OK,"explicit Root accepted despite unused compatibility flag");before=mutations;check(ts_api_call_for_session(root.name,params,3,&r)!=ESP_OK&&mutations==before,"Admin cannot weaken explicit Root with JSON");ts_api_result_free(&r);check(ts_api_call_for_session(root.name,params,4,&r)==ESP_OK&&mutations==before+1,"Root positive");ts_api_result_free(&r);
 root.name="fixture.root_methods_missing";root.allowed_methods=0;check(ts_api_register(&root)==ESP_ERR_INVALID_ARG,"explicit role without verbs rejected");
 root.name="fixture.root_bad_methods";root.allowed_methods=128;check(ts_api_register(&root)==ESP_ERR_INVALID_ARG,"unknown method bits rejected");

 ts_api_endpoint_t snapshot={.name="fixture.snapshot",.handler=public_snapshot_handler,.external_access=TS_API_ACCESS_PUBLIC,.allowed_methods=TS_API_METHOD_READ};
 check(ts_api_register(&snapshot)==ESP_OK,"public snapshot fixture registers");replace_after_snapshot=true;
 check(ts_api_call_for_session(snapshot.name,params,0,&r)==ESP_OK&&public_snapshot_calls==1&&!root_replacement_calls,"concurrent replacement cannot run new Root handler under old public authorization");ts_api_result_free(&r);
 check(ts_api_get_access(snapshot.name,&a)==ESP_OK&&a.minimum_level==TS_PERM_ROOT,"replacement really occurred; later requests see new Root policy");
 cJSON_Delete(params);check(ts_api_deinit()==ESP_OK,"cleanup");printf("%u checks, %u failures: default-deny external dispatch, trusted internal calls and explicit authority; real registry, synthetic business/session boundaries\n",checks,failures);return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix='ts-auth-policy-') as tmp:
    c=Path(tmp)/'probe.c';exe=Path(tmp)/'probe';c.write_text(code)
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in ns['includes']],str(c),str(ns['IDF']/'components/json/cJSON/cJSON.c'),'-lpthread','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
