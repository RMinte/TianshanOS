/**
 * @file ts_core.h
 * @brief TianShanOS Core - Main Include Header
 *
 * TianShanOS 核心模块主头文件
 * 包含所有核心子模块的头文件
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#ifndef TS_CORE_H
#define TS_CORE_H

/* 核心子模块 */
#include "ts_config.h"
#include "ts_log.h"
#include "ts_event.h"
#include "ts_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 版本信息
 * ========================================================================== */

#define TIANSHAN_OS_VERSION_MAJOR   0
#define TIANSHAN_OS_VERSION_MINOR   1
#define TIANSHAN_OS_VERSION_PATCH   0
#define TIANSHAN_OS_VERSION_STRING  "0.1.0-dev"

/**
 * @brief 获取 TianShanOS 版本字符串
 *
 * @return 版本字符串
 */
const char *ts_get_version(void);

/**
 * @brief 获取 TianShanOS 编译时间
 *
 * @return 编译时间字符串
 */
const char *ts_get_build_time(void);

/* ============================================================================
 * 核心初始化
 * ========================================================================== */

/**
 * @brief 初始化 TianShanOS 核心系统
 *
 * 按顺序初始化所有核心子系统：
 * 1. 配置管理 (ts_config)
 * 2. 日志系统 (ts_log)
 * 3. 事件总线 (ts_event)
 * 4. 服务管理 (ts_service)
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_core_init(void);

/**
 * @brief 反初始化 TianShanOS 核心系统
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_core_deinit(void);

/**
 * @brief 检查核心系统是否已初始化
 *
 * @return true 已初始化
 */
bool ts_core_is_initialized(void);

/**
 * @brief 启动 TianShanOS
 *
 * 启动所有已注册的服务
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_core_start(void);

/**
 * @brief 停止 TianShanOS
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_core_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_CORE_H */
