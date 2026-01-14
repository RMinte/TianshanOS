/**
 * @file ts_config_file.h
 * @brief TianShanOS Configuration - File Backend Header
 *
 * 文件系统配置后端公共接口
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#ifndef TS_CONFIG_FILE_H
#define TS_CONFIG_FILE_H

#include "esp_err.h"
#include "ts_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册文件配置后端
 *
 * 将文件后端注册到配置管理系统
 * 应在 ts_config_init() 之后调用
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_ERR_INVALID_STATE: 配置系统未初始化
 *      - ESP_FAIL: 注册失败
 */
esp_err_t ts_config_file_register(void);

/**
 * @brief 设置配置文件目录路径
 *
 * @param path 目录路径（如 "/sdcard/config"）
 * @return ESP_OK 成功
 */
esp_err_t ts_config_file_set_path(const char *path);

/**
 * @brief 获取配置文件目录路径
 *
 * @return 目录路径字符串
 */
const char *ts_config_file_get_path(void);

/**
 * @brief 从配置目录加载所有配置文件
 *
 * 遍历配置目录，加载所有 .json 文件
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_config_file_load_all(void);

/**
 * @brief 保存所有配置到文件
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_config_file_save_all(void);

/**
 * @brief 获取文件后端操作函数集
 *
 * @return 后端操作函数指针
 */
const ts_config_backend_ops_t *ts_config_file_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_CONFIG_FILE_H */
