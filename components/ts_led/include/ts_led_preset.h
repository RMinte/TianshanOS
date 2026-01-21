/**
 * @file ts_led_preset.h
 * @brief TianShanOS LED Preset Device Definitions
 * 
 * Predefined LED device instances for touch, board, and matrix.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_LED_PRESET_H
#define TS_LED_PRESET_H

#include "ts_led.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                          Preset Device Names                               */
/*===========================================================================*/

#define TS_LED_TOUCH_NAME       "led_touch"
#define TS_LED_BOARD_NAME       "led_board"
#define TS_LED_MATRIX_NAME      "led_matrix"

/*===========================================================================*/
/*                          Preset Initialization                             */
/*===========================================================================*/

/**
 * @brief Initialize touch LED device
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_touch_init(void);

/**
 * @brief Initialize board LED device
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_board_init(void);

/**
 * @brief Initialize matrix LED device
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_matrix_init(void);

/**
 * @brief Initialize all preset LED devices
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_preset_init_all(void);

/*===========================================================================*/
/*                          Quick Access                                      */
/*===========================================================================*/

/**
 * @brief Get touch LED device
 */
ts_led_device_t ts_led_touch_get(void);

/**
 * @brief Get board LED device
 */
ts_led_device_t ts_led_board_get(void);

/**
 * @brief Get matrix LED device
 */
ts_led_device_t ts_led_matrix_get(void);

/*===========================================================================*/
/*                          Status Indicators                                 */
/*===========================================================================*/

/**
 * @brief LED status type
 */
typedef enum {
    TS_LED_STATUS_IDLE = 0,      /**< System idle */
    TS_LED_STATUS_BUSY,          /**< System busy */
    TS_LED_STATUS_SUCCESS,       /**< Operation success */
    TS_LED_STATUS_ERROR,         /**< Error occurred */
    TS_LED_STATUS_WARNING,       /**< Warning */
    TS_LED_STATUS_NETWORK,       /**< Network activity */
    TS_LED_STATUS_USB,           /**< USB activity */
    TS_LED_STATUS_BOOT,          /**< Boot sequence */
    TS_LED_STATUS_MAX
} ts_led_status_t;

/**
 * @brief Set status indicator
 * 
 * @param status Status type
 * @return ESP_OK on success
 */
esp_err_t ts_led_set_status(ts_led_status_t status);

/**
 * @brief Clear status indicator (return to idle)
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_led_clear_status(void);

/**
 * @brief Bind event to status indicator
 * 
 * @param event_id Event ID
 * @param status Status to show
 * @param duration_ms Duration in ms (0 = permanent)
 * @return ESP_OK on success
 */
esp_err_t ts_led_bind_event_status(uint32_t event_id, ts_led_status_t status,
                                    uint32_t duration_ms);

/*===========================================================================*/
/*                          Boot Configuration                                */
/*===========================================================================*/

/**
 * @brief LED 启动配置结构
 */
typedef struct {
    char animation[32];     /**< 动画名称，空字符串表示无动画 */
    char filter[32];        /**< 后处理效果名称，空字符串表示无效果 */
    char image_path[128];   /**< 图像/动画路径，空字符串表示无图像 */
    char qrcode_text[128];  /**< QR 码内容，空字符串表示无 QR 码 */
    char qrcode_bg[128];    /**< QR 码背景图路径，空字符串表示无背景图 */
    char text_content[64];  /**< 文本内容，空字符串表示无文本 */
    uint8_t brightness;     /**< 亮度 0-255 */
    ts_led_rgb_t color;     /**< 静态颜色（当animation为空时使用） */
    uint8_t speed;          /**< 动画速度 1-100，0 使用默认 */
    uint8_t filter_speed;   /**< 滤镜速度 1-100，0 使用默认 */
    bool enabled;           /**< 是否启用 */
} ts_led_boot_config_t;

/**
 * @brief 记录当前运行的动画（内部使用）
 * 
 * 当通过命令启动动画时调用，以便后续保存。
 * 
 * @param device_name 设备名
 * @param animation 动画名，NULL 表示无动画
 * @param speed 速度，0 表示默认
 */
void ts_led_preset_set_current_animation(const char *device_name, const char *animation, uint8_t speed);

/**
 * @brief 记录当前运行的后处理效果（内部使用）
 * 
 * @param device_name 设备名
 * @param filter 后处理效果名，NULL 表示无效果
 * @param speed 滤镜速度 1-100
 */
void ts_led_preset_set_current_filter(const char *device_name, const char *filter, uint8_t speed);

/**
 * @brief 记录当前显示的 QR 码内容（内部使用）
 * 
 * @param device_name 设备名
 * @param text QR 码内容，NULL 表示无 QR 码
 */
void ts_led_preset_set_current_qrcode(const char *device_name, const char *text);

/**
 * @brief 记录当前 QR 码的背景图（内部使用）
 * 
 * @param device_name 设备名
 * @param bg_path 背景图路径，NULL 表示无背景图
 */
void ts_led_preset_set_current_qrcode_bg(const char *device_name, const char *bg_path);

/**
 * @brief 记录当前显示的文本内容（内部使用）
 * 
 * @param device_name 设备名
 * @param text 文本内容，NULL 表示无文本
 */
void ts_led_preset_set_current_text(const char *device_name, const char *text);

/**
 * @brief 兼容 API - 映射到 ts_led_preset_set_current_animation
 * @deprecated 使用 ts_led_preset_set_current_animation 替代
 */
void ts_led_preset_set_current_effect(const char *device_name, const char *effect, uint8_t speed);

/**
 * @brief 记录当前运行特效的颜色（内部使用）
 * 
 * @param device_name 设备名
 * @param color 颜色值
 */
void ts_led_preset_set_current_color(const char *device_name, ts_led_rgb_t color);

/**
 * @brief 清除当前特效颜色记录
 * 
 * @param device_name 设备名
 */
void ts_led_preset_clear_current_color(const char *device_name);

/**
 * @brief 记录当前显示的图像路径（内部使用）
 * 
 * @param device_name 设备名
 * @param path 图像文件路径，NULL 表示无图像
 */
void ts_led_preset_set_current_image(const char *device_name, const char *path);

/**
 * @brief 清除当前图像路径记录
 * 
 * @param device_name 设备名
 */
void ts_led_preset_clear_current_image(const char *device_name);

/**
 * @brief 保存当前 LED 状态为启动配置
 * 
 * 保存指定设备的当前特效、亮度等设置，下次启动时自动恢复。
 * 
 * @param device_name 设备名（touch, board, matrix）
 * @return ESP_OK 成功
 */
esp_err_t ts_led_save_boot_config(const char *device_name);

/**
 * @brief 保存所有 LED 设备的当前状态
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_led_save_all_boot_config(void);

/**
 * @brief 加载并应用 LED 启动配置
 * 
 * @param device_name 设备名
 * @return ESP_OK 成功，ESP_ERR_NOT_FOUND 无保存配置
 */
esp_err_t ts_led_load_boot_config(const char *device_name);

/**
 * @brief 加载所有 LED 设备的启动配置
 * 
 * 在 LED 服务初始化后调用，自动恢复上次保存的状态。
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_led_load_all_boot_config(void);

/**
 * @brief 清除 LED 启动配置
 * 
 * @param device_name 设备名，NULL 表示清除所有
 * @return ESP_OK 成功
 */
esp_err_t ts_led_clear_boot_config(const char *device_name);

/**
 * @brief 获取当前 LED 设备的启动配置
 * 
 * @param device_name 设备名
 * @param[out] config 输出配置
 * @return ESP_OK 成功
 */
esp_err_t ts_led_get_boot_config(const char *device_name, ts_led_boot_config_t *config);

/**
 * @brief 获取当前 LED 设备的运行时状态
 * 
 * 返回设备当前正在运行的动画、颜色、亮度等信息。
 * 
 * @param device_name 设备名（touch, board, matrix）
 * @param[out] state 输出状态（使用 ts_led_boot_config_t 结构）
 * @return ESP_OK 成功
 */
esp_err_t ts_led_get_current_state(const char *device_name, ts_led_boot_config_t *state);

#ifdef __cplusplus
}
#endif

#endif /* TS_LED_PRESET_H */
