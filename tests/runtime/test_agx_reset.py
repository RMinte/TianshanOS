"""Actual AGX owner functions: reset completion, shutdown interleaving and power consumers."""
from pathlib import Path
import argparse
import os
from test_console_lifecycle import body, run_native, IDF

os.chdir(Path(__file__).resolve().parents[2])
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--case', choices=['completion', 'interleaved-off', 'errors-notification', 'power', 'lpmu-startup'])
args = parser.parse_args()
driver = 'components/ts_drivers/src/ts_device_ctrl.c'
text = Path(driver).read_text()
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include "esp_err.h"
#include "ts_device_ctrl.h"
#include "cJSON.h"
#define TS_LOGI(...) ((void)0)
#define TS_LOGW(...) ((void)0)
#define TS_LOGE(...) ((void)0)
#define pdMS_TO_TICKS(n) (n)
#define TS_EVENT_BASE_DEVICE_MON 1
#define TS_EVENT_DEVICE_STATUS_CHANGED 2
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin)>=0 && (pin)<49 && ((pin)<22 || (pin)>25))
typedef void *ts_gpio_handle_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(p) assert(!pthread_mutex_lock(p))
#define portEXIT_CRITICAL(p) assert(!pthread_mutex_unlock(p))
static int levels[32],writes,delays[32],delay_count,post_count,shutdown_at_delay;
static uint32_t now_ms;
static bool allocation_fails,post_fails;
static char last_event[256];
static int64_t esp_timer_get_time(void) {return (int64_t)now_ms*1000;}
static int gpio_set_level(int pin,unsigned level) {
    if(!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return ESP_ERR_INVALID_ARG;
    assert(pin==1 && writes<32);levels[writes++]=level;return ESP_OK;
}
static int ts_gpio_get_level(ts_gpio_handle_t handle) {(void)handle;return 1;}
static int agx_power_off(void);
static int agx_power_on(void);
static void check_reset_wait(void);
static void vTaskDelay(unsigned ticks) {
    assert(delay_count<32);delays[delay_count++]=ticks;now_ms+=ticks;
    if(ticks==50 || ticks==1000) check_reset_wait();
    if(shutdown_at_delay==ticks) {shutdown_at_delay=0;assert(agx_power_off()==ESP_OK);}
}
static int ts_event_post(int base,int event,const void *data,size_t length,unsigned timeout) {
    assert(base==1 && event==2 && timeout==0 && length<=sizeof(last_event));
    memcpy(last_event,data,length);++post_count;return post_fails?ESP_FAIL:ESP_OK;
}
static cJSON *fixture_object(void) {return allocation_fails?NULL:cJSON_CreateObject();}
#define cJSON_CreateObject fixture_object
'''
system = (IDF / 'components/esp_system/include/esp_system.h').read_text()
code += system[system.index('typedef enum {'):system.index('} esp_reset_reason_t;') + len('} esp_reset_reason_t;')]
code += r'''
static esp_reset_reason_t reset_reason;
static esp_reset_reason_t esp_reset_reason(void) {return reset_reason;}
static int ping_calls,online_at,task_deletes;
static void *s_lpmu_detect_task;
static bool ping_host(const char *ip,int timeout) {
    assert(!strcmp(ip,"10.10.99.99") && timeout==1000);
    ++ping_calls;return online_at && ping_calls>=online_at;
}
#define LPMU_DEFAULT_IP "10.10.99.99"
static int ts_gpio_set_level(void *pin,int level) {
    assert((uintptr_t)pin==46 || (uintptr_t)pin==2);
    assert(writes<32);levels[writes++]=level;return ESP_OK;
}
static void vTaskDeleteWithCaps(void *task) {assert(!task);++task_deletes;}
'''
start = text.index('typedef struct {')
code += text[start:text.index('static bool s_initialized', start)]
for name in ['agx_power_on', 'agx_power_off', 'agx_reset', 'ts_device_get_status', 'ts_device_is_powered', 'lpmu_power_toggle', 'lpmu_power_on', 'lpmu_power_off', 'lpmu_reset', 'ts_device_power_on', 'ts_device_power_off', 'ts_device_reset', 'ts_device_state_to_str', 'lpmu_startup_detect_task']:
    code += body(driver, name)
code += '\nstatic struct {bool agx_powered,lpmu_powered;} s_pp;\n'
code += body('components/ts_drivers/src/ts_power_policy.c', 'check_device_status')
code += r'''
static void check_reset_wait(void) {
    check_device_status();assert(s_pp.agx_powered);
    int before=writes;uint32_t boots=s_agx.boot_count;
    assert(agx_power_on()==ESP_OK);
    assert(writes==before && s_agx.boot_count==boots);
}
'''
code += r'''
static void fixture(void) {
    memset(&s_agx,0,sizeof(s_agx));memset(&s_lpmu,0,sizeof(s_lpmu));
    s_agx.configured=true;s_agx.pins.gpio_reset=1;s_agx.state=TS_DEVICE_STATE_ON;
    s_agx.power_on_time=500;s_agx.boot_count=7;
    s_lpmu.configured=true;s_lpmu.state=TS_DEVICE_STATE_ON;
    writes=delay_count=post_count=shutdown_at_delay=0;now_ms=10000;
    allocation_fails=post_fails=false;last_event[0]=0;
}
static void completed(void) {
    assert(writes==3 && levels[0]==0 && levels[1]==1 && levels[2]==0);
    assert(delay_count==2 && delays[0]==50 && delays[1]==1000);
    assert(s_agx.state==TS_DEVICE_STATE_ON && s_agx.boot_count==8);
    assert(s_agx.power_on_time==11050);
    ts_device_status_t status;now_ms+=25;
    assert(ts_device_get_status(TS_DEVICE_AGX,&status)==ESP_OK);
    assert(status.state==TS_DEVICE_STATE_ON && status.uptime_ms==25 && status.power_good);
    assert(ts_device_is_powered(TS_DEVICE_AGX));check_device_status();assert(s_pp.agx_powered);
}

static void lpmu_fixture(esp_reset_reason_t reason,int online) {
    fixture();reset_reason=reason;online_at=online;ping_calls=task_deletes=0;
    s_lpmu.state=TS_DEVICE_STATE_OFF;s_lpmu.boot_count=7;
    s_lpmu.gpio.power_btn=(void*)46;s_lpmu.gpio.reset=(void*)2;
    s_lpmu_detect_task=(void*)1;
}
static void startup_case(esp_reset_reason_t reason,int online,bool pulse) {
    lpmu_fixture(reason,online);lpmu_startup_detect_task(NULL);
    assert(writes==(pulse?2:0));if(pulse) assert(levels[0]==1 && levels[1]==0);
    assert(!s_lpmu_detect_task && task_deletes==1 && post_count==1);
    unsigned duration=online==1?5000:online?13000:88000;
    assert(now_ms==10000+duration+(pulse?300:0));
    assert(ping_calls==(online?online:19));
    assert(s_lpmu.state==(online?TS_DEVICE_STATE_ON:TS_DEVICE_STATE_OFF));
    assert(s_lpmu.boot_count==(online>1?8:7));
    assert((bool)strstr(last_event,"\"power\":true")==!!online);
}
static void startup_checks(void) {
    startup_case(ESP_RST_POWERON,4,true); /* Original cold-on pulse, later discovery. */
    startup_case(ESP_RST_POWERON,1,false); /* Already online: no button. */
    startup_case(ESP_RST_USB,4,false);
    startup_case(ESP_RST_SW,4,false);
    startup_case(ESP_RST_UNKNOWN,4,false);
    startup_case(ESP_RST_USB,0,false); /* All 16 original follow-up probes and timeout. */
    lpmu_fixture(ESP_RST_USB,0);
    assert(ts_device_power_on(TS_DEVICE_LPMU)==ESP_OK && writes==2);
    assert(ts_device_power_off(TS_DEVICE_LPMU)==ESP_OK && writes==4);
    assert(ts_device_reset(TS_DEVICE_LPMU)==ESP_OK && writes==6);
    assert(s_lpmu.state==TS_DEVICE_STATE_BOOTING && s_lpmu.boot_count==9);
}

int main(int argc,char **argv) {
    assert(argc==2);const char *name=argv[1];fixture();
    if(!strcmp(name,"lpmu-startup")) {
        startup_checks();
    } else if(!strcmp(name,"completion")) {
        assert(agx_reset()==ESP_OK);completed();assert(post_count==1);
        cJSON *event=cJSON_Parse(last_event);assert(event);
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(event,"device")),"agx"));
        assert(!strcmp(cJSON_GetStringValue(cJSON_GetObjectItem(event,"state")),"on"));
        assert(cJSON_IsTrue(cJSON_GetObjectItem(event,"power")));
        assert(cJSON_GetObjectItem(event,"boot_count")->valueint==8);cJSON_Delete(event);
    } else if(!strcmp(name,"interleaved-off")) {
        /* Actual yield points: a completed shutdown must remain OFF/HIGH. */
        for(int wait=0;wait<2;wait++) {
            fixture();shutdown_at_delay=wait?1000:50;
            int ret=agx_reset();
            fprintf(stderr,"interleaving delay=%d ret=%d final_state=%d final_level=%d\n",wait?1000:50,ret,s_agx.state,levels[writes-1]);
            assert(ret==ESP_ERR_INVALID_STATE);
            assert(s_agx.state==TS_DEVICE_STATE_OFF && levels[writes-1]==1);
            assert(s_agx.boot_count==7 && s_agx.power_on_time==500);
            check_device_status();assert(!s_pp.agx_powered && post_count==1);
            assert(strstr(last_event,"off"));
        }
    } else if(!strcmp(name,"errors-notification")) {
        s_agx.pins.gpio_reset=-1;assert(agx_reset()==ESP_ERR_INVALID_STATE);
        assert(writes==0 && delay_count==0 && post_count==0 && s_agx.boot_count==7);
        fixture();s_agx.pins.gpio_reset=99;assert(agx_reset()==ESP_ERR_INVALID_ARG);
        assert(writes==0 && delay_count==0 && post_count==0 && s_agx.state==TS_DEVICE_STATE_ON);
        fixture();allocation_fails=true;assert(agx_reset()==ESP_OK);completed();assert(post_count==0);
        fixture();post_fails=true;assert(agx_reset()==ESP_OK);completed();assert(post_count==1);
    } else {
        assert(!strcmp(name,"power"));assert(agx_power_off()==ESP_OK);
        assert(s_agx.state==TS_DEVICE_STATE_OFF && writes==1 && levels[0]==1);
        check_device_status();assert(!s_pp.agx_powered);
        assert(agx_power_on()==ESP_OK);assert(s_agx.state==TS_DEVICE_STATE_ON);
        assert(writes==2 && levels[1]==0 && s_agx.boot_count==8);
        check_device_status();assert(s_pp.agx_powered && s_pp.lpmu_powered);
        assert(agx_power_on()==ESP_OK && writes==2 && s_agx.boot_count==8);
    }
    printf("PASS device production functions: %s\n",name);return 0;
}
'''
cases = [args.case] if args.case else ['completion', 'interleaved-off', 'errors-notification', 'power', 'lpmu-startup']
probe = code.replace('int main(int argc,char **argv)', 'static int probe(int argc,char **argv)')
probe += '\nint main(void) {\n'
for index, case in enumerate(cases):
    probe += 'char *args' + str(index) + '[]={"test","' + case + '"};probe(2,args' + str(index) + ');\n'
probe += 'return 0;}\n'
run_native(probe, extra_sources=[IDF / 'components/json/cJSON/cJSON.c'])
