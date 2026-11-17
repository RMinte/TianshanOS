#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_fan.h"
#include "ts_temp_source.h"

typedef void *ts_pwm_handle_t;
typedef void *ts_gpio_handle_t;
#define pdTRUE 1
#define portMAX_DELAY (-1)
#define portTICK_PERIOD_MS 1
#define TS_LOGD(...) ((void)0)
#define TS_LOGI(...) ((void)0)
#define TS_LOGW(...) ((void)0)
#define TS_AUTO_VAL_FLOAT 1
#define TS_AUTO_VAL_INT 2

#define CONFIG_TS_DRIVERS_FAN_TEMP_UPDATE_MS 1000
#include "../fan_review/mutex_host.h"
#include "fan_types.inc"
#include "temp_types.inc"
static fan_instance_t s_fans[TS_FAN_MAX];
static temp_source_state_t s_state;
static atomic_uint s_auto_binding_revision = 1;
static pthread_mutex_t s_auto_binding_commit_lock = PTHREAD_MUTEX_INITIALIZER;
#define portENTER_CRITICAL(mutex) assert(pthread_mutex_lock(mutex) == 0)
#define portEXIT_CRITICAL(mutex) assert(pthread_mutex_unlock(mutex) == 0)
static bool s_auto_temp_enabled = true;
static int64_t now_ms;
static _Thread_local bool inside_timer;
static bool rebind_after_binding_read;
static bool temp_mutex_busy;
static bool variable_mutex_busy;
static bool variable_missing;
static int64_t publish_during_read_ms;
static esp_err_t pwm_error;
static unsigned pwm_writes;
static float pwm_output;
typedef struct {
    int64_t last_update_ms;
    struct { int type; double float_val; int int_val; } value;
} ts_variable_info_t;
static ts_variable_info_t variable, second_variable;

static int64_t esp_timer_get_time(void) { return now_ms * 1000; }
static uint32_t xTaskGetTickCount(void) { return (uint32_t)now_ms; }
static int temp_mutex_take(SemaphoreHandle_t mutex, int ticks) {
    if (inside_timer) assert(ticks == 0);
    return !temp_mutex_busy;
}
static void temp_mutex_give(SemaphoreHandle_t mutex) {}
static esp_err_t ts_variable_get_info(const char *name, ts_variable_info_t *info) {
    assert(!inside_timer&&!variable_mutex_busy); /* Never wait on a busy variable leaf. */
    *info = strcmp(name, "gpu.hotspot") == 0 ? second_variable : variable;
    return ESP_OK;
}
static esp_err_t ts_variable_get_info_try(const char *name, ts_variable_info_t *info) {
    if (variable_mutex_busy) return ESP_ERR_TIMEOUT;
    if (variable_missing) return 0x105;
    if (publish_during_read_ms) {
        now_ms = variable.last_update_ms = publish_during_read_ms;
        publish_during_read_ms = 0;
    }
    *info = strcmp(name, "gpu.hotspot") == 0 ? second_variable : variable;
    return ESP_OK;
}
static void publish_temp_event(int16_t temp, ts_temp_source_type_t source,
                               int16_t previous, ts_temp_source_type_t previous_source) {}
static esp_err_t ts_pwm_set_duty(ts_pwm_handle_t pwm, float duty) {
    pwm_writes++;
    if (pwm_error == ESP_OK) pwm_output = duty;
    return pwm_error;
}
#define ESP_ERR_INVALID_SIZE 0x104
static bool ts_variable_exists(const char *name){return true;}
static esp_err_t save_bound_vars_to_nvs(void){return ESP_OK;}
static esp_err_t save_bound_variable_to_nvs(const char *name){return ESP_OK;}
static void export_temp_config_to_sdcard(void){}
static esp_err_t update_pwm(fan_instance_t *fan, uint8_t duty);
#define xSemaphoreTake temp_mutex_take
#define xSemaphoreGive temp_mutex_give
#include "temp_core.inc"
#undef xSemaphoreTake
#undef xSemaphoreGive

static void *rebind_variable(void *unused)
{
    ts_temp_bound_var_t replacement = {.name = "next.temperature", .weight = 1.0f};
    assert(ts_temp_bind_variables(&replacement, 1) == ESP_OK);
    return NULL;
}

static void test_after_auto_binding_read(void)
{
    if (!rebind_after_binding_read) return;
    rebind_after_binding_read = false;
    pthread_t writer;
    assert(pthread_create(&writer, NULL, rebind_variable, NULL) == 0);
    assert(pthread_join(writer, NULL) == 0);
}

#include "fan_core.inc"

static void setup(ts_fan_mode_t mode) {
    memset(&s_state, 0, sizeof(s_state));
    memset(s_fans, 0, sizeof(s_fans));
    s_state.initialized = true;
    s_state.mutex = (SemaphoreHandle_t)1;
    s_state.bound_var_count = 1;
    strcpy(s_state.bound_vars[0].name, "cpu.temperature");
    s_state.bound_vars[0].weight = 1.0f;
    s_auto_temp_enabled = true;
    temp_mutex_busy = variable_mutex_busy = variable_missing = false;
    rebind_after_binding_read = false;
    publish_during_read_ms = 0;
    pwm_error = ESP_OK;
    pwm_writes = 0;
    now_ms = 1000;
    variable = (ts_variable_info_t){
        .last_update_ms = now_ms,
        .value = {.type = TS_AUTO_VAL_FLOAT, .float_val = 51.2}
    };
    second_variable = variable;
    fan_instance_t *f = &s_fans[0];
    f->initialized = true;
    f->enabled = true;
    f->pwm = (void *)1;
    f->mode = mode;
    f->current_duty = 37;
    f->config.min_duty = 20;
    f->config.max_duty = 100;
    f->config.hysteresis = 30;
    f->config.min_interval = 1000;
    f->config.curve_points = 4;
    ts_fan_curve_point_t points[] = {{300,20}, {500,40}, {700,80}, {800,100}};
    memcpy(f->config.curve, points, sizeof(points));
    reset_adaptive_auto_state(f);
    f->last_stable_temp = -1000;
    /* Simulate the last valid evaluation before changing fan mode. */
    ts_temp_data_t data;
    ts_temp_get_effective_nonblocking(&data);
    assert(data.valid && data.value == 512);
}

static void tick(int64_t ms, double temperature, bool report) {
    now_ms = ms;
    if (report) {
        variable.last_update_ms = now_ms;
        variable.value.float_val = temperature;
    }
    inside_timer = true;
    fan_update_callback(NULL);
    inside_timer = false;
}

static void assert_temperature(int temp, bool valid) {
    ts_temp_status_t global;
    ts_fan_status_t fan;
    assert(ts_temp_get_status(&global) == ESP_OK);
    assert(global.current_temp == temp);
    assert(global.current_valid == valid);
    if (valid) {
        assert(global.current_timestamp_ms == variable.last_update_ms);
        assert(ts_fan_get_status(0, &fan) == ESP_OK);
        assert(fan.temp == temp);
    }
}

static void set_constant_output(uint8_t duty) {
    fan_instance_t *fan = &s_fans[0];
    fan->config.min_duty = 0;
    fan->config.curve_points = 1;
    fan->config.curve[0] = (ts_fan_curve_point_t){490, duty};
    assert(update_pwm(fan, duty) == ESP_OK);
}
static void assert_output(uint8_t duty, bool guard, bool stale) {
    ts_fan_status_t status;
    assert(ts_fan_get_status(0, &status) == ESP_OK);
    assert(status.duty_valid && status.duty_percent == duty && pwm_output == duty);
    assert(status.guard_active == guard && status.temp_stale == stale);
}
static void check_auto_read_continuity(void) {
    const uint8_t duties[] = {15,53};
    for (unsigned d=0; d<2; d++) for (unsigned busy=0; busy<2; busy++) {
        setup(TS_FAN_MODE_AUTO);
        set_constant_output(duties[d]);
        tick(2000, 49.0, true);
        unsigned writes = pwm_writes, history = s_fans[0].temp_history_count;
        int64_t report = s_fans[0].auto_report_ms;
        temp_mutex_busy = busy == 0;
        variable_mutex_busy = busy == 1;
        ts_temp_auto_sample_t sample;
        inside_timer = true;
        ts_temp_read_auto_binding(&sample);
        inside_timer = false;
        assert(sample.result == (busy == 0 ? TS_TEMP_AUTO_SOURCE_BUSY : TS_TEMP_AUTO_VARIABLE_BUSY));
        tick(3000, 49.0, false);
        assert_output(duties[d],false,true);
        assert(pwm_writes == writes && s_fans[0].temp_history_count == history &&
               s_fans[0].auto_report_ms == report);
        temp_mutex_busy = variable_mutex_busy = false;
        tick(4000, 49.0, true);
        assert_output(duties[d],false,false);
        assert(s_fans[0].auto_report_ms == 4000 && s_fans[0].temp_history_count == history+1);
    }
    puts("PASS 15% and 53%: both lock failures hold the confirmed PWM without a write or a fabricated report");

    setup(TS_FAN_MODE_AUTO);
    set_constant_output(53);
    tick(2000,49,true);
    unsigned writes = pwm_writes;
    publish_during_read_ms = 3001;
    tick(3000,49,false);
    assert_output(53,false,false);
    assert(s_fans[0].auto_report_ms == 3001 && pwm_writes==writes+1);
    variable.last_update_ms = 5000;
    tick(4000,49,false);
    assert_output(53,false,true);
    assert(s_fans[0].auto_report_ms == 3001 && pwm_writes==writes+1);
    puts("PASS publication during read is accepted; a truly future timestamp is rejected");

    for (unsigned missing=0;missing<2;missing++) {
        setup(TS_FAN_MODE_AUTO);
        set_constant_output(15);
        tick(2000,49,true);
        variable_missing = missing;
        writes=pwm_writes;
        tick(7999,49,false);
        assert_output(15,false,missing);
        assert(pwm_writes==writes && s_fans[0].auto_report_ms==2000);
        tick(8000,49,false);
        assert_output(25,false,true);
        tick(8001,49,false);
        assert_output(25,false,true);
        variable_missing=false;
        tick(9000,49,true);
        assert_output(15,false,false);
    }
    puts("PASS stale threshold 5999/6000/6001ms for both a missing variable and a repeated report; fresh input restores AUTO");

    setup(TS_FAN_MODE_AUTO);
    set_constant_output(30);
    s_fans[0].config.curve[0].duty=24;
    for(int t=2;t<=22;t++) tick(t*1000,49,t%2==0);
    assert_output(24,false,false);
    assert(s_fans[0].temp_history_count==11);
    setup(TS_FAN_MODE_AUTO);
    set_constant_output(15);
    tick(2000,49,true);
    s_fans[0].config.curve[0].duty=100;
    tick(3000,49,false);
    assert_output(15,false,false);
    tick(4000,60,true);
    assert_output(39,false,false);
    tick(5000,60,false);
    assert_output(39,false,false);
    tick(6000,61,true);
    assert_output(63,false,false);
    puts("PASS asynchronous duplicate reports preserve normal rise and fall progress without manufacturing new samples");

    setup(TS_FAN_MODE_AUTO);
    set_constant_output(15);
    s_state.manual_mode=true;
    s_state.providers[TS_TEMP_SOURCE_MANUAL]=(provider_t){
       .type=TS_TEMP_SOURCE_MANUAL,.registered=true,.active=true,.value=1000,.last_update_ms=1000};
    tick(2000,49,true);
    assert_output(15,false,false);
    assert(s_state.active_source == TS_TEMP_SOURCE_MANUAL && s_fans[0].temperature==490);
    variable_missing=true;
    tick(8000,49,false);
    assert_output(25,false,true);
    variable_missing=false;
    s_state.providers[TS_TEMP_SOURCE_MANUAL].value=490;
    tick(9000,95,true);
    assert_output(100,true,false);
    puts("PASS AUTO ignores the general temperature selection in both directions; only the bound variable can govern AUTO");
}
static void check_auto_guard_continuity(void) {
    for (unsigned break_kind=0; break_kind<4; break_kind++) {
        setup(TS_FAN_MODE_AUTO);
        s_fans[0].config.max_duty = 70;
        tick(1000,95,true);
        tick(2000,89,true);
        for(int t=3;t<=30;t++) tick(t*1000,89,true);
        assert_output(100,true,false);
        if (break_kind==0) temp_mutex_busy=true;
        if (break_kind==1) variable_mutex_busy=true;
        if (break_kind==2) variable_missing=true;
        tick(31000,break_kind==3?90.1:89,true);
        assert_output(100,true,break_kind<3);
        assert(s_fans[0].guard_release_since_ms==0);
        if (break_kind==2) {
            tick(61000,89,false);
            assert_output(100,true,true);
        }
        temp_mutex_busy=variable_mutex_busy=variable_missing=false;
        int start = break_kind==2?62:32;
        for(int t=start;t<=start+30;t++) {
            tick(t*1000,89,true);
            if(t<start+30) assert_output(100,true,false);
        }
        assert(!s_fans[0].guard_active && pwm_output<100 && pwm_output>25);
    }
    puts("PASS guard remains at 100% across both busy locks, sustained loss, and 90.1C; only a fresh 30s low run releases");

    setup(TS_FAN_MODE_AUTO);
    s_fans[0].config.max_duty = 70;
    tick(1000,95,true);tick(2000,89,true);
    for(int t=3000;t<32000;t+=1000) {
        tick(t-200,89,false); /* Normal duplicate before the next producer report. */
        tick(t,89,true);
        assert_output(100,true,false);
    }
    tick(32000,89,false);
    assert_output(100,true,false);
    tick(32200,89,true);
    assert(!s_fans[0].guard_active);
    tick(33200,89,true);
    assert(pwm_output<100 && pwm_output>25);
    puts("PASS cached low snapshots bridge ordinary polling, but a new report must confirm the 30s endpoint");

    setup(TS_FAN_MODE_AUTO);
    tick(1000,95,true);tick(2000,89,true);
    for(int t=3;t<32;t++) tick(t*1000,89,true);
    now_ms=variable.last_update_ms=32000;
    ts_temp_auto_sample_t stale_binding;
    ts_temp_read_auto_binding(&stale_binding);
    ts_temp_bound_var_t replacement={.name="new.temperature",.weight=1.0f};
    assert(ts_temp_bind_variables(&replacement,1)==ESP_OK);
    assert(stale_binding.binding_revision!=ts_temp_auto_binding_revision());
    assert(apply_adaptive_auto(&s_fans[0],&stale_binding)==ESP_OK);
    assert_output(100,true,true);
    assert(s_fans[0].guard_release_since_ms==0);
    tick(33000,89,true);
    assert_output(100,true,false);
    assert(s_fans[0].guard_release_since_ms==33000);
    puts("PASS old-binding snapshot arriving after rebind cannot release guard");

    setup(TS_FAN_MODE_AUTO);
    s_fans[0].config.max_duty = 70;
    tick(1000,95,true);tick(2000,89,true);
    for(int t=3;t<32;t++) tick(t*1000,89,true);
    assert_output(100,true,false);
    uint32_t old_binding = ts_temp_auto_binding_revision();
    rebind_after_binding_read = true;
    tick(32000,89,true);
    assert(!rebind_after_binding_read && old_binding != ts_temp_auto_binding_revision());
    assert_output(100,true,false);
    assert(s_fans[0].guard_release_since_ms==0);
    variable_missing=true;
    tick(33000,89,false);
    assert_output(100,true,true);
    puts("PASS rebind on another thread after initial revision check cannot release guard; replacement loss keeps 100%");

    setup(TS_FAN_MODE_AUTO);
    set_constant_output(15);
    tick(2000,49,true);
    pwm_error = ESP_ERR_INVALID_STATE;
    tick(3000,95,true);
    assert_output(15,true,false);
    assert(s_fans[0].target_duty==100 && s_fans[0].fault);
    temp_mutex_busy=true;
    tick(4000,95,false);
    assert_output(15,true,true);
    assert(s_fans[0].target_duty==100 && s_fans[0].fault);
    pwm_error=ESP_OK;
    tick(5000,95,false);
    assert_output(100,true,true);
    assert(!s_fans[0].fault);
    puts("PASS guard retries after a PWM failure; last confirmed output remains 15% until the write succeeds");

}

static void check_weighted_auto_binding(void)
{
    const ts_temp_bound_var_t bindings[] = {
        {.name = "cpu.temperature", .weight = 0.75f},
        {.name = "gpu.hotspot", .weight = 0.25f},
    };
    setup(TS_FAN_MODE_AUTO);
    assert(ts_temp_bind_variables(bindings, 2) == ESP_OK);
    second_variable.last_update_ms = 2000;
    second_variable.value.float_val = 98;
    tick(2000, 98, true);
    ts_temp_status_t global;
    assert(ts_temp_get_status(&global) == ESP_OK && global.current_valid && global.current_temp == 980);
    assert_output(100, true, false);

    setup(TS_FAN_MODE_AUTO);
    assert(ts_temp_bind_variables(bindings, 2) == ESP_OK);
    second_variable.value.float_val = 80;
    second_variable.last_update_ms = 2000;
    tick(2000, 40, true);
    assert_output(40, false, false);
    assert(s_fans[0].control_temperature == 500 && s_fans[0].guard_temperature == 800);

    /* A new hot report must act even before the other producer reports again. */
    second_variable.value.float_val = 94;
    second_variable.last_update_ms = 3000;
    tick(3000, 40, false);
    assert(s_fans[0].control_temperature == 535 && s_fans[0].target_duty > 40);
    second_variable.value.float_val = 95;
    second_variable.last_update_ms = 4000;
    tick(4000, 40, false);
    assert_output(100, true, false);

    setup(TS_FAN_MODE_AUTO);
    assert(ts_temp_bind_variables(bindings, 2) == ESP_OK);
    second_variable.value.float_val = 98;
    tick(7000, 40, false); /* Both old reports are at the 6s cutoff. */
    assert_output(25, false, true);
    second_variable.last_update_ms = 8000;
    tick(8000, 40, false); /* One fresh hot member, one stale member. */
    assert_output(100, true, true);

    second_variable.value.float_val = 90;
    for (int t = 9000; t <= 70000; t += 1000) {
        /* At 40s the oldest report advances by 2s, but CPU reports 3s apart. */
        if (t != 40000) second_variable.last_update_ms = t;
        tick(t, 40, t != 38000 && t != 39000);
        if (t < 70000) assert(s_fans[0].guard_active);
    }
    assert(!s_fans[0].guard_active);

    const ts_temp_bound_var_t ignored[] = {
        {.name = "cpu.temperature", .weight = 0.0006f},
        {.name = "gpu.hotspot", .weight = 0.0006f},
    };
    assert(ts_temp_bind_variables(ignored, 2) == ESP_ERR_INVALID_ARG);
    puts("PASS weighted AUTO: weighted duty, asynchronous hot member, stale cutoff and continuous low release");
}

int main(void) {
    check_weighted_auto_binding();
    setup(TS_FAN_MODE_MANUAL);
    for (int second = 2; second <= 25; second++) {
        tick(second * 1000LL, 49.5, true);
        assert_temperature(495, true);
        assert(s_fans[0].mode == TS_FAN_MODE_MANUAL);
        assert(s_fans[0].current_duty == 37 && pwm_output == 37);
        assert(s_fans[0].auto_state == TS_FAN_AUTO_STATE_IDLE);
        assert(!s_fans[0].response_observing);
    }
    puts("PASS manual: 51.2 -> 49.5C remains fresh beyond 10s; manual 37% unchanged, no AUTO learning");

    /* Newest sample ages naturally; timer/status queries must not renew its timestamp. */
    tick(36000, 49.5, false);
    assert_temperature(TS_TEMP_DEFAULT_VALUE, false);
    assert(s_fans[0].current_duty == 37);
    tick(37000, 48.7, true);
    assert_temperature(487, true);
    assert(s_fans[0].current_duty == 37);
    puts("PASS actual stale input: still invalid after 10s, recovers on new report; manual speed unchanged");

    setup(TS_FAN_MODE_OFF);
    for (int second = 2; second <= 25; second++) {
        tick(second * 1000LL, 49.5, true);
        assert_temperature(495, true);
        assert(s_fans[0].current_duty == 0 && pwm_output == 0);
    }
    puts("PASS OFF: temperature monitoring stays fresh; output remains zero");

    setup(TS_FAN_MODE_MANUAL);
    s_fans[0].enabled = false;
    tick(2000, 49.5, true);
    assert_temperature(495, true);
    assert(s_fans[0].current_duty == 0 && pwm_output == 0);
    puts("PASS disabled fan: temperature monitoring stays fresh; output remains zero");

    for(unsigned busy=0;busy<2;busy++){
        setup(TS_FAN_MODE_MANUAL);
        temp_mutex_busy=busy==0;variable_mutex_busy=busy==1;
        tick(2000, 49.5, true);
        ts_temp_data_t unavailable;inside_timer=true;ts_temp_get_effective_nonblocking(&unavailable);inside_timer=false;
        assert(!unavailable.valid&&s_fans[0].current_duty==37);
        temp_mutex_busy=variable_mutex_busy=false;
        tick(3000, 49.5, true);assert_temperature(495, true);
    }
    puts("PASS busy temperature/variable service: real nonblocking getter invalid while busy, manual output preserved, refresh resumes");

    setup(TS_FAN_MODE_AUTO);
    for (int second = 2; second <= 25; second++) {
        tick(second * 1000LL, 60.0, true);
        assert_temperature(600, true);
    }
    assert(s_fans[0].auto_state == TS_FAN_AUTO_STATE_ACTIVE);
    assert(s_fans[0].current_duty == 60);
    tick(36000, 60.0, false);
    assert(s_fans[0].current_duty == 25 && s_fans[0].temp_stale);
    puts("PASS AUTO: control progresses once per timer; real stale input keeps approved 25% fallback");

    setup(TS_FAN_MODE_CURVE);
    tick(2000, 60.0, true);
    assert_temperature(600, true);
    assert(s_fans[0].current_duty == 60);
    puts("PASS CURVE: current temperature still drives configured curve");

    setup(TS_FAN_MODE_MANUAL);
    s_auto_temp_enabled = false;
    tick(2000, 49.5, true);
    assert(s_state.current_temp == 512);
    assert(s_fans[0].current_duty == 37);
    s_auto_temp_enabled = true;
    tick(3000, 49.5, true);
    assert_temperature(495, true);
    puts("PASS explicit auto-temperature disable/enable remains honored");
    setup(TS_FAN_MODE_AUTO);
    uint32_t identity=s_state.identity_revision;
    tick(2000,95.0,true);tick(3000,89.0,true);
    for(int t=4;t<=20;t++)tick(t*1000,89.0,true);
    assert(s_state.identity_revision==identity); /* Normal reports do not change identity. */
    ts_temp_bound_var_t binding={.name="gpu.temperature",.weight=1.0f};
    assert(ts_temp_bind_variables(&binding,1)==ESP_OK);
    assert(s_state.identity_revision!=identity);
    tick(21000,89.0,true);assert(s_fans[0].guard_release_since_ms==21000);
    identity=s_state.identity_revision;
    assert(ts_temp_bind_variables(&binding,1)==ESP_OK);
    assert(s_state.identity_revision==identity); /* Saving the same binding is not a source change. */
    for(int t=22;t<=51;t++) {
        tick(t*1000,89.0,true);
        if(t<51)assert(s_fans[0].guard_active);
    }
    assert(!s_fans[0].guard_active);
    identity=s_state.identity_revision;
    assert(ts_temp_unbind_variable()==ESP_OK);
    ts_temp_data_t data;ts_temp_get_effective_nonblocking(&data);
    assert(data.identity_revision!=identity&&!data.valid);
    puts("PASS actual temperature binding -> identity -> protection: normal reports retain identity; rebind restarts 30s qualification; unbind becomes invalid");
    check_auto_read_continuity();
    check_auto_guard_continuity();
    return 0;
}
