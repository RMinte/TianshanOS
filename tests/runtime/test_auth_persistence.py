#!/usr/bin/env python3
"""Production credential/session code and SDK crypto; fault-injected memory NVS."""
from pathlib import Path
import argparse,os,re,subprocess,tempfile
from zipfile import ZipFile
from host_paths import sdk_library_dir

ROOT=Path(__file__).resolve().parents[2]
IDF=Path(os.environ.get("IDF_PATH","/Users/massif/esp/v5.5.2/esp-idf"))
parser=argparse.ArgumentParser();parser.add_argument("--source-ref");parser.add_argument("--source-package");parser.add_argument("--case",choices=["persistence","revocation","lockout","tokens","session_race"],default="persistence");args=parser.parse_args()
def source(path):
    if args.source_package:
        with ZipFile(args.source_package) as z:
            if "current-source/"+path in z.namelist():return z.read("current-source/"+path).decode()
        return subprocess.check_output(["git","show",f"35e081ac8:{path}"],cwd=os.environ.get("SOURCE_REPOSITORY",str(ROOT)),text=True)
    return subprocess.check_output(["git","show",f"{args.source_ref}:{path}"],cwd=ROOT,text=True) if args.source_ref else (ROOT/path).read_text()
code=r'''
#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_security.h"
#include "ts_auth_layout.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ts_crypto.h"
#include "ts_api.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/platform_util.h"
#define TS_LOGI(...) ((void)0)
#define TS_LOGW(...) ((void)0)
#define TS_LOGE(...) ((void)0)
#define TS_MALLOC_PSRAM malloc
#define TS_CONFIG_KEYS_PARTITION "keys"
#define NVS_READWRITE 1
#define ESP_ERR_NVS_NOT_FOUND 0x1102
typedef int nvs_handle_t;
static ts_auth_user_credential_t disk[2],backup[2];
static size_t disk_size[2],backup_size[2];
static bool present[2],version_present,handles[16];
static uint8_t version;
#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1
typedef struct {int unused;} esp_partition_t;
typedef enum {ESP_OTA_IMG_NEW,ESP_OTA_IMG_PENDING_VERIFY,ESP_OTA_IMG_VALID,ESP_OTA_IMG_INVALID,ESP_OTA_IMG_UNDEFINED} esp_ota_img_states_t;
static bool trial_boot,boot_state_error;
static const esp_partition_t *esp_ota_get_running_partition(void){static esp_partition_t p;return &p;}
static int esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *state){(void)p;*state=trial_boot?ESP_OTA_IMG_PENDING_VERIFY:ESP_OTA_IMG_VALID;return boot_state_error?ESP_FAIL:ESP_OK;}
static int next_handle,writes,commits,fail_read=-1,fail_set=-1,bad_size=-1,fail_commit_at;
static bool fail_version_read,fail_version_set,fail_version_readback;
static int fail_blob_readback=-1;
static unsigned checks,failures;
static uint32_t random_state=123;
static void check(bool ok,const char *label){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",label);}}
static const char *esp_err_to_name(int e){(void)e;return "fixture";}
static int64_t clock_us=1000000;
static int64_t esp_timer_get_time(void){return clock_us;}
static void (*random_hook)(void);
static size_t random_length;static uint8_t random_bytes[64];static bool random_zero;
static void esp_fill_random(void *p,size_t n){if(random_hook)random_hook();for(size_t i=0;i<n;i++){random_state=random_state*1664525+1013904223;((uint8_t*)p)[i]=random_zero?0:random_state>>24;}random_length=n;assert(n<=sizeof(random_bytes));memcpy(random_bytes,p,n);}
static bool valid_handle(nvs_handle_t h){return h>0&&h<16&&handles[h];}
static int index_of(const char *k){if(!strcmp(k,"cred_admin"))return 0;assert(!strcmp(k,"cred_root"));return 1;}
static int nvs_open(const char *n,int mode,nvs_handle_t *out){(void)n;(void)mode;assert(next_handle<15);*out=++next_handle;handles[*out]=true;return ESP_OK;}
static int nvs_open_from_partition(const char *p,const char *n,int mode,nvs_handle_t *out){(void)p;return nvs_open(n,mode,out);}
static void nvs_close(nvs_handle_t h){assert(valid_handle(h));handles[h]=false;}
static int nvs_get_blob(nvs_handle_t h,const char *k,void *out,size_t *len){
 if(!valid_handle(h))return ESP_ERR_INVALID_STATE;int i=index_of(k);
 if(i==fail_read){fail_read=-1;return ESP_FAIL;}if(!present[i])return ESP_ERR_NVS_NOT_FOUND;
 size_t size=disk_size[i]?disk_size[i]:sizeof(disk[i]);if(i==bad_size)--size;assert(*len>=size);
 memcpy(out,&disk[i],size);*len=size;return ESP_OK;
}
static int nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t n){
 if(!valid_handle(h))return ESP_ERR_INVALID_STATE;int i=index_of(k);assert(n==sizeof(disk[i])||n==TS_AUTH_LEGACY_CREDENTIAL_SIZE);
 if(i==fail_set){fail_set=-1;return ESP_FAIL;}memset(&disk[i],0,sizeof(disk[i]));memcpy(&disk[i],p,n);disk_size[i]=n;present[i]=true;writes++;if(fail_blob_readback==i){fail_blob_readback=-1;fail_read=i;}return ESP_OK;
}
/* Deliberately adverse: a failed commit does not roll back an already visible set. */
static int nvs_commit(nvs_handle_t h){assert(valid_handle(h));commits++;return fail_commit_at==commits?ESP_FAIL:ESP_OK;}
static int nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *out){
 (void)k;assert(valid_handle(h));if(fail_version_read){fail_version_read=false;return ESP_FAIL;}
 if(!version_present)return ESP_ERR_NVS_NOT_FOUND;*out=version;return ESP_OK;
}
static int nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v){
 (void)k;assert(valid_handle(h));if(fail_version_set)return ESP_FAIL;version=v;version_present=true;if(fail_version_readback){fail_version_readback=false;fail_version_read=true;}return ESP_OK;
}
static int nvs_erase_key(nvs_handle_t h,const char *k){(void)h;(void)k;return ESP_OK;}
static int ts_ssh_hosts_config_init(void){return ESP_OK;}
static int ts_ssh_commands_config_init(void){return ESP_OK;}
static int ts_ssh_service_init(void){return ESP_OK;}
static int ts_ssh_log_watch_init(void){return ESP_OK;}
'''
if args.case in ("revocation", "tokens"):
    code=code.replace("#define TS_LOGI(...) ((void)0)",r'''
#include <stdarg.h>
static char body_diagnostic[1024];static size_t body_diagnostic_used;static bool body_diagnostic_enabled;
static void body_log(const char *tag,const char *format,...){
 (void)tag;if(!body_diagnostic_enabled||body_diagnostic_used>=sizeof(body_diagnostic)-1)return;
 va_list ap;va_start(ap,format);int n=vsnprintf(body_diagnostic+body_diagnostic_used,sizeof(body_diagnostic)-body_diagnostic_used,format,ap);va_end(ap);
 if(n>0)body_diagnostic_used+=(size_t)n<sizeof(body_diagnostic)-body_diagnostic_used?(size_t)n:sizeof(body_diagnostic)-body_diagnostic_used-1;
}
#define TS_LOGI(...) body_log(__VA_ARGS__)
''')
crypto=source("components/ts_security/src/ts_crypto.c")
for name in ["ts_crypto_hash","ts_crypto_base64_encode","ts_crypto_base64_decode"]:
    start=crypto.index("esp_err_t "+name+"(");code+=crypto[start:crypto.index("\n}",start)+2]+"\n"
code+=r'''
static pthread_mutex_t race_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_changed=PTHREAD_COND_INITIALIZER;
static bool login_paused,release_login,setter_at_gate,setter_done,observe_gate;
static _Thread_local bool is_setter;
static int observed_take(SemaphoreHandle_t gate,unsigned timeout){
 if(observe_gate&&is_setter){pthread_mutex_lock(&race_lock);setter_at_gate=true;pthread_cond_broadcast(&race_changed);pthread_mutex_unlock(&race_lock);}
 return xSemaphoreTake(gate,timeout);
}
#define xSemaphoreTake observed_take
'''
if args.case=="session_race":
    code+=r"""
#include <time.h>
static pthread_mutex_t slot_race_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t slot_race_changed=PTHREAD_COND_INITIALIZER;
static bool slot_matched,slot_worker_waiting,slot_worker_finished,slot_hook_armed;
static _Thread_local bool slot_recycler;
static _Thread_local unsigned slot_lock_depth;
static atomic_uint slot_logs_under_gate;
static void slot_log(const char *tag,const char *message,...){(void)tag;(void)message;if(slot_lock_depth)atomic_fetch_add(&slot_logs_under_gate,1);}
#undef TS_LOGI
#undef TS_LOGW
#undef TS_LOGE
#define TS_LOGI slot_log
#define TS_LOGW slot_log
#define TS_LOGE slot_log
static int slot_take(SemaphoreHandle_t gate,unsigned timeout){
 if(slot_recycler){pthread_mutex_lock(&slot_race_lock);slot_worker_waiting=true;pthread_cond_broadcast(&slot_race_changed);pthread_mutex_unlock(&slot_race_lock);}
 int ret=xSemaphoreTakeRecursive(gate,timeout);if(ret)++slot_lock_depth;return ret;
}
static int slot_give(SemaphoreHandle_t gate){assert(slot_lock_depth);--slot_lock_depth;return xSemaphoreGiveRecursive(gate);}
#define xSemaphoreTakeRecursive slot_take
#define xSemaphoreGiveRecursive slot_give
static void wait_slot_flag(bool *flag){
 struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=5;
 while(!*flag)assert(pthread_cond_timedwait(&slot_race_changed,&slot_race_lock,&until)==0);
}
static void token_match_boundary(void){
 if(!slot_hook_armed)return;slot_hook_armed=false;
 pthread_mutex_lock(&slot_race_lock);slot_matched=true;pthread_cond_broadcast(&slot_race_changed);
 if(slot_lock_depth)wait_slot_flag(&slot_worker_waiting);else wait_slot_flag(&slot_worker_finished);
 pthread_mutex_unlock(&slot_race_lock);
}
"""
if "ts_auth_credential_create" in source("components/ts_security/src/ts_auth.c"):
    code+='\n'+re.sub(r"^#include[^\n]*\n","",source("components/ts_auth_layout/src/ts_auth_credential.c"),flags=re.M)
for filename in ["ts_security.c","ts_auth.c"]:
    implementation=source("components/ts_security/src/"+filename)
    if args.case=="session_race" and filename=="ts_security.c":
        anchor='            if (diff != 0) continue;'
        assert implementation.count(anchor)==1
        implementation=implementation.replace(anchor,anchor+'\n            token_match_boundary();')
    code+="\n#undef TAG\n"+re.sub(r"^#include[^\n]*\n","",implementation,flags=re.M)
if args.case in ("revocation","lockout"):
    code+=r'''
esp_err_t ts_api_register(const ts_api_endpoint_t *e){(void)e;return ESP_OK;}
void ts_api_result_ok(ts_api_result_t *r,cJSON *data){r->code=TS_API_OK;r->data=data;}
void ts_api_result_error(ts_api_result_t *r,ts_api_result_code_t code,const char *m){r->code=code;r->message=strdup(m);}
'''
    code+='\n#undef TAG\n'+re.sub(r"^#include[^\n]*\n","",source("components/ts_api/src/ts_api_auth.c"),flags=re.M)
if args.case in ("revocation","lockout"):
    code+=r'''
/* Keep the real login and HTTP handler, inject revocation at their boundary. */
typedef struct {const char *body;size_t body_len;} ts_http_request_t;
static int http_status;static cJSON *http_json;static bool revoke_after_login;static char http_error[160];
static esp_err_t ts_http_send_error(ts_http_request_t *r,int status,const char *m){(void)r;snprintf(http_error,sizeof(http_error),"%s",m);http_status=status;return ESP_OK;}
static esp_err_t ts_http_send_json(ts_http_request_t *r,int status,const char *json){(void)r;http_status=status;http_json=cJSON_Parse(json);return ESP_OK;}
static esp_err_t login_boundary(const char *user,const char *password,uint32_t *sid,char *token,size_t size){
 esp_err_t ret=ts_auth_login(user,password,sid,token,size);
 if(ret==ESP_OK&&revoke_after_login)assert(ts_auth_logout(*sid)==ESP_OK);
 return ret;
}
#define ts_auth_login login_boundary

'''
    web=source("components/ts_webui/src/ts_webui_api.c")
    start=web.index("static esp_err_t login_handler(")
    code+=web[start:web.index("\n}",start)+2]+"\n#undef ts_auth_login\n"
code+=r'''
/* Fixed reader/init from committed 0.6.4 120f0c79b, independent of current codec. */
typedef struct {uint8_t salt[16],hash[32];bool password_changed;uint32_t failed_attempts,lockout_until;} stock_credential_t;
_Static_assert(sizeof(stock_credential_t)==60,"stock 0.6.4 layout");
static nvs_handle_t stock_nvs;static bool stock_initialized;static unsigned stock_resets;
static esp_err_t stock_force_create_user(const char *name,ts_perm_level_t level){(void)name;(void)level;stock_resets++;return ESP_FAIL;}
static esp_err_t stock_load_user_credential(const char *username, stock_credential_t *cred)
{
    char key[32];
    snprintf(key, sizeof(key), "cred_%s", username);

    size_t len = sizeof(stock_credential_t);
    esp_err_t ret = nvs_get_blob(stock_nvs, key, cred, &len);

    return ret;
}
static esp_err_t stock_save_user_credential(const char *username, const stock_credential_t *cred)
{
    char key[32];
    snprintf(key, sizeof(key), "cred_%s", username);

    esp_err_t ret = nvs_set_blob(stock_nvs, key, cred, sizeof(stock_credential_t));
    if (ret == ESP_OK) {
        nvs_commit(stock_nvs);
    }
    return ret;
}
static esp_err_t stock_init_user_if_needed(const char *username, ts_perm_level_t level)
{
    stock_credential_t cred;

    if (stock_load_user_credential(username, &cred) == ESP_OK) {
        /* 开机计时相关的临时锁定不能沿用到本次启动。 */
        if (cred.failed_attempts || cred.lockout_until) {
            cred.failed_attempts = 0;
            cred.lockout_until = 0;
            return stock_save_user_credential(username, &cred);
        }
        return ESP_OK;
    }

    return stock_force_create_user(username, level);
}
esp_err_t stock_auth_init(void)
{
    if (stock_initialized) return ESP_OK;

    esp_err_t ret = nvs_open(TS_AUTH_NVS_NAMESPACE, NVS_READWRITE, &stock_nvs);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 检查配置版本，版本变化时强制重置所有用户 */
    uint8_t stored_version = 0;
    nvs_get_u8(stock_nvs, TS_AUTH_CONFIG_VERSION_KEY, &stored_version);

    if (stored_version != TS_AUTH_CONFIG_VERSION) {
        TS_LOGW(TAG, "Auth config version changed (%d -> %d), resetting all users",
                stored_version, TS_AUTH_CONFIG_VERSION);
        /* 强制重新创建所有用户 */
        stock_force_create_user("admin", TS_PERM_ADMIN);
        stock_force_create_user("root", TS_PERM_ROOT);
        /* 保存新版本号 */
        nvs_set_u8(stock_nvs, TS_AUTH_CONFIG_VERSION_KEY, TS_AUTH_CONFIG_VERSION);
        nvs_commit(stock_nvs);
    } else {
        /* 初始化默认用户（如果不存在） */
        ret = stock_init_user_if_needed("admin", TS_PERM_ADMIN);
        if (ret == ESP_OK) ret = stock_init_user_if_needed("root", TS_PERM_ROOT);
        if (ret != ESP_OK) {
            nvs_close(stock_nvs);
            return ret;
        }
    }

    stock_initialized = true;
    TS_LOGI(TAG, "Auth module initialized (version %d)", TS_AUTH_CONFIG_VERSION);
    return ESP_OK;
}
static void original_firmware_accepts(const char *admin,const char *root){
 stock_initialized=false;stock_resets=0;int previous=writes;
 check(stock_auth_init()==ESP_OK&&!stock_resets&&version==3,"actual original firmware boots without recreating either account");
 check(writes==previous,"stock startup does not replace migrated password records");
 const char *passwords[]={admin,root};
 for(unsigned u=0;u<2;u++){stock_credential_t credential;check(stock_load_user_credential(u?"root":"admin",&credential)==ESP_OK,"original reader accepts exact 60-byte record");unsigned char bytes[80],hash[32];size_t n=strlen(passwords[u]);memcpy(bytes,credential.salt,16);memcpy(bytes+16,passwords[u],n);assert(!mbedtls_sha256(bytes,16+n,hash,0));check(!memcmp(hash,credential.hash,32),"original password algorithm authenticates the same custom password");}
 nvs_close(stock_nvs);
}
static void prototype_record(unsigned u,const char *password,bool changed){
 memset(&disk[u],0,sizeof(disk[u]));memset(disk[u].salt,31+u,16);disk[u].format=TS_AUTH_CREDENTIAL_MAGIC;disk[u].rounds=10000;disk[u].password_changed=changed;
 assert(!mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,(const unsigned char*)password,strlen(password),disk[u].salt,16,10000,32,disk[u].hash));disk_size[u]=68;present[u]=true;version=4;version_present=true;
}

static void power_cycle(void){
 memset(handles,0,sizeof(handles));next_handle=0;s_initialized=false;s_auth_initialized=false;s_auth_nvs=0;
 clock_us=1000000;writes=commits=0;fail_read=fail_set=bad_size=-1;fail_commit_at=0;fail_version_read=fail_version_set=fail_version_readback=false;fail_blob_readback=-1;
 assert(ts_security_init()==ESP_OK);
}
static void restore(void){
 memcpy(disk,backup,sizeof(disk));memcpy(disk_size,backup_size,sizeof(disk_size));present[0]=present[1]=true;version=TS_AUTH_CONFIG_VERSION;version_present=true;power_cycle();
}
static unsigned active_sessions(void){unsigned n=0;for(unsigned i=0;i<MAX_SESSIONS;i++)n+=s_sessions[i].active;return n;}
static void expect_init_failure(const char *label){
 ts_auth_user_credential_t before[2];memcpy(before,disk,sizeof(before));int count=writes;
 check(ts_auth_init()!=ESP_OK&&!s_auth_initialized,label);
 check(writes==count&&!memcmp(before,disk,sizeof(before)),"initialization error preserves credential bytes");
 check(!handles[3],"initialization error closes auth NVS");
}
static void owner_token(char token[128]){
 uint32_t sid=0;assert(ts_auth_login("root","root-fixture",&sid,token,128)==ESP_OK);
}
static void token_still_valid(const char *token){
 uint32_t sid=0;check(ts_security_validate_token(token,&sid)==ESP_OK,"existing session survives failed credential write");
}
static int persistence_tests(void){
 power_cycle();check(ts_auth_init()==ESP_OK,"empty first boot still works");
 check(version_present&&version==TS_AUTH_CONFIG_VERSION&&present[0]&&present[1],"first boot complete records");
 check(ts_auth_set_admin_password("admin-fixture")==ESP_OK,"initial admin custom password");
 check(ts_auth_set_root_password("root-fixture")==ESP_OK,"initial root custom password");
 memcpy(backup,disk,sizeof(backup));memcpy(backup_size,disk_size,sizeof(backup_size));

 restore();fail_version_read=true;expect_init_failure("version read error is not a reset");
 restore();version=2;expect_init_failure("unsupported version is not a reset");
 restore();version=0;expect_init_failure("explicit version zero is not an empty namespace");
 restore();version_present=false;expect_init_failure("missing version with credentials is not new device");
 restore();present[1]=false;expect_init_failure("missing existing root is not default provisioning");
 restore();fail_read=0;expect_init_failure("credential I/O error is not missing user");
 restore();bad_size=0;expect_init_failure("truncated startup record is not a default user");
 restore();disk[0].failed_attempts=1;fail_commit_at=1;
 check(ts_auth_init()==ESP_OK&&s_auth_initialized&&writes==0&&commits==0,"startup preserves retry state without any set/commit");
 check(!memcmp(disk[0].hash,backup[0].hash,TS_AUTH_HASH_LEN)&&!memcmp(disk[0].salt,backup[0].salt,TS_AUTH_SALT_LEN)&&disk[0].failed_attempts==1,"startup preserves password and partial retry budget");
 restore();assert(ts_auth_init()==ESP_OK);bad_size=0;user_credential_t out={0};
 check(load_user_credential("admin",&out)!=ESP_OK,"truncated blob rejected before use");bad_size=-1;

 restore();assert(ts_auth_init()==ESP_OK);char token[128];owner_token(token);
 user_credential_t original=disk[1];int committed=commits;fail_set=1;
 check(ts_auth_set_root_password("new-root-fixture")==ESP_FAIL,"set failure reported");
 check(commits==committed&&!memcmp(&disk[1],&original,sizeof(original)),"failed set does not commit");token_still_valid(token);
 fail_commit_at=commits+1;
 check(ts_auth_set_root_password("new-root-fixture")==ESP_ERR_NOT_FINISHED,"commit failure reported");
 check(memcmp(&disk[1],&original,sizeof(original))!=0,"fixture does not assume commit rollback");
 check(ts_security_validate_token(token,&(uint32_t){0})!=ESP_OK,"unconfirmed password replacement revokes the old target session");

 restore();assert(ts_auth_init()==ESP_OK);owner_token(token);fail_commit_at=commits+1;
 check(ts_auth_change_password("root","root-fixture","changed-root")==ESP_ERR_NOT_FINISHED,"own password change reports commit failure");
 check(ts_security_validate_token(token,&(uint32_t){0})!=ESP_OK,"own unconfirmed replacement requires reauthentication");
 restore();assert(ts_auth_init()==ESP_OK);fail_commit_at=commits+1;
 check(ts_auth_set_admin_password("new-admin-fixture")==ESP_ERR_NOT_FINISHED,"admin set reports commit failure");
 restore();assert(ts_auth_init()==ESP_OK);fail_commit_at=commits+1;
 check(ts_auth_reset_password("admin")==ESP_ERR_NOT_FINISHED,"explicit reset reports commit failure");


 restore();assert(ts_auth_init()==ESP_OK);owner_token(token);fail_blob_readback=1;
 check(ts_auth_set_root_password("readback-root-fixture")==ESP_ERR_NOT_FINISHED,"password readback failure reports unconfirmed write");
 check(ts_security_validate_token(token,&(uint32_t){0})!=ESP_OK,"readback failure cannot preserve pre-update target sessions");
 power_cycle();check(ts_auth_init()==ESP_OK&&ts_auth_verify_password("root","readback-root-fixture",NULL)==ESP_OK,"restart authenticates the complete blob after temporary readback failure");
 for(unsigned fault=0;fault<3;++fault){
  restore();user_credential_t old[2];memcpy(old,disk,sizeof(old));
  if(fault==0)fail_version_set=true;if(fault==1)fail_commit_at=1;if(fault==2)fail_version_readback=true;
  check(ts_auth_init()==ESP_OK&&writes==0&&commits==0&&!memcmp(old,disk,sizeof(old)),"existing v3 startup needs no format rewrite or migration commit");
 }
 restore();assert(ts_auth_init()==ESP_OK);fail_commit_at=commits+1;
 check(ts_auth_verify_password("root","wrong-password",NULL)==ESP_FAIL,"failed guess persistence error propagated");
 restore();assert(ts_auth_init()==ESP_OK);disk[1].failed_attempts=1;fail_commit_at=commits+1;
 uint32_t sid=0;unsigned sessions=active_sessions();
 check(ts_auth_login("root","root-fixture",&sid,token,sizeof(token))==ESP_FAIL,"login cannot ignore failure clearing retry state");
 check(active_sessions()==sessions,"no new session after unconfirmed credential-state write");

 restore();assert(ts_auth_init()==ESP_OK);check(writes==0,"normal reboot does not rewrite valid credentials");
 check(ts_auth_change_password("admin","admin-fixture","changed-admin")==ESP_OK,"normal own password change");
 check(ts_auth_verify_password("admin","changed-admin",NULL)==ESP_OK,"new password works");
 check(ts_auth_set_root_password("changed-root")==ESP_OK,"normal root manager set");
 check(ts_auth_verify_password("root","changed-root",NULL)==ESP_OK,"new root password works");
 check(ts_auth_reset_password("admin")==ESP_OK,"explicit admin recovery preserved");
 check(ts_auth_verify_password("admin",TS_AUTH_DEFAULT_ADMIN_PASSWORD,NULL)==ESP_OK,"explicit default recovery still works");
 check(TS_AUTH_CONFIG_VERSION==3&&TS_AUTH_USER_CREDENTIAL_SIZE==60&&sizeof(user_credential_t)==68,"stock storage layout with a private prototype read bridge");
 user_credential_t decoded,record=backup[0];
 check(!record.format&&!record.rounds&&backup_size[0]==60&&backup_size[1]==60,"all new writes are original v3 without KDF parameters");
 check(memcmp(backup[0].salt,backup[1].salt,16)!=0,"account salts remain independent");
 check(ts_auth_credential_decode(&record,60,&decoded)==ESP_OK,"stock record decodes");
 check(ts_auth_credential_decode(&record,60,&record)==ESP_OK&&!record.format,"stock in-place read preserves input");
 record.format=TS_AUTH_CREDENTIAL_MAGIC;record.rounds=TS_AUTH_KDF_MIN_ROUNDS;
 check(ts_auth_credential_decode(&record,68,&decoded)==ESP_OK,"unreleased prototype reader retained for safe conversion");
 record.rounds=TS_AUTH_KDF_MIN_ROUNDS-1;check(ts_auth_credential_decode(&record,68,&decoded)!=ESP_OK,"corrupt prototype low work factor rejected");
 record.rounds=TS_AUTH_KDF_MAX_ROUNDS+1;check(ts_auth_credential_decode(&record,68,&decoded)!=ESP_OK,"corrupt prototype excessive work factor rejected");
 record=backup[0];check(ts_auth_credential_decode(&record,68,&decoded)!=ESP_OK,"new-length missing prototype magic is not stock data");
 unsigned char malformed[68];memcpy(malformed,&backup[0],sizeof(malformed));malformed[48]=2;check(ts_auth_credential_decode(malformed,60,&decoded)!=ESP_OK,"invalid persisted bool is rejected");
 check(ts_auth_credential_decode(&backup[0],59,&decoded)!=ESP_OK&&ts_auth_credential_decode(&backup[0],67,&decoded)!=ESP_OK,"partial stock/prototype records reject");
 original_firmware_accepts(TS_AUTH_DEFAULT_ADMIN_PASSWORD,"changed-root");
  {const uint8_t expected[32]={212,31,236,154,228,114,17,184,1,92,142,6,44,49,94,67,21,255,141,119,1,230,173,255,253,176,23,97,83,106,201,180};user_credential_t v={0};for(unsigned j=0;j<16;j++)v.salt[j]=(uint8_t)j;memcpy(v.hash,expected,32);v.format=TS_AUTH_CREDENTIAL_MAGIC;v.rounds=10000;bool matches=false;check(ts_auth_credential_verify(&v,"SDK-vector-password",&matches)==ESP_OK&&matches,"real SDK PBKDF2 matches independent hashlib vector");check(ts_auth_credential_verify(&v,"wrong-vector",&matches)==ESP_OK&&!matches,"PBKDF2 rejects wrong password");}
  {const uint8_t expected[32]={114,192,85,111,253,72,251,38,65,236,125,4,125,1,1,68,181,104,21,147,215,205,217,96,167,137,230,143,135,20,208,125};user_credential_t v={0};for(unsigned j=0;j<16;j++)v.salt[j]=(uint8_t)j;memcpy(v.hash,expected,32);v.format=TS_AUTH_CREDENTIAL_MAGIC;v.rounds=10001;bool matches=false;check(ts_auth_credential_verify(&v,"SDK-vector-password",&matches)==ESP_OK&&matches,"real SDK PBKDF2 matches independent hashlib vector");check(ts_auth_credential_verify(&v,"wrong-vector",&matches)==ESP_OK&&!matches,"PBKDF2 rejects wrong password");}
  {const uint8_t expected[32]={138,197,225,213,184,103,26,60,134,180,128,201,237,144,68,3,129,144,131,34,153,106,64,225,4,153,105,234,27,208,156,144};user_credential_t v={0};for(unsigned j=0;j<16;j++)v.salt[j]=(uint8_t)j;memcpy(v.hash,expected,32);v.format=TS_AUTH_CREDENTIAL_MAGIC;v.rounds=200000;bool matches=false;check(ts_auth_credential_verify(&v,"SDK-vector-password",&matches)==ESP_OK&&matches,"real SDK PBKDF2 matches independent hashlib vector");check(ts_auth_credential_verify(&v,"wrong-vector",&matches)==ESP_OK&&!matches,"PBKDF2 rejects wrong password");}
 /* Trial boots retain stock password updates and do not alter the record ABI. */
 restore();trial_boot=true;check(ts_auth_init()==ESP_OK&&version==3&&writes==0,"trial boot retains original storage");
 check(ts_auth_set_root_password("trial-new-password")==ESP_OK&&disk_size[1]==60&&version==3,"trial UI can still change password without incompatible format");
 original_firmware_accepts("admin-fixture","trial-new-password");trial_boot=false;
 /* Already-written prototype defaults convert only after a real verifier match. */
 restore();prototype_record(0,TS_AUTH_DEFAULT_ADMIN_PASSWORD,false);prototype_record(1,TS_AUTH_DEFAULT_ROOT_PASSWORD,false);power_cycle();
 check(ts_auth_init()==ESP_OK&&version==3&&disk_size[0]==60&&disk_size[1]==60,"known shared defaults return to stock format without changing password");
 original_firmware_accepts(TS_AUTH_DEFAULT_ADMIN_PASSWORD,TS_AUTH_DEFAULT_ROOT_PASSWORD);
 /* Custom prototype hashes cannot be inverted: no reset or false v3 marker. */
 restore();prototype_record(0,"proto-admin",true);prototype_record(1,"proto-root",true);power_cycle();
 check(ts_auth_init()==ESP_OK&&version==4&&writes==0,"custom prototype boot retains both records without guessing/resetting");
 check(ts_auth_verify_password("root","wrong",NULL)==ESP_ERR_INVALID_ARG&&disk_size[1]==68&&version==4,"wrong prototype password cannot migrate");
 check(ts_auth_verify_password("root","proto-root",NULL)==ESP_OK&&disk_size[1]==60&&version==4,"one verified account cannot relabel the other account");
 check(ts_auth_verify_password("admin","proto-admin",NULL)==ESP_OK&&disk_size[0]==60&&version==3,"both verified accounts complete original-format conversion");
 check(disk[0].password_changed&&disk[1].password_changed,"conversion preserves customer changed-password flags");
 original_firmware_accepts("proto-admin","proto-root");
 for(unsigned fault=0;fault<3;++fault){
  restore();prototype_record(0,"proto-admin",true);prototype_record(1,"proto-root",true);power_cycle();assert(ts_auth_init()==ESP_OK);
  assert(ts_auth_verify_password("root","proto-root",NULL)==ESP_OK&&version==4);
  if(fault==0)fail_version_set=true;if(fault==1)fail_commit_at=commits+2;if(fault==2)fail_version_readback=true;
  unsigned active=active_sessions();uint32_t migrated_sid=0;char migrated_token[128];
  check(ts_auth_login("admin","proto-admin",&migrated_sid,migrated_token,sizeof(migrated_token))!=ESP_OK&&active_sessions()==active,"failed migration marker cannot issue a session or claim completion");
  power_cycle();check(ts_auth_init()==ESP_OK&&version==3,"migration retry completes durable stock marker without resetting either account");
  original_firmware_accepts("proto-admin","proto-root");
 }



 memset(disk,0,sizeof(disk));memset(present,0,sizeof(present));version_present=false;power_cycle();fail_set=1;
 check(ts_auth_init()!=ESP_OK&&!s_auth_initialized,"partial first boot reports failure");
 user_credential_t partial[2];memcpy(partial,disk,sizeof(partial));int written=writes;
 check(ts_auth_init()!=ESP_OK&&writes==written&&!memcmp(partial,disk,sizeof(partial)),"partial bootstrap is preserved, not silently recreated");
 sid=0;check(ts_auth_login("admin",TS_AUTH_DEFAULT_ADMIN_PASSWORD,&sid,token,sizeof(token))!=ESP_OK,"failed initialization cannot issue default session");
 memset(disk,0,sizeof(disk));memset(present,0,sizeof(present));version_present=false;power_cycle();fail_version_set=true;
 check(ts_auth_init()!=ESP_OK&&!s_auth_initialized,"first boot metadata write failure reported");
 memset(disk,0,sizeof(disk));memset(present,0,sizeof(present));version_present=false;power_cycle();fail_commit_at=3;
 check(ts_auth_init()!=ESP_OK&&!s_auth_initialized,"first boot metadata commit failure reported");
 printf("%u checks, %u failures: actual auth/session/SDK crypto with adversarial NVS; no device or real credentials\n",checks,failures);
 return failures?1:0;
}
'''
if args.case=="revocation":
    code+=r'''
/* The memory NVS applies set immediately; failed commits intentionally stay visible. */
static uint32_t session_for(const char *user,const char *password,char token[128]){
 uint32_t sid=0;assert(ts_auth_login(user,password,&sid,token,128)==ESP_OK);return sid;
}
static bool valid_token(const char *token){uint32_t sid=0;return ts_security_validate_token(token,&sid)==ESP_OK;}
static void setup_users(void){
 restore();assert(ts_auth_init()==ESP_OK);
}
static void api_update(ts_api_handler_t handler,const char *token,const char *old_password,const char *new_password,bool success,bool reauth){
 cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"token",token);
 cJSON_AddStringToObject(p,"username","root");cJSON_AddStringToObject(p,"current_session","forged-keep-session");
 if(old_password)cJSON_AddStringToObject(p,"old_password",old_password);
 if(new_password)cJSON_AddStringToObject(p,"new_password",new_password);
 ts_api_result_t result={0};esp_err_t ret=handler(p,&result);
 check((ret==ESP_OK)==success&&(result.code==TS_API_OK)==success,"mutation returns actual success/failure");
 if(success){
  check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result.data,"sessions_revoked")),"API confirms target sessions revoked");
  check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result.data,"reauth_required"))==reauth,"API reauth flag follows trusted target identity");
 }
 cJSON_Delete(p);cJSON_Delete(result.data);free(result.message);
}
static char race_token[128];static int race_login_ret,race_set_ret;
static void pause_after_verify(void){
 if(is_setter)return;
 pthread_mutex_lock(&race_lock);login_paused=true;pthread_cond_broadcast(&race_changed);
 while(!release_login)pthread_cond_wait(&race_changed,&race_lock);
 pthread_mutex_unlock(&race_lock);
}
static void *racing_login(void *p){(void)p;uint32_t sid=0;race_login_ret=ts_auth_login("root","root-fixture",&sid,race_token,sizeof(race_token));return NULL;}
static void *racing_set(void *p){(void)p;is_setter=true;race_set_ret=ts_auth_set_root_password("race-new-root");pthread_mutex_lock(&race_lock);setter_done=true;pthread_cond_broadcast(&race_changed);pthread_mutex_unlock(&race_lock);return NULL;}
static int json_budget=-1;
static void *limited_json_alloc(size_t size){if(json_budget==0)return NULL;if(json_budget>0)--json_budget;return malloc(size);}

static void release_body_result(ts_api_result_t *r){cJSON_Delete(r->data);free(r->message);*r=(ts_api_result_t){0};}
static void body_token_api_tests(void){
 setup_users();char root_a[128],root_b[128],admin[128];uint32_t root_sid=session_for("root","root-fixture",root_a);
 session_for("root","root-fixture",root_b);session_for("admin","admin-fixture",admin);
 const char *bad[]={"","invalid-token","AAAA","AAAAAAAAAAAAAAAAAAAAAA=="};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
  cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"token",bad[i]);cJSON_AddStringToObject(p,"username","root");cJSON_AddNumberToObject(p,"session_id",root_sid);cJSON_AddStringToObject(p,"role","root");cJSON_AddStringToObject(p,"old_password","root-fixture");cJSON_AddStringToObject(p,"new_password","not-applied-fixture");
  ts_api_result_t result={0};unsigned active=active_sessions();int stored=writes;
  check(api_auth_status(p,&result)==ESP_OK&&cJSON_IsFalse(cJSON_GetObjectItem(result.data,"valid"))&&!cJSON_GetObjectItem(result.data,"username"),"invalid body token does not reveal an account");release_body_result(&result);
  check(api_auth_logout(p,&result)!=ESP_OK&&active_sessions()==active,"invalid body token cannot log out a forged session ID");release_body_result(&result);
  check(api_auth_change_password(p,&result)!=ESP_OK&&writes==stored,"invalid body token cannot mutate a forged account");release_body_result(&result);
  check(valid_token(root_a)&&valid_token(root_b)&&valid_token(admin),"denied independent-token workflows preserve all sessions");cJSON_Delete(p);
 }
 cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"token",admin);cJSON_AddStringToObject(p,"username","root");cJSON_AddStringToObject(p,"role","root");cJSON_AddNumberToObject(p,"session_id",root_sid);
 ts_api_result_t result={0};
 check(api_auth_status(p,&result)==ESP_OK&&cJSON_IsTrue(cJSON_GetObjectItem(result.data,"valid"))&&!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(result.data,"username")),"admin"),"status identity comes from the exact body token, not JSON role/user/SID");release_body_result(&result);
 cJSON_AddStringToObject(p,"old_password","admin-fixture");cJSON_AddStringToObject(p,"new_password","body-admin-new");
 check(api_auth_change_password(p,&result)==ESP_OK,"authenticated own password update works without caller identity in JSON");release_body_result(&result);cJSON_Delete(p);
 check(!valid_token(admin)&&valid_token(root_a)&&valid_token(root_b),"self-service change targets Admin only and revokes its sessions");
 check(ts_auth_verify_password("root","root-fixture",NULL)==ESP_OK&&ts_auth_verify_password("admin","body-admin-new",NULL)==ESP_OK,"forged username did not change Root credentials");
 p=cJSON_CreateObject();cJSON_AddStringToObject(p,"token",root_a);cJSON_AddNumberToObject(p,"session_id",123);
 body_diagnostic[0]=0;body_diagnostic_used=0;body_diagnostic_enabled=true;
 check(api_auth_logout(p,&result)==ESP_OK&&cJSON_IsTrue(cJSON_GetObjectItem(result.data,"success")),"valid token logout succeeds");release_body_result(&result);
 char sid_text[16];snprintf(sid_text,sizeof(sid_text),"%08lx",(unsigned long)root_sid);
 check(!strstr(body_diagnostic,sid_text)&&!strstr(body_diagnostic,root_a)&&!strstr(body_diagnostic,"root-fixture"),"logout diagnostic includes neither SID nor token/password");
 body_diagnostic_enabled=false;check(!valid_token(root_a)&&valid_token(root_b),"logout revokes exact token session and retains the other Root session");
 check(api_auth_status(p,&result)==ESP_OK&&cJSON_IsFalse(cJSON_GetObjectItem(result.data,"valid")),"logged-out token status is invalid");release_body_result(&result);cJSON_Delete(p);
}
static int revocation_tests(void){
 power_cycle();assert(ts_auth_init()==ESP_OK);assert(ts_auth_set_admin_password("admin-fixture")==ESP_OK);assert(ts_auth_set_root_password("root-fixture")==ESP_OK);memcpy(backup,disk,sizeof(backup));memcpy(backup_size,disk_size,sizeof(backup_size));
 body_token_api_tests();
 const char *users[]={"admin","root"};const char *passwords[]={"admin-fixture","root-fixture"};
 for(unsigned u=0;u<2;u++){
  setup_users();char a[128],b[128],other[128],new_token[128];uint32_t sid=session_for(users[u],passwords[u],a);
  session_for(users[u],passwords[u],b);session_for(users[1-u],passwords[1-u],other);
  api_update(api_auth_change_password,a,passwords[u],"replacement-fixture",true,true);
  check(!valid_token(a)&&!valid_token(b),"own password change revokes all target tokens including caller");
  check(ts_security_validate_session(sid,NULL)!=ESP_OK,"revoked session ID cannot authorize dispatch or WS output");
  char header[160];snprintf(header,sizeof(header),"Bearer %s",a);uint32_t request_sid=0;ts_perm_level_t request_level=TS_PERM_NONE;
  check(ts_auth_validate_request(header,&request_sid,&request_level)!=ESP_OK,"actual HTTP Bearer validation rejects the revoked token");
  check(valid_token(other),"other account token survives own password change");
  check(ts_auth_verify_password(users[u],passwords[u],NULL)!=ESP_OK,"old password rejected after successful change");
  session_for(users[u],"replacement-fixture",new_token);check(valid_token(new_token),"new password obtains a fresh valid session");
 }
 for(unsigned u=0;u<2;u++){
  setup_users();cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"username",users[u]);cJSON_AddStringToObject(p,"password",passwords[u]);
  ts_api_result_t result={0};esp_err_t ret=api_auth_login(p,&result);
  check(ret==ESP_OK&&result.code==TS_API_OK,"normal core API login succeeds");
  check(!strcmp(cJSON_GetObjectItemCaseSensitive(result.data,"level")->valuestring,users[u]),"core API login uses actual session level");
  check(valid_token(cJSON_GetObjectItemCaseSensitive(result.data,"token")->valuestring),"core API login returns usable full token");
  cJSON_Delete(result.data);free(result.message);
  char *body=cJSON_PrintUnformatted(p);ts_http_request_t req={.body=body,.body_len=strlen(body)};cJSON_Delete(p);http_status=0;
  assert(login_handler(&req,NULL)==ESP_OK);
  check(http_status==200&&http_json,"normal dedicated HTTP login succeeds");
  check(!strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(http_json,"data"),"level")->valuestring,users[u]),"HTTP login uses actual session level");
  cJSON_Delete(http_json);http_json=NULL;revoke_after_login=true;
  assert(login_handler(&req,NULL)==ESP_OK);revoke_after_login=false;
  check(http_status==401&&!http_json,"HTTP login cannot report success for a session revoked before response creation");
  cJSON_Delete(http_json);http_json=NULL;free(body);
 }
 for(unsigned op=0;op<3;op++){
  setup_users();char root[128],root2[128],admin[128],admin2[128],fresh[128];session_for("root","root-fixture",root);session_for("root","root-fixture",root2);session_for("admin","admin-fixture",admin);session_for("admin","admin-fixture",admin2);
  ts_api_handler_t handler=op==0?api_auth_admin_set_password:op==1?api_auth_admin_reset_password:api_auth_root_set_password;
  api_update(handler,root,NULL,op==1?NULL:"manager-fixture",true,op==2);
  check(valid_token(root)==(op!=2)&&valid_token(root2)==(op!=2),"root sessions revoked only when target is root");
  check(valid_token(admin)==(op==2)&&valid_token(admin2)==(op==2),"all and only target admin sessions revoked");
  const char *user=op==2?"root":"admin";const char *pass=op==1?TS_AUTH_DEFAULT_ADMIN_PASSWORD:"manager-fixture";
  session_for(user,pass,fresh);check(valid_token(fresh),"root-managed credential supports subsequent login");
 }
 for(unsigned failure=0;failure<4;failure++){
  setup_users();char root[128],admin[128];session_for("root","root-fixture",root);session_for("admin","admin-fixture",admin);
  if(failure==0)fail_set=1;if(failure==1)fail_commit_at=commits+1;
  api_update(failure==3?api_auth_admin_set_password:api_auth_change_password,failure==3?admin:root,failure==2?"wrong":"root-fixture","failure-fixture",false,false);
  check(valid_token(root)==(failure!=1)&&valid_token(admin),"only an accepted unconfirmed password write revokes target sessions; other account is retained");
 }
 for(unsigned op=0;op<3;op++)for(unsigned failure=0;failure<2;failure++){
  setup_users();char root[128],admin[128];session_for("root","root-fixture",root);session_for("admin","admin-fixture",admin);
  if(failure==0)fail_set=op==2?1:0;else fail_commit_at=commits+1;
  ts_api_handler_t handler=op==0?api_auth_admin_set_password:op==1?api_auth_admin_reset_password:api_auth_root_set_password;
  api_update(handler,root,NULL,op==1?NULL:"failure-fixture",false,false);
  check(valid_token(root)==!(failure==1&&op==2)&&valid_token(admin)==!(failure==1&&op!=2),"unconfirmed manager writes revoke only target; rejected set preserves sessions");
 }
 for(unsigned op=0;op<4;op++)for(int budget=0;budget<7;budget++){
  setup_users();char root[128],admin[128];session_for("root","root-fixture",root);session_for("admin","admin-fixture",admin);
  cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"token",root);cJSON_AddStringToObject(p,"old_password","root-fixture");cJSON_AddStringToObject(p,"new_password","response-fixture");
  ts_api_handler_t handlers[]={api_auth_change_password,api_auth_admin_set_password,api_auth_admin_reset_password,api_auth_root_set_password};
  int before=writes;json_budget=budget;cJSON_Hooks hooks={.malloc_fn=limited_json_alloc,.free_fn=free};cJSON_InitHooks(&hooks);
  ts_api_result_t result={0};esp_err_t ret=handlers[op](p,&result);json_budget=-1;cJSON_InitHooks(NULL);
  check(ret==ESP_ERR_NO_MEM&&result.code==TS_API_ERR_NO_MEM,"required reauth response allocation failure is reported");
  check(writes==before,"response allocation failure occurs before credential mutation");
  check(valid_token(root)&&valid_token(admin),"response allocation failure does not revoke any session");
  cJSON_Delete(p);cJSON_Delete(result.data);free(result.message);
 }
 setup_users();check(ts_auth_set_root_password("root-fixture")==ESP_OK&&active_sessions()==0,"updating a target without sessions stays usable");
 /* Stop a login immediately after old-password verification. The setter either
  * reaches the real pthread-backed auth gate or completes (old implementation).
  * No sleep controls the ordering. */
 random_hook=pause_after_verify;observe_gate=true;
 pthread_t login_thread,set_thread;assert(!pthread_create(&login_thread,NULL,racing_login,NULL));
 pthread_mutex_lock(&race_lock);while(!login_paused)pthread_cond_wait(&race_changed,&race_lock);pthread_mutex_unlock(&race_lock);
 assert(!pthread_create(&set_thread,NULL,racing_set,NULL));
 pthread_mutex_lock(&race_lock);while(!setter_at_gate&&!setter_done)pthread_cond_wait(&race_changed,&race_lock);release_login=true;pthread_cond_broadcast(&race_changed);pthread_mutex_unlock(&race_lock);
 assert(!pthread_join(login_thread,NULL));assert(!pthread_join(set_thread,NULL));random_hook=NULL;observe_gate=false;
 check(race_login_ret==ESP_OK&&race_set_ret==ESP_OK,"concurrent login and update both terminate normally");
 check(!valid_token(race_token),"old-password login cannot issue a surviving session after update/revocation");
 check(ts_auth_verify_password("root","race-new-root",NULL)==ESP_OK,"raced password update remains usable");
 printf("%u checks, %u failures: actual auth/session/API handlers, SDK crypto and ordered pthread race; synthetic NVS\n",checks,failures);
 return failures?1:0;
}

'''
if args.case=="lockout":
    code+=r'''
static esp_err_t wrong_attempt(unsigned u){return ts_auth_verify_password(u?"root":"admin","wrong-fixture",NULL);}
static esp_err_t correct_attempt(unsigned u){return ts_auth_verify_password(u?"root":"admin",u?"root-fixture":"admin-fixture",NULL);}
static void five_wrong(unsigned u){for(unsigned n=0;n<5;n++)check(wrong_attempt(u)==ESP_ERR_INVALID_ARG,"five guesses reach threshold without granting a session");}
static int lockout_tests(void){
 power_cycle();assert(ts_auth_init()==ESP_OK);assert(ts_auth_set_admin_password("admin-fixture")==ESP_OK);assert(ts_auth_set_root_password("root-fixture")==ESP_OK);memcpy(backup,disk,sizeof(backup));memcpy(backup_size,disk_size,sizeof(backup_size));
 for(unsigned u=0;u<2;u++){
  restore();assert(ts_auth_init()==ESP_OK);
  for(unsigned n=1;n<5;n++){
   check(wrong_attempt(u)==ESP_ERR_INVALID_ARG,"wrong guess rejected");
   check(disk[u].failed_attempts==n,"partial failure count advances across reboots");
   user_credential_t before[2];memcpy(before,disk,sizeof(before));power_cycle();
   check(ts_auth_init()==ESP_OK&&writes==0&&commits==0&&!memcmp(before,disk,sizeof(before)),"reboot preserves both credential blobs and performs no NVS write");
  }
  check(wrong_attempt(u)==ESP_ERR_INVALID_ARG,"fifth wrong guess rejected");
  check(disk[u].failed_attempts==0&&disk[u].lockout_until!=0,"fifth guess persists lock marker");
  char token[128];uint32_t sid=0;unsigned active=active_sessions();
  check(ts_auth_login(u?"root":"admin",u?"root-fixture":"admin-fixture",&sid,token,sizeof(token))==ESP_ERR_INVALID_STATE&&active_sessions()==active,"correct login during lock cannot issue a session");
  int before_writes=writes,before_commits=commits;
  for(unsigned n=0;n<10;n++)check((n%2?wrong_attempt(u):correct_attempt(u))==ESP_ERR_INVALID_STATE,"locked requests reject both correct and wrong passwords");
  check(writes==before_writes&&commits==before_commits,"locked requests do not write or commit NVS");
  check(correct_attempt(1-u)==ESP_OK,"one account lock does not block the other account");
  clock_us+=100LL*1000000;check(ts_auth_init()==ESP_OK,"repeated init is idempotent");
  clock_us+=199LL*1000000;check(correct_attempt(u)==ESP_ERR_INVALID_STATE,"299 seconds remains locked");
  clock_us+=1000000;check(correct_attempt(u)==ESP_OK,"300-second boundary accepts correct password without restarting cooldown");
  check(disk[u].failed_attempts==0&&disk[u].lockout_until==0,"successful post-cooldown verification durably clears marker");
  check(!memcmp(disk[u].salt,backup[u].salt,TS_AUTH_SALT_LEN)&&!memcmp(disk[u].hash,backup[u].hash,TS_AUTH_HASH_LEN),"cooldown does not change password material");
 }
 /* Existing v3 stores may contain a prior boot's large absolute uptime.
  * All nonzero markers receive exactly one new bounded boot cooldown. */
 const uint32_t markers[]={1,7*86400+300,UINT32_MAX};
 for(unsigned u=0;u<2;u++)for(unsigned m=0;m<3;m++){
  restore();disk[u].lockout_until=markers[m];power_cycle();user_credential_t before=disk[u];
  check(ts_auth_init()==ESP_OK&&!memcmp(&before,&disk[u],sizeof(before))&&writes==0&&commits==0,"legacy marker boot is read-only");
  check(correct_attempt(u)==ESP_ERR_INVALID_STATE,"boot rearms a bounded lock regardless of old deadline");
  clock_us+=299LL*1000000;check(correct_attempt(u)==ESP_ERR_INVALID_STATE,"legacy lock remains until boundary");
  clock_us+=1000000;check(correct_attempt(u)==ESP_OK&&disk[u].lockout_until==0,"large old uptime never causes an unbounded lock");
 }
 for(unsigned u=0;u<2;u++){
  restore();assert(ts_auth_init()==ESP_OK);five_wrong(u);clock_us+=300LL*1000000;
  check(wrong_attempt(u)==ESP_ERR_INVALID_ARG&&disk[u].failed_attempts==1&&disk[u].lockout_until==0,"first wrong guess after cooldown starts a fresh persisted budget");
  for(unsigned n=0;n<4;n++)check(wrong_attempt(u)==ESP_ERR_INVALID_ARG,"next bounded budget counts each wrong guess");
  check(correct_attempt(u)==ESP_ERR_INVALID_STATE,"next five wrong guesses lock again");

  restore();disk[u].failed_attempts=UINT32_MAX;power_cycle();assert(ts_auth_init()==ESP_OK);
  check(correct_attempt(u)==ESP_ERR_INVALID_STATE,"out-of-range persisted counter cannot wrap into fresh attempts");
  clock_us+=300LL*1000000;check(correct_attempt(u)==ESP_OK&&disk[u].failed_attempts==0,"counter anomaly still permits bounded recovery");

  restore();disk[u].lockout_until=1;power_cycle();assert(ts_auth_init()==ESP_OK);clock_us+=300LL*1000000;fail_commit_at=commits+1;
  char token[128];uint32_t sid=0;unsigned active=active_sessions();
  check(ts_auth_login(u?"root":"admin",u?"root-fixture":"admin-fixture",&sid,token,sizeof(token))==ESP_FAIL&&active_sessions()==active,"post-cooldown cleanup commit failure cannot issue a new session");

  restore();disk[u].lockout_until=1;power_cycle();fail_read=u;
  check(ts_auth_init()==ESP_FAIL&&!s_auth_initialized&&disk[u].lockout_until==1&&writes==0,"locked record read failure fails closed without resetting credentials");
  check(ts_auth_init()==ESP_OK&&correct_attempt(u)==ESP_ERR_INVALID_STATE,"read retry retains boot lock");

  restore();assert(ts_auth_init()==ESP_OK);char root_token[128];sid=0;assert(ts_auth_login("root","root-fixture",&sid,root_token,sizeof(root_token))==ESP_OK);five_wrong(u);
  cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"token",root_token);cJSON_AddStringToObject(p,"new_password","recovered-fixture");ts_api_result_t result={0};
  esp_err_t ret=(u?api_auth_root_set_password:api_auth_admin_set_password)(p,&result);
  check(ret==ESP_OK&&result.code==TS_API_OK,"existing root session can recover a login-locked account");
  check(ts_auth_verify_password(u?"root":"admin","recovered-fixture",NULL)==ESP_OK&&disk[u].lockout_until==0,"authorized password update clears runtime and persisted lock");
  check(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result.data,"reauth_required"))==(u==1),"lock recovery retains target-based re-login policy");
  cJSON_Delete(p);cJSON_Delete(result.data);free(result.message);

  restore();assert(ts_auth_init()==ESP_OK);five_wrong(u);
  p=cJSON_CreateObject();cJSON_AddStringToObject(p,"username",u?"root":"admin");cJSON_AddStringToObject(p,"password",u?"root-fixture":"admin-fixture");result=(ts_api_result_t){0};
  check(api_auth_login(p,&result)==ESP_ERR_INVALID_STATE&&result.code==TS_API_ERR_AUTH&&!strcmp(result.message,"Account locked due to too many failed attempts"),"core API reports account lock");
  cJSON_Delete(result.data);free(result.message);char *body=cJSON_PrintUnformatted(p);cJSON_Delete(p);ts_http_request_t req={.body=body,.body_len=strlen(body)};http_status=0;
  assert(login_handler(&req,NULL)==ESP_OK);
  check(http_status==401&&!strcmp(http_error,"Account locked due to too many failed attempts"),"dedicated HTTP login reports the same actionable lock reason");
  clock_us+=300LL*1000000;assert(login_handler(&req,NULL)==ESP_OK);
  check(http_status==200&&http_json,"HTTP login remains usable when cooldown ends");cJSON_Delete(http_json);http_json=NULL;free(body);
 }
 check(sizeof(user_credential_t)==68&&TS_AUTH_CONFIG_VERSION==3&&TS_AUTH_USER_CREDENTIAL_SIZE==60,"credential format/version and legacy prefix are explicit");
 printf("%u checks, %u failures: actual auth/session/core API/HTTP login with synthetic time and NVS; no device\n",checks,failures);
 return failures?1:0;
}

'''
if args.case=="tokens":
    code+=r"""
static int token_tests(void){
 power_cycle();char first[128],other[128],fresh[128],changed[128];uint32_t a=0,b=0,out=0;
 assert(ts_auth_init()==ESP_OK&&ts_auth_set_root_password("root-fixture")==ESP_OK);
 body_diagnostic[0]=0;body_diagnostic_used=0;body_diagnostic_enabled=true;
 char logged_token[128],sid_text[16];uint32_t logged_sid=0;
 check(ts_auth_login("root","root-fixture",&logged_sid,logged_token,sizeof(logged_token))==ESP_OK,"actual root login succeeds with current credential owner");
 snprintf(sid_text,sizeof(sid_text),"%08lx",(unsigned long)logged_sid);
 check(strstr(body_diagnostic,"root")&&!strstr(body_diagnostic,sid_text)&&!strstr(body_diagnostic,logged_token),"actual login logs retain account name without SID or token");
 check(ts_auth_logout(logged_sid)==ESP_OK&&!strstr(body_diagnostic,sid_text),"actual logout log does not leak SID");
 body_diagnostic_enabled=false;

 check(ts_security_create_session("root",TS_PERM_ROOT,&a)==ESP_OK&&a!=0,"issued session is nonzero");

 uint8_t unissued_raw[17]={0};memcpy(unissued_raw,&a,4);
 for(size_t length=0;length<=sizeof(unissued_raw);++length){
  size_t capacity=sizeof(changed);assert(ts_crypto_base64_encode(unissued_raw,length,changed,&capacity)==ESP_OK);
  check(ts_security_validate_token(changed,&out)!=ESP_OK,"unissued session and every decoded length 0..17 cannot authenticate");
 }
 check(ts_security_validate_token("not-base64",&out)!=ESP_OK,"malformed base64 cannot authenticate");
 check(ts_security_generate_token(a,first,sizeof(first))==ESP_OK,"normal opaque token generation succeeds");
 uint8_t raw[16];size_t n=sizeof(raw);
 check(ts_crypto_base64_decode(first,strlen(first),raw,&n)==ESP_OK&&n==16&&strlen(first)==24,"16-byte credential retains existing 24-character Base64 wire length");
 check(random_length==sizeof(raw)&&!memcmp(raw,random_bytes,sizeof(raw)),"all 16 credential bytes originate from RNG, not identity/clock");
 check(ts_security_validate_token(first,&out)==ESP_OK&&out==a,"opaque credential resolves original session");
 uint8_t inferred[16]={0};memcpy(inferred,&a,4);uint32_t known_uptime=(uint32_t)(clock_us/1000);memcpy(inferred+12,&known_uptime,4);
 /* Even granting the synthetic test identity and uptime cannot replace the
  * issued nonce. This bounded fixture is not an exhaustive search claim. */
 for(unsigned padding=0;padding<256;++padding){
  memset(inferred+4,(int)padding,8);size_t capacity=sizeof(changed);
  assert(ts_crypto_base64_encode(inferred,sizeof(inferred),changed,&capacity)==ESP_OK);
  check(ts_security_validate_token(changed,&out)!=ESP_OK,"metadata-shaped synthetic reconstruction is not the issued opaque credential");
 }

 check(ts_security_create_session("admin",TS_PERM_ADMIN,&b)==ESP_OK&&b!=a&&b!=0,"second identity is distinct and nonzero");
 check(ts_security_generate_token(b,other,sizeof(other))==ESP_OK,"second session generates independent credential");
 check(ts_security_validate_token(other,&out)==ESP_OK&&out==b&&ts_security_check_permission(out,TS_PERM_ADMIN)&&!ts_security_check_permission(out,TS_PERM_ROOT),"credential lookup across slots retains actual Admin privilege");
 check(ts_security_validate_token(first,&out)==ESP_OK&&out==a,"second login preserves first token");
 for(unsigned i=0;i<sizeof(raw);i++){
  raw[i]^=1;size_t capacity=sizeof(changed);assert(ts_crypto_base64_encode(raw,sizeof(raw),changed,&capacity)==ESP_OK);
  check(ts_security_validate_token(changed,&out)!=ESP_OK,"each of 16 changed bytes is rejected");raw[i]^=1;
 }
 check(ts_security_generate_token(a,fresh,sizeof(fresh))==ESP_OK&&strcmp(first,fresh),"reissue produces new opaque credential");
 check(ts_security_validate_token(first,&out)!=ESP_OK&&ts_security_validate_token(fresh,&out)==ESP_OK&&out==a,"only successful reissue replaces previous credential");
 check(ts_security_generate_token(a,changed,1)==ESP_ERR_INVALID_ARG&&ts_security_validate_token(fresh,&out)==ESP_OK,"failed issuance preserves prior usable credential");
 check(ts_security_destroy_session(a)==ESP_OK&&ts_security_validate_token(fresh,&out)!=ESP_OK,"logout invalidates full issued credential");
 ts_session_t session;assert(ts_security_validate_session(b,&session)==ESP_OK);clock_us=((int64_t)session.expires_at+1)*1000000;
 check(ts_security_validate_token(other,&out)!=ESP_OK,"expiry still rejects matching full credential");
 uint32_t previous=a;
 for(unsigned i=0;i<32;i++){
  uint32_t sid=0;random_state=123;random_zero=(i==0);
  check(ts_security_create_session("root",TS_PERM_ROOT,&sid)==ESP_OK&&sid!=0&&sid!=previous&&sid!=a&&sid!=b,"expired/logged-out owner identity cannot reappear under repeated or zero RNG");
  previous=sid;check(ts_security_destroy_session(sid)==ESP_OK,"normal session teardown remains usable");
 }
 random_zero=false;check(ts_security_deinit()==ESP_OK&&ts_security_init()==ESP_OK,"security service reinitialization remains usable");
 uint32_t sid=0;check(ts_security_create_session("root",TS_PERM_ROOT,&sid)==ESP_OK&&sid!=0&&sid!=previous&&sid!=a&&sid!=b,"service reinitialization does not recycle volatile owner identity");
"""
    if 'static atomic_uint_least32_t s_last_session_id' in source('components/ts_security/src/ts_security.c'):
        code+=r"""
 assert(ts_security_destroy_session(sid)==ESP_OK);atomic_store(&s_last_session_id,UINT32_MAX-1);
 check(ts_security_create_session("root",TS_PERM_ROOT,&sid)==ESP_OK&&sid==UINT32_MAX,"last representable nonzero identity is usable");
 check(ts_security_generate_token(sid,fresh,sizeof(fresh))==ESP_OK,"last identity has a valid opaque credential");
 unsigned active=active_sessions();out=123;
 check(ts_security_create_session("root",TS_PERM_ROOT,&out)==ESP_ERR_INVALID_STATE&&out==123&&active_sessions()==active,"counter exhaustion refuses before eviction and cannot wrap to internal zero");
 check(ts_security_validate_token(fresh,&out)==ESP_OK&&out==sid,"exhaustion preserves previously issued session");
 assert(ts_security_deinit()==ESP_OK&&ts_security_init()==ESP_OK);
 check(ts_security_create_session("root",TS_PERM_ROOT,&out)==ESP_ERR_INVALID_STATE,"service restart cannot reset exhausted boot identity space");
"""
    code+=r"""
 printf("%u checks, %u failures: actual session/SDK codec, opaque RNG bytes and non-reused boot identities; synthetic RNG/time only\n",checks,failures);return failures?1:0;
}
"""

if args.case=="session_race":
    code+=r"""
static uint32_t slot_old_sid,slot_new_sid,slot_validated_sid;
static char slot_old_token[128],slot_new_token[128];
static int slot_validate_ret;static ts_perm_level_t slot_validated_level;
static void *slot_validator(void *unused){
 (void)unused;slot_validate_ret=ts_security_validate_token(slot_old_token,&slot_validated_sid);
 ts_session_t session={0};slot_validated_level=ts_security_validate_session(slot_validated_sid,&session)==ESP_OK?session.level:TS_PERM_NONE;
 return NULL;
}
static void *slot_recycle(void *unused){
 (void)unused;slot_recycler=true;pthread_mutex_lock(&slot_race_lock);wait_slot_flag(&slot_matched);pthread_mutex_unlock(&slot_race_lock);
 assert(ts_security_destroy_session(slot_old_sid)==ESP_OK);
 assert(ts_security_create_session("root",TS_PERM_ROOT,&slot_new_sid)==ESP_OK);
 assert(ts_security_generate_token(slot_new_sid,slot_new_token,sizeof(slot_new_token))==ESP_OK);
 pthread_mutex_lock(&slot_race_lock);slot_worker_finished=true;pthread_cond_broadcast(&slot_race_changed);pthread_mutex_unlock(&slot_race_lock);return NULL;
}
static int session_race_tests(void){
 for(unsigned repeat=0;repeat<32;repeat++){
  power_cycle();slot_matched=slot_worker_waiting=slot_worker_finished=false;slot_hook_armed=true;slot_validate_ret=ESP_FAIL;slot_validated_sid=0;slot_validated_level=TS_PERM_NONE;
  assert(ts_security_create_session("admin",TS_PERM_ADMIN,&slot_old_sid)==ESP_OK);
  assert(ts_security_generate_token(slot_old_sid,slot_old_token,sizeof(slot_old_token))==ESP_OK);
  pthread_t validating,recycling;assert(!pthread_create(&recycling,NULL,slot_recycle,NULL));assert(!pthread_create(&validating,NULL,slot_validator,NULL));
  assert(!pthread_join(validating,NULL)&&!pthread_join(recycling,NULL));
  check(slot_validate_ret==ESP_OK&&slot_validated_sid==slot_old_sid,"matched Admin token returns its own identity even across forced slot recycling");
  check(slot_validated_level!=TS_PERM_ROOT,"matched Admin credential never inherits replacement Root level");
  check(slot_worker_waiting,"recycler enters the shared session gate after the token match boundary");
  uint32_t sid=0;check(ts_security_validate_token(slot_old_token,&sid)!=ESP_OK,"recycled Admin token remains rejected after threads join");
  ts_session_t session;check(ts_security_validate_token(slot_new_token,&sid)==ESP_OK&&sid==slot_new_sid&&ts_security_validate_session(sid,&session)==ESP_OK&&session.level==TS_PERM_ROOT,"legitimate replacement Root credential remains usable");
 }
 power_cycle();uint32_t full_sid;
 for(unsigned i=0;i<MAX_SESSIONS;i++)assert(ts_security_create_session("occupied",TS_PERM_ADMIN,&full_sid)==ESP_OK);
 check(ts_security_create_session("other",TS_PERM_ROOT,&full_sid)==ESP_ERR_NO_MEM&&active_sessions()==MAX_SESSIONS,"capacity exhaustion retains explicit error without evicting unrelated active sessions");
 check(atomic_load(&slot_logs_under_gate)==0,"all session diagnostics, including capacity warning, run outside the session gate");
 printf("%u checks, %u failures: 32 deterministic pthread slot-recycle schedules around actual full-token comparison; actual auth/session/SDK codec, synthetic producer/time/NVS\n",checks,failures);return failures?1:0;
}
"""

code+='\nint main(void){return '+{'revocation':'revocation_tests','persistence':'persistence_tests','lockout':'lockout_tests','tokens':'token_tests','session_race':'session_race_tests'}[args.case]+'();}\n'

with tempfile.TemporaryDirectory(prefix="ts-auth-persistence-") as tmp:
    c=Path(tmp)/"test.c";exe=Path(tmp)/"test";c.write_text(code)
    mbed=IDF/"components/mbedtls/mbedtls"
    native=sdk_library_dir(IDF)
    includes=[ROOT/"tests/runtime/storage_stubs",ROOT/"components/ts_security/include",ROOT/"components/ts_api/include",ROOT/"components/ts_auth_layout/include",mbed/"include",IDF/"components/json/cJSON"]
    subprocess.run(["cc","-std=gnu11","-O2","-g","-Wno-deprecated-declarations","-fsanitize="+os.environ.get("AUTH_TEST_SANITIZER","address,undefined"),"-fno-sanitize-recover=all",
        *["-I"+str(p) for p in includes],str(c),
        str(native/"library/libmbedcrypto.a"),
        str(IDF/"components/json/cJSON/cJSON.c"),"-pthread","-o",str(exe)],check=True,cwd=ROOT)
    subprocess.run([str(exe)],check=True,cwd=ROOT,timeout=120)
