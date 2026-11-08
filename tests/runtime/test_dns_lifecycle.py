#!/usr/bin/env python3
"""Actual bounded DNS owner; synthetic lwIP scheduling, real pthread/semaphore races."""
from pathlib import Path
import re
from test_console_lifecycle import RTOS, run_native

root=Path(__file__).resolve().parents[2]
code=RTOS+r'''
#include <arpa/inet.h>
#include "ts_dns.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
static atomic_uint live_requests,peak_requests;
static bool fail_allocation,fail_semaphore;
static void *request_calloc(size_t count,size_t size){
 if(fail_allocation)return NULL;void *p=calloc(count,size);assert(p);
 unsigned live=atomic_fetch_add(&live_requests,1)+1,peak=atomic_load(&peak_requests);
 while(live>peak&&!atomic_compare_exchange_weak(&peak_requests,&peak,live)){}return p;
}
static void request_free(void *p){assert(atomic_fetch_sub(&live_requests,1));free(p);}
static SemaphoreHandle_t request_semaphore(void){return fail_semaphore?NULL:xSemaphoreCreateBinary();}
static pthread_mutex_t dns_lock=PTHREAD_MUTEX_INITIALIZER;
static atomic_bool pending_ready;
static bool defer_queue,queue_failure;
static int dns_mode;
enum {DNS_CACHED,DNS_PENDING,DNS_ERROR};
static unsigned dns_calls;
static void (*queued)(void *);static void *queued_arg;
static dns_found_callback pending;static void *pending_arg;
static _Thread_local bool in_tcpip;
static ip_addr_t answer;
err_t tcpip_try_callback(void (*fn)(void *),void *arg){
 if(queue_failure)return ERR_MEM;
 if(defer_queue){assert(!queued);queued=fn;queued_arg=arg;return ERR_OK;}
 in_tcpip=true;fn(arg);in_tcpip=false;return ERR_OK;
}
err_t dns_gethostbyname_addrtype(const char *name,ip_addr_t *address,dns_found_callback found,void *context,unsigned char type){
 assert(in_tcpip&&name&&type==LWIP_DNS_ADDRTYPE_DEFAULT);dns_calls++;
 if(dns_mode==DNS_CACHED){*address=answer;return ERR_OK;}
 if(dns_mode==DNS_ERROR)return ERR_VAL;
 pthread_mutex_lock(&dns_lock);assert(!pending);pending=found;pending_arg=context;atomic_store(&pending_ready,true);pthread_mutex_unlock(&dns_lock);return ERR_INPROGRESS;
}
#define calloc request_calloc
#define free request_free
#define xSemaphoreCreateBinary request_semaphore
'''
code+=re.sub(r'^#include[^\n]*\n','',(root/'components/ts_dns/src/ts_dns.c').read_text(),flags=re.M)
code+=r'''
#undef calloc
#undef free
#undef xSemaphoreCreateBinary
static unsigned checks;
static void check(bool yes,const char *message){checks++;if(!yes){fprintf(stderr,"FAIL %s\n",message);abort();}}
static bool current(void *context){return !atomic_load((atomic_bool *)context);}
typedef struct {atomic_bool cancelled;esp_err_t result;ip_addr_t address;} caller_t;
static void *caller(void *context){
 caller_t *c=context;char name[64];strcpy(name,"worker.example");
 c->result=ts_dns_resolve(name,esp_timer_get_time()+1000000,current,&c->cancelled,&c->address);return NULL;
}
typedef struct {dns_found_callback fn;void *argument;} completion_t;
static completion_t take_completion(void){
 pthread_mutex_lock(&dns_lock);assert(pending);completion_t c={pending,pending_arg};pending=NULL;pending_arg=NULL;atomic_store(&pending_ready,false);pthread_mutex_unlock(&dns_lock);return c;
}
static void finish(completion_t c,const ip_addr_t *address){in_tcpip=true;c.fn("worker.example",address,c.argument);in_tcpip=false;}
static SemaphoreHandle_t pause_target,callback_resume;
static atomic_bool callback_paused;
static void pause_after_signal(SemaphoreHandle_t semaphore){
 if(semaphore==pause_target){atomic_store(&callback_paused,true);assert(xSemaphoreTake(callback_resume,portMAX_DELAY));}
}
static void *completer(void *arg){completion_t c=*(completion_t *)arg;finish(c,&answer);return NULL;}
int main(void){
 assert(ipaddr_aton("192.0.2.7",&answer));ip_addr_t result;memset(&result,0xa5,sizeof(result));
 atomic_bool cancelled=false;
 check(ts_dns_resolve("127.0.0.1",esp_timer_get_time()+100000,current,&cancelled,&result)==ESP_OK&&!atomic_load(&live_requests)&&!dns_calls,"numeric IPv4 never queues DNS or allocates callback state");
 check(ts_dns_resolve("::1",esp_timer_get_time()+100000,current,&cancelled,&result)==ESP_OK&&!dns_calls,"numeric IPv6 retains the same immediate path");
 check(ts_dns_resolve("bad",esp_timer_get_time()-1,current,&cancelled,&result)==ESP_ERR_TIMEOUT&&!atomic_load(&live_requests),"expired caller cannot start a resolution");
 atomic_store(&cancelled,true);check(ts_dns_resolve("bad",esp_timer_get_time()+100000,current,&cancelled,&result)==ESP_ERR_INVALID_STATE,"canceled caller cannot queue DNS");atomic_store(&cancelled,false);
 char too_long[257];memset(too_long,'a',256);too_long[256]=0;
 check(ts_dns_resolve(too_long,esp_timer_get_time()+100000,NULL,NULL,&result)==ESP_ERR_INVALID_ARG,"oversized hostname is rejected before allocation");
 dns_mode=DNS_CACHED;
 check(ts_dns_resolve("cached.example",esp_timer_get_time()+100000,NULL,NULL,&result)==ESP_OK&&!memcmp(&result,&answer,sizeof(result))&&!atomic_load(&live_requests)&&!atomic_load(&s_query_active),"cached completion may run before queue submission returns without losing its caller reference");
 for(unsigned failure=0;failure<4;failure++){
  fail_allocation=failure==0;fail_semaphore=failure==1;queue_failure=failure==2;dns_mode=failure==3?DNS_ERROR:DNS_CACHED;
  esp_err_t r=ts_dns_resolve("failure.example",esp_timer_get_time()+100000,NULL,NULL,&result);
  check(r!=ESP_OK&&!atomic_load(&live_requests)&&!atomic_load(&s_query_active),"allocation/semaphore/queue/synchronous DNS failures release exactly their own resources");
 }fail_allocation=fail_semaphore=queue_failure=false;
 /* A caller can expire before the TCP/IP queue dispatches. It owns no borrowed
  * callback input, and no late DNS packet is initiated after it returns. */
 defer_queue=true;dns_mode=DNS_PENDING;unsigned before=dns_calls;
 check(ts_dns_resolve("queued.example",esp_timer_get_time()+20000,NULL,NULL,&result)==ESP_ERR_TIMEOUT&&atomic_load(&live_requests)==1&&atomic_load(&s_query_active),"timeout returns while the queued callback retains its own input and semaphore");
 check(ts_dns_resolve("second.example",esp_timer_get_time()+10000,NULL,NULL,&result)==ESP_ERR_TIMEOUT&&atomic_load(&live_requests)==1,"an orphaned callback keeps one bounded DNS slot; retries do not allocate additional contexts");
 void (*start)(void *)=queued;void *start_arg=queued_arg;queued=NULL;queued_arg=NULL;in_tcpip=true;start(start_arg);in_tcpip=false;
 check(dns_calls==before&&!atomic_load(&live_requests)&&!atomic_load(&s_query_active),"late TCP/IP dispatch observes retired intent before issuing DNS");defer_queue=false;
 /* DNS is already in flight when its caller is canceled and freed. */
 caller_t *retired=calloc(1,sizeof(*retired));pthread_t calling;
 assert(!pthread_create(&calling,NULL,caller,retired));WAIT_UNTIL(atomic_load(&pending_ready));atomic_store(&retired->cancelled,true);assert(!pthread_join(calling,NULL));
 check(retired->result==ESP_ERR_INVALID_STATE&&atomic_load(&live_requests)==1,"in-flight cancellation returns without freeing a callback-owned semaphore");free(retired);
 finish(take_completion(),NULL);
 check(!atomic_load(&live_requests)&&!atomic_load(&s_query_active),"late failure after caller/hostname lifetime ended releases the orphan without accessing freed caller data");
 /* Force callback give -> caller return -> next query before the old callback
  * releases its last reference. Its cleanup must not clear the new slot. */
 caller_t first={0},second={0};pthread_t one,two,finishing;
 assert(!pthread_create(&one,NULL,caller,&first));WAIT_UNTIL(atomic_load(&pending_ready));completion_t old=take_completion();
 pause_target=((resolve_request_t *)old.argument)->completed;callback_resume=xSemaphoreCreateBinary();after_binary_give=pause_after_signal;
 assert(!pthread_create(&finishing,NULL,completer,&old));WAIT_UNTIL(atomic_load(&callback_paused));assert(!pthread_join(one,NULL));
 check(first.result==ESP_OK&&!memcmp(&first.address,&answer,sizeof(answer))&&atomic_load(&live_requests)==1,"caller can return with stable copied address while completing callback still owns its reference");
 assert(!pthread_create(&two,NULL,caller,&second));WAIT_UNTIL(atomic_load(&pending_ready));
 check(atomic_load(&s_query_active)&&atomic_load(&live_requests)==2,"new query has its own context while the old callback finishes");
 xSemaphoreGive(callback_resume);assert(!pthread_join(finishing,NULL));after_binary_give=NULL;
 check(atomic_load(&s_query_active)&&atomic_load(&live_requests)==1,"old post-signal cleanup cannot release the new query's admission slot");
 atomic_store(&second.cancelled,true);assert(!pthread_join(two,NULL));completion_t later=take_completion();finish(later,&answer);
 check(second.result==ESP_ERR_INVALID_STATE&&!atomic_load(&live_requests)&&!atomic_load(&s_query_active),"new canceled caller never receives an old or late address, and all callback state retires");vSemaphoreDelete(callback_resume);
 check(atomic_load(&peak_requests)<=2,"callback state remains bounded across forced schedules");
 printf("%u checks: actual DNS lifecycle, numeric v4/v6 boundary, queue/cache/failure, canceled and delayed callbacks, deterministic pthread ownership race; synthetic lwIP only\n",checks);
}
'''
headers={
 'ts_dns.h':(root/'components/ts_dns/include/ts_dns.h').read_text(),
 'lwip/ip_addr.h':'''#pragma once
#include <arpa/inet.h>
#include <string.h>
typedef struct {int family;unsigned char bytes[16];} ip_addr_t;
#define ip_addr_set_zero(p) memset((p),0,sizeof(*(p)))
#define ip_addr_copy(to,from) ((to)=(from))
static int ipaddr_aton(const char *s,ip_addr_t *p){ip_addr_set_zero(p);if(inet_pton(AF_INET,s,p->bytes)==1){p->family=AF_INET;return 1;}if(inet_pton(AF_INET6,s,p->bytes)==1){p->family=AF_INET6;return 1;}return 0;}
''',
 'lwip/dns.h':'''#pragma once
#include "lwip/ip_addr.h"
#define DNS_MAX_NAME_LENGTH 256
#define LWIP_DNS_ADDRTYPE_DEFAULT 2
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_INPROGRESS -5
#define ERR_VAL -6
typedef int err_t;
typedef void (*dns_found_callback)(const char *,const ip_addr_t *,void *);
err_t dns_gethostbyname_addrtype(const char *,ip_addr_t *,dns_found_callback,void *,unsigned char);
''',
 'lwip/tcpip.h':'#pragma once\n#include "lwip/dns.h"\nerr_t tcpip_try_callback(void (*)(void *),void *);\n',
}
run_native(code,extra_headers=headers)
