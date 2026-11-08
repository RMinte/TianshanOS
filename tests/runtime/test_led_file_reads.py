#!/usr/bin/env python3
"""Three real LED session callbacks and registration + shared storage/FAT classification.
LED/image/config business operations are counters; real business positive tests remain
in the existing LED runtime suite. No actual device or private files.
"""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
f=ROOT/'tests/runtime/test_external_storage_reads.py'
t=f.read_text();ns={'__file__':str(f),'__name__':'led_file_fixture'}
exec(compile(t[:t.index('\nwith tempfile.TemporaryDirectory')],str(f),'exec'),ns)
code=ns['code'].replace('int main(void)','static int storage_cases(void)')
source,function,IDF=ns['source'],ns['function'],ns['IDF']
led=source('components/ts_api/src/ts_api_led.c')
start=led.index('static const ts_api_endpoint_t led_endpoints[]');table=led[start:led.index('\n};',start)+3]
code+='\n#define TS_LED_CC_SDCARD_JSON_PATH "/sdcard/config/led_color_correction.json"\n'
for name in set(re.findall(r'\.handler\s*=\s*(\w+)',table)):code+='#define '+name+' readonly\n'
if 'static esp_err_t led_read_for_session(' in led:
 for name in ['led_read_for_session','led_image_for_session','led_qrcode_for_session','led_cc_import_for_session']:code+=function(led,name)
code+=table+'\n'+function(led,'ts_api_led_register')
count=len(re.findall(r'\.name\s*=',table))
code+=r"""
static void consumer(const char *name,const char *path,unsigned sid,bool private_path,bool invalid){
 cJSON *p=cJSON_CreateObject();if(path)cJSON_AddStringToObject(p,!strcmp(name,"led.qrcode")?"bg_image":"path",path);
 cJSON_AddStringToObject(p,"role","root");cJSON_AddNumberToObject(p,"session_id",4);cJSON_AddBoolToObject(p,"internal",true);
 unsigned before=reads,stat_before=lookups;ts_api_result_t result={0};active_sid=sid;
 esp_err_t ret=ts_api_call_for_session(name,p,sid,&result);
 bool valid=sid==3||sid==4,allowed=valid&&!invalid&&(!private_path||sid>=3);
 check((ret==ESP_OK&&result.code==TS_API_OK)==allowed,"verified identity/private path controls LED consumer");
 check(reads==before+allowed,"refused consumer cannot stop animation, open file, change calibration or hardware");
 if(!valid)check(lookups==stat_before,"non-Admin denied before FAT metadata");
 if(valid&&private_path&&sid<3&&!invalid)check(result.code==TS_API_ERR_NO_PERMISSION&&!strcmp(result.message,"Administrator permission required"),"private consumer returns standard Root upgrade reason");
 ts_api_result_free(&result);cJSON_Delete(p);
}
int main(void){
 if(storage_cases()!=0)return 1;
 check(ts_api_init()==ESP_OK&&ts_api_led_register()==ESP_OK,"actual LED registrations initialize");
"""+f' cJSON *list=ts_api_list(TS_API_CAT_LED);check(cJSON_GetArraySize(list)=={count},"complete LED table retained");cJSON_Delete(list);\n'+r"""
 const char *names[]={"led.image","led.qrcode","led.color_correction.import"};
 const char *private_paths[]={"/sdcard/config/wifi.json","/sdcard/CONFIG/key.bin","/sdcard/confıg/file","/sdcard/CONFID~1/file","/sdcard/config. /file","/sdcard/foo/../config/file","/sdcard/pki/ca-chain.crt","/sdcard/PKIPRI~1/key","/spiffs/private","/www/image.bmp","/autocfg/state.json"};
 const char *ordinary[]={"/sdcard/images/a.bmp","/sdcard/自定义/a.gif","/sdcard/config/../images/a.bmp","/sdcard/missing/a.json"};
 const char *invalid[]={"/unknown/a","/sdcard-other/a","relative/a","/sdcard/config\t/a","/"};
 unsigned identities[]={0,1,2,3,4,5,99};
 for(unsigned i=0;i<7;i++)for(unsigned n=0;n<3;n++){
  unsigned sid=identities[i];ts_api_access_t access;check(ts_api_get_access(names[n],&access)==ESP_OK&&access.minimum_level==TS_PERM_ADMIN&&access.allowed_methods==TS_API_METHOD_WRITE,"all three retain Admin/write metadata with dynamic private upgrade");
  for(unsigned p=0;p<sizeof(private_paths)/sizeof(private_paths[0]);p++)consumer(names[n],private_paths[p],sid,true,false);
  for(unsigned p=0;p<sizeof(ordinary)/sizeof(ordinary[0]);p++)consumer(names[n],ordinary[p],sid,false,false);
  for(unsigned p=0;p<sizeof(invalid)/sizeof(invalid[0]);p++)consumer(names[n],invalid[p],sid,false,true);
  if(n==2)consumer(names[n],NULL,sid,true,false);
  if(n==1){consumer(names[n],NULL,sid,false,false);consumer(names[n],"",sid,false,false);}
 }
 lookup_error=true;for(unsigned n=0;n<3;n++)consumer(names[n],"/sdcard/CONFID~1/file",4,true,true);lookup_error=false;
 revoke_during_lookup=true;for(unsigned n=0;n<3;n++){
  revoked_sid=0;active_sid=4;unsigned before=reads;ts_api_result_t r={0};cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,n==1?"bg_image":"path","/sdcard/CONFID~1/file");
  check(ts_api_call_for_session(names[n],p,4,&r)!=ESP_OK&&reads==before,"revocation during FAT resolution prevents every consumer side effect");ts_api_result_free(&r);cJSON_Delete(p);
 }revoke_during_lookup=false;revoked_sid=0;
 for(unsigned n=0;n<3;n++){
  unsigned before=reads;ts_api_result_t r={0};check(ts_api_call(names[n],NULL,&r)==ESP_OK&&reads==before+1,"trusted internal producer retains original handler");ts_api_result_free(&r);
 }
 printf("%u checks, %u failures: three real LED session callbacks/table, real registry/classifier/FAT resolver; synthetic LED/SDK/FAT effects\n",checks,failures);ts_api_deinit();return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix='ts-led-file-reads-') as tmp:
 c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
 inc=[ROOT/'tests/runtime/storage_stubs',ROOT/'components/ts_api/include',ROOT/'components/ts_security/include',ROOT/'components/ts_core/ts_config/include',ROOT/'components/ts_storage/include',IDF/'components/json/cJSON']
 subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in inc],str(c),str(IDF/'components/json/cJSON/cJSON.c'),'-pthread','-o',str(exe)],check=True,cwd=ROOT)
 subprocess.run([str(exe)],check=True,cwd=ROOT)
