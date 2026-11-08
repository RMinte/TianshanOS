#!/usr/bin/env python3
"""Actual SDK HTTPS startup owner; deterministic allocation and server faults.
--before deliberately fails the original SDK's failed-start ownership assertion.
"""
from pathlib import Path
import re,subprocess,tempfile,os,sys,importlib.util
sdk=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'));path=sdk/'components/esp_https_server/src/https_server.c';source=path.read_text();original=source
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('httpd_preparation',root/'tools/prepare_httpd_admission.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
if '--before' not in sys.argv:
 with tempfile.TemporaryDirectory(prefix='ts-https-prepare-') as temp:
  output=Path(temp)/'out';mod.prepare_https(sdk,output);source=(output/'https_server.c').read_text();mod.prepare_https(sdk,output);assert (output/'https_server.c').read_text()==source
  for i,anchor in enumerate(['Server shuts down, releasing SSL context','    ret = httpd_start(&handle, &config->httpd);','        free((void *) cfg->servercert_buf);']):
   fake=Path(temp)/('drift-'+str(i));target=fake/'components/esp_https_server/src/https_server.c';target.parent.mkdir(parents=True);assert anchor in original;target.write_text(original.replace(anchor,anchor+' /* drift */',1))
   try:mod.prepare_https(fake,fake/'out')
   except ValueError:pass
   else:raise AssertionError('HTTPS SDK allocation anchor drift accepted')
   assert not(fake/'out').exists()
assert path.read_text()==original,'installed SDK must remain unchanged'

def function(name):
 m=re.search(r'^(?:static )?(?:esp_err_t|void) '+name+r'\([^;]+?\)\s*\{',source,re.M);assert m,name;return source[m.start():source.index('\n}',m.end())+2]+'\n'
code=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_SUPPORTED 0x107
#define HTTPD_SSL_TRANSPORT_SECURE 1
#define HTTPS_SERVER_EVENT_START 1
#define HTTPS_SERVER_EVENT_STOP 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
static int budget=-1,live;
static void *blocks[32];
static void *test_malloc(size_t n){if(!budget)return NULL;if(budget>0)budget--;void *p=malloc(n);if(p){for(unsigned i=0;i<32;i++)if(!blocks[i]){blocks[i]=p;live++;return p;}assert(false);}return p;}
static void *test_calloc(size_t n,size_t s){void *p=test_malloc(n*s);if(p)memset(p,0,n*s);return p;}
static void test_free(void *p){if(!p)return;for(unsigned i=0;i<32;i++)if(blocks[i]==p){blocks[i]=NULL;live--;free(p);return;}assert(!"unowned or duplicate SDK free");}
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
typedef struct {void *userdata;const char **alpn_protos;unsigned tls_handshake_timeout_ms;unsigned char *cacert_buf,*servercert_buf;void *serverkey_buf;size_t cacert_bytes,servercert_bytes,serverkey_bytes;bool use_secure_element;void *ticket_ctx;} esp_tls_cfg_server_t;
typedef void *httpd_handle_t;
typedef struct {void *global_transport_ctx;void(*global_transport_ctx_free_fn)(void*);int(*open_fn)(httpd_handle_t,int);unsigned server_port;} httpd_config_t;
struct httpd_ssl_config {httpd_config_t httpd;bool session_tickets,use_secure_element,use_ecdsa_peripheral;void *ssl_userdata;const char **alpn_protos;unsigned tls_handshake_timeout_ms;int user_cb;const unsigned char *cacert_pem,*servercert,*prvtkey_pem;size_t cacert_len,servercert_len,prvtkey_len;unsigned port_secure,port_insecure;int transport_mode;};
typedef struct httpd_ssl_ctx {esp_tls_cfg_server_t *tls_cfg;int user_cb;int(*open_fn)(httpd_handle_t,int);} httpd_ssl_ctx_t;
static int esp_tls_cfg_server_session_tickets_init(esp_tls_cfg_server_t *cfg){cfg->ticket_ctx=malloc(31);return cfg->ticket_ctx?ESP_OK:ESP_ERR_NO_MEM;}
static void esp_tls_cfg_server_session_tickets_free(esp_tls_cfg_server_t *cfg){if(cfg)free(cfg->ticket_ctx);}
static const char *esp_err_to_name(int e){(void)e;return "synthetic";}
static int httpd_ssl_open(httpd_handle_t h,int fd){(void)h;(void)fd;return ESP_OK;}
static bool force_start_failure;
static httpd_config_t active;
static int httpd_start(httpd_handle_t *out,const httpd_config_t *cfg){if(force_start_failure)return ESP_FAIL;active=*cfg;*out=(void*)1;return ESP_OK;}
static int httpd_stop(httpd_handle_t h){assert(h==(void*)1);if(active.global_transport_ctx_free_fn)active.global_transport_ctx_free_fn(active.global_transport_ctx);memset(&active,0,sizeof(active));return ESP_OK;}
static void http_dispatch_event_to_event_loop(int type,void *p,size_t n){(void)type;(void)p;(void)n;}
'''
for name in (['ts_httpd_free_tls_config'] if 'ts_httpd_free_tls_config(' in source else [])+['free_secure_context','create_secure_context','httpd_ssl_start','httpd_ssl_stop']:code+=function(name)
code+=r'''
static struct httpd_ssl_config config(void){return(struct httpd_ssl_config){.transport_mode=HTTPD_SSL_TRANSPORT_SECURE,.session_tickets=true,.cacert_pem=(void*)"CA",.cacert_len=3,.servercert=(void*)"CERT",.servercert_len=5,.prvtkey_pem=(void*)"SYNTHETIC_PRIVATE_KEY",.prvtkey_len=22,.port_secure=8443};}
int main(void){
 struct httpd_ssl_config cfg=config();httpd_handle_t handle=NULL;
 assert(httpd_ssl_start(&handle,&cfg)==ESP_OK&&handle&&live==6);assert(httpd_ssl_stop(handle)==ESP_OK&&live==0);
 force_start_failure=true;cfg=config();handle=NULL;assert(httpd_ssl_start(&handle,&cfg)!=ESP_OK&&!handle);
 fprintf(stderr,"actual SDK start-failure retained allocations=%d\n",live);
 assert(live==0&&"HTTPS start failure must reclaim configuration, tickets, CA, certificate and private key");
 unsigned cases=0;
 for(unsigned tickets=0;tickets<2;tickets++)for(unsigned fail=0;fail<2;fail++)for(int allowance=0;allowance<8;allowance++){
  assert(!live);budget=allowance;force_start_failure=fail;cfg=config();cfg.session_tickets=tickets;handle=NULL;int ret=httpd_ssl_start(&handle,&cfg);budget=-1;
  if(ret==ESP_OK){assert(handle==(void*)1);assert(httpd_ssl_stop(handle)==ESP_OK);}else assert(!handle);
  assert(live==0&&"partial TLS setup must release every successful allocation");cases++;
 }
 force_start_failure=false;cfg=config();cfg.servercert=NULL;handle=NULL;assert(httpd_ssl_start(&handle,&cfg)==ESP_ERR_INVALID_ARG&&!handle&&!live);
 cfg=config();cfg.prvtkey_pem=NULL;assert(httpd_ssl_start(&handle,&cfg)==ESP_ERR_INVALID_ARG&&!handle&&!live);
 printf("PASS %u allocation/failure cases: actual SDK HTTPS create/start/stop cleanup, tickets on/off, missing certificate/key, normal success, no source mutation or TLS socket\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='ts-https-sdk-fault-') as tmp:
 c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code);subprocess.run(['cc','-std=gnu11','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(c),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
