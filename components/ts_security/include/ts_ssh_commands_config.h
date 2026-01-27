/**
 * @file ts_ssh_commands_config.h
 * @brief SSH Command Configuration Storage
 *
 * 管理 SSH 快捷指令配置，存储到 NVS 持久化。
 * 存储的信息包括：
 * - 指令名称、SSH 命令、描述、图标
 * - 模式匹配配置（期望/失败/提取模式）
 * - 变量名（用于存储执行结果）
 * - 超时和停止条件
 *
 * 典型使用场景：
 * 1. WebUI 创建 SSH 快捷指令
 * 2. 自动化规则触发 SSH 指令执行
 * 3. ESP32 重启后恢复指令配置和变量
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_SSH_COMMANDS_CONFIG_H
#define TS_SSH_COMMANDS_CONFIG_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

#define TS_SSH_CMD_ID_MAX         32   /**< 指令 ID 最大长度（自动生成） */
#define TS_SSH_CMD_NAME_MAX       64   /**< 指令名称最大长度 */
#define TS_SSH_CMD_COMMAND_MAX   256   /**< SSH 命令最大长度 */
#define TS_SSH_CMD_DESC_MAX      128   /**< 描述最大长度 */
#define TS_SSH_CMD_ICON_MAX        8   /**< 图标最大长度（emoji） */
#define TS_SSH_CMD_PATTERN_MAX    64   /**< 模式匹配最大长度 */
#define TS_SSH_CMD_VARNAME_MAX    32   /**< 变量名最大长度 */
#define TS_SSH_CMD_HOST_ID_MAX    32   /**< 主机 ID 最大长度 */
#define TS_SSH_COMMANDS_MAX       64   /**< 最大指令数量 */

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief SSH 指令配置
 */
typedef struct {
    char id[TS_SSH_CMD_ID_MAX];              /**< 唯一 ID（自动生成 UUID 风格） */
    char host_id[TS_SSH_CMD_HOST_ID_MAX];    /**< 所属主机 ID */
    char name[TS_SSH_CMD_NAME_MAX];          /**< 指令名称 */
    char command[TS_SSH_CMD_COMMAND_MAX];    /**< SSH 命令 */
    char desc[TS_SSH_CMD_DESC_MAX];          /**< 描述 */
    char icon[TS_SSH_CMD_ICON_MAX];          /**< 图标（emoji） */
    
    /* 模式匹配配置 */
    char expect_pattern[TS_SSH_CMD_PATTERN_MAX];   /**< 成功匹配模式 */
    char fail_pattern[TS_SSH_CMD_PATTERN_MAX];     /**< 失败匹配模式 */
    char extract_pattern[TS_SSH_CMD_PATTERN_MAX];  /**< 提取模式 */
    char var_name[TS_SSH_CMD_VARNAME_MAX];         /**< 变量名前缀 */
    
    uint16_t timeout_sec;                    /**< 超时（秒） */
    bool stop_on_match;                      /**< 匹配后自动停止 */
    bool enabled;                            /**< 是否启用 */
    
    uint32_t created_time;                   /**< 创建时间戳 */
    uint32_t last_exec_time;                 /**< 上次执行时间 */
} ts_ssh_command_config_t;

/*===========================================================================*/
/*                          Initialization                                    */
/*===========================================================================*/

/**
 * @brief 初始化 SSH 指令配置模块
 * 
 * 从 NVS 加载所有配置，并为有 var_name 的指令预创建变量
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_ssh_commands_config_init(void);

/**
 * @brief 反初始化模块
 */
void ts_ssh_commands_config_deinit(void);

/**
 * @brief 检查模块是否已初始化
 */
bool ts_ssh_commands_config_is_initialized(void);

/*===========================================================================*/
/*                          CRUD Operations                                   */
/*===========================================================================*/

/**
 * @brief 添加或更新 SSH 指令配置
 * 
 * 如果 id 为空，将自动生成。如果 id 已存在，则更新。
 * 
 * @param config 指令配置（id 可为空）
 * @param[out] out_id 输出生成的 ID（可为 NULL）
 * @param out_id_size out_id 缓冲区大小
 * @return ESP_OK 成功, ESP_ERR_NO_MEM 已满, ESP_ERR_INVALID_ARG 参数错误
 */
esp_err_t ts_ssh_commands_config_add(const ts_ssh_command_config_t *config, 
                                      char *out_id, size_t out_id_size);

/**
 * @brief 获取指令配置
 * 
 * @param id 指令 ID
 * @param[out] config 输出配置
 * @return ESP_OK 成功, ESP_ERR_NOT_FOUND 不存在
 */
esp_err_t ts_ssh_commands_config_get(const char *id, ts_ssh_command_config_t *config);

/**
 * @brief 删除指令配置
 * 
 * @param id 指令 ID
 * @return ESP_OK 成功, ESP_ERR_NOT_FOUND 不存在
 */
esp_err_t ts_ssh_commands_config_remove(const char *id);

/**
 * @brief 列出所有指令配置
 * 
 * @param[out] configs 输出配置数组
 * @param max_count 数组最大容量
 * @param[out] count 输出实际数量
 * @return ESP_OK 成功
 */
esp_err_t ts_ssh_commands_config_list(ts_ssh_command_config_t *configs, 
                                       size_t max_count, size_t *count);

/**
 * @brief 列出指定主机的所有指令配置
 * 
 * @param host_id 主机 ID
 * @param[out] configs 输出配置数组
 * @param max_count 数组最大容量
 * @param[out] count 输出实际数量
 * @return ESP_OK 成功
 */
esp_err_t ts_ssh_commands_config_list_by_host(const char *host_id,
                                               ts_ssh_command_config_t *configs,
                                               size_t max_count, size_t *count);

/**
 * @brief 获取指令数量
 * 
 * @return 指令数量
 */
size_t ts_ssh_commands_config_count(void);

/**
 * @brief 清空所有指令配置
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_ssh_commands_config_clear(void);

/**
 * @brief 更新指令的最后执行时间
 * 
 * @param id 指令 ID
 * @return ESP_OK 成功
 */
esp_err_t ts_ssh_commands_config_update_exec_time(const char *id);

/**
 * @brief 为所有指令预创建变量
 * 
 * 应在变量系统初始化后调用
 * @return ESP_OK 成功，ESP_ERR_INVALID_STATE 未初始化
 */
esp_err_t ts_ssh_commands_precreate_variables(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_SSH_COMMANDS_CONFIG_H */
