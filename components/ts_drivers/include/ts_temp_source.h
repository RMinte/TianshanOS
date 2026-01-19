/**
 * @file ts_temp_source.h
 * @brief Temperature Source Management for TianShanOS
 * 
 * 温度源管理服务 - 统一温度数据入口
 * 
 * 架构设计：
 * - 支持多个温度 provider（AGX/本地传感器/手动设置）
 * - 基于优先级的温度源选择
 * - 通过事件总线发布温度更新（TS_EVENT_BASE_TEMP）
 * - 风扇等订阅者通过事件获取温度
 * 
 * 数据流：
 *   Provider (AGX/Sensor/Manual) 
 *       → ts_temp_source_update()
 *       → 优先级选择
 *       → ts_event_post(TS_EVT_TEMP_UPDATED)
 *       → 订阅者（风扇曲线控制）
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_TEMP_SOURCE_H
#define TS_TEMP_SOURCE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Version                                       */
/*===========================================================================*/

#define TS_TEMP_SOURCE_VERSION "1.0.0"

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

/** 最大 provider 数量 */
#define TS_TEMP_MAX_PROVIDERS       8

/** 默认温度（0.1°C 单位，即 250 = 25.0°C）*/
#define TS_TEMP_DEFAULT_VALUE       250

/** 温度有效范围 */
#define TS_TEMP_MIN_VALID           (-500)   // -50.0°C
#define TS_TEMP_MAX_VALID           (1500)   // 150.0°C

/** 数据超时（毫秒）- 超过此时间认为数据过期 */
#define TS_TEMP_DATA_TIMEOUT_MS     10000

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief 温度源类型（按优先级排序）
 */
typedef enum {
    TS_TEMP_SOURCE_DEFAULT = 0,     /**< 默认值（25°C）- 最低优先级 */
    TS_TEMP_SOURCE_SENSOR_LOCAL,    /**< 本地温度传感器 */
    TS_TEMP_SOURCE_AGX_AUTO,        /**< AGX WebSocket 自动获取 */
    TS_TEMP_SOURCE_MANUAL,          /**< 手动设置 - 最高优先级 */
    TS_TEMP_SOURCE_MAX
} ts_temp_source_type_t;

/**
 * @brief 温度数据结构
 */
typedef struct {
    int16_t value;                  /**< 温度值（0.1°C 单位）*/
    ts_temp_source_type_t source;   /**< 数据来源 */
    uint32_t timestamp_ms;          /**< 更新时间戳 */
    bool valid;                     /**< 数据有效性 */
} ts_temp_data_t;

/**
 * @brief 温度更新事件数据（通过 TS_EVT_TEMP_UPDATED 发布）
 */
typedef struct {
    int16_t temp;                   /**< 当前有效温度（0.1°C）*/
    ts_temp_source_type_t source;   /**< 当前温度源 */
    int16_t prev_temp;              /**< 前一次温度 */
    ts_temp_source_type_t prev_source;  /**< 前一次温度源 */
} ts_temp_event_data_t;

/**
 * @brief Provider 信息结构
 */
typedef struct {
    ts_temp_source_type_t type;     /**< Provider 类型 */
    const char *name;               /**< Provider 名称 */
    int16_t last_value;             /**< 最后报告的温度 */
    uint32_t last_update_ms;        /**< 最后更新时间 */
    uint32_t update_count;          /**< 更新计数 */
    bool active;                    /**< 是否激活 */
} ts_temp_provider_info_t;

/**
 * @brief 温度源状态信息
 */
typedef struct {
    bool initialized;                       /**< 初始化状态 */
    ts_temp_source_type_t active_source;    /**< 当前活动源 */
    int16_t current_temp;                   /**< 当前温度 */
    bool manual_mode;                       /**< 手动模式启用 */
    uint32_t provider_count;                /**< 注册的 provider 数量 */
    ts_temp_provider_info_t providers[TS_TEMP_MAX_PROVIDERS]; /**< Provider 信息 */
} ts_temp_status_t;

/*===========================================================================*/
/*                              API Functions                                 */
/*===========================================================================*/

/**
 * @brief 初始化温度源管理
 * @return ESP_OK 成功
 */
esp_err_t ts_temp_source_init(void);

/**
 * @brief 反初始化温度源管理
 * @return ESP_OK 成功
 */
esp_err_t ts_temp_source_deinit(void);

/**
 * @brief 检查是否已初始化
 * @return true 已初始化
 */
bool ts_temp_source_is_initialized(void);

/*---------------------------------------------------------------------------*/
/*                          Provider 接口                                     */
/*---------------------------------------------------------------------------*/

/**
 * @brief 注册温度 provider
 * 
 * @param type Provider 类型
 * @param name Provider 名称（用于日志和调试）
 * @return ESP_OK 成功
 */
esp_err_t ts_temp_provider_register(ts_temp_source_type_t type, const char *name);

/**
 * @brief 注销温度 provider
 * 
 * @param type Provider 类型
 * @return ESP_OK 成功
 */
esp_err_t ts_temp_provider_unregister(ts_temp_source_type_t type);

/**
 * @brief Provider 更新温度值
 * 
 * 由各 provider（AGX monitor、本地传感器等）调用。
 * 内部会进行优先级判断，如果是当前活动源则发布事件。
 * 
 * @param type Provider 类型
 * @param temp_01c 温度值（0.1°C 单位）
 * @return ESP_OK 成功
 */
esp_err_t ts_temp_provider_update(ts_temp_source_type_t type, int16_t temp_01c);

/*---------------------------------------------------------------------------*/
/*                          消费者接口                                        */
/*---------------------------------------------------------------------------*/

/**
 * @brief 获取当前有效温度
 * 
 * 基于优先级返回最高优先级且未过期的温度。
 * 
 * @param[out] data 温度数据（可为 NULL 仅获取温度值）
 * @return 当前温度（0.1°C 单位）
 */
int16_t ts_temp_get_effective(ts_temp_data_t *data);

/**
 * @brief 获取指定源的温度
 * 
 * @param type 温度源类型
 * @param[out] data 温度数据
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 未找到
 */
esp_err_t ts_temp_get_by_source(ts_temp_source_type_t type, ts_temp_data_t *data);

/*---------------------------------------------------------------------------*/
/*                          手动模式控制                                      */
/*---------------------------------------------------------------------------*/

/**
 * @brief 设置手动温度值
 * 
 * 启用手动模式并设置温度值。手动模式具有最高优先级。
 * 
 * @param temp_01c 温度值（0.1°C 单位）
 * @return ESP_OK 成功
 */
esp_err_t ts_temp_set_manual(int16_t temp_01c);

/**
 * @brief 启用/禁用手动模式
 * 
 * @param enable true 启用手动模式（使用上次设置的手动温度）
 * @return ESP_OK 成功
 */
esp_err_t ts_temp_set_manual_mode(bool enable);

/**
 * @brief 检查是否为手动模式
 * 
 * @return true 手动模式启用
 */
bool ts_temp_is_manual_mode(void);

/*---------------------------------------------------------------------------*/
/*                          状态查询                                          */
/*---------------------------------------------------------------------------*/

/**
 * @brief 获取温度源状态
 * 
 * @param[out] status 状态信息
 * @return ESP_OK 成功
 */
esp_err_t ts_temp_get_status(ts_temp_status_t *status);

/**
 * @brief 获取当前活动的温度源类型
 * 
 * @return 当前活动的温度源类型
 */
ts_temp_source_type_t ts_temp_get_active_source(void);

/**
 * @brief 温度源类型转字符串
 * 
 * @param type 温度源类型
 * @return 类型名称字符串
 */
const char *ts_temp_source_type_to_str(ts_temp_source_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* TS_TEMP_SOURCE_H */
