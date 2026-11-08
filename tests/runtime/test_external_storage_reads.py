#!/usr/bin/env python3
"""Actual registry, external storage callbacks, HTTP download, path classifier and FAT resolver.
FS/SDK/FAT/auth-record boundaries are synthetic; this is not a physical-card test.
Before package uses its original handler when no session callback was registered.
"""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile,sys
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
        return subprocess.check_output(['git','show','35e081ac8:'+path],cwd=os.environ.get('SOURCE_REPOSITORY',str(ROOT)),text=True)
    return (ROOT/path).read_text()
f=ROOT/'tests/runtime/test_cert_admission.py';text=f.read_text();ns={'__file__':str(f),'__name__':'storage_read_fixture'};a=text.index('api=source(');b=text.index('\nwith tempfile.TemporaryDirectory');argv=sys.argv;sys.argv=[sys.argv[0]]
try:exec(compile(text[:a],str(f),'exec'),ns)
finally:sys.argv=argv
ns['source']=source;exec(compile(text[a:b],str(f),'exec'),ns);code=ns['code'];code=code[:code.index('int main(void)')]
code=code.replace('#include "ts_api.h"','#include "ts_api.h"\n#include "ts_storage.h"',1)
code=code.replace('esp_err_t ts_storage_check_write_path(const char *s,bool children){(void)s;(void)children;return ESP_OK;}','')
code=code.replace('esp_err_t ts_security_validate_session(uint32_t sid,ts_session_t *out){','static unsigned revoked_sid;\nesp_err_t ts_security_validate_session(uint32_t sid,ts_session_t *out){\n if(sid&&sid==revoked_sid)return ESP_ERR_NOT_FOUND;')
function=ns['function'];IDF=ns['IDF']
code+=r"""
#include "ts_storage.h"
#include "ts_config_partitions.h"
#define api_storage_status readonly
#define api_storage_mount mutation
#define api_storage_unmount mutation
#define api_storage_delete mutation
#define api_storage_mkdir mutation
#define api_storage_rename mutation
#define api_storage_list readonly
#define api_storage_info readonly
#define api_storage_read readonly
static unsigned lookups,reads_after_lookup,active_sid;
static bool lookup_error,revoke_during_lookup;
typedef unsigned char BYTE;
typedef enum {FR_OK,FR_NO_FILE,FR_NO_PATH,FR_DISK_ERR} FRESULT;
typedef struct {char fname[256];} FILINFO;
static char s_mount_point[32]="/sdcard";
static void *s_card=(void *)1;
void ts_storage_sd_lock(void){}
void ts_storage_sd_unlock(void){}
static BYTE ff_diskio_get_pdrv_card(void *card){(void)card;return 0;}
static FRESULT f_stat(const char *path,FILINFO *info){
 ++lookups;if(revoke_during_lookup)revoked_sid=active_sid;
 if(lookup_error)return FR_DISK_ERR;
 const char *name=strrchr(path,'/')+1;
 if(!strcmp(name,"missing"))return FR_NO_FILE;
 if(!strcmp(name,"CONFID~1")||!strcmp(name,"confıg"))name="config";
 if(!strcmp(name,"PKIPRI~1"))name="pki";
 /* Synthetic resolver return tests the shared alias branch, not an on-card SFN claim. */
 if(!strcmp(name,"SSH_ALIAS"))name="ssh";
 snprintf(info->fname,sizeof(info->fname),"%s",name);return FR_OK;
}
"""
driver=source('components/ts_storage/src/ts_storage_sd.c')
if 'esp_err_t ts_storage_sd_resolve_name(' in driver:code+=function(driver,'ts_storage_sd_resolve_name')
if 'bool ts_storage_sd_name_is(' in driver:code+=function(driver,'ts_storage_sd_name_is')
code+=source('components/ts_storage/src/ts_storage_paths.c')+'\n'
storage=source('components/ts_api/src/ts_api_storage.c')
if 'static esp_err_t storage_read_for_session(' in storage:
    for name in ['storage_read_for_session','storage_list_for_session','storage_info_for_session','storage_file_for_session']:code+=function(storage,name)
start=storage.index('static const ts_api_endpoint_t s_storage_endpoints[]');end=storage.index('\n};',start)+3;code+=storage[start:end]+'\n'+function(storage,'ts_api_storage_register')
code+=r"""
typedef struct {const char *path;unsigned identity;} ts_http_request_t;
static int status;
static unsigned sent_files;
static esp_err_t ts_http_get_query_param(ts_http_request_t *r,const char *key,char *out,size_t n){(void)key;snprintf(out,n,"%s",r->path);return ESP_OK;}
static esp_err_t check_auth(ts_http_request_t *r,uint32_t *sid,ts_perm_level_t required){ts_session_t s;if(ts_security_validate_session(r->identity,&s)!=ESP_OK)return ESP_ERR_NOT_FOUND;*sid=r->identity;return s.level>=required?ESP_OK:ESP_ERR_NOT_ALLOWED;}
static esp_err_t ts_http_send_error(ts_http_request_t *r,int code,const char *why){(void)r;(void)why;status=code;return ESP_OK;}
bool ts_storage_exists(const char *p){(void)p;return true;}
bool ts_storage_is_dir(const char *p){(void)p;return false;}
static esp_err_t ts_http_send_private_file(ts_http_request_t *r,const char *p){(void)r;(void)p;++sent_files;status=200;return ESP_OK;}
"""
code+=function(source('components/ts_webui/src/ts_webui_api.c'),'file_download_handler')
code+=r"""
static void reading(const char *name,const char *path,unsigned identity,bool private_path,bool invalid){
 cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"path",path);cJSON_AddStringToObject(p,"role","root");cJSON_AddNumberToObject(p,"session_id",4);cJSON_AddBoolToObject(p,"requires_root",false);
 unsigned before=reads,stat_before=lookups;ts_api_result_t r={0};active_sid=identity;esp_err_t rc=ts_api_call_for_session(name,p,identity,&r);
 bool valid=identity>=1&&identity<=4,allowed=valid&&!invalid&&(!private_path||identity>=3);
 check((rc==ESP_OK&&r.code==TS_API_OK)==allowed,"path/verified identity decides external read result");check(reads==before+allowed,"rejected private/invalid read never enters business/file handler");if(!valid)check(lookups==stat_before,"anonymous/invalid session denied before FAT metadata");
 ts_api_result_free(&r);cJSON_Delete(p);
}
static void downloading(const char *path,unsigned identity,bool private_path,bool invalid){
 unsigned before=sent_files,stat_before=lookups;status=0;active_sid=identity;ts_http_request_t r={.path=path,.identity=identity};file_download_handler(&r,NULL);
 bool valid=identity>=1&&identity<=4,allowed=valid&&!invalid&&(!private_path||identity>=3);int expected=!valid?401:invalid?400:private_path&&identity<3?403:200;
 check(status==expected,"dedicated download gives correct login/admin/path status");check(sent_files==before+allowed,"rejected download never reaches private-file sender");if(!valid)check(lookups==stat_before,"anonymous download denied before FAT metadata");
}
int main(void){
 check(ts_api_init()==ESP_OK&&ts_api_storage_register()==ESP_OK,"real registry and nine storage names initialize");

 const struct {const char *name;unsigned level,methods,mode;} policies[]={
 {"storage.status",0,3,TS_API_ACCESS_PUBLIC},{"storage.mount",3,14,TS_API_ACCESS_ADMIN},{"storage.unmount",3,14,TS_API_ACCESS_ADMIN},{"storage.list",1,3,TS_API_ACCESS_READ},{"storage.delete",3,14,TS_API_ACCESS_ADMIN},{"storage.mkdir",3,14,TS_API_ACCESS_ADMIN},{"storage.rename",3,14,TS_API_ACCESS_ADMIN},{"storage.info",1,3,TS_API_ACCESS_READ},{"storage.read",1,3,TS_API_ACCESS_READ}};
 cJSON *names_all=ts_api_list(TS_API_CAT_MAX);check(cJSON_GetArraySize(names_all)==9,"actual nine storage registrations are complete");cJSON_Delete(names_all);
 for(unsigned i=0;i<9;i++){ts_api_access_t access={0};check(ts_api_get_access(policies[i].name,&access)==ESP_OK,"all storage policies query");check(access.minimum_level==policies[i].level&&access.allowed_methods==policies[i].methods&&access.external_access==policies[i].mode&&access.requires_auth==(policies[i].level>0),"nine independent role/method contracts");}
 const char *private_paths[]={"/sdcard/ssh","/sdcard/SSH","/sdcard/ssh/sub/sentinel.txt","/sdcard/foo/../ssh/sentinel.txt","/sdcard/ssh. /sentinel.txt","/sdcard/ssh\\sentinel.txt","/sdcard/SSH_ALIAS/sentinel.txt","/sdcard/config/network.json","/sdcard/CONFIG/ssh_hosts/a","/sdcard/foo/../config/wifi.json","/sdcard/config. /wifi.json","/sdcard/config\\wifi.json","/sdcard/confıg/wifi.json","/sdcard/CONFID~1/wifi.json","/sdcard/pki/ca-chain.crt","/sdcard/PKIPRI~1/ca-chain.crt","/spiffs/keys","/www/js/app.js","/autocfg/state.json"};
 const char *data_paths[]={"/sdcard/ssh-other/sentinel.txt","/sdcard/ssh/../images/pic.png","/sdcard","/sdcard/images/pic.png","/sdcard/自定义/file.txt","/sdcard/logs/sys.log","/sdcard/missing/file.txt","/sdcard/config/../images/pic.png","/sdcard/literal%2Fconfig/file"};
 const char *invalid_paths[]={"/sdcard-other/a","/spiffs-other/a","/unknown/a","relative","/","/sdcard/config\t/a"};
 const char *names[]={"storage.list","storage.info","storage.read"};unsigned identities[]={0,1,2,3,4,5,99};
 for(unsigned who=0;who<7;who++){
  for(unsigned i=0;i<sizeof(private_paths)/sizeof(private_paths[0]);i++){for(unsigned n=0;n<3;n++)reading(names[n],private_paths[i],identities[who],true,false);downloading(private_paths[i],identities[who],true,false);}
  for(unsigned i=0;i<sizeof(data_paths)/sizeof(data_paths[0]);i++){for(unsigned n=0;n<3;n++)reading(names[n],data_paths[i],identities[who],false,false);downloading(data_paths[i],identities[who],false,false);}
  for(unsigned i=0;i<sizeof(invalid_paths)/sizeof(invalid_paths[0]);i++){for(unsigned n=0;n<3;n++)reading(names[n],invalid_paths[i],identities[who],false,true);downloading(invalid_paths[i],identities[who],false,true);}
 }
 lookup_error=true;check(ts_storage_check_write_path("/sdcard/config/AUTOMA~1/state.json",false)==ESP_FAIL,"owner write guard propagates FAT lookup I/O error before any mutation");unsigned before=reads,send_before=sent_files;ts_api_result_t r={0};cJSON *p=cJSON_Parse("{\"path\":\"/sdcard/CONFID~1/wifi.json\"}");active_sid=4;
 check(ts_api_call_for_session("storage.read",p,4,&r)!=ESP_OK&&reads==before,"FAT I/O error refuses even Root before read, never treated as missing");ts_api_result_free(&r);status=0;ts_http_request_t req={.path="/sdcard/CONFID~1/wifi.json",.identity=4};file_download_handler(&req,NULL);check(status==500&&sent_files==send_before,"download refuses FAT I/O error before sender");lookup_error=false;
 revoke_during_lookup=true;revoked_sid=0;active_sid=4;before=reads;r=(ts_api_result_t){0};check(ts_api_call_for_session("storage.read",p,4,&r)!=ESP_OK&&reads==before,"Root revoked during FAT lookup cannot enter reader");ts_api_result_free(&r);revoked_sid=0;status=0;file_download_handler(&req,NULL);check(status==401&&sent_files==send_before,"Root revoked during FAT lookup cannot download");revoke_during_lookup=false;revoked_sid=0;cJSON_Delete(p);
 before=reads;r=(ts_api_result_t){0};check(ts_api_call("storage.read",NULL,&r)==ESP_OK&&reads==before+1,"trusted internal read retains original handler without external path restrictions");ts_api_result_free(&r);
 printf("%u checks, %u failures: actual storage registry/session callbacks/download/classifier/FAT resolver; synthetic SDK/FAT/business boundaries\n",checks,failures);ts_api_deinit();return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix='ts-storage-reads-') as tmp:
    c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
    inc=[ROOT/'tests/runtime/storage_stubs',ROOT/'components/ts_api/include',ROOT/'components/ts_security/include',ROOT/'components/ts_core/ts_config/include',ROOT/'components/ts_storage/include',IDF/'components/json/cJSON']
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in inc],str(c),str(IDF/'components/json/cJSON/cJSON.c'),'-pthread','-o',str(exe)],check=True,cwd=ROOT)
    subprocess.run([str(exe)],check=True,cwd=ROOT)
