/**
 * @file ts_compute_monitor.h
 * @brief Compute Device Monitor for TianShanOS
 * 
 * 算力设备监控服务 - 通过 WebSocket 获取 Jetson 系统数据
 * 
 * 功能：
 * - WebSocket 客户端连接 AGX tegrastats 服务器
 * - 解析 CPU/GPU/温度/功耗/内存 数据
 * - 作为温度 provider 注册到 ts_temp_source
 * - 通过事件总线发布 AGX 数据更新
 * 
 * 通信协议：
 * - 协议：Socket.IO over WebSocket
 * - 端口：58090（默认）
 * - 事件：tegrastats_update
 * - 格式：JSON（CPU/Memory/Temperature/Power/GPU）
 * - 频率：1Hz
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_COMPUTE_MONITOR_H
#define TS_COMPUTE_MONITOR_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Version                                       */
/*===========================================================================*/

#define TS_COMPUTE_MONITOR_VERSION "1.0.0"

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

/** 最大 URL 长度 */
#define TS_COMPUTE_MAX_URL_LEN          128

/** 最大错误消息长度 */
#define TS_COMPUTE_MAX_ERROR_MSG_LEN    64

/** 最大时间戳长度 */
#define TS_COMPUTE_MAX_TIMESTAMP_LEN    32

/** 最大 CPU 核心数 */
#define TS_COMPUTE_MAX_CPU_CORES        16

/** 默认配置 */
#define TS_COMPUTE_DEFAULT_SERVER_IP    "10.10.99.98"
#define TS_COMPUTE_DEFAULT_SERVER_PORT  58090
#define TS_COMPUTE_DEFAULT_RECONNECT_MS 3000
#define TS_COMPUTE_DEFAULT_STARTUP_DELAY_MS 45000   // AGX 启动需要约 45 秒
#define TS_COMPUTE_DEFAULT_HEARTBEAT_TIMEOUT_MS 10000
#define TS_COMPUTE_DEFAULT_TASK_STACK   8192
#define TS_COMPUTE_DEFAULT_TASK_PRIORITY 5

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief 算力监控连接状态
 */
typedef enum {
    TS_COMPUTE_STATUS_UNINITIALIZED = 0,    /**< 未初始化 */
    TS_COMPUTE_STATUS_INITIALIZED,          /**< 已初始化未启动 */
    TS_COMPUTE_STATUS_CONNECTING,           /**< 连接中 */
    TS_COMPUTE_STATUS_CONNECTED,            /**< 已连接 */
    TS_COMPUTE_STATUS_DISCONNECTED,         /**< 已断开 */
    TS_COMPUTE_STATUS_RECONNECTING,         /**< 重连中 */
    TS_COMPUTE_STATUS_ERROR                 /**< 错误状态 */
} ts_compute_status_t;

/**
 * @brief CPU 核心信息
 */
typedef struct {
    uint8_t id;         /**< 核心 ID */
    uint8_t usage;      /**< 使用率 (0-100%) */
    uint16_t freq_mhz;  /**< 频率 (MHz) */
} ts_compute_cpu_core_t;

/**
 * @brief 内存信息
 */
typedef struct {
    uint32_t used_mb;   /**< 已使用 (MB) */
    uint32_t total_mb;  /**< 总量 (MB) */
} ts_compute_memory_t;

/**
 * @brief 功耗信息
 */
typedef struct {
    uint32_t current_mw;    /**< 当前功耗 (mW) */
    uint32_t average_mw;    /**< 平均功耗 (mW) */
} ts_compute_power_t;

/**
 * @brief AGX 完整监控数据
 */
typedef struct {
    char timestamp[TS_COMPUTE_MAX_TIMESTAMP_LEN];   /**< ISO 8601 时间戳 */
    
    /* CPU 信息 */
    struct {
        uint8_t core_count;                     /**< CPU 核心数 */
        ts_compute_cpu_core_t cores[TS_COMPUTE_MAX_CPU_CORES];  /**< 各核心数据 */
    } cpu;
    
    /* 内存信息 */
    struct {
        ts_compute_memory_t ram;    /**< RAM 使用 */
        ts_compute_memory_t swap;   /**< SWAP 使用 */
    } memory;
    
    /* 温度信息 (°C) */
    struct {
        float cpu;      /**< CPU 温度 */
        float soc0;     /**< SoC 传感器 0 */
        float soc1;     /**< SoC 传感器 1 */
        float soc2;     /**< SoC 传感器 2 */
        float tj;       /**< 结温 (Junction Temperature) */
    } temperature;
    
    /* 功耗信息 */
    struct {
        ts_compute_power_t gpu_soc;     /**< GPU+SoC 功耗 */
        ts_compute_power_t cpu_cv;      /**< CPU 功耗 */
        ts_compute_power_t sys_5v;      /**< 系统 5V 功耗 */
    } power;
    
    /* GPU 信息 */
    struct {
        uint8_t gr3d_freq_pct;  /**< 3D GPU 频率百分比 */
    } gpu;
    
    bool is_valid;              /**< 数据有效性 */
    uint64_t update_time_us;    /**< 更新时间 (微秒) */
} ts_compute_data_t;

/**
 * @brief 算力监控配置
 */
typedef struct {
    char server_ip[TS_COMPUTE_MAX_URL_LEN];     /**< 服务器 IP */
    uint16_t server_port;                   /**< 服务器端口 */
    uint32_t reconnect_interval_ms;         /**< 重连间隔 (ms) */
    uint32_t startup_delay_ms;              /**< 启动延迟 (ms) */
    uint32_t heartbeat_timeout_ms;          /**< 心跳超时 (ms) */
    bool auto_start;                        /**< 自动启动 */
    bool update_temp_source;                /**< 更新温度源 */
    uint32_t task_stack_size;               /**< 任务栈大小 */
    uint8_t task_priority;                  /**< 任务优先级 */
} ts_compute_config_t;

/**
 * @brief 算力监控状态信息
 */
typedef struct {
    bool initialized;                       /**< 初始化状态 */
    bool running;                           /**< 运行状态 */
    ts_compute_status_t connection_status;      /**< 连接状态 */
    uint32_t total_reconnects;              /**< 重连次数 */
    uint32_t messages_received;             /**< 接收消息数 */
    uint32_t parse_errors;                  /**< 解析错误数 */
    uint64_t last_message_time_us;          /**< 最后消息时间 */
    uint64_t connected_time_ms;             /**< 总连接时间 */
    float connection_reliability;           /**< 连接可靠性 (%) */
    char last_error[TS_COMPUTE_MAX_ERROR_MSG_LEN]; /**< 最后错误消息 */
} ts_compute_status_info_t;

/**
 * @brief AGX 事件回调函数类型
 */
typedef void (*ts_compute_event_callback_t)(ts_compute_status_t status, 
                                        const ts_compute_data_t *data,
                                        void *user_data);

/*===========================================================================*/
/*                              API Functions                                 */
/*===========================================================================*/

/**
 * @brief 获取默认配置
 * 
 * @param[out] config 配置结构体
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_get_default_config(ts_compute_config_t *config);

/**
 * @brief 获取当前配置
 * 
 * @param[out] config 配置结构体
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_get_config(ts_compute_config_t *config);

/**
 * @brief 设置服务器配置
 * 
 * 设置 AGX 监控服务器的 IP 和端口，并保存到 NVS。
 * 如果监控正在运行，会自动重启以应用新配置。
 * 
 * @param server_ip 服务器 IP 地址
 * @param server_port 服务器端口（0 使用默认端口）
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_set_server(const char *server_ip, uint16_t server_port);

/**
 * @brief 从 NVS 加载配置
 * 
 * @param[out] config 配置结构体
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 无保存的配置
 */
esp_err_t ts_compute_monitor_load_config(ts_compute_config_t *config);

/**
 * @brief 保存配置到 NVS
 * 
 * @param config 配置结构体
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_save_config(const ts_compute_config_t *config);

/**
 * @brief 初始化 算力监控
 * 
 * @param config 配置（NULL 使用默认配置）
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_init(const ts_compute_config_t *config);

/**
 * @brief 反初始化 算力监控
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_deinit(void);

/**
 * @brief 启动 算力监控
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_start(void);

/**
 * @brief 停止 算力监控
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_stop(void);

/**
 * @brief 检查是否已初始化
 * 
 * @return true 已初始化
 */
bool ts_compute_monitor_is_initialized(void);

/**
 * @brief 检查是否正在运行
 * 
 * @return true 正在运行
 */
bool ts_compute_monitor_is_running(void);

/**
 * @brief 获取最新 AGX 数据
 * 
 * @param[out] data AGX 数据
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_get_data(ts_compute_data_t *data);

/**
 * @brief 检查数据是否有效
 * 
 * @return true 数据有效
 */
bool ts_compute_monitor_is_data_valid(void);

/**
 * @brief 获取状态信息
 * 
 * @param[out] status 状态信息
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_get_status(ts_compute_status_info_t *status);

/**
 * @brief 获取连接状态
 * 
 * @return 当前连接状态
 */
ts_compute_status_t ts_compute_monitor_get_connection_status(void);

/**
 * @brief 注册事件回调
 * 
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_register_callback(ts_compute_event_callback_t callback,
                                           void *user_data);

/**
 * @brief 注销事件回调
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_compute_monitor_unregister_callback(void);

/**
 * @brief 连接状态转字符串
 * 
 * @param status 连接状态
 * @return 状态名称
 */
const char *ts_compute_status_to_str(ts_compute_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* TS_COMPUTE_MONITOR_H */
