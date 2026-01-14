/**
 * @file ts_core_init.c
 * @brief TianShanOS Core Initialization Implementation
 *
 * 核心初始化实现
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#include "ts_core.h"
#include "ts_config_nvs.h"
#include "ts_config_file.h"
#include "esp_log.h"

static const char *TAG = "ts_core";

static bool s_core_initialized = false;
static bool s_core_started = false;

/* ============================================================================
 * 版本信息
 * ========================================================================== */

const char *ts_get_version(void)
{
    return TIANSHAN_OS_VERSION_STRING;
}

const char *ts_get_build_time(void)
{
    static char build_time[32];
    snprintf(build_time, sizeof(build_time), "%s %s", __DATE__, __TIME__);
    return build_time;
}

/* ============================================================================
 * 核心初始化
 * ========================================================================== */

esp_err_t ts_core_init(void)
{
    esp_err_t ret;

    if (s_core_initialized) {
        ESP_LOGW(TAG, "Core already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "TianShanOS Core v%s initializing...", TIANSHAN_OS_VERSION_STRING);

    // 1. 初始化配置管理
    ESP_LOGI(TAG, "Initializing configuration system...");
    ret = ts_config_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize config: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册 NVS 后端
    ret = ts_config_nvs_register();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register NVS backend: %s", esp_err_to_name(ret));
        // 继续，NVS 不是必须的
    }

    // 注册文件后端
    ret = ts_config_file_register();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to register file backend: %s", esp_err_to_name(ret));
        // 继续，文件后端不是必须的
    }

    // 2. 初始化日志系统
    ESP_LOGI(TAG, "Initializing logging system...");
    ret = ts_log_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize log: %s", esp_err_to_name(ret));
        ts_config_deinit();
        return ret;
    }

    // 3. 初始化事件系统
    ESP_LOGI(TAG, "Initializing event system...");
    ret = ts_event_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize event: %s", esp_err_to_name(ret));
        ts_log_deinit();
        ts_config_deinit();
        return ret;
    }

    // 4. 初始化服务管理
    ESP_LOGI(TAG, "Initializing service management...");
    ret = ts_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize service: %s", esp_err_to_name(ret));
        ts_event_deinit();
        ts_log_deinit();
        ts_config_deinit();
        return ret;
    }

    s_core_initialized = true;
    ESP_LOGI(TAG, "TianShanOS Core initialized successfully");

    return ESP_OK;
}

esp_err_t ts_core_deinit(void)
{
    if (!s_core_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing TianShanOS Core...");

    // 如果已启动，先停止
    if (s_core_started) {
        ts_core_stop();
    }

    // 按相反顺序反初始化
    ts_service_deinit();
    ts_event_deinit();
    ts_log_deinit();
    ts_config_deinit();

    s_core_initialized = false;
    ESP_LOGI(TAG, "TianShanOS Core deinitialized");

    return ESP_OK;
}

bool ts_core_is_initialized(void)
{
    return s_core_initialized;
}

esp_err_t ts_core_start(void)
{
    if (!s_core_initialized) {
        ESP_LOGE(TAG, "Core not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_core_started) {
        ESP_LOGW(TAG, "Core already started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting TianShanOS...");

    // 尝试从文件加载配置
    esp_err_t ret = ts_config_file_load_all();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not load config files: %s", esp_err_to_name(ret));
        // 继续，使用默认配置
    }

    // 启动所有服务
    ret = ts_service_start_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start services: %s", esp_err_to_name(ret));
        return ret;
    }

    s_core_started = true;

    // 发送系统启动完成事件
    ts_event_post(TS_EVENT_BASE_SYSTEM, TS_EVENT_SYSTEM_STARTED, NULL, 0, 100);

    ESP_LOGI(TAG, "TianShanOS started");

    return ESP_OK;
}

esp_err_t ts_core_stop(void)
{
    if (!s_core_started) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping TianShanOS...");

    // 发送系统关闭事件
    ts_event_post_sync(TS_EVENT_BASE_SYSTEM, TS_EVENT_SYSTEM_SHUTDOWN, NULL, 0);

    // 停止所有服务
    ts_service_stop_all();

    // 保存配置
    ts_config_save();

    s_core_started = false;
    ESP_LOGI(TAG, "TianShanOS stopped");

    return ESP_OK;
}
