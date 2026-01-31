/**
 * @file ts_power_policy.c
 * @brief Power Policy Engine Implementation
 * 
 * 移植自 robOS voltage_protection 组件
 * 
 * 状态机流程：
 * 1. NORMAL: 正常运行，持续监控电压
 * 2. LOW_VOLTAGE: 电压低于阈值，开始倒计时
 *    - 电压恢复 → 回到 NORMAL
 *    - 倒计时归零 → 进入 SHUTDOWN
 * 3. SHUTDOWN: 执行设备关机（AGX reset, LPMU toggle）
 * 4. PROTECTED: 等待电压恢复
 *    - 电压恢复 → 进入 RECOVERY
 * 5. RECOVERY: 电压稳定等待
 *    - 稳定后 → esp_restart() 重启系统
 */

#include "ts_power_policy.h"
#include "ts_power_monitor.h"
#include "ts_device_ctrl.h"
#include "ts_event.h"
#include "ts_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <string.h>

#define TAG "ts_power_policy"

/*===========================================================================*/
/*                          Internal State                                    */
/*===========================================================================*/

typedef struct {
    bool initialized;
    bool running;
    ts_power_policy_config_t config;
    ts_power_policy_state_t state;
    
    /* Voltage tracking */
    float current_voltage;
    float last_voltage;
    
    /* Timers */
    uint32_t countdown_remaining_sec;
    uint32_t recovery_timer_sec;
    uint32_t shutdown_timer_sec;
    
    /* Device status */
    bool agx_powered;
    bool lpmu_powered;
    bool agx_connected;
    bool fans_stopped;
    
    /* Statistics */
    uint32_t protection_count;
    uint64_t start_time_us;
    
    /* Task and sync */
    TaskHandle_t monitor_task_handle;
    SemaphoreHandle_t state_mutex;
    
    /* Test mode */
    bool test_mode;
    float test_voltage;
    
    /* Debug mode */
    bool debug_mode;
    uint32_t debug_remaining_sec;
    
    /* Callback */
    ts_power_policy_callback_t callback;
    void *callback_user_data;
    
} power_policy_state_t;

static power_policy_state_t s_pp = {0};

/*===========================================================================*/
/*                          Forward Declarations                              */
/*===========================================================================*/

static void power_policy_task(void *pvParameters);
static void check_voltage(float voltage);
static void check_device_status(void);
static void execute_shutdown(void);
static void execute_recovery(void);
static void update_led_status(void);
static void trigger_event(ts_power_policy_event_t event);

/*===========================================================================*/
/*                          Public API                                        */
/*===========================================================================*/

esp_err_t ts_power_policy_get_default_config(ts_power_policy_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(config, 0, sizeof(ts_power_policy_config_t));
    config->low_voltage_threshold = TS_POWER_POLICY_LOW_VOLTAGE_DEFAULT;
    config->recovery_voltage_threshold = TS_POWER_POLICY_RECOVERY_VOLTAGE_DEFAULT;
    config->shutdown_delay_sec = TS_POWER_POLICY_SHUTDOWN_DELAY_DEFAULT;
    config->recovery_hold_sec = TS_POWER_POLICY_RECOVERY_HOLD_DEFAULT;
    config->auto_recovery_enabled = true;
    config->enable_led_feedback = true;
    config->enable_device_shutdown = true;
    
    return ESP_OK;
}

esp_err_t ts_power_policy_init(const ts_power_policy_config_t *config)
{
    if (s_pp.initialized) {
        TS_LOGW(TAG, "Power policy already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Initializing power policy v%s", TS_POWER_POLICY_VERSION);
    
    /* 使用提供的配置或默认配置 */
    if (config != NULL) {
        memcpy(&s_pp.config, config, sizeof(ts_power_policy_config_t));
    } else {
        ts_power_policy_get_default_config(&s_pp.config);
    }
    
    /* 创建互斥锁 */
    s_pp.state_mutex = xSemaphoreCreateMutex();
    if (s_pp.state_mutex == NULL) {
        TS_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    
    /* 初始化状态 */
    s_pp.state = TS_POWER_POLICY_STATE_NORMAL;
    s_pp.start_time_us = esp_timer_get_time();
    s_pp.test_mode = false;
    s_pp.test_voltage = 0.0f;
    
    s_pp.initialized = true;
    
    TS_LOGI(TAG, "Power policy initialized (Low: %.1fV, Recovery: %.1fV, Delay: %lus)",
            s_pp.config.low_voltage_threshold,
            s_pp.config.recovery_voltage_threshold,
            (unsigned long)s_pp.config.shutdown_delay_sec);
    
    return ESP_OK;
}

esp_err_t ts_power_policy_deinit(void)
{
    if (!s_pp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Deinitializing power policy");
    
    /* 停止监控 */
    ts_power_policy_stop();
    
    /* 清理互斥锁 */
    if (s_pp.state_mutex) {
        vSemaphoreDelete(s_pp.state_mutex);
        s_pp.state_mutex = NULL;
    }
    
    memset(&s_pp, 0, sizeof(power_policy_state_t));
    
    return ESP_OK;
}

esp_err_t ts_power_policy_start(void)
{
    if (!s_pp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_pp.running) {
        TS_LOGW(TAG, "Power policy already running");
        return ESP_OK;
    }
    
    TS_LOGI(TAG, "Starting power policy monitoring");
    
    /* 先设置 running 标志，避免竞态条件 */
    s_pp.running = true;
    
    /* 创建监控任务 */
    BaseType_t ret = xTaskCreate(
        power_policy_task,
        "power_policy",
        3072,
        NULL,
        5,
        &s_pp.monitor_task_handle
    );
    
    if (ret != pdPASS) {
        TS_LOGE(TAG, "Failed to create power policy task");
        s_pp.running = false;
        return ESP_FAIL;
    }
    
    TS_LOGI(TAG, "Power policy monitoring started");
    return ESP_OK;
}

esp_err_t ts_power_policy_stop(void)
{
    if (!s_pp.initialized || !s_pp.running) {
        return ESP_OK;
    }
    
    TS_LOGI(TAG, "Stopping power policy monitoring");
    
    s_pp.running = false;
    
    /* 删除监控任务 */
    if (s_pp.monitor_task_handle) {
        vTaskDelete(s_pp.monitor_task_handle);
        s_pp.monitor_task_handle = NULL;
    }
    
    TS_LOGI(TAG, "Power policy monitoring stopped");
    return ESP_OK;
}

esp_err_t ts_power_policy_get_status(ts_power_policy_status_t *status)
{
    if (!s_pp.initialized || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        status->initialized = s_pp.initialized;
        status->running = s_pp.running;
        status->state = s_pp.state;
        status->current_voltage = s_pp.current_voltage;
        status->countdown_remaining_sec = s_pp.countdown_remaining_sec;
        status->recovery_timer_sec = s_pp.recovery_timer_sec;
        status->protection_count = s_pp.protection_count;
        status->uptime_ms = (esp_timer_get_time() - s_pp.start_time_us) / 1000;
        
        status->device_status.agx_powered = s_pp.agx_powered;
        status->device_status.lpmu_powered = s_pp.lpmu_powered;
        status->device_status.agx_connected = s_pp.agx_connected;
        
        xSemaphoreGive(s_pp.state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool ts_power_policy_is_initialized(void)
{
    return s_pp.initialized;
}

bool ts_power_policy_is_running(void)
{
    return s_pp.running;
}

ts_power_policy_state_t ts_power_policy_get_state(void)
{
    return s_pp.state;
}

const char *ts_power_policy_get_state_name(ts_power_policy_state_t state)
{
    switch (state) {
        case TS_POWER_POLICY_STATE_NORMAL:      return "正常运行";
        case TS_POWER_POLICY_STATE_LOW_VOLTAGE: return "低电压保护";
        case TS_POWER_POLICY_STATE_SHUTDOWN:    return "关机中";
        case TS_POWER_POLICY_STATE_PROTECTED:   return "保护状态";
        case TS_POWER_POLICY_STATE_RECOVERY:    return "电压恢复中";
        default:                                return "未知";
    }
}

esp_err_t ts_power_policy_trigger_test(void)
{
    if (!s_pp.initialized) {
        TS_LOGE(TAG, "Cannot trigger test: not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!s_pp.running) {
        TS_LOGE(TAG, "Cannot trigger test: not running");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_pp.test_mode = true;
        s_pp.test_voltage = s_pp.config.low_voltage_threshold - 0.5f;
        
        TS_LOGW(TAG, "Test mode activated: simulating %.2fV (threshold: %.2fV)",
                s_pp.test_voltage, s_pp.config.low_voltage_threshold);
        
        xSemaphoreGive(s_pp.state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t ts_power_policy_reset(void)
{
    if (!s_pp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        TS_LOGI(TAG, "Protection reset requested - will restart ESP32");
        
        /* 重置状态 */
        s_pp.test_mode = false;
        s_pp.state = TS_POWER_POLICY_STATE_NORMAL;
        s_pp.countdown_remaining_sec = 0;
        s_pp.recovery_timer_sec = 0;
        
        xSemaphoreGive(s_pp.state_mutex);
        
        TS_LOGI(TAG, "Restarting system...");
        vTaskDelay(pdMS_TO_TICKS(100));  /* 允许日志刷新 */
        esp_restart();
        
        return ESP_OK;  /* 不会到达 */
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t ts_power_policy_set_thresholds(float low_threshold, float recovery_threshold)
{
    if (!s_pp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (low_threshold >= recovery_threshold) {
        TS_LOGE(TAG, "Low threshold must be less than recovery threshold");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_pp.config.low_voltage_threshold = low_threshold;
        s_pp.config.recovery_voltage_threshold = recovery_threshold;
        
        TS_LOGI(TAG, "Thresholds updated: Low=%.1fV, Recovery=%.1fV",
                low_threshold, recovery_threshold);
        
        xSemaphoreGive(s_pp.state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t ts_power_policy_get_thresholds(float *low_threshold, float *recovery_threshold)
{
    if (!s_pp.initialized || low_threshold == NULL || recovery_threshold == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *low_threshold = s_pp.config.low_voltage_threshold;
    *recovery_threshold = s_pp.config.recovery_voltage_threshold;
    
    return ESP_OK;
}

esp_err_t ts_power_policy_set_shutdown_delay(uint32_t delay_sec)
{
    if (!s_pp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (delay_sec < 5 || delay_sec > 300) {
        TS_LOGE(TAG, "Shutdown delay must be 5-300 seconds");
        return ESP_ERR_INVALID_ARG;
    }
    
    s_pp.config.shutdown_delay_sec = delay_sec;
    TS_LOGI(TAG, "Shutdown delay set to %lu seconds", (unsigned long)delay_sec);
    
    return ESP_OK;
}

esp_err_t ts_power_policy_register_callback(ts_power_policy_callback_t callback, void *user_data)
{
    s_pp.callback = callback;
    s_pp.callback_user_data = user_data;
    return ESP_OK;
}

esp_err_t ts_power_policy_set_debug_mode(bool enable, uint32_t duration_sec)
{
    if (!s_pp.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_pp.debug_mode = enable;
        s_pp.debug_remaining_sec = duration_sec;
        
        if (enable) {
            TS_LOGI(TAG, "Debug mode enabled for %lu seconds", (unsigned long)duration_sec);
        } else {
            TS_LOGI(TAG, "Debug mode disabled");
        }
        
        xSemaphoreGive(s_pp.state_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool ts_power_policy_is_debug_mode(void)
{
    return s_pp.debug_mode;
}

/*===========================================================================*/
/*                          Internal Functions                                */
/*===========================================================================*/

static void trigger_event(ts_power_policy_event_t event)
{
    /* 注意：此函数在持有 state_mutex 时调用，不能再获取锁 */
    /* 直接读取状态（调用者已持有锁） */
    ts_power_policy_status_t status = {
        .initialized = s_pp.initialized,
        .running = s_pp.running,
        .state = s_pp.state,
        .current_voltage = s_pp.current_voltage,
        .countdown_remaining_sec = s_pp.countdown_remaining_sec,
        .recovery_timer_sec = s_pp.recovery_timer_sec,
        .protection_count = s_pp.protection_count,
        .uptime_ms = (esp_timer_get_time() - s_pp.start_time_us) / 1000,
        .device_status = {
            .agx_powered = s_pp.agx_powered,
            .lpmu_powered = s_pp.lpmu_powered,
            .agx_connected = s_pp.agx_connected,
        }
    };
    
    /* 调用用户回调 */
    if (s_pp.callback) {
        s_pp.callback(event, &status, s_pp.callback_user_data);
    }
    
    /* 发布到事件总线 */
    ts_event_post(TS_EVENT_BASE_POWER, (int32_t)event, &status, sizeof(status), 0);
}

static void power_policy_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t check_period = pdMS_TO_TICKS(1000);  /* 每秒检查一次 */
    uint32_t voltage_read_fail_count = 0;
    
    TS_LOGI(TAG, "Power policy task started");
    
    while (s_pp.running) {
        /* 读取电压 */
        float voltage_to_check;
        bool voltage_valid = false;
        
        /* 检查测试模式 */
        bool test_mode_active = false;
        float test_voltage_value = 0.0f;
        
        if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            test_mode_active = s_pp.test_mode;
            test_voltage_value = s_pp.test_voltage;
            xSemaphoreGive(s_pp.state_mutex);
        }
        
        if (test_mode_active) {
            voltage_to_check = test_voltage_value;
            voltage_valid = true;
            TS_LOGD(TAG, "Test mode: using simulated voltage %.2fV", voltage_to_check);
        } else {
            /* 从 power_monitor 读取电压 */
            ts_power_voltage_data_t voltage_data;
            esp_err_t ret = ts_power_monitor_read_voltage_now(&voltage_data);
            
            if (ret == ESP_OK) {
                voltage_to_check = voltage_data.supply_voltage;
                
                /* 忽略无效读数（0V 或 < 5V）*/
                if (voltage_to_check > TS_POWER_POLICY_MIN_VALID_VOLTAGE) {
                    voltage_valid = true;
                    
                    if (voltage_read_fail_count > 0) {
                        TS_LOGI(TAG, "Voltage reading recovered: %.2fV", voltage_to_check);
                        voltage_read_fail_count = 0;
                    }
                } else {
                    voltage_read_fail_count++;
                    if (voltage_read_fail_count == 1 || voltage_read_fail_count % 10 == 0) {
                        TS_LOGW(TAG, "Invalid voltage reading: %.2fV (count: %lu)",
                                voltage_to_check, (unsigned long)voltage_read_fail_count);
                    }
                }
            } else {
                voltage_read_fail_count++;
                if (voltage_read_fail_count == 1 || voltage_read_fail_count % 10 == 0) {
                    TS_LOGW(TAG, "Failed to read voltage (count: %lu): %s",
                            (unsigned long)voltage_read_fail_count, esp_err_to_name(ret));
                }
            }
        }
        
        /* 电压有效时进行状态检查 */
        if (voltage_valid) {
            check_voltage(voltage_to_check);
        }
        
        /* 检查设备状态 */
        check_device_status();
        
        /* 状态机处理 */
        if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (s_pp.state) {
                case TS_POWER_POLICY_STATE_LOW_VOLTAGE:
                    if (s_pp.countdown_remaining_sec > 0) {
                        s_pp.countdown_remaining_sec--;
                        
                        /* 每 10 秒或最后 5 秒打印日志 */
                        if (s_pp.countdown_remaining_sec % 10 == 0 ||
                            s_pp.countdown_remaining_sec <= 5) {
                            TS_LOGW(TAG, "Low voltage countdown: %lus remaining",
                                    (unsigned long)s_pp.countdown_remaining_sec);
                            trigger_event(TS_POWER_POLICY_EVENT_COUNTDOWN_TICK);
                        }
                        
                        if (s_pp.countdown_remaining_sec == 0) {
                            TS_LOGW(TAG, "Countdown complete, initiating shutdown");
                            s_pp.state = TS_POWER_POLICY_STATE_SHUTDOWN;
                            trigger_event(TS_POWER_POLICY_EVENT_SHUTDOWN_START);
                        }
                    }
                    break;
                    
                case TS_POWER_POLICY_STATE_RECOVERY:
                    if (s_pp.recovery_timer_sec > 0) {
                        s_pp.recovery_timer_sec--;
                        TS_LOGI(TAG, "Recovery timer: %lus remaining",
                                (unsigned long)s_pp.recovery_timer_sec);
                        
                        if (s_pp.recovery_timer_sec == 0) {
                            TS_LOGI(TAG, "Recovery confirmed, restarting system");
                            trigger_event(TS_POWER_POLICY_EVENT_RECOVERY_COMPLETE);
                            xSemaphoreGive(s_pp.state_mutex);
                            execute_recovery();
                            continue;  /* execute_recovery 会重启，不会返回 */
                        }
                    }
                    break;
                    
                case TS_POWER_POLICY_STATE_SHUTDOWN:
                    xSemaphoreGive(s_pp.state_mutex);
                    execute_shutdown();
                    if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        s_pp.state = TS_POWER_POLICY_STATE_PROTECTED;
                        trigger_event(TS_POWER_POLICY_EVENT_PROTECTED);
                        TS_LOGW(TAG, "Entered protected state");
                    }
                    break;
                    
                case TS_POWER_POLICY_STATE_PROTECTED:
                    /* 在保护状态下处理风扇关闭定时器 */
                    if (!s_pp.fans_stopped && s_pp.shutdown_timer_sec > 0) {
                        s_pp.shutdown_timer_sec--;
                        
                        if (s_pp.shutdown_timer_sec % 10 == 0 ||
                            s_pp.shutdown_timer_sec <= 5) {
                            TS_LOGI(TAG, "Fan shutdown countdown: %lus remaining",
                                    (unsigned long)s_pp.shutdown_timer_sec);
                        }
                        
                        if (s_pp.shutdown_timer_sec == 0) {
                            TS_LOGW(TAG, "Stopping all fans");
                            /* TODO: 调用风扇控制器停止所有风扇 */
                            s_pp.fans_stopped = true;
                        }
                    }
                    break;
                    
                default:
                    break;
            }
            
            /* 调试模式：每秒输出状态 */
            if (s_pp.debug_mode) {
                /* 发送 WebSocket 事件（Web 终端） */
                trigger_event(TS_POWER_POLICY_EVENT_DEBUG_TICK);
                
                /* 打印到串口日志 */
                TS_LOGI(TAG, "[DEBUG] %s | V: %.2fV | 倒计时: %lus | 恢复: %lus | 剩余: %lus",
                        ts_power_policy_get_state_name(s_pp.state),
                        s_pp.current_voltage,
                        (unsigned long)s_pp.countdown_remaining_sec,
                        (unsigned long)s_pp.recovery_timer_sec,
                        (unsigned long)s_pp.debug_remaining_sec);
                
                /* 递减调试计时器 */
                if (s_pp.debug_remaining_sec > 0) {
                    s_pp.debug_remaining_sec--;
                    if (s_pp.debug_remaining_sec == 0) {
                        s_pp.debug_mode = false;
                        TS_LOGI(TAG, "Debug mode auto-disabled");
                    }
                }
            }
            
            xSemaphoreGive(s_pp.state_mutex);
        }
        
        /* 更新 LED 状态 */
        update_led_status();
        
        /* 等待下一个周期 */
        vTaskDelayUntil(&last_wake_time, check_period);
    }
    
    TS_LOGI(TAG, "Power policy task ended");
    vTaskDelete(NULL);
}

static void check_voltage(float voltage)
{
    if (!s_pp.initialized) {
        return;
    }
    
    if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    
    s_pp.current_voltage = voltage;
    ts_power_policy_state_t old_state = s_pp.state;
    
    switch (s_pp.state) {
        case TS_POWER_POLICY_STATE_NORMAL:
            if (voltage < s_pp.config.low_voltage_threshold) {
                TS_LOGW(TAG, "[STATE] NORMAL -> LOW_VOLTAGE: %.2fV < %.2fV",
                        voltage, s_pp.config.low_voltage_threshold);
                s_pp.state = TS_POWER_POLICY_STATE_LOW_VOLTAGE;
                s_pp.countdown_remaining_sec = s_pp.config.shutdown_delay_sec;
                s_pp.protection_count++;
                trigger_event(TS_POWER_POLICY_EVENT_LOW_VOLTAGE);
            }
            break;
            
        case TS_POWER_POLICY_STATE_LOW_VOLTAGE:
            /* 倒计时期间电压恢复 → 取消关机 */
            if (voltage >= s_pp.config.recovery_voltage_threshold) {
                TS_LOGI(TAG, "[STATE] LOW_VOLTAGE -> NORMAL: %.2fV >= %.2fV (countdown canceled)",
                        voltage, s_pp.config.recovery_voltage_threshold);
                s_pp.state = TS_POWER_POLICY_STATE_NORMAL;
                s_pp.countdown_remaining_sec = 0;
            }
            break;
            
        case TS_POWER_POLICY_STATE_PROTECTED:
            /* 保护状态下电压恢复 → 开始恢复流程 */
            if (voltage >= s_pp.config.recovery_voltage_threshold) {
                TS_LOGI(TAG, "[STATE] PROTECTED -> RECOVERY: %.2fV >= %.2fV",
                        voltage, s_pp.config.recovery_voltage_threshold);
                s_pp.state = TS_POWER_POLICY_STATE_RECOVERY;
                s_pp.recovery_timer_sec = s_pp.config.recovery_hold_sec;
                trigger_event(TS_POWER_POLICY_EVENT_RECOVERY_START);
            }
            break;
            
        case TS_POWER_POLICY_STATE_RECOVERY:
            /* 恢复期间电压再次下降 → 回到保护状态或低电压状态 */
            if (voltage < s_pp.config.recovery_voltage_threshold) {
                s_pp.recovery_timer_sec = 0;
                
                if (voltage < s_pp.config.low_voltage_threshold) {
                    /* 电压再次过低，重新进入低电压倒计时（但设备已关机） */
                    TS_LOGW(TAG, "[STATE] RECOVERY -> LOW_VOLTAGE: %.2fV < %.2fV",
                            voltage, s_pp.config.low_voltage_threshold);
                    s_pp.state = TS_POWER_POLICY_STATE_LOW_VOLTAGE;
                    s_pp.countdown_remaining_sec = s_pp.config.shutdown_delay_sec;
                } else {
                    /* 电压不够恢复阈值，回到保护状态继续等待 */
                    TS_LOGI(TAG, "[STATE] RECOVERY -> PROTECTED: %.2fV < %.2fV (voltage dropped)",
                            voltage, s_pp.config.recovery_voltage_threshold);
                    s_pp.state = TS_POWER_POLICY_STATE_PROTECTED;
                }
            }
            break;
            
        default:
            break;
    }
    
    /* 状态变化时触发事件 */
    if (old_state != s_pp.state) {
        trigger_event(TS_POWER_POLICY_EVENT_STATE_CHANGED);
    }
    
    xSemaphoreGive(s_pp.state_mutex);
}

static void check_device_status(void)
{
    /* 从 device_ctrl 获取 AGX/LPMU 状态 */
    ts_device_status_t agx_status, lpmu_status;
    
    if (ts_device_get_status(TS_DEVICE_AGX, &agx_status) == ESP_OK) {
        s_pp.agx_powered = (agx_status.state == TS_DEVICE_STATE_ON);
    }
    
    if (ts_device_get_status(TS_DEVICE_LPMU, &lpmu_status) == ESP_OK) {
        s_pp.lpmu_powered = (lpmu_status.state == TS_DEVICE_STATE_ON);
    }
    
    /* TODO: 检查 AGX WebSocket 连接状态（需要 agx_monitor 组件）*/
}

static void execute_shutdown(void)
{
    TS_LOGW(TAG, "Executing protective shutdown");
    
    if (!s_pp.config.enable_device_shutdown) {
        TS_LOGI(TAG, "Device shutdown disabled in config, skipping");
        return;
    }
    
    /* AGX: 通过 device_ctrl 执行关机（拉高 reset 引脚保持断电状态）*/
    if (s_pp.agx_powered) {
        TS_LOGI(TAG, "Powering off AGX via device_ctrl");
        esp_err_t ret = ts_device_power_off(TS_DEVICE_AGX);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to power off AGX: %s", esp_err_to_name(ret));
        }
    }
    
    /* LPMU: 发送电源切换信号（脉冲触发）*/
    if (s_pp.lpmu_powered) {
        TS_LOGI(TAG, "Sending LPMU power toggle");
        /* TODO: 检查 LPMU 是否在线（ping），如果在线才执行关机 */
        ts_device_power_off(TS_DEVICE_LPMU);
    }
    
    /* 启动风扇关闭定时器（60 秒后关闭风扇）*/
    s_pp.shutdown_timer_sec = 60;
    s_pp.fans_stopped = false;
    TS_LOGI(TAG, "Shutdown timer started: will stop fans in 60s");
}

static void execute_recovery(void)
{
    TS_LOGI(TAG, "Voltage recovered - restarting ESP32 to restore system");
    
    /* 等待日志刷新 */
    vTaskDelay(pdMS_TO_TICKS(100));
    
    /* 重启 ESP32 */
    esp_restart();
}

static void update_led_status(void)
{
    if (!s_pp.config.enable_led_feedback) {
        return;
    }
    
    static ts_power_policy_state_t last_state = TS_POWER_POLICY_STATE_NORMAL;
    ts_power_policy_state_t current_state;
    
    if (xSemaphoreTake(s_pp.state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        current_state = s_pp.state;
        xSemaphoreGive(s_pp.state_mutex);
    } else {
        return;
    }
    
    if (current_state != last_state) {
        TS_LOGI(TAG, "LED state change: %s -> %s",
                ts_power_policy_get_state_name(last_state),
                ts_power_policy_get_state_name(current_state));
        
        switch (current_state) {
            case TS_POWER_POLICY_STATE_LOW_VOLTAGE:
            case TS_POWER_POLICY_STATE_SHUTDOWN:
            case TS_POWER_POLICY_STATE_PROTECTED:
                /* TODO: 启动 Touch LED 橙色呼吸动画 */
                TS_LOGI(TAG, "Should start orange breathe animation");
                break;
                
            case TS_POWER_POLICY_STATE_NORMAL:
            case TS_POWER_POLICY_STATE_RECOVERY:
                /* TODO: 恢复 Touch LED 正常状态 */
                TS_LOGI(TAG, "Should restore normal LED state");
                break;
                
            default:
                break;
        }
        
        last_state = current_state;
    }
}
