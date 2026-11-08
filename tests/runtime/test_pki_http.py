#!/usr/bin/env python3
"""Actual PKI operations and request-local replies; synthetic transport/cert/NVS.
Shared real HTTP/TLS framing, DNS and deadlines run in test_http_fetch.py.
"""
from pathlib import Path
import re
from test_console_lifecycle import RTOS,IDF,run_native,body
root=Path(__file__).resolve().parents[2]
source=(root/'components/ts_pki_client/src/ts_pki_client.c').read_text()
code=RTOS+r'''
typedef int StaticSemaphore_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) pthread_mutex_lock(p)
#define portEXIT_CRITICAL(p) pthread_mutex_unlock(p)
static SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *storage){(void)storage;return xSemaphoreCreateRecursiveMutex();}

#include "ts_pki_client.h"
#include "ts_cert.h"
#include "ts_http_fetch.h"
#include "ts_http_url.h"
#include "cJSON.h"
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#ifndef ESP_ERR_INVALID_RESPONSE
#define ESP_ERR_INVALID_RESPONSE 0x108
#endif
static atomic_uint checks,failures,json_allocs,json_frees;
static atomic_int json_budget=-1;
static pthread_mutex_t log_mutex=PTHREAD_MUTEX_INITIALIZER;
static char diagnostics[16384];
static void log_capture(const char *tag,const char *format,...){(void)tag;pthread_mutex_lock(&log_mutex);size_t n=strlen(diagnostics);va_list args;va_start(args,format);vsnprintf(diagnostics+n,sizeof(diagnostics)-n,format,args);va_end(args);pthread_mutex_unlock(&log_mutex);}
#undef ESP_LOGD
#undef ESP_LOGI
#undef ESP_LOGE
#undef ESP_LOGW
#define ESP_LOGD log_capture
#define ESP_LOGI log_capture
#define ESP_LOGE log_capture
#define ESP_LOGW log_capture
static void check(bool ok,const char *why){checks++;if(!ok){failures++;fprintf(stderr,"FAIL %s\n",why);}}
static pthread_mutex_t json_mutex=PTHREAD_MUTEX_INITIALIZER;
static void *json_live[128];
static void *json_alloc(size_t n){if(json_budget==0)return NULL;if(json_budget>0)json_budget--;void *p=malloc(n);if(p){pthread_mutex_lock(&json_mutex);unsigned i=0;while(i<128&&json_live[i])i++;assert(i<128);json_live[i]=p;json_allocs++;pthread_mutex_unlock(&json_mutex);}return p;}
static void json_free(void *p){if(p){pthread_mutex_lock(&json_mutex);for(unsigned i=0;i<128;i++)if(json_live[i]==p){json_live[i]=NULL;json_frees++;break;}pthread_mutex_unlock(&json_mutex);}free(p);}
static _Thread_local const char *fixture_reply="{\"status\":\"pending\",\"request_id\":7}";
static _Thread_local int fixture_status=200;
static _Thread_local esp_err_t fixture_error=ESP_OK;
static _Thread_local bool hold_response;
static SemaphoreHandle_t reply_entered,reply_release;
static atomic_uint requests,certificate_installs,ca_installs;
static char requested_url[256];static unsigned captured_timeout;
static SemaphoreHandle_t enroll_wait_entered;
static void tracked_delay(unsigned ticks){if(enroll_wait_entered)xSemaphoreGive(enroll_wait_entered);vTaskDelay(ticks);}
#define vTaskDelay tracked_delay
static bool reject_certificate,reject_ca,stored_ca_valid=true,has_stored_certificate;
#define ESP_MAC_WIFI_STA 1
static esp_err_t esp_read_mac(uint8_t *mac,int role){(void)role;memset(mac,0x11,6);return ESP_OK;}
esp_err_t ts_cert_get_status(ts_cert_pki_status_t *out){memset(out,0,sizeof(*out));out->has_ca_chain=out->ca_valid=stored_ca_valid;out->has_certificate=out->has_private_key=has_stored_certificate;out->cert_info.validity=TS_CERT_VALIDITY_VALID;return ESP_OK;}
bool ts_cert_has_keypair(void){return true;}
esp_err_t ts_cert_generate_keypair(void){return ESP_OK;}
esp_err_t ts_cert_generate_csr_default(char *out,size_t *n){const char *csr="SYNTHETIC_PUBLIC_CSR";assert(*n>strlen(csr));strcpy(out,csr);*n=strlen(csr)+1;return ESP_OK;}
esp_err_t ts_cert_install_certificate(const char *pem,size_t n){certificate_installs++;check(n==strlen(pem)+1&&!strcmp(pem,"SYNTHETIC_CERTIFICATE"),"exact requested certificate reaches material owner");return reject_certificate?ESP_FAIL:ESP_OK;}
esp_err_t ts_cert_install_ca_chain(const char *pem,size_t n){ca_installs++;check(n==strlen(pem)+1&&!strcmp(pem,"SYNTHETIC_CA_CHAIN"),"exact requested CA reaches material owner");return reject_ca?ESP_FAIL:ESP_OK;}
esp_err_t ts_cert_install_issued_certificate_ex(const char *pem,size_t n,ts_cert_op_error_t *detail){
 (void)detail;certificate_installs++;check(n==strlen(pem)+1&&!strcmp(pem,"SYNTHETIC_CERTIFICATE"),"exact certificate uses the single issuer-verifying material owner");return reject_certificate?ESP_FAIL:stored_ca_valid?ESP_OK:ESP_ERR_INVALID_STATE;
}
bool ts_cert_prerequisites(const ts_cert_pki_status_t *s,bool ca){(void)s;(void)ca;return false;}
esp_err_t ts_http_fetch(const ts_http_fetch_config_t *req,ts_http_fetch_result_t *out){
    captured_timeout=req->timeout_ms;snprintf(requested_url,sizeof(requested_url),"%s",req->url);requests++;check(req->timeout_ms>0&&req->timeout_ms<=TS_PKI_HTTP_TIMEOUT_MS&&req->maximum_body==8191,"existing PKI 30s total/8KiB reply limit passes to shared owner");
    check(req->follow_redirects,"legacy PKI perform consumer explicitly follows redirects");
    check(req->method==HTTP_METHOD_GET||req->method==HTTP_METHOD_POST,"PKI methods retained");
    if(req->method==HTTP_METHOD_POST){check(req->content_type&&!strcmp(req->content_type,"application/json")&&req->body,"CSR JSON content type/body retained");int saved_budget=atomic_exchange(&json_budget,-1);cJSON *csr=cJSON_Parse(req->body);check(csr&&cJSON_IsString(cJSON_GetObjectItem(csr,"csr_pem"))&&cJSON_IsString(cJSON_GetObjectItem(csr,"device_id"))&&cJSON_IsString(cJSON_GetObjectItem(csr,"description"))&&cJSON_IsArray(cJSON_GetObjectItem(csr,"suggested_san_ips")),"every transmitted CSR body retains all required fields");cJSON_Delete(csr);json_budget=saved_budget;}
    if(fixture_error!=ESP_OK)return fixture_error;
    out->status=fixture_status;out->length=strlen(fixture_reply);out->body=strdup(fixture_reply);
    if(hold_response){xSemaphoreGive(reply_entered);xSemaphoreTake(reply_release,portMAX_DELAY);}
    return ESP_OK;
}
#define free json_free
'''+re.sub(r'^#include[^\n]*\n','',source,flags=re.M)+r'''
#undef vTaskDelay
static unsigned dependency_stops;
void ts_cert_deinit(void){dependency_stops++;}
static void ts_known_hosts_deinit(void){dependency_stops++;}
static void ts_keystore_deinit(void){dependency_stops++;}
static void ts_security_deinit(void){dependency_stops++;}
'''+body(root/'main/ts_services.c','security_service_stop')+r'''
static void reset_reply(const char *reply,int status){fixture_reply=reply;fixture_status=status;fixture_error=ESP_OK;reject_certificate=reject_ca=false;}
static void *held_status(void *cancel){hold_response=true;reset_reply("{\"status\":\"approved\"}",200);ts_pki_enroll_status_t status;esp_err_t ret=ts_pki_client_check_status(&status);check(cancel?ret!=ESP_OK&&status==TS_PKI_ENROLL_ERROR:ret==ESP_OK&&status==TS_PKI_ENROLL_APPROVED,"held reply is either its own approved result or cooperatively cancelled by deinit");return NULL;}
static void *blocking_enrollment(void *unused){(void)unused;reset_reply("{\"status\":\"pending\",\"request_id\":7}",200);check(ts_pki_client_enroll_blocking(60)==ESP_ERR_INVALID_STATE,"blocking workflow cooperatively exits while waiting after deinit cancellation");return NULL;}
int main(void){
    cJSON_Hooks hooks={json_alloc,json_free};cJSON_InitHooks(&hooks);
    ts_pki_client_config_t config;ts_pki_client_get_default_config(&config);config.auto_start=false;config.use_https=true;strcpy(config.server_host,"synthetic-host.invalid");
    check(ts_pki_client_init_with_config(&config)==ESP_OK,"PKI init no global HTTP buffer allocation");
    check(ts_pki_client_submit_csr()==ESP_OK&&s_client.request_id==7,"manual CSR submission and request ID retained");
    const char *states[]={"approved","rejected","pending","unknown"};const ts_pki_enroll_status_t want[]={TS_PKI_ENROLL_APPROVED,TS_PKI_ENROLL_REJECTED,TS_PKI_ENROLL_PENDING,TS_PKI_ENROLL_ERROR};
    for(unsigned i=0;i<4;i++){char text[128];snprintf(text,sizeof(text),"{\"status\":\"%s\"}",states[i]);reset_reply(text,200);ts_pki_enroll_status_t status;check(ts_pki_client_check_status(&status)==ESP_OK&&status==want[i],"complete status JSON maps unchanged");}
    const char *invalid[]={"", "not-json", "[]", "null", "true", "{} trailing", "{\"status\":\"approved\"}{}"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){reset_reply(invalid[i],200);s_client.request_id=7;ts_pki_enroll_status_t status;check(ts_pki_client_check_status(&status)!=ESP_OK&&status==TS_PKI_ENROLL_ERROR,"invalid/trailing/non-object response cannot confirm status");check(ts_pki_client_submit_csr()!=ESP_OK,"invalid successful HTTP body cannot confirm CSR acceptance");unsigned before=certificate_installs;check(ts_pki_client_install_certificate()!=ESP_OK&&certificate_installs==before,"invalid body has no certificate effects");}
    for(unsigned i=0;i<3;i++){const int codes[]={404,503,201};reset_reply("{\"status\":\"approved\"}",codes[i]);s_client.request_id=7;ts_pki_enroll_status_t status;check(ts_pki_client_check_status(&status)==ESP_OK&&status==(codes[i]==404?TS_PKI_ENROLL_NOT_FOUND:TS_PKI_ENROLL_ERROR),"non-status HTTP result never confirms approval");unsigned before=certificate_installs;check(ts_pki_client_install_certificate()!=ESP_OK&&certificate_installs==before,"unexpected status cannot install");}
    reset_reply("{\"status\":\"pending\"}",200);check(ts_pki_client_install_certificate()!=ESP_OK,"pending response cannot install");
    reset_reply("{\"status\":\"approved\"}",200);check(ts_pki_client_install_certificate()!=ESP_OK,"approved without certificate cannot install");
    const char *approved="{\"request_id\":8,\"status\":\"approved\",\"certificate\":\"SYNTHETIC_CERTIFICATE\",\"ca_chain\":\"SYNTHETIC_CA_CHAIN\"}";
    reset_reply(approved,200);check(ts_pki_client_install_certificate()==ESP_OK,"approved identity installed without a peer trust-root write");
    reset_reply(approved,201);check(ts_pki_client_submit_csr()==ESP_OK&&s_client.request_id==8,"auto-issued identity uses existing owner trust");
    reset_reply(approved,200);reject_certificate=true;unsigned before_ca=ca_installs;check(ts_pki_client_install_certificate()!=ESP_OK&&ca_installs==before_ca,"failed leaf install cannot continue CA install");
    reset_reply(approved,200);stored_ca_valid=false;check(ts_pki_client_install_certificate()!=ESP_OK&&!ca_installs,"missing pretrusted issuer cannot be replaced by peer CA response");stored_ca_valid=true;
    reset_reply("{\"status\":\"approved\",\"certificate\":\"SYNTHETIC_CERTIFICATE\",\"ca_chain\":\"ATTACKER_CA_IGNORED\"}",200);check(ts_pki_client_install_certificate()==ESP_OK&&!ca_installs,"peer CA does not become a trust root when signed identity owner accepts leaf");
    reset_reply("not-json-health-ack",200);check(ts_pki_client_is_server_reachable(),"health status can acknowledge without parsing a JSON body");
    reset_reply("{}",503);check(!ts_pki_client_is_server_reachable(),"health non200 failure retained");
    reset_reply("{}",200);fixture_error=ESP_ERR_TIMEOUT;ts_pki_enroll_status_t status;check(ts_pki_client_check_status(&status)!=ESP_OK&&status==TS_PKI_ENROLL_ERROR&&!ts_pki_client_is_server_reachable(),"shared transport errors cannot publish stale body");
    reply_entered=xSemaphoreCreateBinary();reply_release=xSemaphoreCreateBinary();pthread_t thread;assert(!pthread_create(&thread,NULL,held_status,NULL));xSemaphoreTake(reply_entered,portMAX_DELAY);
    reset_reply("{\"status\":\"rejected\"}",200);check(ts_pki_client_check_status(&status)==ESP_ERR_INVALID_STATE,"concurrent request returns busy instead of sharing mutable workflow state");check(ts_pki_client_set_server("changed.invalid",443)==ESP_ERR_INVALID_STATE,"configuration cannot change in-flight target");xSemaphoreGive(reply_release);assert(!pthread_join(thread,NULL));vSemaphoreDelete(reply_entered);vSemaphoreDelete(reply_release);
    reset_reply("{}",200);int http_code=0;check(do_http_request("https://synthetic-host.invalid/api?token=SYNTHETIC_PKI_QUERY",HTTP_METHOD_GET,NULL,&http_code,NULL)==ESP_OK&&http_code==200,"private query still reaches request owner");
    // Response ownership includes JSON allocation failure and retry.
    for(int budget=0;budget<12;budget++){reset_reply("{\"status\":\"approved\"}",200);json_budget=budget;esp_err_t ret=ts_pki_client_check_status(&status);json_budget=-1;check(ret==ESP_OK?status==TS_PKI_ENROLL_APPROVED:status==TS_PKI_ENROLL_ERROR,"reply JSON allocation failures never publish partial or stale success");}
    check(!strstr(diagnostics,"SYNTHETIC_PKI_QUERY")&&!strstr(diagnostics,"SYNTHETIC_CERTIFICATE")&&!strstr(diagnostics,"SYNTHETIC_CA_CHAIN"),"shared logs contain status, not full URL or PKI material");

    unsigned before_requests=requests;
    s_client.enroll_running=true;s_client.enroll_task=(TaskHandle_t)2;
    check(ts_pki_client_submit_csr()==ESP_ERR_INVALID_STATE&&ts_pki_client_install_certificate()==ESP_ERR_INVALID_STATE&&ts_pki_client_enroll_blocking(1)==ESP_ERR_INVALID_STATE&&ts_pki_client_set_server("other.invalid",443)==ESP_ERR_INVALID_STATE&&requests==before_requests,"manual mutations/configuration cannot hijack an active background enrollment");
    s_client.enroll_running=false;s_client.enroll_task=NULL;
    fail_next_create=true;check(ts_pki_client_start_auto_enroll(NULL,NULL)==ESP_ERR_NO_MEM&&!ts_pki_client_is_enrolling(),"failed worker creation releases enrollment admission");
    reset_reply("{}",409);check(ts_pki_client_submit_csr()==ESP_ERR_INVALID_STATE&&!s_client.request_id,"conflict without request ID cannot claim CSR acceptance");
    const char *bad_ids[]={"0","-1","1.5","2147483648","1e100","null","true","\"7\""};
    for(unsigned i=0;i<sizeof(bad_ids)/sizeof(*bad_ids);i++){char body[128];snprintf(body,sizeof(body),"{\"request_id\":%s,\"status\":\"pending\"}",bad_ids[i]);reset_reply(body,200);check(ts_pki_client_submit_csr()!=ESP_OK&&!s_client.request_id,"malformed remote request ID cannot bind workflow");}
    reset_reply("{\"request_id\":7,\"status\":\"pending\"}",200);check(ts_pki_client_submit_csr()==ESP_OK,"normal submission after bad IDs recovers");
    check(ts_pki_client_set_server("synthetic-host.invalid",config.server_port)==ESP_OK&&s_client.request_id==7,"unchanged target retains request binding");
    const char *bad_hosts[]={"","host/path","host?query","user:pass@host","host\r\nX:y"};
    for(unsigned i=0;i<sizeof(bad_hosts)/sizeof(*bad_hosts);i++)check(ts_pki_client_set_server(bad_hosts[i],443)!=ESP_OK&&s_client.request_id==7,"invalid target cannot change previous configuration or request ID");
    check(ts_pki_client_set_server("::1",8443)==ESP_OK&&!s_client.request_id,"changed target clears old server-scoped request ID");
    reset_reply("health",200);check(ts_pki_client_is_server_reachable()&&!strcmp(requested_url,"https://[::1]:8443/api/health"),"valid IPv6 target retains TLS and brackets");
    reset_reply("{\"request_id\":7,\"status\":\"pending\"}",200);check(ts_pki_client_submit_csr()==ESP_OK,"request on new server binds normally");
    reply_entered=xSemaphoreCreateBinary();reply_release=xSemaphoreCreateBinary();assert(!pthread_create(&thread,NULL,held_status,(void*)1));xSemaphoreTake(reply_entered,portMAX_DELAY);
    check(ts_pki_client_deinit()==ESP_ERR_INVALID_STATE&&s_client.initialized&&s_deinitializing,"deinit with in-flight request retains its resources and reports busy");
    check(security_service_stop(NULL,NULL)==ESP_ERR_INVALID_STATE&&!dependency_stops,"actual security service retains certificate and auth dependencies when PKI request still owns resources");check(ts_pki_client_check_status(&status)==ESP_ERR_INVALID_STATE,"closing owner rejects new requests");xSemaphoreGive(reply_release);assert(!pthread_join(thread,NULL));vSemaphoreDelete(reply_entered);vSemaphoreDelete(reply_release);
    check(security_service_stop(NULL,NULL)==ESP_OK&&dependency_stops==4&&!s_client.initialized,"actual service stop releases dependencies only after PKI request joins");
    ts_pki_client_config_t bad_config=config;bad_config.poll_interval_sec=0;check(ts_pki_client_init_with_config(&bad_config)==ESP_ERR_INVALID_ARG&&!s_client.initialized,"zero interval rejected before blocking enrollment can divide by zero");
    bad_config=config;memset(bad_config.server_host,'x',sizeof(bad_config.server_host));check(ts_pki_client_init_with_config(&bad_config)==ESP_ERR_INVALID_ARG&&!s_client.initialized,"unterminated host not truncated or read past config");
    check(ts_pki_client_init_with_config(&config)==ESP_OK,"same stable operation owner reused on reinit");
    for(int budget=0;budget<48;budget++){reset_reply("{\"request_id\":7,\"status\":\"pending\"}",200);json_budget=budget;esp_err_t ret=ts_pki_client_submit_csr();json_budget=-1;check(ret==ESP_OK||ret==ESP_ERR_NO_MEM||ret==ESP_ERR_INVALID_RESPONSE,"CSR builder/reply allocation failures never emit a partial body or leak operation ownership");}
    reset_reply("{\"request_id\":7,\"status\":\"pending\"}",200);int64_t start=esp_timer_get_time();unsigned before_budget_requests=requests;
    check(ts_pki_client_enroll_blocking(1)==ESP_ERR_TIMEOUT&&esp_timer_get_time()-start<2000000&&requests==before_budget_requests+1&&captured_timeout<=1000&&!s_request_deadline,"whole blocking workflow budget includes CSR and wait;short timeout is not divided/truncated into zero attempts");
    enroll_wait_entered=xSemaphoreCreateBinary();assert(!pthread_create(&thread,NULL,blocking_enrollment,NULL));xSemaphoreTake(enroll_wait_entered,portMAX_DELAY);
    start=esp_timer_get_time();check(ts_pki_client_deinit()==ESP_ERR_INVALID_STATE,"blocking wait retains its operation owner during cancellation");assert(!pthread_join(thread,NULL));check(esp_timer_get_time()-start<1000000,"blocking wait observes cancellation within a short cooperative step");vSemaphoreDelete(enroll_wait_entered);enroll_wait_entered=NULL;
    check(ts_pki_client_deinit()==ESP_OK&&!s_client.initialized,"normal deinit after blocking workflow exits");
    vSemaphoreDelete(s_operation);s_operation=NULL;
check(json_allocs==json_frees,"all operation-local JSON objects freed, including unexpected201/error cases");cJSON_InitHooks(NULL);
    printf("%u checks, %u failures: full production PKI operations with owned responses, malformed/status/transport/certificate failures and concurrent reply isolation; synthetic I/O/material only\n",checks,failures);return failures?1:0;
}
'''
run_native(code,[IDF/'components/json/cJSON/cJSON.c',IDF/'components/http_parser/http_parser.c',root/'components/ts_http_fetch/src/ts_http_url.c'],{'http_parser.h':(IDF/'components/http_parser/http_parser.h').read_text(),'esp_timer.h':'#pragma once\n'},[root/'components/ts_pki_client/include',root/'components/ts_cert/include',root/'components/ts_http_fetch/include',root/'tests/runtime/http_fetch_stubs'])
