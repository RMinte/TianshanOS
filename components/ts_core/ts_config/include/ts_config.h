/**
 * @file ts_config.h
 * @brief TianShanOS Configuration Management System
 *
 * 配置管理系统主头文件
 * 提供统一的配置读取、写入、监听接口
 *
 * 配置优先级（从高到低）：
 * 1. CLI 运行时设置
 * 2. NVS 持久化存储
 * 3. SD 卡配置文件
 * 4. 默认值
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#ifndef TS_CONFIG_H
#define TS_CONFIG_H

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

#ifndef CONFIG_TS_CONFIG_MAX_KEY_LENGTH
#define CONFIG_TS_CONFIG_MAX_KEY_LENGTH 64
#endif

#ifndef CONFIG_TS_CONFIG_MAX_VALUE_SIZE
#define CONFIG_TS_CONFIG_MAX_VALUE_SIZE 512
#endif

#ifndef CONFIG_TS_CONFIG_MAX_LISTENERS
#define CONFIG_TS_CONFIG_MAX_LISTENERS 16
#endif

/** 配置键最大长度 */
#define TS_CONFIG_KEY_MAX_LEN CONFIG_TS_CONFIG_MAX_KEY_LENGTH

/** 配置值最大大小 */
#define TS_CONFIG_VALUE_MAX_SIZE CONFIG_TS_CONFIG_MAX_VALUE_SIZE

/** 最大监听器数量 */
#define TS_CONFIG_LISTENERS_MAX CONFIG_TS_CONFIG_MAX_LISTENERS

/* ============================================================================
 * 类型定义
 * ========================================================================== */

/**
 * @brief 配置值类型
 */
typedef enum {
    TS_CONFIG_TYPE_NONE = 0,    /**< 无值/空 */
    TS_CONFIG_TYPE_BOOL,        /**< 布尔值 */
    TS_CONFIG_TYPE_INT8,        /**< 8位有符号整数 */
    TS_CONFIG_TYPE_UINT8,       /**< 8位无符号整数 */
    TS_CONFIG_TYPE_INT16,       /**< 16位有符号整数 */
    TS_CONFIG_TYPE_UINT16,      /**< 16位无符号整数 */
    TS_CONFIG_TYPE_INT32,       /**< 32位有符号整数 */
    TS_CONFIG_TYPE_UINT32,      /**< 32位无符号整数 */
    TS_CONFIG_TYPE_INT64,       /**< 64位有符号整数 */
    TS_CONFIG_TYPE_UINT64,      /**< 64位无符号整数 */
    TS_CONFIG_TYPE_FLOAT,       /**< 单精度浮点 */
    TS_CONFIG_TYPE_DOUBLE,      /**< 双精度浮点 */
    TS_CONFIG_TYPE_STRING,      /**< 字符串 */
    TS_CONFIG_TYPE_BLOB,        /**< 二进制数据 */
    TS_CONFIG_TYPE_MAX          /**< 类型数量（用于边界检查）*/
} ts_config_type_t;

/**
 * @brief 配置来源/后端
 */
typedef enum {
    TS_CONFIG_BACKEND_DEFAULT = 0,  /**< 默认值 */
    TS_CONFIG_BACKEND_NVS,          /**< NVS 存储 */
    TS_CONFIG_BACKEND_FILE,         /**< 文件系统 */
    TS_CONFIG_BACKEND_CLI,          /**< CLI 运行时设置 */
    TS_CONFIG_BACKEND_MAX           /**< 后端数量 */
} ts_config_backend_t;

/**
 * @brief 配置更改事件类型
 */
typedef enum {
    TS_CONFIG_EVENT_SET = 0,    /**< 配置被设置 */
    TS_CONFIG_EVENT_DELETE,     /**< 配置被删除 */
    TS_CONFIG_EVENT_RESET,      /**< 配置被重置 */
} ts_config_event_type_t;

/**
 * @brief 配置值联合体
 */
typedef union {
    bool        val_bool;
    int8_t      val_i8;
    uint8_t     val_u8;
    int16_t     val_i16;
    uint16_t    val_u16;
    int32_t     val_i32;
    uint32_t    val_u32;
    int64_t     val_i64;
    uint64_t    val_u64;
    float       val_float;
    double      val_double;
    char       *val_string;
    struct {
        void   *data;
        size_t  size;
    } val_blob;
} ts_config_value_t;

/**
 * @brief 配置项结构
 */
typedef struct {
    char                key[TS_CONFIG_KEY_MAX_LEN];  /**< 配置键 */
    ts_config_type_t    type;                        /**< 值类型 */
    ts_config_value_t   value;                       /**< 配置值 */
    ts_config_backend_t source;                      /**< 值来源 */
    uint8_t             priority;                    /**< 优先级 */
} ts_config_item_t;

/**
 * @brief 配置更改事件数据
 */
typedef struct {
    ts_config_event_type_t  event_type;     /**< 事件类型 */
    const char             *key;            /**< 配置键 */
    ts_config_type_t        value_type;     /**< 值类型 */
    const ts_config_value_t *old_value;     /**< 旧值（可能为 NULL） */
    const ts_config_value_t *new_value;     /**< 新值（删除事件为 NULL） */
    ts_config_backend_t     source;         /**< 来源后端 */
} ts_config_change_t;

/**
 * @brief 配置更改回调函数类型
 *
 * @param change 配置更改信息
 * @param user_data 用户数据
 */
typedef void (*ts_config_listener_t)(const ts_config_change_t *change, void *user_data);

/**
 * @brief 配置监听器句柄
 */
typedef struct ts_config_listener_handle *ts_config_listener_handle_t;

/**
 * @brief 后端操作函数集
 */
typedef struct {
    /** 初始化后端 */
    esp_err_t (*init)(void);
    
    /** 反初始化后端 */
    esp_err_t (*deinit)(void);
    
    /** 读取配置 */
    esp_err_t (*get)(const char *key, ts_config_type_t type, 
                     ts_config_value_t *value, size_t *size);
    
    /** 写入配置 */
    esp_err_t (*set)(const char *key, ts_config_type_t type,
                     const ts_config_value_t *value, size_t size);
    
    /** 删除配置 */
    esp_err_t (*erase)(const char *key);
    
    /** 检查配置是否存在 */
    esp_err_t (*exists)(const char *key, bool *exists);
    
    /** 清空所有配置 */
    esp_err_t (*clear)(void);
    
    /** 提交更改（用于批量操作） */
    esp_err_t (*commit)(void);
} ts_config_backend_ops_t;

/* ============================================================================
 * 初始化和反初始化
 * ========================================================================== */

/**
 * @brief 初始化配置管理系统
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_ERR_INVALID_STATE: 已经初始化
 *      - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t ts_config_init(void);

/**
 * @brief 反初始化配置管理系统
 *
 * @return
 *      - ESP_OK: 成功
 *      - ESP_ERR_INVALID_STATE: 未初始化
 */
esp_err_t ts_config_deinit(void);

/**
 * @brief 检查配置管理系统是否已初始化
 *
 * @return true 已初始化，false 未初始化
 */
bool ts_config_is_initialized(void);

/* ============================================================================
 * 基础配置读取 API
 * ========================================================================== */

/**
 * @brief 获取布尔配置值
 *
 * @param key 配置键
 * @param[out] value 输出值
 * @param default_value 默认值（键不存在时使用）
 * @return ESP_OK 成功，其他错误码表示失败
 */
esp_err_t ts_config_get_bool(const char *key, bool *value, bool default_value);

/**
 * @brief 获取整数配置值
 *
 * @param key 配置键
 * @param[out] value 输出值
 * @param default_value 默认值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_get_int32(const char *key, int32_t *value, int32_t default_value);

/**
 * @brief 获取无符号整数配置值
 *
 * @param key 配置键
 * @param[out] value 输出值
 * @param default_value 默认值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_get_uint32(const char *key, uint32_t *value, uint32_t default_value);

/**
 * @brief 获取 64 位整数配置值
 *
 * @param key 配置键
 * @param[out] value 输出值
 * @param default_value 默认值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_get_int64(const char *key, int64_t *value, int64_t default_value);

/**
 * @brief 获取浮点配置值
 *
 * @param key 配置键
 * @param[out] value 输出值
 * @param default_value 默认值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_get_float(const char *key, float *value, float default_value);

/**
 * @brief 获取双精度浮点配置值
 *
 * @param key 配置键
 * @param[out] value 输出值
 * @param default_value 默认值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_get_double(const char *key, double *value, double default_value);

/**
 * @brief 获取字符串配置值
 *
 * @param key 配置键
 * @param[out] buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param default_value 默认值（可以为 NULL）
 * @return ESP_OK 成功，ESP_ERR_INVALID_SIZE 缓冲区太小
 */
esp_err_t ts_config_get_string(const char *key, char *buffer, size_t buffer_size, 
                                const char *default_value);

/**
 * @brief 获取二进制配置值
 *
 * @param key 配置键
 * @param[out] buffer 输出缓冲区
 * @param[in,out] size 输入缓冲区大小，输出实际数据大小
 * @return ESP_OK 成功
 */
esp_err_t ts_config_get_blob(const char *key, void *buffer, size_t *size);

/* ============================================================================
 * 基础配置写入 API
 * ========================================================================== */

/**
 * @brief 设置布尔配置值
 *
 * @param key 配置键
 * @param value 配置值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_set_bool(const char *key, bool value);

/**
 * @brief 设置整数配置值
 *
 * @param key 配置键
 * @param value 配置值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_set_int32(const char *key, int32_t value);

/**
 * @brief 设置无符号整数配置值
 *
 * @param key 配置键
 * @param value 配置值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_set_uint32(const char *key, uint32_t value);

/**
 * @brief 设置 64 位整数配置值
 *
 * @param key 配置键
 * @param value 配置值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_set_int64(const char *key, int64_t value);

/**
 * @brief 设置浮点配置值
 *
 * @param key 配置键
 * @param value 配置值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_set_float(const char *key, float value);

/**
 * @brief 设置双精度浮点配置值
 *
 * @param key 配置键
 * @param value 配置值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_set_double(const char *key, double value);

/**
 * @brief 设置字符串配置值
 *
 * @param key 配置键
 * @param value 配置值
 * @return ESP_OK 成功
 */
esp_err_t ts_config_set_string(const char *key, const char *value);

/**
 * @brief 设置二进制配置值
 *
 * @param key 配置键
 * @param value 数据指针
 * @param size 数据大小
 * @return ESP_OK 成功
 */
esp_err_t ts_config_set_blob(const char *key, const void *value, size_t size);

/* ============================================================================
 * 高级配置操作
 * ========================================================================== */

/**
 * @brief 删除配置项
 *
 * @param key 配置键
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 键不存在
 */
esp_err_t ts_config_delete(const char *key);

/**
 * @brief 检查配置项是否存在
 *
 * @param key 配置键
 * @return true 存在，false 不存在
 */
bool ts_config_exists(const char *key);

/**
 * @brief 获取配置项的类型
 *
 * @param key 配置键
 * @param[out] type 输出类型
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 键不存在
 */
esp_err_t ts_config_get_type(const char *key, ts_config_type_t *type);

/**
 * @brief 获取配置项的来源后端
 *
 * @param key 配置键
 * @param[out] backend 输出后端
 * @return ESP_OK 成功
 */
esp_err_t ts_config_get_source(const char *key, ts_config_backend_t *backend);

/**
 * @brief 重置配置项为默认值
 *
 * @param key 配置键
 * @return ESP_OK 成功
 */
esp_err_t ts_config_reset(const char *key);

/**
 * @brief 重置所有配置为默认值
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_config_reset_all(void);

/**
 * @brief 强制保存所有未保存的配置更改
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_config_save(void);

/* ============================================================================
 * 配置监听器
 * ========================================================================== */

/**
 * @brief 注册配置更改监听器
 *
 * @param key_prefix 监听的键前缀（NULL 表示监听所有）
 * @param listener 回调函数
 * @param user_data 用户数据
 * @param[out] handle 输出监听器句柄
 * @return ESP_OK 成功
 */
esp_err_t ts_config_add_listener(const char *key_prefix,
                                  ts_config_listener_t listener,
                                  void *user_data,
                                  ts_config_listener_handle_t *handle);

/**
 * @brief 取消注册配置更改监听器
 *
 * @param handle 监听器句柄
 * @return ESP_OK 成功
 */
esp_err_t ts_config_remove_listener(ts_config_listener_handle_t handle);

/* ============================================================================
 * 后端管理
 * ========================================================================== */

/**
 * @brief 注册配置后端
 *
 * @param backend 后端类型
 * @param ops 后端操作函数集
 * @param priority 后端优先级
 * @return ESP_OK 成功
 */
esp_err_t ts_config_register_backend(ts_config_backend_t backend,
                                      const ts_config_backend_ops_t *ops,
                                      uint8_t priority);

/**
 * @brief 从指定后端加载配置
 *
 * @param backend 后端类型
 * @return ESP_OK 成功
 */
esp_err_t ts_config_load_from_backend(ts_config_backend_t backend);

/**
 * @brief 保存配置到指定后端
 *
 * @param backend 后端类型
 * @return ESP_OK 成功
 */
esp_err_t ts_config_save_to_backend(ts_config_backend_t backend);

/* ============================================================================
 * 文件配置操作
 * ========================================================================== */

/**
 * @brief 从 JSON 文件加载配置
 *
 * @param filepath 文件路径
 * @return ESP_OK 成功
 */
esp_err_t ts_config_load_json_file(const char *filepath);

/**
 * @brief 从 JSON 字符串加载配置
 *
 * @param json_str JSON 字符串
 * @return ESP_OK 成功
 */
esp_err_t ts_config_load_json_string(const char *json_str);

/**
 * @brief 保存配置到 JSON 文件
 *
 * @param filepath 文件路径
 * @return ESP_OK 成功
 */
esp_err_t ts_config_save_json_file(const char *filepath);

/* ============================================================================
 * 调试和诊断
 * ========================================================================== */

/**
 * @brief 打印所有配置项（用于调试）
 */
void ts_config_dump(void);

/**
 * @brief 获取配置统计信息
 *
 * @param[out] total_count 总配置数
 * @param[out] nvs_count NVS 配置数
 * @param[out] file_count 文件配置数
 */
void ts_config_get_stats(size_t *total_count, size_t *nvs_count, size_t *file_count);

#ifdef __cplusplus
}
#endif

#endif /* TS_CONFIG_H */
