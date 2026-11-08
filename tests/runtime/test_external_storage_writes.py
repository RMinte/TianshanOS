#!/usr/bin/env python3
"""Production path guard and API writers; synthetic FS/SSH/LED mutation boundary."""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'))
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
        return subprocess.check_output(['git','show','35e081ac8:'+path],cwd=os.environ.get('SOURCE_REPOSITORY',str(ROOT)),text=True)
    return (ROOT/path).read_text()
def body(path,name):
    t=source(path);m=re.search(r'^(?:static )?[^\n;]+\b'+name+r'\([^;]+?\)\s*\{',t,re.M);assert m,name
    return t[m.start():t.index('\n}',m.start())+2]+'\n'
code=r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_api.h"
#include "ts_storage.h"
#include "ts_config_partitions.h"
#include "ts_config_pack.h"
#define TS_LED_CC_SDCARD_JSON_PATH "/sdcard/config/led_color_correction.json"
#define TS_LOGI(...) ((void)0)
#define TS_LOGD(...) ((void)0)
#define TS_LOGE(...) ((void)0)
typedef void *ts_ssh_session_t,*ts_sftp_session_t;
static unsigned checks,failures,writes,connections,closed;
static char target[256];
static void check(bool value,const char *why){++checks;if(!value){++failures;fprintf(stderr,"FAIL: %s\n",why);}}
void ts_api_result_ok(ts_api_result_t *r,cJSON *d){r->code=TS_API_OK;r->data=d;}
void ts_api_result_error(ts_api_result_t *r,ts_api_result_code_t c,const char *m){r->code=c;r->message=(char *)m;}
bool ts_storage_sd_name_is(const char *path,const char *name){return !strcmp(name,"automation")&&!strcmp(path,"/sdcard/config/AUTOMA~1");}
bool ts_storage_exists(const char *p){(void)p;return true;}
bool ts_storage_is_dir(const char *p){(void)p;return false;}
esp_err_t ts_storage_delete(const char *p){++writes;snprintf(target,sizeof(target),"%s",p);return ESP_OK;}
esp_err_t ts_storage_rmdir_r(const char *p){return ts_storage_delete(p);}
esp_err_t ts_storage_mkdir_p(const char *p){return ts_storage_delete(p);}
esp_err_t ts_storage_rename(const char *a,const char *b){(void)a;return ts_storage_delete(b);}
static esp_err_t connect_sftp(const cJSON *p,ts_ssh_session_t *a,ts_sftp_session_t *b){(void)p;++connections;*a=(void *)1;*b=(void *)2;return ESP_OK;}
static void close_sftp(ts_ssh_session_t a,ts_sftp_session_t b){assert(a==(void *)1&&b==(void *)2);++closed;}
static esp_err_t ts_sftp_get(ts_sftp_session_t s,const char *remote,const char *local,void *a,void *b){(void)s;(void)remote;(void)a;(void)b;return ts_storage_delete(local);}
static bool ts_led_cc_is_initialized(void){return true;}
static esp_err_t ts_led_cc_save_to_sdcard(const char *p){return ts_storage_delete(p?p:TS_LED_CC_SDCARD_JSON_PATH);}
"""
code+=r"""
bool ts_config_pack_can_export(void){return true;}
ts_config_pack_result_t ts_config_pack_create(const char *n,const char *j,size_t z,const ts_config_pack_export_opts_t *o,char **out,size_t *len){(void)n;(void)j;(void)z;(void)o;*out=strdup("{}");*len=2;return TS_CONFIG_PACK_OK;}
esp_err_t ts_config_pack_save(const char *p,const char *d,size_t z){(void)d;(void)z;ts_storage_delete(p);return TS_CONFIG_PACK_OK;}
static void pack_failure(ts_api_result_t *r,ts_config_pack_result_t e){(void)e;ts_api_result_error(r,TS_API_ERR_INTERNAL,"pack error");}
"""
code+='esp_err_t ts_storage_sd_resolve_name(const char *p,char *n,size_t z){const char *name=!strcmp(p,"/sdcard/config/AUTOMA~1")?"automation":strrchr(p,\'/\')+1;snprintf(n,z,"%s",name);return ESP_OK;}\n'
code+=source('components/ts_storage/src/ts_storage_paths.c')+'\n'
for path,names in [('components/ts_api/src/ts_api_storage.c',['storage_mutation_failure','api_storage_delete','api_storage_mkdir','api_storage_rename']),('components/ts_api/src/ts_api_sftp.c',['api_sftp_get']),('components/ts_api/src/ts_api_led.c',['api_led_cc_export']),('components/ts_api/src/ts_api_config_pack.c',['api_config_pack_export'])]:
    for name in names:code+=body(path,name)
code+=r"""
static void call_writer(unsigned which,const char *path,bool allowed){
 cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"path",path);cJSON_AddStringToObject(p,"remote","/remote/data");cJSON_AddStringToObject(p,"local",path);
 cJSON_AddStringToObject(p,"from",which==2?path:"/sdcard/data/a.txt");cJSON_AddStringToObject(p,"to",which==2?"/sdcard/data/b.txt":path);
 cJSON_AddStringToObject(p,"name","synthetic");cJSON_AddStringToObject(p,"content","{}");cJSON_AddStringToObject(p,"recipient_cert","synthetic");cJSON_AddStringToObject(p,"save_path",path);
 ts_api_result_t r={0};unsigned before=writes,conn=connections,done=closed;esp_err_t rc;
 switch(which){case 0:rc=api_storage_delete(p,&r);break;case 1:rc=api_storage_mkdir(p,&r);break;case 2:case 3:rc=api_storage_rename(p,&r);break;case 4:rc=api_sftp_get(p,&r);break;case 5:rc=api_led_cc_export(p,&r);break;default:rc=api_config_pack_export(p,&r);break;}
 if(allowed){check(rc==ESP_OK&&r.code==TS_API_OK&&writes==before+(which==6?2:1),"ordinary SD destination reaches only intended mkdir/save boundaries");if(which==4)check(connections==conn+1&&closed==done+1,"valid SFTP connects and closes exactly once");}
 else {check(rc!=ESP_OK&&r.code!=TS_API_OK,"unsupported mount/owned destination is refused");check(writes==before,"refused operation never reaches mutation boundary");check(connections==conn&&closed==done,"invalid SFTP destination never contacts remote");}
 cJSON_Delete(p);cJSON_Delete(r.data);
}
int main(void){
 const char *blocked[]={"/sdcard","/sdcard/","/","/www-other/a","/autocfg/state.json","/unknown/a","relative/file","/sdcard-other/a","/sdcardx/a","/sdcard/config/automation/state.json","/sdcard/config/AUTOMA~1/state.json","/sdcard/config\\automation\\state.json","/sdcard/config\t/file"};
 for(unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++)for(unsigned w=0;w<7;w++)call_writer(w,blocked[i],false);
 const char *ordinary[]={"/www/js/app.js","/www/sub","/spiffs/config.json","/sdcard/data/a.txt","/sdcard/自定义/a.txt","/sdcard/images/my image.png","/sdcard/config/fan_curve_config_1.json","/sdcard/config/ordinary.json","/sdcard/data/literal%2Fname.txt"};
 for(unsigned i=0;i<sizeof(ordinary)/sizeof(ordinary[0]);i++)for(unsigned w=0;w<7;w++)call_writer(w,ordinary[i],true);
 cJSON *empty=cJSON_CreateObject();ts_api_result_t r={0};unsigned before=writes;check(api_led_cc_export(empty,&r)==ESP_OK&&r.code==TS_API_OK&&writes==before+1&&!strcmp(target,TS_LED_CC_SDCARD_JSON_PATH),"existing default LED export path unchanged");cJSON_Delete(empty);cJSON_Delete(r.data);
 check(ts_storage_check_write_path("/www/internal",false)==ESP_OK,"original admin/CLI WWW file behavior preserved");
 check(ts_storage_check_write_path("/spiffs/internal",false)==ESP_OK,"trusted internal SPIFFS persistence remains available");
 printf("%u checks, %u failures: actual external storage/SFTP/LED APIs and FAT owner guard; synthetic mutation/SSH boundaries\n",checks,failures);return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix='ts-external-writes-') as tmp:
    c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
    includes=[ROOT/'tests/runtime/storage_stubs',ROOT/'components/ts_storage/include',ROOT/'components/ts_core/ts_config/include',ROOT/'components/ts_api/include',ROOT/'components/ts_config_pack/include',IDF/'components/json/cJSON']
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(i) for i in includes],str(c),str(IDF/'components/json/cJSON/cJSON.c'),'-o',str(exe)],check=True,cwd=ROOT)
    subprocess.run([str(exe)],check=True,cwd=ROOT)
