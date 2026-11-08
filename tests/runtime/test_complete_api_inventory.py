#!/usr/bin/env python3
"""All real API registrations/dispatch against independent role and verb expectations.

Business effects and identities are synthetic; path/body/lifecycle suites stay separate.
The checked-in contract preserves the old eight focused probes plus storage policy.
Rows: [external_access, minimum role, HTTP method mask, raw requires_auth (-1: not asserted)].
Raw legacy flags are checked only where the old probes checked them; normalized
auth metadata and independent role/method admission are checked for every API.
"""
from pathlib import Path
from zipfile import ZipFile
import argparse,json,os,sys,re,subprocess,tempfile,importlib.util
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
p=argparse.ArgumentParser();p.add_argument('--source-package');p.add_argument('--write-method-contract', action='store_true');args=p.parse_args()
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
ns['source']=source;exec(compile(text[a:b],str(f),'exec'),ns);code=ns['code'];code=code[:code.index('static esp_err_t mutation(')]
capacity=int(re.search(r'^CONFIG_TS_API_MAX_ENDPOINTS=(\d+)$', source('sdkconfig'), re.M)[1])
code=f'#define CONFIG_TS_API_MAX_ENDPOINTS {capacity}\n'+code
sp=importlib.util.spec_from_file_location('h',ROOT/'audit/api-security-20261103/tools/extract_inventory.py');h=importlib.util.module_from_spec(sp);sp.loader.exec_module(h)
code+='#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\nstatic esp_err_t ts_config_pack_init(void){return ESP_OK;}\nstatic esp_err_t ts_ota_init(void){return ESP_OK;}\nstatic void load_ota_server_config(void){}\n'
code+='static esp_err_t lpmu_ensure_mutex(void){return ESP_OK;}\nesp_err_t ts_auth_init(void){return ESP_OK;}\n'
# Expected values are fixed test data, never derived from ts_api_get_access().
policy = json.loads((ROOT/'tests/runtime/api-policy-contract.json').read_text())
assert len(policy) == 277
code += 'static const struct {const char *name; unsigned mode,level,methods; int raw_auth;} expected[]={\n'
code += ''.join('{'+json.dumps(name)+','+','.join(map(str,values))+'},\n' for name,values in policy.items())
code += r"""
};
static const unsigned expected_count=sizeof(expected)/sizeof(*expected);
static unsigned expected_index(const char *name){
 for(unsigned i=0;i<expected_count;i++)if(!strcmp(name,expected[i].name))return i;
 fprintf(stderr,"UNEXPECTED API %s\n",name);check(false,"registered name belongs to independent contract");return 0;
}
static esp_err_t checked_register(const ts_api_endpoint_t *endpoint){
 unsigned i=expected_index(endpoint->name);
 if(expected[i].raw_auth>=0)check(endpoint->requires_auth==expected[i].raw_auth,"raw source auth flag retains prior focused assertion");
 return ts_api_register(endpoint);
}
static esp_err_t checked_multiple(const ts_api_endpoint_t *endpoints,size_t count){
 for(size_t i=0;i<count;i++){esp_err_t rc=checked_register(&endpoints[i]);if(rc!=ESP_OK)return rc;}
 return ESP_OK;
}
"""
for module in ['auth', 'automation', 'cert', 'config', 'config_pack', 'device', 'dhcp', 'fan', 'gpio', 'hosts', 'key', 'led', 'lpmu_access', 'log', 'monitor', 'nat', 'network', 'ota', 'power', 'service', 'sftp', 'ssh', 'storage', 'system', 'temp', 'time', 'ui', 'wifi']:
    source_text=source('components/ts_api/src/ts_api_'+module+'.c');mask=h.masked(source_text)
    m=re.search(r'^(?:esp_err_t|void) ts_api_'+module+r'_register\(void\)\s*\{',source_text,re.M);assert m,module
    parts=[]
    for table in re.finditer(r'static\s+const\s+ts_api_endpoint_t\s+\w+\[\]\s*=\s*\{',source_text[:m.start()]):
        parts.append(source_text[table.start():h.endbrace(mask,source_text.index('{',table.start()))+1])
    parts.append(source_text[m.start():h.endbrace(mask,source_text.index('{',m.start()))]);part='\n'.join(parts)
    for table in re.findall(r'static\s+const\s+ts_api_endpoint_t\s+(\w+)\[\]', '\n'.join(parts[:-1])):
        part=re.sub(r'\b'+table+r'\b','fixture_'+module+'_'+table,part)
    sessions=set(re.findall(r'\.session_handler\s*=\s*(\w+)',part))
    for handler in sorted(set(re.findall(r'\.(?:handler|session_handler)\s*=\s*(\w+)',part))):
        stub='fixture_'+module+'_'+handler;native_args='const cJSON *p,uint32_t sid,ts_api_result_t *r' if handler in sessions else 'const cJSON *p,ts_api_result_t *r';code+='static esp_err_t '+stub+'('+native_args+'){(void)p;'+('(void)sid;' if handler in sessions else '')+'++mutations;ts_api_result_ok(r,NULL);return ESP_OK;}\n';part=re.sub(r'\b'+handler+r'\b',stub,part)
    part=re.sub(r'\bts_api_register_multiple\b','checked_multiple',part)
    part=re.sub(r'\bts_api_register\b','checked_register',part)
    code+=part+'\n'
code+=ns['function'](source('components/ts_api/src/ts_api.c'),'ts_api_register_all')
code+=r"""
int main(void){
 check(ts_api_init()==ESP_OK,"registry initializes with full configured capacity");
 check(ts_api_register_all()==ESP_OK,"real register_all orchestrates all28 modules in production order");
"""
code+=r"""
 cJSON *names=ts_api_list(TS_API_CAT_MAX);
 check(cJSON_GetArraySize(names)==expected_count,"all277 expected APIs registered exactly once, including five runtime aliases");
 const cJSON *item;cJSON_ArrayForEach(item,names){const char *name=cJSON_GetStringValue(item);check(name!=NULL,"inventory returns names");if(name)expected_index(name);}
 const unsigned ids[]={0,1,2,3,4,5,6,99};
 cJSON *params=cJSON_Parse("{\"session_id\":4,\"role\":\"root\",\"minimum_level\":0,\"external_access\":1,\"allowed_methods\":15,\"key\":\"system.language\"}");
 for(unsigned i=0;i<expected_count;i++){
  const char *name=expected[i].name;unsigned level=expected[i].level;
  ts_api_access_t access={0};check(ts_api_get_access(name,&access)==ESP_OK,"expected name exists after actual register_all");
  printf("METHOD_CONTRACT %s %u\n",name,access.allowed_methods);
  check(access.external_access==expected[i].mode&&access.minimum_level==level&&access.requires_auth==(level>0),"role matches independent policy, including TOKEN vs PUBLIC");
  check(access.allowed_methods==expected[i].methods,"method declaration matches independent contract");
  cJSON *info=ts_api_get_info(name);
  check(info&&cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(info,"requires_auth"))==(level>0),"serialized auth metadata agrees with independent role");cJSON_Delete(info);
  for(unsigned j=0;j<sizeof(ids)/sizeof(*ids);j++){
   unsigned before=mutations;bool allowed=!level||(ids[j]>=level&&ids[j]<=4);ts_api_result_t result={0};
   esp_err_t rc=ts_api_call_for_session(name,params,ids[j],&result);
   if((rc==ESP_OK)!=allowed)fprintf(stderr,"ADMISSION name=%s sid=%u mode=%u expected=%u code=%u\n",name,ids[j],access.external_access,allowed,result.code);
   check((rc==ESP_OK)==allowed,"verified C role decides admission; forged JSON and valid NONE cannot elevate");
   check(mutations==before+allowed,"denial never reaches substituted business boundary");ts_api_result_free(&result);
  }
 }
 const char *internal[]={"cert.install","fan.set","system.reboot","config.set","time.sync","ssh.exec","auth.root.set_password","monitor.set_server"};
 for(unsigned i=0;i<sizeof(internal)/sizeof(*internal);i++){
  unsigned before=mutations;ts_api_result_t result={0};
  check(ts_api_call(internal[i],params,&result)==ESP_OK&&mutations==before+1,"trusted internal callers retain each former focused probe's contract");ts_api_result_free(&result);
 }
 ts_api_endpoint_t invalid={.name="fixture.invalid",.handler=fixture_cert_api_cert_install,.external_access=TS_API_ACCESS_ROOT};
 check(ts_api_register(&invalid)==ESP_ERR_INVALID_ARG,"explicit role without methods rejected");
 invalid.allowed_methods=128;check(ts_api_register(&invalid)==ESP_ERR_INVALID_ARG,"unknown method bit rejected");
 ts_api_endpoint_t mislabeled={.name="fixture.management",.handler=fixture_cert_api_cert_install,.permission="cert.manage",.external_access=TS_API_ACCESS_PUBLIC,.allowed_methods=TS_API_METHOD_READ};
 check(ts_api_register(&mislabeled)==ESP_ERR_INVALID_ARG,"contradictory management declaration rejected before admission");
 ts_api_access_t guarded={0};check(ts_api_get_access(mislabeled.name,&guarded)==ESP_ERR_NOT_FOUND,"rejected declaration leaves no callable entry");
 cJSON_Delete(params);cJSON_Delete(names);check(ts_api_deinit()==ESP_OK,"cleanup");
 printf("%u checks, %u failures: 277 independent role/method contracts,28 real registration bodies,5 aliases,8 identities, actual registry/dispatcher; synthetic initialization/business/session boundaries\n",checks,failures);return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix='ts-auth-policy-') as tmp:
    c=Path(tmp)/'probe.c';exe=Path(tmp)/'probe';c.write_text(code)
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in ns['includes']],str(c),str(ns['IDF']/'components/json/cJSON/cJSON.c'),'-lpthread','-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],timeout=20,capture_output=True,text=True)
    print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr)
    result.check_returncode()
    contract={name:int(methods) for name,methods in re.findall(r'^METHOD_CONTRACT (\S+) (\d+)$',result.stdout,re.M)}
    assert len(contract)==277
    routes=ns['function'](source('components/ts_webui/src/ts_webui_api.c'),'ts_webui_api_init')
    http={name.lstrip('/'): {'GET':1,'POST':2,'PUT':4,'DELETE':8}[method] for name,method in re.findall(r'\.uri\s*=\s*API_PREFIX\s*"([^"]+)",\s*\.method\s*=\s*TS_HTTP_(GET|POST|PUT|DELETE)', routes) if name != '/*'}
    assert len(http)==6, 'Review dedicated routes before updating browser contracts'
    contract={'logical':contract,'http':http}
    fixture=ROOT/'tests/prompts/api-methods.json'
    if args.write_method_contract:
        assert not args.source_package, 'Only the current source can update the browser contract'
        fixture.write_text(json.dumps(contract,sort_keys=True,indent=2)+'\n')
    else:
        assert json.loads(fixture.read_text())==contract, 'Browser method contract differs from the real registry; review and regenerate it'
