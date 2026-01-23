/**
 * @file ts_device_ctrl.c
 * @brief Device Power Control (AGX/LPMU) Implementation
 * 
 * AGX 使用电平控制（保持 HIGH/LOW）
 * LPMU 使用脉冲控制（按按钮方式切换）
 */

#include "ts_device_ctrl.h"
#include "ts_hal_gpio.h"
#include "ts_log.h"
#include "ts_event.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>

#define TAG "ts_device"

/*===========================================================================*/
/*                          AGX Instance                                      */
/*===========================================================================*/

typedef struct {
    ts_gpio_handle_t power_en;
    ts_gpio_handle_t reset;
    ts_gpio_handle_t force_recovery;
    ts_gpio_handle_t sys_rst;
    ts_gpio_handle_t power_good;
    ts_gpio_handle_t carrier_pwr_on;
    ts_gpio_handle_t shutdown_req;
    ts_gpio_handle_t sleep_wake;
} agx_gpio_handles_t;

typedef struct {
    bool configured;
    ts_agx_pins_t pins;
    agx_gpio_handles_t gpio;
    ts_device_state_t state;
    uint32_t power_on_time;
    uint32_t boot_count;
    int32_t last_error;
} agx_instance_t;

/*===========================================================================*/
/*                          LPMU Instance                                     */
/*===========================================================================*/

typedef struct {
    ts_gpio_handle_t power_btn;
    ts_gpio_handle_t reset;
} lpmu_gpio_handles_t;

typedef struct {
    bool configured;
    ts_lpmu_pins_t pins;
    lpmu_gpio_handles_t gpio;
    ts_device_state_t state;
    uint32_t power_on_time;
    uint32_t boot_count;
    int32_t last_error;
} lpmu_instance_t;

/*===========================================================================*/
/*                          Static Instances                                  */
/*===========================================================================*/

static agx_instance_t s_agx = {0};
static lpmu_instance_t s_lpmu = {0};
static bool s_initialized = false;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief Create output GPIO with specified initial level
 * @param gpio_num GPIO number
 * @param name GPIO name for debugging
 * @param initial_level Initial output level (0=LOW, 1=HIGH)
 * @return GPIO handle or NULL on failure
 */
static ts_gpio_handle_t create_output_gpio_with_level(int gpio_num, const char *name, int initial_level)
{
    if (gpio_num < 0) return NULL;
    
    ts_gpio_handle_t handle = ts_gpio_create_raw(gpio_num, name);
    if (!handle) return NULL;
    
    ts_gpio_config_t cfg = {
        .direction = TS_GPIO_DIR_OUTPUT,
        .pull_mode = TS_GPIO_PULL_NONE,
        .intr_type = TS_GPIO_INTR_DISABLE,
        .drive = TS_GPIO_DRIVE_2,
        .invert = false,
        .initial_level = initial_level
    };
    ts_gpio_configure(handle, &cfg);
    return handle;
}

/**
 * @brief Create output GPIO with default LOW level
 * @note Use create_output_gpio_with_level() for pins requiring HIGH initial state
 */
static ts_gpio_handle_t create_output_gpio(int gpio_num, const char *name)
{
    return create_output_gpio_with_level(gpio_num, name, 0);
}

static ts_gpio_handle_t create_input_gpio(int gpio_num, const char *name, bool with_pullup)
{
    if (gpio_num < 0) return NULL;
    
    ts_gpio_handle_t handle = ts_gpio_create_raw(gpio_num, name);
    if (!handle) return NULL;
    
    ts_gpio_config_t cfg = {
        .direction = TS_GPIO_DIR_INPUT,
        .pull_mode = with_pullup ? TS_GPIO_PULL_UP : TS_GPIO_PULL_NONE,
        .intr_type = TS_GPIO_INTR_DISABLE,
        .drive = TS_GPIO_DRIVE_2,
        .invert = false,
        .initial_level = -1
    };
    ts_gpio_configure(handle, &cfg);
    return handle;
}

static void shutdown_req_callback(ts_gpio_handle_t handle, void *arg)
{
    (void)handle;
    (void)arg;
    // AGX 发送了 shutdown 请求，可以发布事件
    // ts_event_post(TS_EVENT_BASE_DEVICE, TS_EVENT_DEVICE_SHUTDOWN_REQ, ...);
}

/*===========================================================================*/
/*                          AGX Control Functions                             */
/*===========================================================================*/

/**
 * AGX 电源控制说明（基于 robOS 硬件设计）：
 * 
 * AGX 没有独立的"电源开关"引脚！它是上电自启动的设计。
 * 
 * GPIO 3 (AGX_FORCE_SHUTDOWN / gpio_power_en):
 *   - LOW  = 允许开机/正常运行
 *   - HIGH = 强制关机
 * 
 * GPIO 1 (AGX_RESET):
 *   - LOW  = 正常运行（必须保持）
 *   - HIGH = 断电（持续）或 重置（脉冲）
 * 
 * 开机/关机流程：只操作 FORCE_SHUTDOWN (GPIO3)，不操作 RESET
 * 重置流程：RESET 脉冲 HIGH 后回 LOW
 * 
 * 注意：
 * 1. 只要不断电，就不要操作 RESET
 * 2. 避免操作 GPIO 后立即读取状态，可能导致状态变化（robOS 经验）
 */

static esp_err_t agx_power_on(void)
{
    TS_LOGI(TAG, "AGX powering on (force_shutdown=GPIO%d)...", s_agx.pins.gpio_power_en);
    
    if (s_agx.state == TS_DEVICE_STATE_ON) {
        TS_LOGI(TAG, "AGX already ON");
        return ESP_OK;
    }
    
    s_agx.state = TS_DEVICE_STATE_BOOTING;
    
    int pin_force_off = s_agx.pins.gpio_power_en;  // GPIO 3 = FORCE_SHUTDOWN
    
    if (pin_force_off < 0) {
        TS_LOGE(TAG, "Invalid FORCE_SHUTDOWN pin: %d", pin_force_off);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 直接设置 FORCE_SHUTDOWN = LOW（允许开机）
    // 注意：不要调用 gpio_reset_pin，会导致电平毛刺
    gpio_set_level(pin_force_off, 0);  // LOW = 允许开机
    TS_LOGI(TAG, "FORCE_SHUTDOWN set to LOW (GPIO%d), AGX powering on", pin_force_off);
    
    // Wait for power stabilization
    vTaskDelay(pdMS_TO_TICKS(TS_AGX_POWER_ON_DELAY_MS));
    
    s_agx.power_on_time = esp_timer_get_time() / 1000;
    s_agx.boot_count++;
    s_agx.state = TS_DEVICE_STATE_ON;
    
    TS_LOGI(TAG, "AGX powered on (boot #%lu)", s_agx.boot_count);
    
    // 发送设备状态变更事件 (WebSocket)
    cJSON *status = cJSON_CreateObject();
    if (status) {
        cJSON_AddStringToObject(status, "device", "agx");
        cJSON_AddBoolToObject(status, "power", true);
        cJSON_AddStringToObject(status, "state", "on");
        cJSON_AddNumberToObject(status, "boot_count", s_agx.boot_count);
        char *json_str = cJSON_PrintUnformatted(status);
        if (json_str) {
            ts_event_post(TS_EVENT_BASE_DEVICE_MON, TS_EVENT_DEVICE_STATUS_CHANGED, 
                         json_str, strlen(json_str) + 1, 0);
            cJSON_free(json_str);
        }
        cJSON_Delete(status);
    }
    
    return ESP_OK;
}

static esp_err_t agx_power_off(void)
{
    TS_LOGI(TAG, "AGX powering off (force_shutdown=GPIO%d)...", s_agx.pins.gpio_power_en);
    
    int pin_force_off = s_agx.pins.gpio_power_en;  // GPIO 3 = FORCE_SHUTDOWN
    
    if (pin_force_off < 0) {
        TS_LOGE(TAG, "Invalid FORCE_SHUTDOWN pin: %d", pin_force_off);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 直接设置 FORCE_SHUTDOWN = HIGH（强制关机）
    // 注意：不要调用 gpio_reset_pin，会导致电平毛刺
    gpio_set_level(pin_force_off, 1);  // HIGH = 强制关机
    TS_LOGI(TAG, "FORCE_SHUTDOWN set to HIGH (GPIO%d), AGX powered off", pin_force_off);
    
    s_agx.state = TS_DEVICE_STATE_OFF;
    TS_LOGI(TAG, "AGX powered off");
    
    // 发送设备状态变更事件 (WebSocket)
    cJSON *status = cJSON_CreateObject();
    if (status) {
        cJSON_AddStringToObject(status, "device", "agx");
        cJSON_AddBoolToObject(status, "power", false);
        cJSON_AddStringToObject(status, "state", "off");
        char *json_str = cJSON_PrintUnformatted(status);
        if (json_str) {
            ts_event_post(TS_EVENT_BASE_DEVICE_MON, TS_EVENT_DEVICE_STATUS_CHANGED, 
                         json_str, strlen(json_str) + 1, 0);
            cJSON_free(json_str);
        }
        cJSON_Delete(status);
    }
    
    return ESP_OK;
}

static esp_err_t agx_reset(void)
{
    TS_LOGI(TAG, "AGX resetting (reset=GPIO%d)...", s_agx.pins.gpio_reset);
    
    int pin_reset = s_agx.pins.gpio_reset;  // GPIO 1 = RESET
    
    if (pin_reset < 0) {
        TS_LOGE(TAG, "Invalid RESET pin");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 发送 RESET 脉冲：HIGH -> 延时 -> LOW
    // 注意：不要调用 gpio_reset_pin()，避免电平毛刺（robOS 经验）
    TS_LOGI(TAG, "Sending RESET pulse (GPIO%d HIGH for %dms)...", 
            pin_reset, TS_AGX_RESET_PULSE_MS);
    gpio_set_level(pin_reset, 1);  // HIGH = 重置
    vTaskDelay(pdMS_TO_TICKS(TS_AGX_RESET_PULSE_MS));
    gpio_set_level(pin_reset, 0);  // LOW = 恢复正常
    TS_LOGI(TAG, "RESET pulse complete, GPIO%d back to LOW", pin_reset);
    
    s_agx.boot_count++;
    s_agx.state = TS_DEVICE_STATE_BOOTING;
    
    TS_LOGI(TAG, "AGX reset complete (boot #%lu)", s_agx.boot_count);
    
    // 发送设备状态变更事件 (WebSocket)
    cJSON *status = cJSON_CreateObject();
    if (status) {
        cJSON_AddStringToObject(status, "device", "agx");
        cJSON_AddBoolToObject(status, "power", true);
        cJSON_AddStringToObject(status, "state", "resetting");
        cJSON_AddNumberToObject(status, "boot_count", s_agx.boot_count);
        char *json_str = cJSON_PrintUnformatted(status);
        if (json_str) {
            ts_event_post(TS_EVENT_BASE_DEVICE_MON, TS_EVENT_DEVICE_STATUS_CHANGED, 
                         json_str, strlen(json_str) + 1, 0);
            cJSON_free(json_str);
        }
        cJSON_Delete(status);
    }
    
    return ESP_OK;
}

static esp_err_t agx_force_off(void)
{
    TS_LOGW(TAG, "AGX force power off...");
    
    // Pulse force_off signal (like holding power button)
    if (s_agx.gpio.force_recovery) {  // Use force_recovery pin as force_off
        ts_gpio_set_level(s_agx.gpio.force_recovery, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_POWER_PULSE_MS));
        ts_gpio_set_level(s_agx.gpio.force_recovery, 0);
    }
    
    return agx_power_off();
}

static esp_err_t agx_enter_recovery(void)
{
    TS_LOGI(TAG, "AGX entering recovery mode...");
    
    // robOS recovery sequence:
    // 1. Assert force recovery HIGH, hold 1000ms
    if (s_agx.gpio.force_recovery) {
        ts_gpio_set_level(s_agx.gpio.force_recovery, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_RECOVERY_DELAY_MS));
    }
    
    // 2. Execute reset pulse
    if (s_agx.gpio.reset) {
        ts_gpio_set_level(s_agx.gpio.reset, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_RESET_PULSE_MS));
        ts_gpio_set_level(s_agx.gpio.reset, 0);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_RECOVERY_DELAY_MS));
    }
    
    // 3. Release force recovery
    if (s_agx.gpio.force_recovery) {
        ts_gpio_set_level(s_agx.gpio.force_recovery, 0);
    }
    
    s_agx.state = TS_DEVICE_STATE_RECOVERY;
    s_agx.boot_count++;
    
    TS_LOGI(TAG, "AGX in recovery mode");
    return ESP_OK;
}

/*===========================================================================*/
/*                          LPMU Control Functions                            */
/*===========================================================================*/

static esp_err_t lpmu_power_toggle(void)
{
    TS_LOGI(TAG, "LPMU power toggle (pulse)");
    
    // LPMU uses pulse to toggle power (like pressing a button)
    if (s_lpmu.gpio.power_btn) {
        ts_gpio_set_level(s_lpmu.gpio.power_btn, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_LPMU_POWER_PULSE_MS));
        ts_gpio_set_level(s_lpmu.gpio.power_btn, 0);
    }
    
    return ESP_OK;
}

static esp_err_t lpmu_power_on(void)
{
    if (s_lpmu.state == TS_DEVICE_STATE_ON) {
        return ESP_OK;
    }
    
    TS_LOGI(TAG, "LPMU powering on...");
    s_lpmu.state = TS_DEVICE_STATE_BOOTING;
    
    lpmu_power_toggle();
    
    s_lpmu.power_on_time = esp_timer_get_time() / 1000;
    s_lpmu.boot_count++;
    s_lpmu.state = TS_DEVICE_STATE_ON;
    
    TS_LOGI(TAG, "LPMU powered on (boot #%lu)", s_lpmu.boot_count);
    
    // 发送设备状态变更事件 (WebSocket)
    cJSON *status = cJSON_CreateObject();
    if (status) {
        cJSON_AddStringToObject(status, "device", "lpmu");
        cJSON_AddBoolToObject(status, "power", true);
        cJSON_AddStringToObject(status, "state", "on");
        cJSON_AddNumberToObject(status, "boot_count", s_lpmu.boot_count);
        char *json_str = cJSON_PrintUnformatted(status);
        if (json_str) {
            ts_event_post(TS_EVENT_BASE_DEVICE_MON, TS_EVENT_DEVICE_STATUS_CHANGED, 
                         json_str, strlen(json_str) + 1, 0);
            cJSON_free(json_str);
        }
        cJSON_Delete(status);
    }
    
    return ESP_OK;
}

static esp_err_t lpmu_power_off(void)
{
    if (s_lpmu.state == TS_DEVICE_STATE_OFF) {
        return ESP_OK;
    }
    
    TS_LOGI(TAG, "LPMU powering off...");
    
    lpmu_power_toggle();
    
    s_lpmu.state = TS_DEVICE_STATE_OFF;
    TS_LOGI(TAG, "LPMU powered off");
    
    // 发送设备状态变更事件 (WebSocket)
    cJSON *status = cJSON_CreateObject();
    if (status) {
        cJSON_AddStringToObject(status, "device", "lpmu");
        cJSON_AddBoolToObject(status, "power", false);
        cJSON_AddStringToObject(status, "state", "off");
        char *json_str = cJSON_PrintUnformatted(status);
        if (json_str) {
            ts_event_post(TS_EVENT_BASE_DEVICE_MON, TS_EVENT_DEVICE_STATUS_CHANGED, 
                         json_str, strlen(json_str) + 1, 0);
            cJSON_free(json_str);
        }
        cJSON_Delete(status);
    }
    
    return ESP_OK;
}

static esp_err_t lpmu_reset(void)
{
    TS_LOGI(TAG, "LPMU resetting...");
    
    if (s_lpmu.gpio.reset) {
        // Pulse reset HIGH
        ts_gpio_set_level(s_lpmu.gpio.reset, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_LPMU_RESET_PULSE_MS));
        ts_gpio_set_level(s_lpmu.gpio.reset, 0);
    }
    
    s_lpmu.boot_count++;
    s_lpmu.state = TS_DEVICE_STATE_BOOTING;
    
    TS_LOGI(TAG, "LPMU reset complete (boot #%lu)", s_lpmu.boot_count);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API                                        */
/*===========================================================================*/

esp_err_t ts_device_ctrl_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(&s_agx, 0, sizeof(s_agx));
    memset(&s_lpmu, 0, sizeof(s_lpmu));
    
    // Initialize AGX pins to -1
    s_agx.pins = (ts_agx_pins_t){
        .gpio_power_en = -1,
        .gpio_reset = -1,
        .gpio_force_recovery = -1,
        .gpio_sys_rst = -1,
        .gpio_power_good = -1,
        .gpio_carrier_pwr_on = -1,
        .gpio_shutdown_req = -1,
        .gpio_sleep_wake = -1
    };
    
    // Initialize LPMU pins to -1
    s_lpmu.pins = (ts_lpmu_pins_t){
        .gpio_power_btn = -1,
        .gpio_reset = -1
    };
    
    s_initialized = true;
    TS_LOGI(TAG, "Device control initialized");
    return ESP_OK;
}

esp_err_t ts_device_ctrl_deinit(void)
{
    // Cleanup AGX GPIO
    if (s_agx.gpio.power_en) ts_gpio_destroy(s_agx.gpio.power_en);
    if (s_agx.gpio.reset) ts_gpio_destroy(s_agx.gpio.reset);
    if (s_agx.gpio.force_recovery) ts_gpio_destroy(s_agx.gpio.force_recovery);
    if (s_agx.gpio.sys_rst) ts_gpio_destroy(s_agx.gpio.sys_rst);
    if (s_agx.gpio.power_good) ts_gpio_destroy(s_agx.gpio.power_good);
    if (s_agx.gpio.carrier_pwr_on) ts_gpio_destroy(s_agx.gpio.carrier_pwr_on);
    if (s_agx.gpio.shutdown_req) ts_gpio_destroy(s_agx.gpio.shutdown_req);
    if (s_agx.gpio.sleep_wake) ts_gpio_destroy(s_agx.gpio.sleep_wake);
    
    // Cleanup LPMU GPIO
    if (s_lpmu.gpio.power_btn) ts_gpio_destroy(s_lpmu.gpio.power_btn);
    if (s_lpmu.gpio.reset) ts_gpio_destroy(s_lpmu.gpio.reset);
    
    memset(&s_agx, 0, sizeof(s_agx));
    memset(&s_lpmu, 0, sizeof(s_lpmu));
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_device_configure_agx(const ts_agx_pins_t *pins)
{
    if (!pins) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    s_agx.pins = *pins;
    
    /*
     * AGX GPIO 初始状态：
     * 
     * AGX 是上电自启动设计！ESP32 不应该阻止它启动。
     * 
     * - FORCE_SHUTDOWN (gpio_power_en): LOW=允许开机, HIGH=强制关机
     *   → 初始化为 LOW，让 AGX 可以正常上电启动
     * 
     * - RESET: LOW=正常运行（必须保持）, HIGH=断电
     *   → 初始化为 LOW，启用电源控制
     * 
     * - FORCE_RECOVERY: HIGH=恢复模式, LOW=正常模式
     *   → 初始化为 LOW
     */
    
    // FORCE_SHUTDOWN: 初始化为 LOW（允许 AGX 上电自启动）
    s_agx.gpio.power_en = create_output_gpio_with_level(pins->gpio_power_en, "agx_pwr", 0);
    
    // RESET: 初始化为 LOW（正常运行，电源控制启用）
    s_agx.gpio.reset = create_output_gpio_with_level(pins->gpio_reset, "agx_rst", 0);
    
    // FORCE_RECOVERY: 初始化为 LOW（正常模式）
    s_agx.gpio.force_recovery = create_output_gpio_with_level(pins->gpio_force_recovery, "agx_rcv", 0);
    
    // 其他输出引脚（如果配置）
    s_agx.gpio.carrier_pwr_on = create_output_gpio(pins->gpio_carrier_pwr_on, "agx_carrier");
    s_agx.gpio.sleep_wake = create_output_gpio(pins->gpio_sleep_wake, "agx_sw");
    
    // Configure input pins
    s_agx.gpio.power_good = create_input_gpio(pins->gpio_power_good, "agx_pg", true);
    s_agx.gpio.sys_rst = create_input_gpio(pins->gpio_sys_rst, "agx_rst_in", true);
    
    // Shutdown request with interrupt
    if (pins->gpio_shutdown_req >= 0) {
        s_agx.gpio.shutdown_req = ts_gpio_create_raw(pins->gpio_shutdown_req, "agx_shutdown");
        if (s_agx.gpio.shutdown_req) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_INPUT,
                .pull_mode = TS_GPIO_PULL_UP,
                .intr_type = TS_GPIO_INTR_NEGEDGE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = -1
            };
            ts_gpio_configure(s_agx.gpio.shutdown_req, &cfg);
            ts_gpio_set_isr_callback(s_agx.gpio.shutdown_req, shutdown_req_callback, NULL);
            ts_gpio_intr_enable(s_agx.gpio.shutdown_req);
        }
    }
    
    s_agx.configured = true;
    s_agx.state = TS_DEVICE_STATE_ON;  // 假设 AGX 已经在运行（上电自启动）
    
    TS_LOGI(TAG, "AGX configured: FORCE_SHUTDOWN=GPIO%d(LOW=run), RESET=GPIO%d(LOW=normal)",
            pins->gpio_power_en, pins->gpio_reset);
    return ESP_OK;
}

esp_err_t ts_device_configure_lpmu(const ts_lpmu_pins_t *pins)
{
    if (!pins) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    s_lpmu.pins = *pins;
    
    /*
     * LPMU GPIO 初始状态：
     * - LPMU_POWER_BTN: 脉冲控制（HIGH 300ms 切换电源）→ 初始化为 LOW
     * - LPMU_RESET: 脉冲控制（HIGH 300ms 复位）→ 初始化为 LOW
     */
    s_lpmu.gpio.power_btn = create_output_gpio_with_level(pins->gpio_power_btn, "lpmu_pwr", 0);
    s_lpmu.gpio.reset = create_output_gpio_with_level(pins->gpio_reset, "lpmu_rst", 0);
    
    s_lpmu.configured = true;
    s_lpmu.state = TS_DEVICE_STATE_OFF;
    
    TS_LOGI(TAG, "LPMU configured (power=%d[pulse HIGH], reset=%d[pulse HIGH])",
            pins->gpio_power_btn, pins->gpio_reset);
    return ESP_OK;
}

esp_err_t ts_device_power_on(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            return agx_power_on();
        case TS_DEVICE_LPMU:
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            return lpmu_power_on();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t ts_device_power_off(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            return agx_power_off();
        case TS_DEVICE_LPMU:
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            return lpmu_power_off();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t ts_device_force_off(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            return agx_force_off();
        case TS_DEVICE_LPMU:
            // LPMU 没有 force_off，直接 power_off
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            return lpmu_power_off();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t ts_device_reset(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            return agx_reset();
        case TS_DEVICE_LPMU:
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            return lpmu_reset();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t ts_device_enter_recovery(ts_device_id_t device)
{
    if (device != TS_DEVICE_AGX) {
        TS_LOGW(TAG, "Recovery mode only supported for AGX");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
    return agx_enter_recovery();
}

esp_err_t ts_device_get_status(ts_device_id_t device, ts_device_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            status->state = s_agx.state;
            status->boot_count = s_agx.boot_count;
            status->last_error = s_agx.last_error;
            if (s_agx.gpio.power_good) {
                status->power_good = ts_gpio_get_level(s_agx.gpio.power_good) == 1;
            } else {
                status->power_good = s_agx.state == TS_DEVICE_STATE_ON;
            }
            if (s_agx.state == TS_DEVICE_STATE_ON && s_agx.power_on_time > 0) {
                status->uptime_ms = (esp_timer_get_time() / 1000) - s_agx.power_on_time;
            } else {
                status->uptime_ms = 0;
            }
            break;
            
        case TS_DEVICE_LPMU:
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            status->state = s_lpmu.state;
            status->boot_count = s_lpmu.boot_count;
            status->last_error = s_lpmu.last_error;
            status->power_good = s_lpmu.state == TS_DEVICE_STATE_ON;
            if (s_lpmu.state == TS_DEVICE_STATE_ON && s_lpmu.power_on_time > 0) {
                status->uptime_ms = (esp_timer_get_time() / 1000) - s_lpmu.power_on_time;
            } else {
                status->uptime_ms = 0;
            }
            break;
            
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

bool ts_device_is_powered(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            return s_agx.state == TS_DEVICE_STATE_ON || s_agx.state == TS_DEVICE_STATE_RECOVERY;
        case TS_DEVICE_LPMU:
            return s_lpmu.state == TS_DEVICE_STATE_ON;
        default:
            return false;
    }
}

bool ts_device_is_configured(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            return s_agx.configured;
        case TS_DEVICE_LPMU:
            return s_lpmu.configured;
        default:
            return false;
    }
}

esp_err_t ts_device_request_shutdown(ts_device_id_t device)
{
    if (device != TS_DEVICE_AGX) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
    
    TS_LOGI(TAG, "Requesting AGX shutdown...");
    
    if (s_agx.gpio.sleep_wake) {
        // Pulse sleep/wake to signal shutdown request
        ts_gpio_set_level(s_agx.gpio.sleep_wake, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        ts_gpio_set_level(s_agx.gpio.sleep_wake, 0);
    }
    
    return ESP_OK;
}

esp_err_t ts_device_handle_shutdown_request(ts_device_id_t device)
{
    TS_LOGI(TAG, "Handling shutdown request from device %d", device);
    return ts_device_power_off(device);
}

const char *ts_device_state_to_str(ts_device_state_t state)
{
    switch (state) {
        case TS_DEVICE_STATE_OFF:      return "off";
        case TS_DEVICE_STATE_STANDBY:  return "standby";
        case TS_DEVICE_STATE_ON:       return "on";
        case TS_DEVICE_STATE_BOOTING:  return "booting";
        case TS_DEVICE_STATE_RECOVERY: return "recovery";
        case TS_DEVICE_STATE_ERROR:    return "error";
        default:                       return "unknown";
    }
}
