/**
 * @file ts_config_nvs.h
 * @brief TianShanOS Configuration - NVS Backend Header
 *
 * NVS 配置后端公共接口
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#ifndef TS_CONFIG_NVS_H
#define TS_CONFIG_NVS_H

#include "esp_err.h"
#include "ts_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 NVS 配置后端
 *
 * 将 NVS 后端注册到配置管理系统
 * 应在 ts_config_init() 之后调用
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_ERR_INVALID_STATE: 配置系统未初始化
 *      - ESP_FAIL: 注册失败
 */
esp_err_t ts_config_nvs_register(void);

/**
 * @brief 获取 NVS 后端操作函数集
 *
 * @return 后端操作函数指针
 */
const ts_config_backend_ops_t *ts_config_nvs_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_CONFIG_NVS_H */
