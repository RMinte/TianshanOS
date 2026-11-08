"""Actual SDK worker publication/destruction and external close contract.
Synthetic scheduling only; actual prepared SDK function bodies, ASan/UBSan.
"""
from pathlib import Path
import importlib.util, os, re, subprocess, tempfile
from test_console_lifecycle import IDF
root = Path(__file__).resolve().parents[2]
source = Path(os.environ.get('TS_WS_COMPONENT_SOURCE', str(root / 'managed_components/espressif__esp_websocket_client/esp_websocket_client.c')))
spec = importlib.util.spec_from_file_location('prepare', root / 'tools/prepare_http_transport_polling.py')
prepare = importlib.util.module_from_spec(spec); spec.loader.exec_module(prepare)

def function(text, name):
    match = re.search(r'^(?:static )?esp_err_t ' + name + r'\([^;]+?\)\s*\{', text, re.M)
    assert match, name
    return text[match.start():text.index('\n}', match.end()) + 2] + '\n'

def native(directory, name, code):
    src = directory / (name + '.c'); exe = directory / name
    src.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', str(src), '-pthread', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)

with tempfile.TemporaryDirectory(prefix='ts-ws-sdk-lifetime-') as temporary:
    directory = Path(temporary)
    prepare.prepare(IDF, directory, source)
    text = (directory / 'esp_websocket_client.c').read_text()
    code=r'''
    #include <assert.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>
    typedef int esp_err_t;typedef void *TaskHandle_t;
    #define ESP_OK 0
    #define ESP_FAIL -1
    #define ESP_ERR_INVALID_ARG 2
    #define WEBSOCKET_STATE_INIT 1
    #define WEBSOCKET_STATE_UNKNOW 0
    #define STOPPED_BIT 1
    #define CLOSE_FRAME_SENT_BIT 2
    #define portMAX_DELAY 0xffffffffu
    #define pdTRUE 1
    #define ESP_LOGE(...) ((void)0)
    #define ESP_LOGI(...) ((void)0)
    #define ESP_LOGW(...) ((void)0)
    typedef struct {void *ext_transport;const char *task_name;unsigned task_stack,task_prio;} config_t;
    typedef struct client {int state;config_t *config;void *transport;TaskHandle_t task_handle;unsigned *status_bits;bool run;void *lock;} *esp_websocket_client_handle_t;
    static bool fail_create;static unsigned failures,waits;
    static int xSemaphoreTakeRecursive(void *lock,unsigned timeout){(void)lock;(void)timeout;return 1;}
    static int xSemaphoreGiveRecursive(void *lock){(void)lock;return 1;}
    static void check(bool v,const char *s){if(!v){failures++;printf("FAIL %s\n",s);}}
    static TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void*)1;}
    static unsigned xEventGroupClearBits(unsigned *bits,unsigned mask){*bits &=~mask;return *bits;}
    static unsigned xEventGroupSetBits(unsigned *bits,unsigned mask){*bits |=mask;return *bits;}
    static unsigned xEventGroupGetBits(unsigned *bits){return *bits;}
    static unsigned xEventGroupWaitBits(unsigned *bits,unsigned mask,bool clear,bool all,unsigned timeout){(void)clear;(void)all;(void)timeout;waits++;check((*bits&mask)==mask,"stop/destroy would wait forever after early completed worker");return *bits;}
    static void esp_websocket_client_task(void *p){esp_websocket_client_handle_t c=p;c->task_handle=NULL;c->state=WEBSOCKET_STATE_UNKNOW;xEventGroupSetBits(c->status_bits,STOPPED_BIT);}
    static int xTaskCreate(void (*task)(void*),const char *name,unsigned stack,void *arg,unsigned prio,TaskHandle_t *out){(void)name;(void)stack;(void)prio;if(fail_create)return 0;*out=(void*)2;task(arg);return pdTRUE;}
    static int esp_websocket_client_create_transport(esp_websocket_client_handle_t c){(void)c;return ESP_FAIL;}
    '''+function(text, 'stop_wait_task')+function(text, 'esp_websocket_client_start')+function(text, 'esp_websocket_client_stop')+r'''
    int main(void){
     for(unsigned fault=0;fault<2;fault++){
      unsigned bits=STOPPED_BIT;config_t config={.ext_transport=(void*)3};struct client client={.config=&config,.status_bits=&bits};fail_create=fault;
      check((esp_websocket_client_start(&client)==ESP_OK)==!fault,"actual SDK start success/failure");
      check(bits&STOPPED_BIT,"creator must not clear completed worker or failed-create stop state");
      esp_websocket_client_stop(&client);
     }
     printf("actual SDK early-task/stop state: %u failures;synthetic scheduler only,no task/network\n",failures);return failures?1:0;
    }
    '''
    native(directory, 'start', code)

    a=text.index('    esp_websocket_client_dispatch_event(client, WEBSOCKET_EVENT_FINISH, NULL, 0);');b=text.index('    vTaskDelete(NULL);',a)+len('    vTaskDelete(NULL);');tail=text[a:b]
    code=r'''
    #define _POSIX_C_SOURCE 200809L
    #include <assert.h>
    #include <stdatomic.h>
    #include <pthread.h>
    #include <stdbool.h>
    #include <stdint.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <sched.h>
    #define ESP_OK 0
    #define ESP_ERR_INVALID_ARG 2
    #define WEBSOCKET_STATE_UNKNOW 0
    #define WEBSOCKET_EVENT_FINISH 12
    #define STOPPED_BIT 1
    #define portMAX_DELAY 0xffffffffu
    #define ESP_LOGE(...) ((void)0)
    typedef int esp_err_t;
    typedef struct client {void *transport;unsigned *status_bits;int state;bool selected_for_destroying;pthread_mutex_t *lock;} *esp_websocket_client_handle_t;
    static pthread_mutex_t gate;static pthread_t worker,destroyer;static atomic_bool worker_locked,destructor_entered;static unsigned destroyed;
    static esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t);
    static int xSemaphoreTakeRecursive(pthread_mutex_t *lock,unsigned timeout){(void)timeout;pthread_mutex_lock(lock);if(pthread_equal(pthread_self(),worker))atomic_store(&worker_locked,true);return 1;}
    static int xSemaphoreGiveRecursive(pthread_mutex_t *lock){if(pthread_equal(pthread_self(),worker))atomic_store(&worker_locked,false);pthread_mutex_unlock(lock);return 1;}
    static void destroy_and_free_resources(esp_websocket_client_handle_t client){destroyed++;free(client);}
    static int stop_wait_task(esp_websocket_client_handle_t client){(void)client;assert(!"already stopped fixture must not wait");return 0;}
    static unsigned xEventGroupGetBits(unsigned *bits){return *bits;}
    static void *destroy_client(void *p){atomic_store(&destructor_entered,true);esp_websocket_client_destroy(p);return NULL;}
    static esp_websocket_client_handle_t observed;
    static unsigned xEventGroupSetBits(unsigned *bits,unsigned mask){
     *bits|=mask;pthread_create(&destroyer,NULL,destroy_client,observed);while(!atomic_load(&destructor_entered))sched_yield();
     /* Before the fix there is no publication lock: force the real destructor
      * to complete before the SDK worker performs its next client access. */
     if(!atomic_load(&worker_locked))pthread_join(destroyer,NULL);
     return *bits;
    }
    static void esp_websocket_client_dispatch_event(void *c,int e,void *d,int n){(void)c;(void)e;(void)d;(void)n;}
    static int esp_transport_close(void *p){(void)p;return 0;}
    static void vTaskDelete(void *p){(void)p;}
    '''+function(text, 'esp_websocket_client_destroy')+'\nstatic void finish(esp_websocket_client_handle_t client){\n'+tail+'\n}\n'+r'''
    int main(void){
     pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);pthread_mutex_init(&gate,&attr);pthread_mutexattr_destroy(&attr);worker=pthread_self();unsigned bits=0;
     observed=calloc(1,sizeof(*observed));observed->lock=&gate;observed->status_bits=&bits;observed->state=1;finish(observed);
     if(atomic_load(&destructor_entered)&&!atomic_load(&worker_locked))pthread_join(destroyer,NULL);
     assert(destroyed==1);pthread_mutex_destroy(&gate);puts("PASS actual SDK finish/destructor: publication barrier joins last client/event access;deterministic pthread schedule,no network");return 0;
    }
    '''
    native(directory, 'finish', code)
    # Execute the real close-poll expression. Opaque external transports must
    # never reach direct socket access; built-in transport keeps SDK behavior.
    match = re.search(r'int ret = client->config->ext_transport \? 1 :\s*esp_transport_ws_poll_connection_closed\(client->transport, 1000\);', text)
    assert match, 'external close contract changed'
    code = r"""
#include <assert.h>
#include <stdio.h>
static unsigned polls;
static int esp_transport_ws_poll_connection_closed(void *t,int ms){assert(t && ms==1000);polls++;return -7;}
struct config{void *ext_transport;};struct client{struct config *config;void *transport;};
static int close_poll(struct client *client){
""" + match.group(0) + r"""
return ret;}
int main(void){struct config cfg={.ext_transport=(void*)1};struct client c={.config=&cfg,.transport=(void*)2};assert(close_poll(&c)==1 && !polls);cfg.ext_transport=0;assert(close_poll(&c)==-7 && polls==1);puts("PASS actual SDK external close skips direct socket;built-in close retained");}
"""
    native(directory, 'close', code)
