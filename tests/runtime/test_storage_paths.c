/* Exercise production storage mutations and storage APIs against real temp files.
 * Only the mounted VFS, FAT name lookup, API result and driver boundaries are replaced. */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ts_storage.h"
#include "ts_api.h"
#include "ts_security.h"
#include "ts_config_pack.h"
#include "freertos/semphr.h"

static char root[] = "/tmp/tianshan-storage-paths-XXXXXX";
static unsigned mutations;
static uint32_t next_nonce;
uint32_t esp_random(void) { return ++next_nonce; }
static pthread_mutex_t gate;
static bool fail_dir_read;
static bool fail_name_lookup;
static char alias_leaf[256] = "Long Original Name.bin";
static unsigned dirs_closed;
enum storage_fault { F_NONE, F_OPEN, F_OPEN_RACE, F_WRITE, F_FLUSH, F_SYNC,
    F_CLOSE, F_READBACK, F_BACKUP_RENAME, F_INSTALL_RENAME, F_INSTALL_AFTER,
    F_ROLLBACK, F_OLD_CLEANUP, F_PENDING_STAT };
static enum storage_fault storage_fault;
static FILE *pending_write, *pending_read;
static char observed_pending[512], observed_previous[512];
static bool sidecar(const char *path, char type)
{
    const char *dot=strrchr(path,'.');
    return dot && strlen(dot)==8 && dot[1]==type;
}
static char removed[16][512];
static unsigned removed_count;
static const char *mapped(const char *path)
{
    static char paths[4][1024];
    static unsigned next;
    char *out = paths[next++ % 4];
    snprintf(out, 1024, "%s%s", root, path);
    return out;
}
static FILE *test_open(const char *path, const char *mode)
{
    bool pending=sidecar(path,'n');
    if (pending && mode[0]=='w') {
        snprintf(observed_pending,sizeof(observed_pending),"%s",path);
        if (storage_fault==F_OPEN || storage_fault==F_OPEN_RACE) {
            if (storage_fault==F_OPEN_RACE) {
                FILE *foreign=fopen(mapped(path),"wb");assert(foreign);
                assert(fwrite("unrelated",1,9,foreign)==9 && !fclose(foreign));
            }
            errno=EEXIST;return NULL;
        }
    }
    if (strchr(mode, 'w') || strchr(mode, 'a')) ++mutations;
    FILE *file=fopen(mapped(path),mode);
    if (file && mode[0]=='w' && (pending || storage_fault!=F_NONE)) pending_write=file;
    if (file && pending && mode[0]=='r') pending_read=file;
    return file;
}
static size_t test_write(const void *data,size_t size,size_t count,FILE *file)
{
    if (file==pending_write && storage_fault==F_WRITE && count) {
        errno=ENOSPC;return fwrite(data,size,count/2,file);
    }
    return fwrite(data,size,count,file);
}
static size_t test_read(void *data,size_t size,size_t count,FILE *file)
{
    if (file==pending_read && storage_fault==F_READBACK) {errno=EIO;return 0;}
    return fread(data,size,count,file);
}
static int test_flush(FILE *file)
{
    int rc=fflush(file);
    if (file==pending_write && storage_fault==F_FLUSH) {errno=EIO;return EOF;}
    return rc;
}
static int test_sync(int fd)
{
    if (pending_write && fd==fileno(pending_write) && storage_fault==F_SYNC) {errno=EIO;return -1;}
    return fsync(fd);
}
static int test_close(FILE *file)
{
    bool fail=file==pending_write && storage_fault==F_CLOSE;
    if (file==pending_write) pending_write=NULL;
    if (file==pending_read) pending_read=NULL;
    int rc=fclose(file);if(fail){errno=EIO;return EOF;}return rc;
}
static int test_stat(const char *path, struct stat *out)
{
    if (storage_fault==F_PENDING_STAT && sidecar(path,'n')) {errno=EIO;return -1;}
    return stat(mapped(path),out);
}
static int test_mkdir(const char *path, mode_t mode) { ++mutations;return mkdir(mapped(path), mode); }
static int test_unlink(const char *path) { if(storage_fault==F_OLD_CLEANUP&&sidecar(path,'o')){errno=EIO;return -1;}++mutations;if(removed_count<16)snprintf(removed[removed_count++],512,"%s",path);return unlink(mapped(path)); }
static int test_rmdir(const char *path) { ++mutations;return rmdir(mapped(path)); }
static int test_rename(const char *from, const char *to)
{
    ++mutations;bool install=sidecar(from,'n'),backup=sidecar(to,'o'),restore=sidecar(from,'o');
    if(backup)snprintf(observed_previous,sizeof(observed_previous),"%s",to);
    if((backup&&storage_fault==F_BACKUP_RENAME)||(install&&(storage_fault==F_INSTALL_RENAME||storage_fault==F_ROLLBACK))||(restore&&storage_fault==F_ROLLBACK)){errno=EIO;return -1;}
    struct stat st;
    /* Real FAT/SPIFFS rename refuses an existing destination. */
    if(!stat(mapped(to),&st)){errno=EEXIST;return -1;}
    int rc=rename(mapped(from),mapped(to));
    if(!rc&&install&&storage_fault==F_INSTALL_AFTER){errno=EIO;return -1;}
    return rc;
}
static DIR *test_opendir(const char *path) { return opendir(mapped(path)); }
static struct dirent *test_readdir(DIR *dir) { if(fail_dir_read){errno=EIO;return NULL;}return readdir(dir); }
static int test_closedir(DIR *dir) { ++dirs_closed;return closedir(dir); }

#define fopen(path, mode) test_open(path, mode)
#define fwrite(data,size,count,file) test_write(data,size,count,file)
#define fread(data,size,count,file) test_read(data,size,count,file)
#define fflush(file) test_flush(file)
#define fsync(fd) test_sync(fd)
#define fclose(file) test_close(file)
#define stat(path, out) test_stat(path, out)
#define mkdir(path, mode) test_mkdir(path, mode)
#define unlink(path) test_unlink(path)
#define rmdir(path) test_rmdir(path)
#define rename(from, to) test_rename(from, to)
#define opendir(path) test_opendir(path)
#define readdir(dir) test_readdir(dir)
#define closedir(dir) test_closedir(dir)
#include "../../components/ts_storage/src/ts_file_store.c"
#undef TAG
#include "../../components/ts_storage/src/ts_storage.c"
#include "../../components/ts_storage/src/ts_storage_paths.c"
#undef TAG
#include "../../components/ts_api/src/ts_api_storage.c"
#undef fopen
#undef fwrite
#undef fread
#undef fflush
#undef fsync
#undef fclose
#undef stat
#undef mkdir
#undef unlink
#undef rmdir
#undef rename
#undef opendir
#undef readdir
#undef closedir

void ts_storage_sd_lock(void) { assert(!pthread_mutex_lock(&gate)); }
void ts_storage_sd_unlock(void) { assert(!pthread_mutex_unlock(&gate)); }
bool ts_storage_sd_name_is(const char *path, const char *name)
{
    /* Boundary models names actually resolved by FatFs. No guessed '~1' policy. */
    if (!strcmp(name, "config")) return !strcmp(path, "/sdcard/confıg");
    return !strcmp(name, "automation") &&
        (!strcmp(path, "/sdcard/config/AUTOMA~1") || !strcmp(path, "/sdcard/config/automatıon"));
}
esp_err_t ts_storage_sd_resolve_name(const char *path,char *name,size_t capacity)
{
    if (fail_name_lookup && !strcmp(path,"/sdcard/LONGOR~1.BIN")) return ESP_FAIL;
    const char *resolved=!strcmp(path,"/sdcard/LONGOR~1.BIN")?alias_leaf:
        !strcmp(path,"/sdcard/RECOVE~1")?"recovery":
        !strcmp(path,"/sdcard/recovery/OBJECT~1")?"objects":
        !strcmp(path,"/sdcard/confıg")?"config":
        (!strcmp(path,"/sdcard/config/AUTOMA~1")||!strcmp(path,"/sdcard/config/automatıon"))?"automation":strrchr(path,'/')+1;
    snprintf(name,capacity,"%s",resolved);return ESP_OK;
}
esp_err_t ts_storage_unmount_auto(void) { return ESP_OK; }
esp_err_t ts_storage_unmount_spiffs(void) { return ESP_OK; }
esp_err_t ts_storage_unmount_sd(void) { return ESP_OK; }
esp_err_t ts_storage_mount_sd(const ts_sd_config_t *config) { (void)config;return ESP_OK; }
bool ts_storage_spiffs_mounted(void) { return false; }
bool ts_storage_sd_mounted(void) { return true; }
void ts_api_result_ok(ts_api_result_t *result, cJSON *data) { result->code=TS_API_OK;result->data=data; }
void ts_api_result_error(ts_api_result_t *result, ts_api_result_code_t code, const char *message)
{ result->code=code;result->message=(char *)message; }
esp_err_t ts_api_register_multiple(const ts_api_endpoint_t *endpoints, size_t count)
{ (void)endpoints;(void)count;return ESP_OK; }

typedef struct {const char *query;} httpd_req_t;
typedef struct {httpd_req_t *req;const void *body;size_t body_len;bool root,authenticated;} ts_http_request_t;
static const char *upload_path;
static int upload_status;
static char upload_reply[512];
static char downloaded_path[256],downloaded_body[256];
static ssize_t downloaded_size;
#define TS_MALLOC_PSRAM malloc
/* These requests have only path: the SDK boundary returns raw encoded bytes,
 * and the production query helper below decodes them. */
static size_t httpd_req_get_url_query_len(httpd_req_t *req) {return strlen(req->query);}
static esp_err_t httpd_req_get_url_query_str(httpd_req_t *req,char *out,size_t length)
{ assert(strlen(req->query)<length);strcpy(out,req->query);return ESP_OK; }
static esp_err_t httpd_query_key_value(const char *query,const char *key,char *out,size_t length)
{ assert(!strcmp(key,"path")&&!strncmp(query,"path=",5));assert(strlen(query+5)<length);strcpy(out,query+5);return ESP_OK; }
static esp_err_t check_auth(ts_http_request_t *req,uint32_t *session,ts_perm_level_t permission)
{ *session=0;if(!req->root&&!req->authenticated)return ESP_ERR_NOT_FOUND;return permission<=TS_PERM_READ||req->root?ESP_OK:ESP_ERR_NOT_ALLOWED; }
static esp_err_t ts_http_send_private_file(ts_http_request_t *req,const char *path)
{ (void)req;snprintf(downloaded_path,sizeof(downloaded_path),"%s",path);downloaded_size=ts_storage_read_file(path,downloaded_body,sizeof(downloaded_body));return downloaded_size<0?ESP_FAIL:ESP_OK; }
static esp_err_t ts_http_send_json(ts_http_request_t *req,int status,const char *body)
{ (void)req;upload_status=status;snprintf(upload_reply,sizeof(upload_reply),"%s",body);return ESP_OK; }
static esp_err_t ts_http_send_error(ts_http_request_t *req,int status,const char *reason)
{ return ts_http_send_json(req,status,reason); }
ts_config_pack_result_t ts_config_pack_verify(const char *path,ts_config_pack_sig_info_t *info)
{ (void)path;(void)info;assert(!"configuration upload must be rejected before pack verification");return TS_CONFIG_PACK_ERR_IO; }
const char *ts_config_pack_strerror(ts_config_pack_result_t result) { (void)result;return "unused"; }
const char *ts_config_pack_reason(ts_config_pack_result_t result) { (void)result;return "unused"; }
static esp_err_t ts_ws_broadcast_to_topic(const char *topic,const cJSON *data)
{ (void)topic;(void)data;assert(!"no pack notification for a rejected upload");return ESP_FAIL; }
#include "storage_upload.inc"

static void upload(const char *path,int expected_status,const char *reason)
{
    upload_path=path;upload_status=0;upload_reply[0]=0;
    char query[1024];snprintf(query,sizeof(query),"path=%s",upload_path);httpd_req_t http={.query=query};
    ts_http_request_t req={.req=&http,.body="uploaded",.body_len=8};
    assert(file_upload_handler(&req,NULL)==ESP_OK&&upload_status==expected_status);
    assert(strstr(upload_reply,reason));
}

static void original_file(const char *path, const char *text)
{
    FILE *file=test_open(path,"wb");assert(file);
    assert(fwrite(text,1,strlen(text),file)==strlen(text));assert(!fclose(file));
}
static void has_text(const char *path,const char *expected)
{
    char text[64];ssize_t size=ts_storage_read_file(path,text,sizeof(text));
    assert(size==(ssize_t)strlen(expected)&&!memcmp(text,expected,(size_t)size));
}

static void ordinary_names(void)
{
    static const struct {const char *name,*query;} files[]={
        {"report.txt","report.txt"},
        {"report+v1.txt","report%2Bv1.txt"},
        {"report v1.txt","report%20v1.txt"},
        {"report%20v1.txt","report%2520v1.txt"},
        {"report%25v1.txt","report%2525v1.txt"},
        {"进度100%.txt","%E8%BF%9B%E5%BA%A6100%25.txt"},
        {"记录.txt","%E8%AE%B0%E5%BD%95.txt"}
    };
    for(size_t i=0;i<sizeof(files)/sizeof(files[0]);i++){
        char query[256],path[256],body[64];
        snprintf(query,sizeof(query),"path=%%2Fsdcard%%2Fb02%%2F%s",files[i].query);
        snprintf(path,sizeof(path),"/sdcard/b02/%s",files[i].name);
        snprintf(body,sizeof(body),"independent file contents %zu",i);
        httpd_req_t http={.query=query};ts_http_request_t req={.req=&http,.body=body,.body_len=strlen(body)};
        upload_status=0;assert(file_upload_handler(&req,NULL)==ESP_OK&&upload_status==200);
        cJSON *reply=cJSON_Parse(upload_reply);assert(reply&&!strcmp(cJSON_GetObjectItem(reply,"path")->valuestring,path));cJSON_Delete(reply);
        has_text(path,body);
    }
    /* Read every name only after all uploads: similar names must coexist. */
    for(size_t i=0;i<sizeof(files)/sizeof(files[0]);i++){
        char query[256],path[256],body[64];
        snprintf(query,sizeof(query),"path=%%2Fsdcard%%2Fb02%%2F%s",files[i].query);
        snprintf(path,sizeof(path),"/sdcard/b02/%s",files[i].name);
        snprintf(body,sizeof(body),"independent file contents %zu",i);
        httpd_req_t http={.query=query};ts_http_request_t req={.req=&http,.authenticated=true};
        assert(file_download_handler(&req,NULL)==ESP_OK);
        assert(!strcmp(downloaded_path,path)&&downloaded_size==(ssize_t)strlen(body)&&!memcmp(downloaded_body,body,strlen(body)));
    }
    assert(ts_storage_rmdir_r("/sdcard/b02")==ESP_OK);
    puts("PASS seven ordinary names: actual query decode, upload storage and download bytes remain independent");
}

static void recursive_names(void)
{
    assert(ts_storage_mkdir_p("/sdcard/long/sub/empty")==ESP_OK);
    char name[120],path[256];memset(name,'x',100);name[100]=0;
    snprintf(path,sizeof(path),"/sdcard/long/sub/%s",name);original_file(path,"long");
    name[63]=0;snprintf(path,sizeof(path),"/sdcard/long/sub/%s",name);original_file(path,"prefix");
    original_file("/sdcard/long/普通文件名超过六十三字节仍应完整删除而不是截短名字.json","chinese");
    original_file("/sdcard/long/short.json","short");
    assert(ts_storage_mkdir_p("/sdcard/keep")==ESP_OK);original_file("/sdcard/keep/a","keep");
    removed_count=0;assert(ts_storage_rmdir_r("/sdcard/long")==ESP_OK);
    bool full_name=false,short_name=false;
    for(unsigned i=0;i<removed_count;i++){
        const char *name=strrchr(removed[i],'/')+1;
        if(strlen(name)==100)full_name=true;if(strlen(name)==63)short_name=true;
    }
    assert(full_name&&short_name);
    assert(!ts_storage_exists("/sdcard/long"));has_text("/sdcard/keep/a","keep");
    assert(ts_storage_rmdir_r("/sdcard/keep")==ESP_OK);
    assert(ts_storage_mkdir_p("/sdcard/read-error")==ESP_OK);unsigned closed=dirs_closed;
    fail_dir_read=true;assert(ts_storage_rmdir_r("/sdcard/read-error")==ESP_FAIL);fail_dir_read=false;
    assert(dirs_closed==closed+1&&ts_storage_exists("/sdcard/read-error"));
    errno=EIO;assert(ts_storage_rmdir_r("/sdcard/read-error")==ESP_OK); /* stale errno is not a read error */
    char deep[256]="/sdcard/",child[512];memset(deep+8,'d',210);deep[218]=0;
    assert(ts_storage_mkdir_p(deep)==ESP_OK);memset(name,'f',100);name[100]=0;
    snprintf(child,sizeof(child),"%s/%s",deep,name);original_file(child,"too long as a full storage path");
    unsigned before=mutations;closed=dirs_closed;
    assert(ts_storage_rmdir_r(deep)==ESP_ERR_INVALID_ARG&&mutations==before&&dirs_closed==closed+1);
    assert(!test_unlink(child)&&!test_rmdir(deep));
    puts("PASS F06 complete long and Chinese names, same short prefix, nested/empty directories; unselected files retained");
}

static void api_denied(ts_api_handler_t handler,const char *json)
{
    cJSON *params=cJSON_Parse(json);assert(params);ts_api_result_t result={0};
    unsigned before=mutations;
    assert(handler(params,&result)==ESP_ERR_NOT_ALLOWED);
    assert(result.code==TS_API_ERR_NO_PERMISSION&&!strcmp(result.message,"automation_configuration_managed"));
    assert(mutations==before);cJSON_Delete(params);cJSON_Delete(result.data);
}
static void original_code_volume(void)
{
    assert(!test_mkdir("/www",0755));assert(!test_mkdir("/www/js",0755));assert(!test_mkdir("/spiffs",0755));
    assert(ts_storage_write_file("/www/js/app.js","original",8)==ESP_OK);
    assert(ts_storage_append("/www/js/app.js","+",1)==ESP_OK);has_text("/www/js/app.js","original+");
    assert(ts_storage_copy("/www/js/app.js","/sdcard/www-export.js")==ESP_OK);has_text("/sdcard/www-export.js","original+");
    assert(ts_storage_replace_file("/spiffs/settings.json","settings",8)==ESP_OK);has_text("/spiffs/settings.json","settings");
    assert(ts_storage_check_external_write_path("/www/js/app.js",false)==ESP_OK);
    assert(ts_storage_check_external_write_path("/autocfg/state.json",false)==ESP_ERR_INVALID_ARG);
    assert(ts_storage_check_external_write_path(NULL,false)==ESP_ERR_INVALID_ARG);
    assert(ts_storage_delete("/spiffs/settings.json")==ESP_OK);assert(ts_storage_delete("/sdcard/www-export.js")==ESP_OK);
    assert(!test_unlink("/www/js/app.js"));assert(!test_rmdir("/www/js"));assert(!test_rmdir("/www"));assert(!test_rmdir("/spiffs"));
    puts("PASS original mounted WWW/SPIFFS read/write/export and direct SPIFFS replacement; managed automation paths remain protected");
}

static void api_scope_denied(ts_api_handler_t handler,const char *json)
{
    cJSON *params=cJSON_Parse(json);assert(params);ts_api_result_t result={0};
    unsigned before=mutations;
    assert(handler(params,&result)==ESP_ERR_INVALID_ARG);
    assert(result.code==TS_API_ERR_INVALID_ARG&&!strcmp(result.message,"storage_path_invalid"));
    assert(mutations==before);cJSON_Delete(params);cJSON_Delete(result.data);
}
static void has_bytes(const char *path,const void *bytes,size_t length)
{
    FILE *file=fopen(mapped(path),"rb");assert(file);
    unsigned char *buffer=malloc(length?length:1);assert(buffer);
    assert(fread(buffer,1,length,file)==length && !memcmp(buffer,bytes,length));
    assert(fgetc(file)==EOF && !ferror(file) && !fclose(file));free(buffer);
}
static void transactional_uploads(void)
{
    const char *path="/sdcard/replacement.bin",*old="ORIGINAL COMPLETE BYTES";
    unsigned char bytes[8193];for(size_t i=0;i<sizeof(bytes);++i)bytes[i]=(unsigned char)(i%251);
    assert(ts_storage_replace_file(path,bytes,sizeof(bytes))==ESP_OK);has_bytes(path,bytes,sizeof(bytes));
    /* OTA caches already exist on SD: raw FAT rename fails, while the shared
     * owner backs up and installs without overwriting a rename destination. */
    assert(ts_storage_mkdir_p("/sdcard/recovery")==ESP_OK);
    const char *caches[]={"/sdcard/recovery/TianShanOS.bin","/sdcard/recovery/www.bin"};
    for(unsigned i=0;i<2;++i){
        original_file(caches[i],old);original_file("/sdcard/recovery/upload.bin","new upload");
        assert(test_rename("/sdcard/recovery/upload.bin",caches[i])==-1 && errno==EEXIST);
        has_text(caches[i],old);has_text("/sdcard/recovery/upload.bin","new upload");
        assert(ts_storage_replace_file(caches[i],bytes,sizeof(bytes))==ESP_OK);
        has_bytes(caches[i],bytes,sizeof(bytes));
        assert(!test_unlink(caches[i]));assert(!test_unlink("/sdcard/recovery/upload.bin"));
    }
    assert(!test_rmdir("/sdcard/recovery"));
    const enum storage_fault failures[]={F_OPEN,F_WRITE,F_FLUSH,F_SYNC,F_CLOSE,F_READBACK,F_BACKUP_RENAME,F_INSTALL_RENAME,F_PENDING_STAT};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);++i){
        original_file(path,old);observed_pending[0]=observed_previous[0]=0;
        storage_fault=failures[i];assert(ts_storage_replace_file(path,bytes,sizeof(bytes))!=ESP_OK);storage_fault=F_NONE;
        has_text(path,old);
        if(observed_pending[0])assert(!ts_storage_exists(observed_pending));
        if(observed_previous[0])assert(!ts_storage_exists(observed_previous));
    }
    original_file(path,old);storage_fault=F_INSTALL_AFTER;
    assert(ts_storage_replace_file(path,bytes,sizeof(bytes))==ESP_OK);storage_fault=F_NONE;has_bytes(path,bytes,sizeof(bytes));
    original_file(path,old);storage_fault=F_ROLLBACK;
    assert(ts_storage_replace_file(path,bytes,sizeof(bytes))==ESP_ERR_NOT_FINISHED);storage_fault=F_NONE;
    assert(!ts_storage_exists(path));has_text(observed_previous,old);has_bytes(observed_pending,bytes,sizeof(bytes));
    ts_file_store_paths_t interrupted={path,observed_pending,observed_previous};
    assert(ts_file_store_recover(&interrupted,false,NULL,NULL)==ESP_OK);has_text(path,old);
    storage_fault=F_OLD_CLEANUP;assert(ts_storage_replace_file(path,bytes,sizeof(bytes))==ESP_OK);storage_fault=F_NONE;
    has_bytes(path,bytes,sizeof(bytes));has_text(observed_previous,old);
    interrupted=(ts_file_store_paths_t){path,observed_pending,observed_previous};
    assert(ts_file_store_recover(&interrupted,false,NULL,NULL)==ESP_OK);
    /* A failed exclusive open is not evidence that we own the new sidecar. */
    original_file(path,old);storage_fault=F_OPEN_RACE;
    assert(ts_storage_replace_file(path,bytes,sizeof(bytes))!=ESP_OK);storage_fault=F_NONE;
    has_text(path,old);has_text(observed_pending,"unrelated");assert(!test_unlink(observed_pending));
    char collision[512];snprintf(collision,sizeof(collision),"%s.n%06lx",path,(unsigned long)(next_nonce+1));original_file(collision,"keep this user file");
    assert(ts_storage_replace_file(path,bytes,sizeof(bytes))==ESP_OK);has_text(collision,"keep this user file");assert(!test_unlink(collision));
    snprintf(collision,sizeof(collision),"%s.o%06lx",path,(unsigned long)(next_nonce+1));original_file(collision,"keep this user file too");
    assert(ts_storage_replace_file(path,bytes,sizeof(bytes))==ESP_OK);has_text(collision,"keep this user file too");assert(!test_unlink(collision));
    /* Keep the full existing 255-byte path allowance, including its last byte. */
    char longest[256]="/sdcard/";memset(longest+8,'q',247);longest[255]=0;
    assert(ts_storage_replace_file(longest,bytes,sizeof(bytes))==ESP_OK);has_bytes(longest,bytes,sizeof(bytes));assert(!test_unlink(longest));
    /* FAT's final-component lookup must preserve the long name on overwrite
     * and rollback. The host VFS does not silently translate the alias. */
    char long_path[512];
    for (unsigned variant=0;variant<2;++variant) {
        if (variant) { memset(alias_leaf,'L',255); alias_leaf[255]=0; }
        snprintf(long_path,sizeof(long_path),"/sdcard/%s",alias_leaf);
        original_file(long_path,old);
        storage_fault=F_INSTALL_RENAME;
        assert(ts_storage_replace_file("/sdcard/LONGOR~1.BIN",bytes,sizeof(bytes))!=ESP_OK);
        storage_fault=F_NONE;has_text(long_path,old);
        assert(!ts_storage_exists("/sdcard/LONGOR~1.BIN"));
        fail_name_lookup=true;unsigned unchanged=mutations;
        assert(ts_storage_replace_file("/sdcard/LONGOR~1.BIN",bytes,sizeof(bytes))!=ESP_OK && mutations==unchanged);
        fail_name_lookup=false;
        assert(ts_storage_replace_file("/sdcard/LONGOR~1.BIN",bytes,sizeof(bytes))==ESP_OK);
        has_bytes(long_path,bytes,sizeof(bytes));assert(!ts_storage_exists("/sdcard/LONGOR~1.BIN"));
        assert(!test_unlink(long_path));
    }
    assert(ts_storage_replace_file("/sdcard/empty.bin",NULL,0)==ESP_OK);has_bytes("/sdcard/empty.bin","",0);assert(!test_unlink("/sdcard/empty.bin"));
    unsigned before=mutations;assert(ts_storage_check_external_write_path("/www/app.js",false)==ESP_OK && mutations==before);
    assert(ts_storage_replace_file("/sdcard/config/automation/state.json",bytes,sizeof(bytes))==ESP_ERR_NOT_ALLOWED && mutations==before);
    /* The actual HTTP handler must use this owner, not the old truncating write. */
    original_file(path,old);httpd_req_t native={.query="path=/sdcard/replacement.bin"};
    ts_http_request_t req={.req=&native,.body=bytes,.body_len=sizeof(bytes)};upload_status=0;storage_fault=F_WRITE;
    assert(file_upload_handler(&req,NULL)==ESP_OK && upload_status==500);storage_fault=F_NONE;has_text(path,old);
    assert(!test_unlink(path));
    puts("PASS shared file replacement: binary/maximum path, short-write/flush/sync/close/readback/rename failures, observed commit, uncertain rollback retains old bytes, collision/exclusive ownership, actual upload preserves old target");
}


int main(void)
{
    assert(mkdtemp(root));
    pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&gate,&attr));assert(!pthread_mutexattr_destroy(&attr));
    assert(!test_mkdir("/sdcard",0755));assert(!test_mkdir("/sdcard/config",0755));
    assert(!test_mkdir("/sdcard/config/automation",0755));assert(!test_mkdir("/autocfg",0755));
    original_file("/sdcard/config/automation/state.json","current");
    original_file("/sdcard/config/ordinary.json","ordinary");
    assert(!test_mkdir("/sdcard/ssh",0755));
    original_file("/sdcard/ssh/sentinel.txt","public test sentinel");
    bool ssh_private=false;
    assert(ts_storage_external_read_requires_admin("/sdcard/ssh/sentinel.txt",&ssh_private)==ESP_OK&&ssh_private);
    has_text("/sdcard/ssh/sentinel.txt","public test sentinel");
    puts("PASS protected SSH sentinel: internal production reader retains original bytes");
    original_file("/autocfg/state.json","emergency");

    original_code_volume();
    const char *owned[]={
        "/autocfg", "/autocfg/state.pending", "/autocfg/../state.json",
        "/sdcard/config/automation", "/sdcard/config/automation/state.previous",
        "/sdcard/CONFIG/Automation/state.json", "/sdcard/config/automation. /state.json",
        "/sdcard/other/../config/./automation/state.json", "/sdcard/../config/automation/state.json",
        "/sdcard/config\\automation\\state.json", "/sdcard/config/AUTOMA~1/state.json",
        "/sdcard/confıg/automation/state.json", "/sdcard/config/automatıon/state.json"
    };
    for(size_t i=0;i<sizeof(owned)/sizeof(owned[0]);++i){
        assert(ts_storage_check_write_path(owned[i],false)==ESP_ERR_NOT_ALLOWED);
        unsigned before=mutations;
        assert(ts_storage_write_file(owned[i],"x",1)==ESP_ERR_NOT_ALLOWED);
        assert(ts_storage_append(owned[i],"x",1)==ESP_ERR_NOT_ALLOWED);
        assert(ts_storage_mkdir_p(owned[i])==ESP_ERR_NOT_ALLOWED);
        assert(ts_storage_copy("/sdcard/config/ordinary.json",owned[i])==ESP_ERR_NOT_ALLOWED);
        FILE *stream=(FILE *)1;
        assert(ts_storage_open_write(owned[i],&stream)==ESP_ERR_NOT_ALLOWED&&!stream);
        assert(mutations==before);
    }
    assert(ts_storage_check_write_path("/sdcard/config\t/not-config",true)==ESP_ERR_INVALID_ARG);
    assert(ts_storage_check_write_path("config/automation",true)==ESP_ERR_INVALID_ARG);
    assert(ts_storage_check_write_path("/spiffs/../config/automation/state.json",false)==ESP_OK);
    assert(ts_storage_check_write_path("/sdcard/config/AUTOMA~2/state.json",false)==ESP_OK);
    assert(ts_storage_check_write_path("/sdcard/config/automation-other/state.json",false)==ESP_OK);

    unsigned before=mutations;
    assert(ts_storage_delete("/sdcard/config/automation/state.json")==ESP_ERR_NOT_ALLOWED);
    assert(ts_storage_rmdir_r("/sdcard/config")==ESP_ERR_NOT_ALLOWED);
    assert(ts_storage_rmdir("/sdcard/config/automation")==ESP_ERR_NOT_ALLOWED);
    assert(ts_storage_rename("/sdcard/config","/sdcard/moved")==ESP_ERR_NOT_ALLOWED);
    assert(ts_storage_rename("/sdcard/config/ordinary.json","/sdcard/config/automation/state.json")==ESP_ERR_NOT_ALLOWED);
    assert(mutations==before);
    api_denied(api_storage_delete,"{\"path\":\"/sdcard/config\"}");
    api_denied(api_storage_rename,"{\"from\":\"/sdcard/config\",\"to\":\"/sdcard/moved\"}");
    api_scope_denied(api_storage_rename,"{\"from\":\"/sdcard/config/ordinary.json\",\"to\":\"/autocfg/state.json\"}");
    api_denied(api_storage_mkdir,"{\"path\":\"/sdcard/config/automation/new\"}");
    before=mutations;
    upload("%2Fsdcard%2Fconfig%2Fautomation%2Fstate.json",409,"automation_configuration_managed");
    upload("/sdcard/config/AUTOMA~1/state.pending",409,"automation_configuration_managed");
    upload("/sdcard/config/automation/new.tscfg",409,"automation_configuration_managed");
    upload("/sdcard/config%09/other.txt",400,"storage_path_invalid");
    assert(mutations==before);
    has_text("/sdcard/config/automation/state.json","current");
    has_text("/sdcard/config/ordinary.json","ordinary");
    has_text("/autocfg/state.json","emergency");

    /* Reading/copying out stays available. Ordinary files keep normal operations. */
    assert(ts_storage_copy("/sdcard/config/automation/state.json","/sdcard/export.json")==ESP_OK);
    has_text("/sdcard/export.json","current");
    assert(ts_storage_mkdir_p("/sdcard/work/nested")==ESP_OK);
    FILE *stream=NULL;assert(ts_storage_open_write("/sdcard/work/nested/a",&stream)==ESP_OK&&stream);
    assert(fwrite("a",1,1,stream)==1&&!fclose(stream));
    assert(ts_storage_append("/sdcard/work/nested/a","b",1)==ESP_OK);
    assert(ts_storage_rename("/sdcard/work/nested/a","/sdcard/work/nested/b")==ESP_OK);
    has_text("/sdcard/work/nested/b","ab");
    cJSON *params=cJSON_Parse("{\"path\":\"/sdcard/export.json\"}");ts_api_result_t result={0};
    assert(api_storage_delete(params,&result)==ESP_OK&&result.code==TS_API_OK);
    cJSON_Delete(params);cJSON_Delete(result.data);
    assert(ts_storage_rmdir_r("/sdcard/work")==ESP_OK&&!ts_storage_exists("/sdcard/work"));

    upload("/sdcard/upload.txt",200,"uploaded");
    has_text("/sdcard/upload.txt","uploaded");
    assert(ts_storage_delete("/sdcard/upload.txt")==ESP_OK);

    httpd_req_t managed_http={.query="path=%2Fsdcard%2Fconfig%2Fautomation%2Fstate.json"};
    ts_http_request_t managed_read={.req=&managed_http};
    before=mutations;upload_status=0;
    assert(file_download_handler(&managed_read,NULL)==ESP_OK&&upload_status==401&&mutations==before);
    managed_read.authenticated=true;
    assert(file_download_handler(&managed_read,NULL)==ESP_OK&&upload_status==403&&mutations==before);
    managed_read.root=true;
    assert(file_download_handler(&managed_read,NULL)==ESP_OK&&mutations==before);
    assert(!strcmp(downloaded_path,"/sdcard/config/automation/state.json")&&downloaded_size==7&&!memcmp(downloaded_body,"current",7));
    transactional_uploads();
    ordinary_names();
    recursive_names();

    /* Cleanup through the raw test VFS, since the configuration owner is private. */
    assert(!test_unlink("/sdcard/ssh/sentinel.txt"));assert(!test_rmdir("/sdcard/ssh"));
    assert(!test_unlink("/sdcard/config/ordinary.json"));assert(!test_unlink("/sdcard/config/automation/state.json"));
    assert(!test_rmdir("/sdcard/config/automation"));assert(!test_rmdir("/sdcard/config"));
    assert(!test_rmdir("/sdcard"));assert(!test_unlink("/autocfg/state.json"));
    assert(!test_rmdir("/autocfg"));assert(!rmdir(root));assert(!pthread_mutex_destroy(&gate));
    puts("PASS storage ownership: actual file/API/upload mutations, whole-parent rejection, FAT alias boundary, read/export and ordinary operations");
}
