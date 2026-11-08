#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_config_pack.h"
#include "ts_crypto.h"
#include "ts_cert.h"
#include "ts_storage.h"
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
static char scratch[512];
static const char *mapped(const char *path,char out[1024]) {
 if(!strncmp(path,"/sdcard",7)){assert(scratch[0]);snprintf(out,1024,"%s%s",scratch,path+7);return out;}return path;
}
FILE *fixture_fopen(const char *path,const char *mode){char out[1024];return fopen(mapped(path,out),mode);}
int fixture_stat(const char *path,struct stat *st){char out[1024];return stat(mapped(path,out),st);}
int fixture_mkdir(const char *path,mode_t mode){char out[1024];return mkdir(mapped(path,out),mode);}
#include "nvs.h"
static unsigned file_writes, directories;
static bool fail_file_write;
static char stored_path[128];
static char *stored_pack;
esp_err_t ts_storage_write_file(const char *path,const void *data,size_t size)
{ (void)path;(void)data;(void)size;assert(!"unexpected non-atomic file export in crypto test");return ESP_FAIL; }
esp_err_t ts_storage_mkdir_p(const char *path){
 assert(!strcmp(path,"/sdcard/config"));directories++;
 if(scratch[0]){char out[1024];if(mkdir(mapped(path,out),0700)&&errno!=EEXIST)return ESP_FAIL;}
 return ESP_OK;
}
esp_err_t ts_storage_replace_file(const char *path,const void *data,size_t size){
 file_writes++;if(fail_file_write)return ESP_FAIL;snprintf(stored_path,sizeof stored_path,"%s",path);
 free(stored_pack);stored_pack=malloc(size+1);assert(stored_pack);memcpy(stored_pack,data,size);stored_pack[size]=0;
 if(scratch[0]){FILE *f=fixture_fopen(path,"wb");if(!f)return ESP_FAIL;bool ok=fwrite(data,1,size,f)==size;return fclose(f)==0&&ok?ESP_OK:ESP_FAIL;}
 return ESP_OK;
}
#include "ts_api.h"
#include <sys/stat.h>
#include <errno.h>
#define heap_caps_malloc(n,c) malloc(n)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGW(...) ((void)0)
esp_err_t ts_cert_init(void){return ESP_OK;}
#define TS_LOGI(...) ((void)0)
#define TS_STRDUP_PSRAM strdup
#include "ts_automation_document.h"
#include "ts_rule_codec.h"
#define PACK_MALLOC malloc
#include "pack_api.inc"
#include "mbedtls/x509_crt.h"
#include "mbedtls/oid.h"
#include <time.h>
static time_t fixture_time;
time_t ts_time_sync_security_time(void) {return time(NULL);}
time_t time(time_t *out){time_t now=fixture_time?fixture_time:TEST_TIME;if(out)*out=now;return now;}
static char *device_key, *device_cert, *ca;
static uint32_t generation=1,clock_generation;
uint32_t ts_time_sync_security_generation(void) {return clock_generation;}
static bool trusted_time=true;
/* Internal NVS is a storage boundary; real pack crypto and file reads stay real. */
static struct {char key[16];ts_config_pack_acceptance_t value;} saved[8],pending;
static unsigned saved_count,accept_commits;
static bool fail_accept_read,fail_accept_set,fail_accept_commit,drop_accept_commit,change_clock_on_status;
static int saved_index(const char *key){for(unsigned i=0;i<saved_count;i++)if(!strcmp(saved[i].key,key))return i;return -1;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"ts_pack_accept"));if(fail_accept_read)return ESP_FAIL;if(mode==NVS_READONLY&&!saved_count)return ESP_ERR_NVS_NOT_FOUND;*h=1;memset(&pending,0,sizeof pending);return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){assert(h==1);int i=saved_index(key);if(i<0)return ESP_ERR_NVS_NOT_FOUND;if(out){assert(*n>=sizeof(saved[i].value));memcpy(out,&saved[i].value,sizeof saved[i].value);}*n=sizeof saved[i].value;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *bytes,size_t n){assert(h==1&&n==sizeof pending.value);if(fail_accept_set)return ESP_FAIL;assert(strlen(key)==15);strcpy(pending.key,key);memcpy(&pending.value,bytes,n);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==1&&pending.key[0]);if(fail_accept_commit)return ESP_FAIL;if(drop_accept_commit)return ESP_OK;int i=saved_index(pending.key);if(i<0){assert(saved_count<8);i=saved_count++;}saved[i]=pending;++accept_commits;return ESP_OK;}

static const char *certificate_after_read;
static esp_err_t certificate_read_error;
static char *read_file(const char *dir,const char *name) {
 char path[512];snprintf(path,sizeof path,"%s/%s",dir,name);
 FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 char *s=malloc(n+1);assert(fread(s,1,n,f)==(size_t)n);s[n]=0;fclose(f);return s;
}
static esp_err_t copy(const char *s,char *out,size_t *n) {
 if(!s)return ESP_ERR_INVALID_STATE;
 size_t len=strlen(s)+1;if(*n<len)return ESP_ERR_INVALID_SIZE;memcpy(out,s,len);*n=len;return ESP_OK;
}
esp_err_t ts_cert_get_private_key(char *s,size_t *n){return copy(device_key,s,n);}
esp_err_t ts_cert_get_certificate(char *s,size_t *n){if(certificate_read_error)return certificate_read_error;int ret=copy(device_cert,s,n);if(ret==ESP_OK&&certificate_after_read){free(device_cert);device_cert=strdup(certificate_after_read);certificate_after_read=NULL;}return ret;}
esp_err_t ts_cert_get_ca_chain(char *s,size_t *n){return copy(ca,s,n);}
esp_err_t ts_cert_parse_certificate(const char *s,size_t n,ts_cert_info_t *info) {
 mbedtls_x509_crt c;mbedtls_x509_crt_init(&c);memset(info,0,sizeof *info);
 if(mbedtls_x509_crt_parse(&c,(const unsigned char *)s,n)){mbedtls_x509_crt_free(&c);return ESP_FAIL;}
 for(mbedtls_x509_name *p=&c.subject;p;p=p->next){
  char *out=NULL;size_t cap=0;
  if(!MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORG_UNIT,&p->oid)){out=info->subject_ou;cap=sizeof info->subject_ou;}
  if(!MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN,&p->oid)){out=info->subject_cn;cap=sizeof info->subject_cn;}
  if(out){size_t len=p->val.len<cap-1?p->val.len:cap-1;memcpy(out,p->val.p,len);out[len]=0;}
 }
 mbedtls_x509_crt_free(&c);return ESP_OK;
}
esp_err_t ts_cert_get_info(ts_cert_info_t *i){return device_cert?ts_cert_parse_certificate(device_cert,strlen(device_cert)+1,i):ESP_ERR_INVALID_STATE;}
esp_err_t ts_cert_get_status(ts_cert_pki_status_t *s){memset(s,0,sizeof *s);s->time_ready=trusted_time;s->generation=generation;if(change_clock_on_status)clock_generation++;return ESP_OK;}
esp_err_t ts_cert_get_pack_snapshot(ts_cert_snapshot_t *s){
 memset(s,0,sizeof *s);if(certificate_read_error)return certificate_read_error;
 if(!device_key||!device_cert)return ESP_ERR_INVALID_STATE;
 s->key=strdup(device_key);s->certificate=strdup(device_cert);s->ca=ca?strdup(ca):NULL;s->generation=generation;return ESP_OK;
}
void ts_cert_free_snapshot(ts_cert_snapshot_t *s){if(s->key)memset(s->key,0,strlen(s->key));free(s->key);free(s->certificate);free(s->ca);memset(s,0,sizeof *s);}

#ifndef PACK_BASELINE
static void expected_fingerprint(const char *pem,char fp[65]) {
 size_t len=strlen(pem);while(len&&strchr(" \t\r\n",pem[len-1]))--len;
 unsigned char hash[32];assert(ts_crypto_hash(TS_HASH_SHA256,pem,len,hash,32)==ESP_OK);
 assert(ts_crypto_hex_encode(hash,32,fp,65)==ESP_OK);
}
static void assert_export(const char *pem) {
 ts_api_result_t response={0};assert(api_config_pack_export_cert(NULL,&response)==ESP_OK&&response.code==TS_API_OK);
 assert(!strcmp(cJSON_GetObjectItem(response.data,"certificate")->valuestring,pem));
 char expected[65];expected_fingerprint(pem,expected);
 assert(!strcmp(cJSON_GetObjectItem(response.data,"fingerprint")->valuestring,expected));cJSON_Delete(response.data);
}
/* Use the actual legacy file reader and strict command-bearing loader. */
static ts_config_pack_result_t load_rule_file(const char *path, ts_config_pack_t **pack) {
 char *bytes=NULL;size_t length=0;*pack=NULL;
 if(file_bytes(path,&bytes,&length)!=ESP_OK)return TS_CONFIG_PACK_ERR_IO;
 ts_config_pack_result_t result=ts_config_pack_load_rule_mem(bytes,length,pack);free(bytes);return result;
}
static void ordinary_consumers(const char *bytes,size_t size,ts_config_pack_result_t expected,const char *content) {
 ts_config_pack_t *pack=NULL;assert(ts_config_pack_load_mem(bytes,size,&pack)==expected);
 if(pack){assert(expected==TS_CONFIG_PACK_OK&&pack->sig_info.valid&&!strcmp(pack->content,content));ts_config_pack_free(pack);}
 char input[1024];snprintf(input,sizeof(input),"%s/input.tscfg",scratch);FILE *f=fopen(input,"wb");assert(f&&fwrite(bytes,1,size,f)==size);assert(!fclose(f));
 ts_config_pack_metadata_t metadata={0};assert(ts_config_pack_validate_file(input,&metadata)==expected);
 char saved[128]={0};assert(ts_config_pack_import(bytes,size,&metadata,saved,sizeof(saved))==expected);
 if(expected==TS_CONFIG_PACK_OK){assert(metadata.sig_info.valid);ts_config_pack_t *loaded=NULL;assert(ts_config_pack_load(saved,&loaded)==TS_CONFIG_PACK_OK);assert(!strcmp(loaded->content,content));ts_config_pack_free(loaded);}
 unlink(input);
}
static void ordinary_identity_cases(const char *dir,const char *certificate_name,const char *key_name) {
 char template[]="/tmp/tianshan-ordinary-pack-XXXXXX";assert(mkdtemp(template));strcpy(scratch,template);
 char *a_cert=strdup(device_cert),*a_key=strdup(device_key),*b_cert=read_file(dir,certificate_name),*b_key=read_file(dir,key_name);
 const char *content="{\"ordinary\":true,\"value\":23}";
 char *a_bytes=NULL,*b_bytes=NULL;size_t a_len=0,b_len=0;
 ts_config_pack_export_opts_t options={.recipient_cert_pem=a_cert,.recipient_cert_len=strlen(a_cert)+1};
 assert(ts_config_pack_create("ordinary",content,strlen(content),&options,&a_bytes,&a_len)==TS_CONFIG_PACK_OK);
 ordinary_consumers(a_bytes,a_len,TS_CONFIG_PACK_OK,content);assert_export(a_cert);
 options.recipient_cert_pem=b_cert;options.recipient_cert_len=strlen(b_cert)+1;
 assert(ts_config_pack_create("ordinary",content,strlen(content),&options,&b_bytes,&b_len)==TS_CONFIG_PACK_OK);
 free(device_cert);free(device_key);device_cert=strdup(b_cert);device_key=strdup(b_key);
 char fp[65],expected[65];expected_fingerprint(b_cert,expected);
 assert(ts_config_pack_get_cert_fingerprint(fp,sizeof(fp))==ESP_OK&&!strcmp(fp,expected));assert_export(b_cert);
 ordinary_consumers(b_bytes,b_len,TS_CONFIG_PACK_OK,content);
 ordinary_consumers(a_bytes,a_len,TS_CONFIG_PACK_ERR_RECIPIENT,content);
 cJSON *bad=cJSON_Parse(b_bytes);cJSON *signature=cJSON_GetObjectItem(cJSON_GetObjectItem(bad,"signature"),"signature");signature->valuestring[0]=signature->valuestring[0]=='A'?'B':'A';char *invalid=cJSON_PrintUnformatted(bad);
 ordinary_consumers(invalid,strlen(invalid),TS_CONFIG_PACK_ERR_SIGNATURE,content);free(invalid);cJSON_Delete(bad);
 /* Hardened import/validate share recipient, trust and decrypt admission. */
 free(device_key);device_key=read_file(dir,"other.key");ts_config_pack_t *pack=NULL;
 assert(ts_config_pack_load_mem(b_bytes,b_len,&pack)==TS_CONFIG_PACK_ERR_DECRYPT&&!pack);
 free(device_key);device_key=strdup(b_key);
 free(device_cert);device_cert=NULL;assert(ts_config_pack_get_cert_fingerprint(fp,sizeof(fp))!=ESP_OK);
 ts_api_result_t response={0};assert(api_config_pack_export_cert(NULL,&response)!=ESP_OK&&!response.data);
 ordinary_consumers(b_bytes,b_len,TS_CONFIG_PACK_ERR_CERT_CHAIN,content);
 device_cert=strdup(b_cert);
 certificate_read_error=ESP_ERR_NO_MEM;assert(ts_config_pack_get_cert_fingerprint(fp,sizeof(fp))==ESP_ERR_NO_MEM);
 ordinary_consumers(b_bytes,b_len,TS_CONFIG_PACK_ERR_NO_MEM,content);certificate_read_error=ESP_OK;
 bool old_time=trusted_time;char *old_ca=ca;trusted_time=false;ca=NULL;
 assert(ts_config_pack_get_cert_fingerprint(fp,sizeof(fp))==ESP_OK);assert_export(b_cert);
 /* Simulate replacement immediately after the export read: response stays on A. */
 free(device_cert);device_cert=strdup(a_cert);certificate_after_read=b_cert;assert_export(a_cert);assert_export(b_cert);
 trusted_time=old_time;ca=old_ca;
 free(device_key);free(device_cert);device_key=strdup(a_key);device_cert=strdup(a_cert);
 free(a_cert);free(a_key);free(b_cert);free(b_key);free(a_bytes);free(b_bytes);
 char path[1024];snprintf(path,sizeof(path),"%s/config/ordinary.tscfg",scratch);assert(!unlink(path));snprintf(path,sizeof(path),"%s/config",scratch);assert(!rmdir(path));assert(!rmdir(scratch));scratch[0]=0;
 puts("PASS F73 ordinary production load/import/file validation with real signatures/ECDH/AES: A, replacement B, reject old/other identity, bad signature/decryption, delete/reinstall; actual export API same-PEM response, no CA/time gate");
}
#endif
#ifdef PACK_EXPORT_PAYLOADS
static void export_payload_cases(const char *dir,const char *path)
{
 FILE *file=fopen(path,"rb");assert(file);fseek(file,0,SEEK_END);long size=ftell(file);assert(size>0);rewind(file);
 char *json=malloc(size+1);assert(json&&fread(json,1,size,file)==(size_t)size);json[size]=0;assert(!fclose(file));
 cJSON *records=cJSON_Parse(json);free(json);assert(cJSON_IsArray(records)&&cJSON_GetArraySize(records)>0);
 cJSON *record;unsigned count=0;
 cJSON_ArrayForEach(record,records){
  const char *name=cJSON_GetStringValue(cJSON_GetObjectItem(record,"name")),*description=cJSON_GetStringValue(cJSON_GetObjectItem(record,"description"));
  const char *content=cJSON_GetStringValue(cJSON_GetObjectItem(record,"content")),*hash=cJSON_GetStringValue(cJSON_GetObjectItem(record,"sha256"));
  const char *target=cJSON_GetStringValue(cJSON_GetObjectItem(record,"recipient"));assert(name&&description&&content&&hash&&target);
  size_t content_len=strlen(content);assert(content_len==(size_t)cJSON_GetObjectItem(record,"length")->valuedouble);
  unsigned char digest[32];char hex[65];assert(ts_crypto_hash(TS_HASH_SHA256,content,content_len,digest,32)==ESP_OK&&ts_crypto_hex_encode(digest,32,hex,65)==ESP_OK&&!strcmp(hex,hash));
  free(device_key);free(device_cert);device_key=read_file(dir,"developer.key");device_cert=read_file(dir,"developer.pem");
  char *recipient=!strcmp(target,"self")?strdup(device_cert):read_file(dir,"recipient.pem");
  ts_config_pack_export_opts_t opts={.recipient_cert_pem=recipient,.recipient_cert_len=strlen(recipient),.description=description};
  char *bytes=NULL;size_t length=0;assert(ts_config_pack_create(name,content,content_len,&opts,&bytes,&length)==TS_CONFIG_PACK_OK&&length==strlen(bytes));
  cJSON *wire=ts_config_pack_parse_json(bytes,length);assert(wire);const cJSON *metadata=cJSON_GetObjectItem(wire,"metadata");
  assert(cJSON_IsString(cJSON_GetObjectItem(metadata,"name"))&&!strcmp(cJSON_GetObjectItem(metadata,"name")->valuestring,name));
  assert(cJSON_IsString(cJSON_GetObjectItem(metadata,"description"))&&!strcmp(cJSON_GetObjectItem(metadata,"description")->valuestring,description));cJSON_Delete(wire);
  if(strcmp(target,"self")){free(device_key);free(device_cert);device_key=read_file(dir,"recipient.key");device_cert=strdup(recipient);}
  ts_config_pack_t *pack=NULL;assert(ts_config_pack_load_verified_mem(bytes,length,NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_OK&&pack);
  assert(pack->content_len==content_len&&!memcmp(pack->content,content,content_len));
  assert(ts_crypto_hash(TS_HASH_SHA256,pack->content,pack->content_len,digest,32)==ESP_OK&&ts_crypto_hex_encode(digest,32,hex,65)==ESP_OK&&!strcmp(hex,hash));
  printf("PASS export payload %s %s %zu %s: actual signed/encrypted create + verified decrypt, exact content/hash; original outbound metadata\n",name,target,content_len,hash);
  ts_config_pack_free(pack);free(bytes);free(recipient);++count;
 }
 cJSON_Delete(records);printf("PASS %u exact configuration-owner export payload records through production PKI crypto\n",count);
}
#endif
int main(int argc,char **argv) {
#ifdef PACK_EXPORT_PAYLOADS
 assert(argc==3);
#else
 assert(argc==2);
#endif
 const char *dir=argv[1];
 device_key=read_file(dir,"developer.key");device_cert=read_file(dir,"developer.pem");ca=read_file(dir,"root.pem");
#ifdef PACK_BASELINE
 assert(ts_config_pack_init()==ESP_OK);
#else
 char *initial=device_cert;device_cert=NULL;assert(ts_config_pack_init()==ESP_OK);
 char initial_fp[65];assert(ts_config_pack_get_cert_fingerprint(initial_fp,sizeof(initial_fp))!=ESP_OK);
 device_cert=initial;char expected[65];expected_fingerprint(device_cert,expected);
 assert(ts_config_pack_get_cert_fingerprint(initial_fp,sizeof(initial_fp))==ESP_OK&&!strcmp(initial_fp,expected));
 mbedtls_x509_crt identity;mbedtls_x509_crt_init(&identity);assert(!mbedtls_x509_crt_parse(&identity,(unsigned char*)device_cert,strlen(device_cert)+1));
 unsigned char der_hash[32];char der_fp[65];assert(ts_crypto_hash(TS_HASH_SHA256,identity.raw.p,identity.raw.len,der_hash,32)==ESP_OK);assert(ts_crypto_hex_encode(der_hash,32,der_fp,65)==ESP_OK&&strcmp(initial_fp,der_fp));mbedtls_x509_crt_free(&identity);
 puts("PASS F73 first certificate after init is current; historical trimmed-PEM identity differs from DER");
 #ifdef PACK_ORDINARY_TLS
 free(device_cert);free(device_key);device_cert=read_file(dir,"tls-developer.pem");device_key=read_file(dir,"tls-developer.key");
 ordinary_identity_cases(dir,"recipient.pem","recipient.key");
 char *local=NULL;size_t local_len=0;
 ts_config_pack_export_opts_t local_opts={.recipient_cert_pem=device_cert,.recipient_cert_len=strlen(device_cert)+1};
 assert(ts_config_pack_create("offline-data","{\"ordinary\":true}",17,&local_opts,&local,&local_len)==TS_CONFIG_PACK_OK);
 ts_config_pack_t *data_pack=NULL;
 assert(ts_config_pack_load_rule_mem(local,local_len,&data_pack)==TS_CONFIG_PACK_ERR_SIGNER_ROLE&&!data_pack);
 char *selected_ca=ca;ca=read_file(dir,"alternate-root.pem");
 assert(ts_config_pack_load_mem(local,local_len,&data_pack)!=TS_CONFIG_PACK_OK&&!data_pack);free(ca);ca=selected_ca;
 char offline_path[1024];snprintf(offline_path,sizeof(offline_path),"%s/offline-data.tscfg",dir);
 FILE *offline_file=fopen(offline_path,"wb");assert(offline_file&&fwrite(local,1,local_len,offline_file)==local_len&&!fclose(offline_file));
 memset(saved,0,sizeof(saved));saved_count=accept_commits=0;trusted_time=false;fixture_time=1;
 assert(ts_config_pack_load(offline_path,&data_pack)==TS_CONFIG_PACK_OK&&data_pack&&!data_pack->acceptance.accepted_at);
 ts_config_pack_free(data_pack);data_pack=NULL;assert(!saved_count&&!accept_commits);
 assert(ts_config_pack_load_mem(local,local_len,&data_pack)==TS_CONFIG_PACK_ERR_TIME_UNVERIFIED&&!data_pack);
 assert(ts_config_pack_load_rule_mem(local,local_len,&data_pack)==TS_CONFIG_PACK_ERR_TIME_UNVERIFIED&&!data_pack);
 assert(!unlink(offline_path));free(local);fixture_time=0;trusted_time=true;
 free(device_cert);free(device_key);free(ca);puts("PASS original TLS Developer ordinary round-trip and existing local data before NTP; unselected CA rejected; code-signing and new-import time boundaries retained");return 0;
 #endif
 #ifndef PACK_UNCONFIGURED
 ordinary_identity_cases(dir,"developer-renewed.pem","developer.key");
 ordinary_identity_cases(dir,"recipient.pem","recipient.key");
 /* Separate the upstream identity scenario from the original admission scenario. */
 memset(saved,0,sizeof saved);memset(&pending,0,sizeof pending);saved_count=accept_commits=0;
 file_writes=directories=0;free(stored_pack);stored_pack=NULL;stored_path[0]=0;
 #endif
#endif
#ifdef PACK_EXPORT_PAYLOADS
 export_payload_cases(dir,argv[2]);free(device_key);free(device_cert);free(ca);return 0;
#endif
 char *recipient=read_file(dir,"recipient.pem");
 const char *content="{\"type\":\"automation_rule\",\"rule\":{\"id\":\"night\",\"name\":\"Night\",\"enabled\":true,\"actions\":[{\"type\":\"log\",\"message\":\"test\"}]}}";
 ts_config_pack_export_opts_t opts={.recipient_cert_pem=recipient,.recipient_cert_len=strlen(recipient),.description="retained metadata"};
#ifndef PACK_BASELINE
 char *exact=malloc(opts.recipient_cert_len);memcpy(exact,recipient,opts.recipient_cert_len);opts.recipient_cert_pem=exact;
#endif
 char *bytes=NULL;size_t length=0;
 ts_config_pack_result_t made=ts_config_pack_create("ignored-name",content,strlen(content),&opts,&bytes,&length);
#ifndef PACK_BASELINE
 free(exact);
#endif
#ifdef PACK_BASELINE
 printf("baseline explicit recipient PEM length: create=%d (expected OK)\n",made);
 fflush(stdout);
 free(bytes);free(recipient);free(device_key);free(device_cert);free(ca);
 assert(made==TS_CONFIG_PACK_OK);
#elif defined(PACK_UNCONFIGURED)
 assert(made==TS_CONFIG_PACK_OK);
 free(device_key);free(device_cert);device_key=read_file(dir,"recipient.key");device_cert=strdup(recipient);
 ts_config_pack_t *pack=NULL;
 assert(ts_config_pack_load_verified_mem(bytes,length,NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_TRUST_NOT_CONFIGURED&&!pack);
 free(bytes);free(recipient);free(device_key);free(device_cert);free(ca);
 puts("PASS unconfigured signing root refuses a cryptographically valid package");
#else
 assert(made==TS_CONFIG_PACK_OK&&bytes&&length==strlen(bytes));
 ts_config_pack_sig_info_t outbound;
 assert(ts_config_pack_verify_mem(bytes,length,&outbound)==TS_CONFIG_PACK_OK&&outbound.valid&&!saved_count);

 ts_config_pack_export_opts_t self_opts={.recipient_cert_pem=device_cert,.recipient_cert_len=strlen(device_cert)+1};
 char *self=NULL;size_t self_len=0;ts_config_pack_t *self_pack=NULL;
 assert(ts_config_pack_create("self",content,strlen(content),&self_opts,&self,&self_len)==TS_CONFIG_PACK_OK);
 assert(ts_config_pack_load_verified_mem(self,self_len,NULL,&self_pack,NULL,NULL)==TS_CONFIG_PACK_OK&&!strcmp(self_pack->content,content));
 ts_config_pack_free(self_pack);free(self);
 free(device_key);free(device_cert);device_key=read_file(dir,"recipient.key");device_cert=strdup(recipient);
 ts_config_pack_t *pack=NULL;ts_config_pack_acceptance_t accepted={0};uint32_t gen=0;
 assert(ts_config_pack_load_verified_mem(bytes,length,NULL,&pack,&accepted,&gen)==TS_CONFIG_PACK_OK);
 assert(gen==generation&&!strcmp(pack->content,content));ts_config_pack_free(pack);
 /* File and legacy-memory entry points now share exact durable acceptance. */
 assert(ts_config_pack_load_rule_mem(bytes,length,&pack)==TS_CONFIG_PACK_OK);
 assert(!memcmp(&pack->acceptance,&accepted,sizeof accepted));
 assert(pack->name&&!strcmp(pack->name,"ignored-name")&&pack->description&&!strcmp(pack->description,"retained metadata")&&pack->sig_info.is_official);ts_config_pack_free(pack);
 assert(saved_count==1&&accept_commits==1);
 ts_config_pack_sig_info_t verified_info;
 assert(ts_config_pack_verify_mem(bytes,length,&verified_info)==TS_CONFIG_PACK_OK&&verified_info.valid&&verified_info.is_official);
 cJSON *request=cJSON_CreateObject();cJSON_AddStringToObject(request,"content",bytes);ts_api_result_t api={0};
 assert(api_config_pack_verify(request,&api)==ESP_OK&&api.code==TS_API_OK&&cJSON_IsTrue(cJSON_GetObjectItem(api.data,"valid")));ts_api_result_free(&api);
 assert(api_config_pack_import(request,&api)==ESP_OK&&api.code==TS_API_OK&&file_writes==1&&directories==1);
 assert(stored_pack&&!strcmp(stored_pack,bytes)&&!strcmp(stored_path,"/sdcard/config/ignored-name.tscfg"));
 assert(!strcmp(cJSON_GetObjectItem(api.data,"saved_path")->valuestring,stored_path));ts_api_result_free(&api);
 char *old_stored=stored_pack;fail_file_write=true;
 assert(api_config_pack_import(request,&api)!=ESP_OK&&api.code!=TS_API_OK&&stored_pack==old_stored&&!strcmp(stored_pack,bytes));
 ts_api_result_free(&api);fail_file_write=false;cJSON_Delete(request);

 char config_path[1024],json_path[1024];snprintf(config_path,sizeof config_path,"%s/boot.tscfg",dir);snprintf(json_path,sizeof json_path,"%s/boot.json",dir);
 FILE *boot=fopen(config_path,"wb");assert(boot&&fwrite(bytes,1,length,boot)==length&&!fclose(boot));
 boot=fopen(json_path,"wb");assert(boot&&fputs("{\"untrusted_fallback\":true}",boot)>=0&&!fclose(boot));
 ts_config_pack_metadata_t imported;
 assert(ts_config_pack_validate_file(config_path,&imported)==TS_CONFIG_PACK_OK&&imported.sig_info.valid&&imported.created_at>0&&!strcmp(imported.name,"ignored-name"));
 request=cJSON_CreateObject();cJSON_AddStringToObject(request,"path",config_path);
 assert(api_config_pack_import(request,&api)==ESP_OK&&api.code==TS_API_OK);ts_api_result_free(&api);cJSON_Delete(request);
 assert(ts_config_pack_verify(config_path,&verified_info)==TS_CONFIG_PACK_OK&&verified_info.valid);
 trusted_time=false;
 assert(ts_config_pack_verify(config_path,&verified_info)==TS_CONFIG_PACK_OK&&verified_info.valid);
 assert(ts_config_pack_validate_file(config_path,&imported)==TS_CONFIG_PACK_OK);
 assert(ts_config_pack_load(config_path,&pack)==TS_CONFIG_PACK_OK&&!strcmp(pack->content,content));ts_config_pack_free(pack);assert(accept_commits==1);
 char *selected=NULL;size_t selected_length=0;bool encrypted=false;
 assert(ts_config_pack_load_with_priority(json_path,&selected,&selected_length,&encrypted)==ESP_OK&&encrypted&&!strcmp(selected,content));free(selected);
 /* Same valid signature with one extra whitespace byte is a different package. */
 boot=fopen(config_path,"ab");assert(boot&&fputc(' ',boot)!=EOF&&!fclose(boot));
 /* Ordinary local data retains its original offline behavior; executable
  * legacy automation still requires exact admitted bytes. */
 assert(ts_config_pack_load_with_priority(json_path,&selected,&selected_length,&encrypted)==ESP_OK&&encrypted&&!strcmp(selected,content));free(selected);
 assert(load_rule_file(config_path,&pack)==TS_CONFIG_PACK_ERR_TIME_UNVERIFIED&&!pack);
 trusted_time=true;
 drop_accept_commit=true;assert(load_rule_file(config_path,&pack)==TS_CONFIG_PACK_ERR_IO&&!pack);drop_accept_commit=false;
 fail_accept_set=true;assert(load_rule_file(config_path,&pack)==TS_CONFIG_PACK_ERR_IO&&!pack);fail_accept_set=false;
 fail_accept_commit=true;assert(load_rule_file(config_path,&pack)==TS_CONFIG_PACK_ERR_IO&&!pack);fail_accept_commit=false;assert(saved_count==1&&accept_commits==1);
 assert(load_rule_file(config_path,&pack)==TS_CONFIG_PACK_OK);ts_config_pack_free(pack);assert(saved_count==2);
 trusted_time=false;assert(load_rule_file(config_path,&pack)==TS_CONFIG_PACK_OK);ts_config_pack_free(pack);
 fail_accept_read=true;assert(load_rule_file(config_path,&pack)==TS_CONFIG_PACK_ERR_IO&&!pack);fail_accept_read=false;
 /* An NVS index hit cannot substitute another complete package identity. */
 saved[1].value.package_sha256[0]^=1;assert(load_rule_file(config_path,&pack)==TS_CONFIG_PACK_ERR_TIME_UNVERIFIED&&!pack);saved[1].value.package_sha256[0]^=1;
 assert(remove(config_path)==0);assert(ts_config_pack_load_with_priority(json_path,&selected,&selected_length,&encrypted)==ESP_OK&&!encrypted&&strstr(selected,"untrusted_fallback"));free(selected);assert(remove(json_path)==0);
 trusted_time=true;change_clock_on_status=true;
 assert(ts_config_pack_load_verified_mem(bytes,length,NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_CREDENTIAL_CHANGED&&!pack);change_clock_on_status=false;

 /* Expiry is checked when first accepting; accepted exact bytes may reload. */
 fixture_time=TEST_TIME+4000LL*86400;
 assert(ts_config_pack_load_verified_mem(bytes,length,NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_CERT_CHAIN);
 assert(ts_config_pack_load_verified_mem(bytes,length,&accepted,&pack,NULL,NULL)==TS_CONFIG_PACK_OK);ts_config_pack_free(pack);
 fixture_time=0;
 ts_config_pack_acceptance_t changed=accepted;changed.policy++;
 assert(ts_config_pack_load_verified_mem(bytes,length,&changed,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_CERT_CHAIN);
 changed=accepted;changed.root_sha256[0]^=1;
 assert(ts_config_pack_load_verified_mem(bytes,length,&changed,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_CERT_CHAIN);
 changed=accepted;changed.package_sha256[0]^=1;
 assert(ts_config_pack_load_verified_mem(bytes,length,&changed,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_CERT_CHAIN);
 changed=accepted;changed.signer_sha256[0]^=1;
 assert(ts_config_pack_load_verified_mem(bytes,length,&changed,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_CERT_CHAIN);
 changed=accepted;changed.recipient[0]^=1;
 assert(ts_config_pack_load_verified_mem(bytes,length,&changed,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_CERT_CHAIN);
 trusted_time=false;
 assert(ts_config_pack_load_verified_mem(bytes,length,NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_TIME_UNVERIFIED);
 assert(ts_config_pack_load_verified_mem(bytes,length,&accepted,&pack,NULL,NULL)==TS_CONFIG_PACK_OK);ts_config_pack_free(pack);
 trusted_time=true;
 char *save=device_cert;device_cert=read_file(dir,"other.pem");
 assert(ts_config_pack_load_verified_mem(bytes,length,NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_RECIPIENT);
 free(device_cert);device_cert=save;
 const char *unauthorized[]={"not-developer.pem","tls-developer.pem","no-sign.pem","p384.pem","duplicate-role.pem"};
 for(unsigned i=0;i<sizeof unauthorized/sizeof *unauthorized;++i){
  cJSON *j=cJSON_Parse(bytes);char *certificate=read_file(dir,unauthorized[i]);
  assert(cJSON_ReplaceItemInObject(cJSON_GetObjectItem(j,"signature"),"signer_certificate",cJSON_CreateString(certificate)));
  char *bad=cJSON_PrintUnformatted(j);
  assert(ts_config_pack_load_verified_mem(bad,strlen(bad),NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_SIGNER_ROLE);
  free(certificate);free(bad);cJSON_Delete(j);
 }
 char *old_ca=ca,*alternate=read_file(dir,"alternate-root.pem");
 ca=malloc(strlen(old_ca)+strlen(alternate)+1);strcpy(ca,old_ca);strcat(ca,alternate);
 cJSON *foreign=cJSON_Parse(bytes);char *foreign_cert=read_file(dir,"alternate-developer.pem");
 assert(cJSON_ReplaceItemInObject(cJSON_GetObjectItem(foreign,"signature"),"signer_certificate",cJSON_CreateString(foreign_cert)));
 char *foreign_bytes=cJSON_PrintUnformatted(foreign);
 assert(ts_config_pack_load_verified_mem(foreign_bytes,strlen(foreign_bytes),NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_CERT_CHAIN);
 free(foreign_bytes);free(foreign_cert);cJSON_Delete(foreign);free(ca);free(alternate);ca=old_ca;
 cJSON *j=cJSON_Parse(bytes);cJSON *enc=cJSON_GetObjectItem(j,"encryption");
 cJSON_ReplaceItemInObject(enc,"salt",cJSON_CreateNumber(7));char *bad=cJSON_PrintUnformatted(j);
 assert(ts_config_pack_load_verified_mem(bad,strlen(bad),NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_PARSE);
 free(bad);cJSON_Delete(j);
 j=cJSON_Parse(bytes);enc=cJSON_GetObjectItem(j,"encryption");
 char *tag=cJSON_GetObjectItem(enc,"tag")->valuestring;tag[0]=tag[0]=='A'?'B':'A';bad=cJSON_PrintUnformatted(j);
 assert(ts_config_pack_load_verified_mem(bad,strlen(bad),NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_DECRYPT);
 free(bad);cJSON_Delete(j);
 j=cJSON_Parse(bytes);enc=cJSON_GetObjectItem(j,"encryption");
 cJSON_ReplaceItemInObject(enc,"iv",cJSON_CreateString("AA=="));bad=cJSON_PrintUnformatted(j);
 assert(ts_config_pack_load_verified_mem(bad,strlen(bad),NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_PARSE);
 free(bad);cJSON_Delete(j);
 assert(!ts_config_pack_parse_json("{\"x\":1,\"x\":2}",13));
 assert(!ts_config_pack_parse_json("{} {}",5));
 assert(!ts_config_pack_parse_json("{\"x\":\"\\u0000\"}",14));
 assert(!ts_config_pack_parse_import_request("{\"tscfg\":\"ok\\u0000tail\"}",24));
 assert(!ts_config_pack_parse_import_request("{\"preview\":true,\"preview\":false}",32));
 cJSON *empty=ts_config_pack_parse_json("{}",2);assert(empty);cJSON_Delete(empty);
 j=cJSON_Parse(bytes);char *cipher=cJSON_GetObjectItem(j,"payload")->valuestring;
 cipher[0]=cipher[0]=='A'?'B':'A';bad=cJSON_PrintUnformatted(j);
 assert(ts_config_pack_load_verified_mem(bad,strlen(bad),NULL,&pack,NULL,NULL)==TS_CONFIG_PACK_ERR_SIGNATURE);
 free(bad);cJSON_Delete(j);
 free(stored_pack);stored_pack=NULL;free(bytes);free(recipient);free(device_key);free(device_cert);free(ca);
 puts("PASS real production create/trust/signature/ECDH/HKDF/GCM, cross-device PEM, offline accepted reload, malformed inputs");
#endif
 return 0;
}
