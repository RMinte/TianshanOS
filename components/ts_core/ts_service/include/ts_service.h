/**
 * @file ts_service.h
 * @brief TianShanOS Service Management System
 *
 * 服务管理系统主头文件
 * 提供服务注册、生命周期管理、依赖注入等功能
 *
 * 特性：
 * - 服务注册和发现
 * - 分阶段启动（8个阶段）
 * - 阶段内依赖图解析
 * - 服务生命周期管理
 * - 服务健康监控
 *
 * 启动阶段：
 * 1. PLATFORM - 平台初始化（GPIO、时钟等）
 * 2. CORE     - 核心服务（配置、日志、事件）
 * 3. HAL      - 硬件抽象层
 * 4. DRIVER   - 设备驱动
 * 5. NETWORK  - 网络服务
 * 6. SECURITY - 安全服务
 * 7. SERVICE  - 应用服务
 * 8. UI       - 用户界面
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#ifndef TS_SERVICE_H
#define TS_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 常量定义
 * ========================================================================== */

#ifndef CONFIG_TS_SERVICE_MAX_SERVICES
#define CONFIG_TS_SERVICE_MAX_SERVICES 32
#endif

#ifndef CONFIG_TS_SERVICE_NAME_MAX_LENGTH
#define CONFIG_TS_SERVICE_NAME_MAX_LENGTH 32
#endif

#ifndef CONFIG_TS_SERVICE_MAX_DEPENDENCIES
#define CONFIG_TS_SERVICE_MAX_DEPENDENCIES 8
#endif

/** 最大服务数量 */
#define TS_SERVICE_MAX_COUNT CONFIG_TS_SERVICE_MAX_SERVICES

/** 服务名称最大长度 */
#define TS_SERVICE_NAME_MAX_LEN CONFIG_TS_SERVICE_NAME_MAX_LENGTH

/** 最大依赖数量 */
#define TS_SERVICE_DEPS_MAX CONFIG_TS_SERVICE_MAX_DEPENDENCIES

/* ============================================================================
 * 类型定义
 * ========================================================================== */

/**
 * @brief 服务启动阶段
 */
typedef enum {
    TS_SERVICE_PHASE_PLATFORM = 0,  /**< 平台初始化 */
    TS_SERVICE_PHASE_CORE,          /**< 核心服务 */
    TS_SERVICE_PHASE_HAL,           /**< 硬件抽象层 */
    TS_SERVICE_PHASE_DRIVER,        /**< 设备驱动 */
    TS_SERVICE_PHASE_NETWORK,       /**< 网络服务 */
    TS_SERVICE_PHASE_SECURITY,      /**< 安全服务 */
    TS_SERVICE_PHASE_SERVICE,       /**< 应用服务 */
    TS_SERVICE_PHASE_UI,            /**< 用户界面 */
    TS_SERVICE_PHASE_MAX            /**< 阶段数量 */
} ts_service_phase_t;

/**
 * @brief 服务状态
 */
typedef enum {
    TS_SERVICE_STATE_UNREGISTERED = 0,  /**< 未注册 */
    TS_SERVICE_STATE_REGISTERED,        /**< 已注册 */
    TS_SERVICE_STATE_STARTING,          /**< 启动中 */
    TS_SERVICE_STATE_RUNNING,           /**< 运行中 */
    TS_SERVICE_STATE_STOPPING,          /**< 停止中 */
    TS_SERVICE_STATE_STOPPED,           /**< 已停止 */
    TS_SERVICE_STATE_ERROR,             /**< 错误状态 */
    TS_SERVICE_STATE_MAX                /**< 状态数量 */
} ts_service_state_t;

/**
 * @brief 服务能力标志
 */
typedef enum {
    TS_SERVICE_CAP_NONE         = 0,        /**< 无特殊能力 */
    TS_SERVICE_CAP_RESTARTABLE  = (1 << 0), /**< 可重启 */
    TS_SERVICE_CAP_SUSPENDABLE  = (1 << 1), /**< 可暂停 */
    TS_SERVICE_CAP_CONFIGURABLE = (1 << 2), /**< 可配置 */
    TS_SERVICE_CAP_PROVIDES_API = (1 << 3), /**< 提供 API */
} ts_service_capability_t;

/**
 * @brief 服务句柄
 */
typedef struct ts_service_handle *ts_service_handle_t;

/**
 * @brief 服务初始化回调
 *
 * @param handle 服务句柄
 * @param user_data 用户数据
 * @return ESP_OK 成功，其他表示失败
 */
typedef esp_err_t (*ts_service_init_fn)(ts_service_handle_t handle, void *user_data);

/**
 * @brief 服务启动回调
 *
 * @param handle 服务句柄
 * @param user_data 用户数据
 * @return ESP_OK 成功
 */
typedef esp_err_t (*ts_service_start_fn)(ts_service_handle_t handle, void *user_data);

/**
 * @brief 服务停止回调
 *
 * @param handle 服务句柄
 * @param user_data 用户数据
 * @return ESP_OK 成功
 */
typedef esp_err_t (*ts_service_stop_fn)(ts_service_handle_t handle, void *user_data);

/**
 * @brief 服务健康检查回调
 *
 * @param handle 服务句柄
 * @param user_data 用户数据
 * @return true 健康，false 不健康
 */
typedef bool (*ts_service_health_fn)(ts_service_handle_t handle, void *user_data);

/**
 * @brief 服务定义结构
 */
typedef struct {
    const char *name;                               /**< 服务名称 */
    ts_service_phase_t phase;                       /**< 启动阶段 */
    uint32_t capabilities;                          /**< 能力标志 */
    
    /** 依赖的服务名称数组（以 NULL 结尾） */
    const char *dependencies[TS_SERVICE_DEPS_MAX + 1];
    
    /** 回调函数 */
    ts_service_init_fn init;                        /**< 初始化回调 */
    ts_service_start_fn start;                      /**< 启动回调 */
    ts_service_stop_fn stop;                        /**< 停止回调 */
    ts_service_health_fn health_check;              /**< 健康检查回调 */
    
    void *user_data;                                /**< 用户数据 */
} ts_service_def_t;

/**
 * @brief 服务信息结构
 */
typedef struct {
    char name[TS_SERVICE_NAME_MAX_LEN];             /**< 服务名称 */
    ts_service_phase_t phase;                       /**< 启动阶段 */
    ts_service_state_t state;                       /**< 当前状态 */
    uint32_t capabilities;                          /**< 能力标志 */
    uint32_t start_time_ms;                         /**< 启动时间 */
    uint32_t start_duration_ms;                     /**< 启动耗时 */
    uint32_t last_health_check_ms;                  /**< 上次健康检查时间 */
    bool healthy;                                   /**< 是否健康 */
} ts_service_info_t;

/**
 * @brief 服务统计信息
 */
typedef struct {
    uint32_t total_services;                        /**< 总服务数 */
    uint32_t running_services;                      /**< 运行中服务数 */
    uint32_t stopped_services;                      /**< 已停止服务数 */
    uint32_t error_services;                        /**< 错误服务数 */
    uint32_t startup_time_ms;                       /**< 总启动时间 */
    uint32_t phase_times_ms[TS_SERVICE_PHASE_MAX];  /**< 各阶段耗时 */
} ts_service_stats_t;

/**
 * @brief 服务事件数据
 */
typedef struct {
    const char *service_name;                       /**< 服务名称 */
    ts_service_state_t old_state;                   /**< 旧状态 */
    ts_service_state_t new_state;                   /**< 新状态 */
    esp_err_t error_code;                           /**< 错误码（如果有）*/
} ts_service_event_data_t;

/* ============================================================================
 * 服务事件 ID
 * ========================================================================== */

/** 服务状态改变 */
#define TS_EVENT_SERVICE_STATE_CHANGED  0x0001
/** 服务启动完成 */
#define TS_EVENT_SERVICE_STARTED        0x0002
/** 服务停止完成 */
#define TS_EVENT_SERVICE_STOPPED        0x0003
/** 服务启动失败 */
#define TS_EVENT_SERVICE_START_FAILED   0x0004
/** 阶段启动完成 */
#define TS_EVENT_SERVICE_PHASE_COMPLETE 0x0005
/** 所有服务启动完成 */
#define TS_EVENT_SERVICE_ALL_STARTED    0x0006
/** 服务健康状态改变 */
#define TS_EVENT_SERVICE_HEALTH_CHANGED 0x0007

/* ============================================================================
 * 初始化和反初始化
 * ========================================================================== */

/**
 * @brief 初始化服务管理系统
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_service_init(void);

/**
 * @brief 反初始化服务管理系统
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_service_deinit(void);

/**
 * @brief 检查服务管理系统是否已初始化
 *
 * @return true 已初始化
 */
bool ts_service_is_initialized(void);

/* ============================================================================
 * 服务注册
 * ========================================================================== */

/**
 * @brief 注册服务
 *
 * @param def 服务定义
 * @param[out] handle 输出服务句柄
 * @return ESP_OK 成功
 */
esp_err_t ts_service_register(const ts_service_def_t *def, ts_service_handle_t *handle);

/**
 * @brief 取消注册服务
 *
 * @param handle 服务句柄
 * @return ESP_OK 成功
 */
esp_err_t ts_service_unregister(ts_service_handle_t handle);

/**
 * @brief 通过名称查找服务
 *
 * @param name 服务名称
 * @return 服务句柄，未找到返回 NULL
 */
ts_service_handle_t ts_service_find(const char *name);

/**
 * @brief 检查服务是否存在
 *
 * @param name 服务名称
 * @return true 存在
 */
bool ts_service_exists(const char *name);

/* ============================================================================
 * 服务生命周期
 * ========================================================================== */

/**
 * @brief 启动所有服务
 *
 * 按阶段和依赖顺序启动所有已注册的服务
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_service_start_all(void);

/**
 * @brief 停止所有服务
 *
 * 按相反顺序停止所有运行中的服务
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_service_stop_all(void);

/**
 * @brief 启动指定阶段的服务
 *
 * @param phase 启动阶段
 * @return ESP_OK 成功
 */
esp_err_t ts_service_start_phase(ts_service_phase_t phase);

/**
 * @brief 启动单个服务
 *
 * 会自动启动依赖的服务
 *
 * @param handle 服务句柄
 * @return ESP_OK 成功
 */
esp_err_t ts_service_start(ts_service_handle_t handle);

/**
 * @brief 停止单个服务
 *
 * 会自动停止依赖此服务的其他服务
 *
 * @param handle 服务句柄
 * @return ESP_OK 成功
 */
esp_err_t ts_service_stop(ts_service_handle_t handle);

/**
 * @brief 重启服务
 *
 * @param handle 服务句柄
 * @return ESP_OK 成功
 */
esp_err_t ts_service_restart(ts_service_handle_t handle);

/* ============================================================================
 * 服务状态查询
 * ========================================================================== */

/**
 * @brief 获取服务状态
 *
 * @param handle 服务句柄
 * @return 服务状态
 */
ts_service_state_t ts_service_get_state(ts_service_handle_t handle);

/**
 * @brief 获取服务信息
 *
 * @param handle 服务句柄
 * @param[out] info 输出服务信息
 * @return ESP_OK 成功
 */
esp_err_t ts_service_get_info(ts_service_handle_t handle, ts_service_info_t *info);

/**
 * @brief 检查服务是否正在运行
 *
 * @param handle 服务句柄
 * @return true 正在运行
 */
bool ts_service_is_running(ts_service_handle_t handle);

/**
 * @brief 检查服务是否健康
 *
 * @param handle 服务句柄
 * @return true 健康
 */
bool ts_service_is_healthy(ts_service_handle_t handle);

/**
 * @brief 等待服务进入指定状态
 *
 * @param handle 服务句柄
 * @param state 目标状态
 * @param timeout_ms 超时时间
 * @return ESP_OK 成功，ESP_ERR_TIMEOUT 超时
 */
esp_err_t ts_service_wait_state(ts_service_handle_t handle,
                                 ts_service_state_t state,
                                 uint32_t timeout_ms);

/**
 * @brief 等待所有服务启动完成
 *
 * @param timeout_ms 超时时间
 * @return ESP_OK 成功
 */
esp_err_t ts_service_wait_all_started(uint32_t timeout_ms);

/* ============================================================================
 * 服务 API 访问
 * ========================================================================== */

/**
 * @brief 获取服务提供的 API
 *
 * @param handle 服务句柄
 * @return API 指针，无 API 返回 NULL
 */
void *ts_service_get_api(ts_service_handle_t handle);

/**
 * @brief 设置服务提供的 API
 *
 * @param handle 服务句柄
 * @param api API 指针
 * @return ESP_OK 成功
 */
esp_err_t ts_service_set_api(ts_service_handle_t handle, void *api);

/* ============================================================================
 * 服务枚举
 * ========================================================================== */

/**
 * @brief 服务枚举回调
 *
 * @param handle 服务句柄
 * @param info 服务信息
 * @param user_data 用户数据
 * @return true 继续枚举，false 停止枚举
 */
typedef bool (*ts_service_enum_fn)(ts_service_handle_t handle,
                                    const ts_service_info_t *info,
                                    void *user_data);

/**
 * @brief 枚举所有服务
 *
 * @param callback 枚举回调
 * @param user_data 用户数据
 * @return 枚举的服务数量
 */
size_t ts_service_enumerate(ts_service_enum_fn callback, void *user_data);

/**
 * @brief 枚举指定阶段的服务
 *
 * @param phase 阶段
 * @param callback 枚举回调
 * @param user_data 用户数据
 * @return 枚举的服务数量
 */
size_t ts_service_enumerate_phase(ts_service_phase_t phase,
                                   ts_service_enum_fn callback,
                                   void *user_data);

/* ============================================================================
 * 统计和调试
 * ========================================================================== */

/**
 * @brief 获取服务统计信息
 *
 * @param[out] stats 输出统计信息
 * @return ESP_OK 成功
 */
esp_err_t ts_service_get_stats(ts_service_stats_t *stats);

/**
 * @brief 打印服务状态
 */
void ts_service_dump(void);

/**
 * @brief 获取阶段名称
 *
 * @param phase 阶段
 * @return 阶段名称字符串
 */
const char *ts_service_phase_to_string(ts_service_phase_t phase);

/**
 * @brief 获取状态名称
 *
 * @param state 状态
 * @return 状态名称字符串
 */
const char *ts_service_state_to_string(ts_service_state_t state);

/* ============================================================================
 * 便捷宏
 * ========================================================================== */

/**
 * @brief 定义服务（静态）
 */
#define TS_SERVICE_DEFINE(var_name, svc_name, svc_phase) \
    static const ts_service_def_t var_name = { \
        .name = svc_name, \
        .phase = svc_phase, \
        .capabilities = TS_SERVICE_CAP_NONE, \
        .dependencies = {NULL}, \
        .init = NULL, \
        .start = NULL, \
        .stop = NULL, \
        .health_check = NULL, \
        .user_data = NULL, \
    }

/**
 * @brief 检查服务是否可用
 */
#define TS_SERVICE_AVAILABLE(name) \
    (ts_service_exists(name) && ts_service_is_running(ts_service_find(name)))

#ifdef __cplusplus
}
#endif

#endif /* TS_SERVICE_H */
