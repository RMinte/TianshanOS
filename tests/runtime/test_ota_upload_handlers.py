"""Actual original-format APP/WWW HTTP handlers; real cJSON, synthetic SD/SDK flash.
Checks API admission, complete bodies, SDK/cache faults and preservation of the old recovery set.
Never opens a device or a real /sdcard path.
"""
from pathlib import Path
from test_console_lifecycle import RTOS, IDF, body, run_native
root=Path(__file__).resolve().parents[2]
code=RTOS+r"""
#include "cJSON.h"
#include "ts_ota.h"
#include "esp_partition.h"
#include "ts_security.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include <stdatomic.h>
#include <errno.h>
#define OTA_UPLOAD_REBOOT_DELAY_MS 2000
#define ESP_ERR_OTA_VALIDATE_FAILED 0x1503
#define ESP_ERR_NOT_ALLOWED 0x10d
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_SPIFFS 0x82
#define TS_OTA_RECOVERY_FIRMWARE_PATH "/sdcard/recovery/TianShanOS.bin"
#define TS_OTA_RECOVERY_WWW_PATH "/sdcard/recovery/www.bin"
static bool busy,no_reboot,send_failure,deny_auth,cache_failure,manifest_failure;
static unsigned writes,installs,renames,replacements,manifests,reboots,unlinks,status,allocations,fail_allocation,checks;
static esp_err_t write_result,install_result;
static unsigned cache[2];
static bool pending_exists;
static char response[512];
static const esp_partition_t target={.size=4096};
typedef struct {int content_len;} raw_request_t;
typedef struct {raw_request_t *req;void *body;size_t body_len;} ts_http_request_t;
static void *json_malloc(size_t n){return ++allocations==fail_allocation?NULL:malloc(n);}
static esp_err_t check_auth(ts_http_request_t *r,uint32_t *sid,ts_perm_level_t level){(void)r;assert(level==TS_PERM_ADMIN);*sid=11;return deny_auth?ESP_ERR_NOT_ALLOWED:ESP_OK;}
static const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p){assert(!p);return &target;}
static const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label){assert(type==1&&subtype==0x82&&!strcmp(label,"www"));return &target;}
static bool is_browser_ota_busy(void){return busy;}
static esp_err_t ts_http_get_query_param(ts_http_request_t *r,const char *key,char *out,size_t n){(void)r;assert(!strcmp(key,"auto_reboot"));snprintf(out,n,"%s",no_reboot?"false":"true");return ESP_OK;}
static esp_err_t ts_http_send_error(ts_http_request_t *r,int code,const char *why){(void)r;status=code;snprintf(response,sizeof(response),"%s",why);return ESP_OK;}
static esp_err_t ts_http_send_json(ts_http_request_t *r,int code,const char *json){(void)r;status=code;snprintf(response,sizeof(response),"%s",json);return send_failure?ESP_FAIL:ESP_OK;}
static void schedule_reboot_after_response(uint32_t delay){assert(delay==2000);reboots++;}
static esp_err_t ts_storage_mkdir_p(const char *path){assert(!strcmp(path,"/sdcard/recovery"));return ESP_OK;}
static esp_err_t ts_storage_write_file(const char *path,const void *bytes,size_t n){assert(strstr(path,"-upload.bin")&&bytes&&n);writes++;pending_exists=true;return write_result;}
esp_err_t ts_ota_flash_file_sync(const char *path,bool downgrade,ts_ota_progress_cb_t cb,void *ctx){assert(strstr(path,".firmware-upload.bin")&&downgrade&&!cb&&!ctx);installs++;return install_result;}
esp_err_t ts_ota_www_flash_file_sync(const char *path,ts_ota_progress_cb_t cb,void *ctx){assert(strstr(path,".www-upload.bin")&&!cb&&!ctx);installs++;return install_result;}
static bool ts_storage_exists(const char *path){assert(!strcmp(path,TS_OTA_RECOVERY_FIRMWARE_PATH));return true;}
esp_err_t ts_ota_write_recovery_manifest_from_files(bool www,bool force,bool armed){(void)www;assert(!force&&!armed);manifests++;return manifest_failure?ESP_FAIL:ESP_OK;}
/* FAT rename cannot overwrite either existing recovery image. */
static int fake_rename(const char *from,const char *to){assert(strstr(from,"-upload.bin"));unsigned kind=!strcmp(to,TS_OTA_RECOVERY_WWW_PATH);assert(kind||!strcmp(to,TS_OTA_RECOVERY_FIRMWARE_PATH));renames++;if(cache[kind]){errno=EEXIST;return -1;}cache[kind]=2;pending_exists=false;return 0;}
static esp_err_t ts_storage_replace_file(const char *path,const void *bytes,size_t n){unsigned kind=!strcmp(path,TS_OTA_RECOVERY_WWW_PATH);assert(kind||!strcmp(path,TS_OTA_RECOVERY_FIRMWARE_PATH));assert(bytes&&n&&pending_exists&&installs==1);replacements++;if(cache_failure)return ESP_FAIL;cache[kind]=2;return ESP_OK;}
static int fake_unlink(const char *path){assert(strstr(path,"-upload.bin"));unlinks++;pending_exists=false;return 0;}
#define rename fake_rename
#define unlink fake_unlink
"""
source=root/'components/ts_webui/src/ts_webui_api.c'
for name in ['is_binary_upload_complete','ota_upload_reply','ota_firmware_upload_handler','ota_www_upload_handler']:code+=body(source,name)
code+=r"""
static void check(bool ok,const char *why){checks++;if(!ok){fprintf(stderr,"FAIL %s HTTP %u\n",why,status);abort();}}
static void reset(void){busy=no_reboot=send_failure=deny_auth=cache_failure=manifest_failure=false;writes=installs=renames=replacements=manifests=reboots=unlinks=status=allocations=fail_allocation=0;write_result=install_result=ESP_OK;response[0]=0;cache[0]=cache[1]=1;pending_exists=false;}
int main(void){
 cJSON_Hooks hooks={json_malloc,free};cJSON_InitHooks(&hooks);
 uint8_t bytes[1024]={ESP_IMAGE_HEADER_MAGIC};esp_app_desc_t app={.magic_word=ESP_APP_DESC_MAGIC_WORD};memcpy(bytes+sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t),&app,sizeof(app));
 raw_request_t raw={sizeof(bytes)};ts_http_request_t req={&raw,bytes,sizeof(bytes)};
 esp_err_t (*handlers[])(ts_http_request_t*,void*)={ota_firmware_upload_handler,ota_www_upload_handler};
 for(unsigned kind=0;kind<2;kind++){
  reset();handlers[kind](&req,NULL);check(status==200&&writes==1&&installs==1&&!renames&&replacements==1&&cache[kind]==2&&cache[!kind]==1&&manifests==1&&reboots==(kind==0)&&unlinks==1&&!pending_exists,"existing APP/WWW cache replaced through owner under FAT rename semantics");unsigned allocated=allocations;
  for(unsigned fail=1;fail<=allocated;fail++){reset();fail_allocation=fail;handlers[kind](&req,NULL);check(status==500&&!writes&&!installs&&!renames&&!replacements&&!manifests&&!reboots,"all acknowledgement allocation faults precede persistent effects");}
  reset();deny_auth=true;handlers[kind](&req,NULL);check(status==403&&!writes&&!installs&&!allocations,"real admin identity required before effects");
  reset();raw.content_len++;handlers[kind](&req,NULL);check(status==400&&!writes&&!installs&&!allocations,"truncated upload rejected before persistence");raw.content_len--;
  reset();req.body_len=5000;raw.content_len=5000;handlers[kind](&req,NULL);check(status==400&&!writes&&!installs,"capacity guard precedes header access/write");req.body_len=raw.content_len=sizeof(bytes);
  reset();busy=true;handlers[kind](&req,NULL);check(status==409&&!writes&&!installs,"active original worker still blocks browser upload");
  reset();write_result=ESP_FAIL;handlers[kind](&req,NULL);check(status==500&&writes==1&&!installs&&!renames&&!replacements&&!manifests&&!reboots,"staging failure preserves old recovery materials");
  const esp_err_t faults[]={ESP_ERR_OTA_VALIDATE_FAILED,ESP_ERR_INVALID_SIZE,ESP_ERR_INVALID_STATE,ESP_FAIL};
  for(unsigned i=0;i<4;i++){reset();install_result=faults[i];handlers[kind](&req,NULL);check(status==(i<2?400:i==2?409:500)&&installs==1&&!renames&&!replacements&&!manifests&&!reboots&&unlinks==1&&cache[kind]==1&&cache[!kind]==1&&!pending_exists,"SDK validation/write failure cannot replace old files or manifest");}
  for(unsigned i=0;i<2;i++){reset();cache_failure=!i;manifest_failure=i;handlers[kind](&req,NULL);check(status==409&&installs==1&&!renames&&replacements==1&&cache[kind]==(i?2:1)&&cache[!kind]==1&&manifests==i&&!reboots&&unlinks==1&&!pending_exists&&strstr(response,"flash_written")&&strstr(response,"partial"),"post-flash cache failure is explicit partial result, no automatic restart");}
  reset();no_reboot=true;handlers[kind](&req,NULL);check(status==200&&installs==1&&!reboots,"original APP+WWW no-reboot sequence preserved");
  reset();send_failure=true;check(handlers[kind](&req,NULL)==ESP_FAIL&&installs==1&&!reboots,"lost HTTP acknowledgement cannot auto-reboot");
 }
 reset();bytes[0]=0;ota_firmware_upload_handler(&req,NULL);check(status==400&&!writes&&!installs,"invalid original firmware magic cannot affect old recovery set");
 cJSON_InitHooks(NULL);printf("%u checks: actual original-format APP/WWW handlers; cJSON allocation and SD/SDK/cache failure boundaries, no device\n",checks);
}
"""
headers={'esp_partition.h':'#pragma once\n#include <stdint.h>\ntypedef struct {char label[17];uint32_t address,size;} esp_partition_t;\n','esp_app_format.h':(IDF/'components/bootloader_support/include/esp_app_format.h').read_text(),'esp_app_desc.h':(IDF/'components/esp_app_format/include/esp_app_desc.h').read_text(),'esp_assert.h':'#pragma once\n#define ESP_STATIC_ASSERT _Static_assert\n','esp_attr.h':'#pragma once\n#define FORCE_INLINE_ATTR static inline\n'}
run_native(code,[IDF/'components/json/cJSON/cJSON.c'],headers,[root/'components/ts_ota/include',root/'components/ts_security/include',root/'components/ts_core/ts_event/include'])
