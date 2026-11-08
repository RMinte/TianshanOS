#!/usr/bin/env python3
"""Actual legacy API/core functions reject unimplemented work; synthetic decrypted content only."""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'))
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if 'current-source/'+path in z.namelist():return z.read('current-source/'+path).decode()
        return subprocess.check_output(['git','show','35e081ac8:'+path],cwd=os.environ.get('SOURCE_REPOSITORY',str(ROOT)),text=True)
    return (ROOT/path).read_text()
def function(path,name):
    t=source(path);m=re.search(r'^(?:static )?[^\n;]+\b'+name+r'\([^;]+?\)\s*\{',t,re.M);assert m,name
    return t[m.start():t.index('\n}',m.start())+2]+'\n'
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_api.h"
#include "ts_config_pack.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define TS_LOGI(...) ((void)0)
#define TS_MALLOC_PSRAM malloc
#define TS_STRDUP_PSRAM strdup
static unsigned checks,failures,opens,loads,module_writes;
static bool s_initialized=true,fail_json;
static void check(bool ok,const char *why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static void *json_malloc(size_t n){return fail_json?NULL:malloc(n);}
/* These are only old-function read/decrypt boundaries, never a PKI trust test. */
static FILE *fixture_open(const char *p,const char *mode){(void)p;(void)mode;opens++;FILE *f=tmpfile();assert(f);fputs("{}",f);rewind(f);return f;}
ts_config_pack_result_t ts_config_pack_load_mem(const char *text,size_t len,ts_config_pack_t **out){
 (void)text;(void)len;loads++;*out=calloc(1,sizeof(**out));assert(*out);(*out)->content=strdup("{\"network\":{},\"led\":{},\"fan\":{}}");assert((*out)->content);return TS_CONFIG_PACK_OK;
}
void ts_config_pack_free(ts_config_pack_t *p){if(p){free(p->content);free(p);}}
'''
for name in ['ts_api_result_init','ts_api_result_free','ts_api_result_ok','ts_api_result_error','ts_api_result_reason']:code+=function('components/ts_api/src/ts_api.c',name)
t=source('components/ts_config_pack/src/ts_config_pack.c');a=t.index('static const char *s_error_messages[]');b=t.index('\n};',a)+3;code+=t[a:b]+'\n'
for name in ['ts_config_pack_strerror','ts_config_pack_reason']:code+=function('components/ts_config_pack/src/ts_config_pack.c',name)
code+='#define fopen fixture_open\n'+function('components/ts_config_pack/src/ts_config_pack.c','ts_config_pack_apply_file')+'#undef fopen\n'
code+=source('components/ts_api/src/ts_api_pack.h')+'\n'
code+=function('components/ts_api/src/ts_api_config_pack.c','api_config_pack_apply')
code+=r'''
int main(void){
 cJSON_Hooks hooks={json_malloc,free};cJSON_InitHooks(&hooks);
 cJSON *params=cJSON_Parse("{\"path\":\"/synthetic/old.tscfg\",\"device\":\"matrix\",\"index\":1,\"count\":2,\"color\":\"#010203\"}");assert(params);
 ts_api_result_t result={0};
 cJSON *modules=(cJSON*)(uintptr_t)1;ts_config_pack_result_t rc=ts_config_pack_apply_file("/synthetic/old.tscfg",&modules);
 check(rc==TS_CONFIG_PACK_ERR_UNSUPPORTED,"core rejects unimplemented module application");check(modules==NULL,"module names are not claimed applied");check(opens==0&&loads==0&&module_writes==0,"unsupported apply opens/decrypts/applies nothing");
 if(modules&&(uintptr_t)modules!=1)cJSON_Delete(modules);
 check(api_config_pack_apply(params,&result)==ESP_ERR_NOT_SUPPORTED&&result.code==TS_API_ERR_NOT_SUPPORTED,"API reports unsupported instead of success");
 check(result.message&&!strcmp(result.message,"pack_apply_unsupported"),"specific reason reaches client");check(!result.data||!cJSON_IsTrue(cJSON_GetObjectItem(result.data,"success")),"no false apply success payload");ts_api_result_free(&result);
 check(!strcmp(ts_config_pack_reason(TS_CONFIG_PACK_ERR_UNSUPPORTED),"pack_apply_unsupported")&&strstr(ts_config_pack_strerror(TS_CONFIG_PACK_ERR_UNSUPPORTED),"not supported"),"core reason and detail defined");
 modules=(cJSON*)(uintptr_t)1;check(ts_config_pack_apply_file(NULL,&modules)==TS_CONFIG_PACK_ERR_INVALID_ARG&&modules==NULL,"null path has no stale applied output");
 s_initialized=false;modules=(cJSON*)(uintptr_t)1;check(ts_config_pack_apply_file("x",&modules)==TS_CONFIG_PACK_ERR_NOT_INIT&&modules==NULL,"uninitialized has no stale applied output");s_initialized=true;
 check(api_config_pack_apply(NULL,&result)==ESP_ERR_INVALID_ARG&&result.code==TS_API_ERR_INVALID_ARG,"missing path keeps argument error");ts_api_result_free(&result);
 /* Failure to allocate optional detail must never turn rejection into success. */
 fail_json=true;check(api_config_pack_apply(params,&result)==ESP_ERR_NOT_SUPPORTED&&result.code==TS_API_ERR_NOT_SUPPORTED,"JSON OOM retains apply rejection");ts_api_result_free(&result);
 fail_json=false;
 cJSON_Delete(params);printf("%u checks, %u failures: actual unsupported pack API/core; no file/decrypt/application side effects; LED0.6.4 positive and invalid-range checks in test_led_controls.py; synthetic old decrypted payload is not PKI evidence\n",checks,failures);return failures?1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='ts-unimplemented-') as tmp:
    c=Path(tmp)/'probe.c';exe=Path(tmp)/'probe';c.write_text(code)
    includes=[ROOT/'tests/runtime/rule_pack/stubs']
    includes += [ROOT/('components/'+module+'/include') for module in ['ts_api','ts_drivers','ts_led','ts_net','ts_config_pack','ts_storage','ts_security','ts_core/ts_config']]
    includes += [IDF/'components/json/cJSON']
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in includes],str(c),str(IDF/'components/json/cJSON/cJSON.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=20)
