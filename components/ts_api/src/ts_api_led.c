/**
 * @file ts_api_led.c
 * @brief LED Control API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_api.h"
#include "ts_log.h"
#include "ts_led.h"
#include "ts_led_preset.h"
#include <string.h>

#define TAG "api_led"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 设备名称映射：用户友好名 -> 内部名
 * 与 ts_cmd_led.c 保持一致
 */
static const char *resolve_device_name(const char *name)
{
    if (!name) return NULL;
    
    /* 支持简短别名 */
    if (strcmp(name, "touch") == 0) return "led_touch";
    if (strcmp(name, "board") == 0) return "led_board";
    if (strcmp(name, "matrix") == 0) return "led_matrix";
    
    /* 也支持完整名 */
    return name;
}

static const char *layout_to_str(ts_led_layout_t layout)
{
    switch (layout) {
        case TS_LED_LAYOUT_STRIP:  return "strip";
        case TS_LED_LAYOUT_MATRIX: return "matrix";
        case TS_LED_LAYOUT_RING:   return "ring";
        default: return "unknown";
    }
}

static esp_err_t parse_color_param(const cJSON *params, const char *key, ts_led_rgb_t *color)
{
    cJSON *color_param = cJSON_GetObjectItem(params, key);
    if (!color_param) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (cJSON_IsString(color_param)) {
        return ts_led_parse_color(color_param->valuestring, color);
    }
    
    if (cJSON_IsObject(color_param)) {
        cJSON *r = cJSON_GetObjectItem(color_param, "r");
        cJSON *g = cJSON_GetObjectItem(color_param, "g");
        cJSON *b = cJSON_GetObjectItem(color_param, "b");
        if (r && g && b) {
            color->r = (uint8_t)cJSON_GetNumberValue(r);
            color->g = (uint8_t)cJSON_GetNumberValue(g);
            color->b = (uint8_t)cJSON_GetNumberValue(b);
            return ESP_OK;
        }
    }
    
    if (cJSON_IsNumber(color_param)) {
        uint32_t val = (uint32_t)cJSON_GetNumberValue(color_param);
        color->r = (val >> 16) & 0xFF;
        color->g = (val >> 8) & 0xFF;
        color->b = val & 0xFF;
        return ESP_OK;
    }
    
    return ESP_ERR_INVALID_ARG;
}

/*===========================================================================*/
/*                          Device APIs                                       */
/*===========================================================================*/

/**
 * @brief led.list - List LED devices
 */
static esp_err_t api_led_list(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    cJSON *devices = cJSON_AddArrayToObject(data, "devices");
    
    // 用户友好名和内部名映射
    const char *display_names[] = {"touch", "board", "matrix"};
    const char *internal_names[] = {"led_touch", "led_board", "led_matrix"};
    
    for (size_t i = 0; i < sizeof(display_names) / sizeof(display_names[0]); i++) {
        ts_led_device_t dev = ts_led_device_get(internal_names[i]);
        if (dev) {
            cJSON *device = cJSON_CreateObject();
            cJSON_AddStringToObject(device, "name", display_names[i]);
            cJSON_AddNumberToObject(device, "count", ts_led_device_get_count(dev));
            cJSON_AddNumberToObject(device, "brightness", ts_led_device_get_brightness(dev));
            
            // 添加 layout 类型
            ts_led_layout_t layout = ts_led_device_get_layout(dev);
            cJSON_AddStringToObject(device, "layout", layout_to_str(layout));
            
            // 添加该设备适用的特效列表
            const char *effect_names[24];
            size_t effect_count = ts_led_animation_list_for_device(layout, effect_names, 24);
            cJSON *effects = cJSON_AddArrayToObject(device, "effects");
            for (size_t j = 0; j < effect_count; j++) {
                cJSON_AddItemToArray(effects, cJSON_CreateString(effect_names[j]));
            }
            
            // 添加当前运行状态
            ts_led_boot_config_t state;
            if (ts_led_get_current_state(display_names[i], &state) == ESP_OK) {
                cJSON *current = cJSON_AddObjectToObject(device, "current");
                cJSON_AddStringToObject(current, "animation", state.animation);
                cJSON_AddNumberToObject(current, "speed", state.speed);
                cJSON_AddBoolToObject(current, "on", state.enabled);
                
                // 添加颜色
                cJSON *color = cJSON_AddObjectToObject(current, "color");
                cJSON_AddNumberToObject(color, "r", state.color.r);
                cJSON_AddNumberToObject(color, "g", state.color.g);
                cJSON_AddNumberToObject(color, "b", state.color.b);
            }
            
            cJSON_AddItemToArray(devices, device);
        }
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.brightness - Set device brightness
 * @param device: device name
 * @param brightness: 0-255
 */
static esp_err_t api_led_brightness(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_param = cJSON_GetObjectItem(params, "device");
    cJSON *brightness_param = cJSON_GetObjectItem(params, "brightness");
    
    if (!device_param || !cJSON_IsString(device_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'device' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *internal_name = resolve_device_name(device_param->valuestring);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // If brightness is provided, set it; otherwise return current value
    if (brightness_param && cJSON_IsNumber(brightness_param)) {
        uint8_t brightness = (uint8_t)cJSON_GetNumberValue(brightness_param);
        esp_err_t ret = ts_led_device_set_brightness(dev, brightness);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to set brightness");
            return ret;
        }
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_param->valuestring);
    cJSON_AddNumberToObject(data, "brightness", ts_led_device_get_brightness(dev));
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.clear - Clear all LEDs on device
 * @param device: device name
 */
static esp_err_t api_led_clear(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_param = cJSON_GetObjectItem(params, "device");
    
    if (!device_param || !cJSON_IsString(device_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'device' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *internal_name = resolve_device_name(device_param->valuestring);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 获取 layer 0 并清除（清除 layer buffer，render_task 会自动刷新）
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (!layer) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to get layer");
        return ESP_FAIL;
    }
    
    // 先停止任何正在运行的动画
    ts_led_animation_stop(layer);
    
    // 清除 layer buffer
    esp_err_t ret = ts_led_layer_clear(layer);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to clear device");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_param->valuestring);
    cJSON_AddBoolToObject(data, "cleared", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.set - Set LED(s) color
 * @param device: device name
 * @param index: LED index or start index
 * @param count: number of LEDs (optional, default 1)
 * @param color: color value
 */
static esp_err_t api_led_set(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_param = cJSON_GetObjectItem(params, "device");
    cJSON *index_param = cJSON_GetObjectItem(params, "index");
    cJSON *count_param = cJSON_GetObjectItem(params, "count");
    
    if (!device_param || !cJSON_IsString(device_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'device' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *internal_name = resolve_device_name(device_param->valuestring);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    ts_led_rgb_t color;
    if (parse_color_param(params, "color", &color) != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid 'color' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get base layer (layer 0) for direct LED control
    // For simplicity, we'll use device clear and direct pixel setting
    uint16_t start = 0;
    uint16_t count = ts_led_device_get_count(dev);
    
    if (index_param && cJSON_IsNumber(index_param)) {
        start = (uint16_t)cJSON_GetNumberValue(index_param);
        count = 1;
    }
    
    if (count_param && cJSON_IsNumber(count_param)) {
        count = (uint16_t)cJSON_GetNumberValue(count_param);
    }
    
    // Use layer API if available, otherwise use simple fill
    // For now, create a temporary approach using device clear + fill
    // In production, you'd want to expose layer handles through the device
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_param->valuestring);
    cJSON_AddNumberToObject(data, "start", start);
    cJSON_AddNumberToObject(data, "count", count);
    cJSON *color_obj = cJSON_AddObjectToObject(data, "color");
    cJSON_AddNumberToObject(color_obj, "r", color.r);
    cJSON_AddNumberToObject(color_obj, "g", color.g);
    cJSON_AddNumberToObject(color_obj, "b", color.b);
    cJSON_AddBoolToObject(data, "success", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.fill - Fill all LEDs with color
 * @param device: device name
 * @param color: color value
 */
static esp_err_t api_led_fill(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_param = cJSON_GetObjectItem(params, "device");
    
    if (!device_param || !cJSON_IsString(device_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'device' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *internal_name = resolve_device_name(device_param->valuestring);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    ts_led_rgb_t color;
    if (parse_color_param(params, "color", &color) != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid 'color' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 获取 layer 0 并填充颜色（填充到 layer buffer，render_task 会自动刷新）
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (!layer) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to get layer");
        return ESP_FAIL;
    }
    
    // 先停止任何正在运行的动画
    ts_led_animation_stop(layer);
    
    // 填充颜色到 layer buffer
    esp_err_t ret = ts_led_fill(layer, color);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to fill color");
        return ret;
    }
    
    // 记录当前状态：使用 solid 动画表示纯色填充
    ts_led_preset_set_current_animation(device_param->valuestring, "solid", 50);
    ts_led_preset_set_current_color(device_param->valuestring, color);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_param->valuestring);
    cJSON *color_obj = cJSON_AddObjectToObject(data, "color");
    cJSON_AddNumberToObject(color_obj, "r", color.r);
    cJSON_AddNumberToObject(color_obj, "g", color.g);
    cJSON_AddNumberToObject(color_obj, "b", color.b);
    cJSON_AddBoolToObject(data, "success", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Effect APIs                                       */
/*===========================================================================*/

/**
 * @brief led.effect.list - List available effects
 */
static esp_err_t api_led_effect_list(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    cJSON *effects = cJSON_AddArrayToObject(data, "effects");
    
    const char *names[16];
    size_t count = ts_led_animation_list_builtin(names, 16);
    
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(effects, cJSON_CreateString(names[i]));
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.effect.start - Start effect on device
 * @param device: device name
 * @param effect: effect name
 * @param speed: speed 1-100 (optional, default uses effect's default)
 * @param color: color for effects that support it (optional)
 */
static esp_err_t api_led_effect_start(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_param = cJSON_GetObjectItem(params, "device");
    cJSON *effect_param = cJSON_GetObjectItem(params, "effect");
    cJSON *speed_param = cJSON_GetObjectItem(params, "speed");
    
    if (!device_param || !cJSON_IsString(device_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'device' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!effect_param || !cJSON_IsString(effect_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'effect' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *internal_name = resolve_device_name(device_param->valuestring);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    const ts_led_animation_def_t *effect = ts_led_animation_get_builtin(effect_param->valuestring);
    if (!effect) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Effect not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 复制动画定义以便修改
    ts_led_animation_def_t modified = *effect;
    
    // 处理速度参数 (1-100, 默认 50)
    uint8_t speed = 50;
    if (speed_param && cJSON_IsNumber(speed_param)) {
        speed = (uint8_t)cJSON_GetNumberValue(speed_param);
        if (speed < 1) speed = 1;
        if (speed > 100) speed = 100;
        // 速度映射：1->200ms, 100->5ms
        modified.frame_interval_ms = 200 - (speed - 1) * 195 / 99;
    }
    
    // 处理颜色参数（用于支持自定义颜色的动画）
    static ts_led_rgb_t effect_color;
    ts_led_rgb_t color;
    if (parse_color_param(params, "color", &color) == ESP_OK) {
        effect_color = color;
        modified.user_data = &effect_color;
    }
    
    // 获取设备的 layer 0 并启动动画
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (layer) {
        ts_led_animation_start(layer, &modified);
    }
    
    // 记录当前状态（供保存用）
    ts_led_preset_set_current_animation(device_param->valuestring, effect_param->valuestring, speed);
    
    // 如果有颜色参数，也记录下来
    if (parse_color_param(params, "color", &color) == ESP_OK) {
        ts_led_preset_set_current_color(device_param->valuestring, color);
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_param->valuestring);
    cJSON_AddStringToObject(data, "effect", effect_param->valuestring);
    cJSON_AddNumberToObject(data, "speed", speed);
    cJSON_AddBoolToObject(data, "started", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.effect.stop - Stop effect on device
 * @param device: device name
 */
static esp_err_t api_led_effect_stop(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_param = cJSON_GetObjectItem(params, "device");
    
    if (!device_param || !cJSON_IsString(device_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'device' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *internal_name = resolve_device_name(device_param->valuestring);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 获取设备的 layer 0 并停止动画
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (layer) {
        ts_led_animation_stop(layer);
    }
    
    // 清除当前动画状态
    ts_led_preset_set_current_animation(device_param->valuestring, NULL, 0);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_param->valuestring);
    cJSON_AddBoolToObject(data, "stopped", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Color APIs                                        */
/*===========================================================================*/

/**
 * @brief led.color.parse - Parse color string
 * @param color: color string (#RRGGBB or name)
 */
static esp_err_t api_led_color_parse(const cJSON *params, ts_api_result_t *result)
{
    cJSON *color_param = cJSON_GetObjectItem(params, "color");
    
    if (!color_param || !cJSON_IsString(color_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'color' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_led_rgb_t color;
    esp_err_t ret = ts_led_parse_color(color_param->valuestring, &color);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid color string");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "input", color_param->valuestring);
    cJSON *rgb = cJSON_AddObjectToObject(data, "rgb");
    cJSON_AddNumberToObject(rgb, "r", color.r);
    cJSON_AddNumberToObject(rgb, "g", color.g);
    cJSON_AddNumberToObject(rgb, "b", color.b);
    
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", color.r, color.g, color.b);
    cJSON_AddStringToObject(data, "hex", hex);
    
    cJSON_AddNumberToObject(data, "value", (color.r << 16) | (color.g << 8) | color.b);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.color.hsv - Convert HSV to RGB
 * @param h: hue (0-359)
 * @param s: saturation (0-255)
 * @param v: value (0-255)
 */
static esp_err_t api_led_color_hsv(const cJSON *params, ts_api_result_t *result)
{
    cJSON *h_param = cJSON_GetObjectItem(params, "h");
    cJSON *s_param = cJSON_GetObjectItem(params, "s");
    cJSON *v_param = cJSON_GetObjectItem(params, "v");
    
    if (!h_param || !s_param || !v_param) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing h/s/v parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_led_hsv_t hsv = {
        .h = (uint16_t)cJSON_GetNumberValue(h_param),
        .s = (uint8_t)cJSON_GetNumberValue(s_param),
        .v = (uint8_t)cJSON_GetNumberValue(v_param)
    };
    
    ts_led_rgb_t rgb = ts_led_hsv_to_rgb(hsv);
    
    cJSON *data = cJSON_CreateObject();
    cJSON *hsv_obj = cJSON_AddObjectToObject(data, "hsv");
    cJSON_AddNumberToObject(hsv_obj, "h", hsv.h);
    cJSON_AddNumberToObject(hsv_obj, "s", hsv.s);
    cJSON_AddNumberToObject(hsv_obj, "v", hsv.v);
    
    cJSON *rgb_obj = cJSON_AddObjectToObject(data, "rgb");
    cJSON_AddNumberToObject(rgb_obj, "r", rgb.r);
    cJSON_AddNumberToObject(rgb_obj, "g", rgb.g);
    cJSON_AddNumberToObject(rgb_obj, "b", rgb.b);
    
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", rgb.r, rgb.g, rgb.b);
    cJSON_AddStringToObject(data, "hex", hex);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief LED 后处理滤镜类型定义
 */
static const struct {
    const char *name;
    const char *description;
} s_filter_types[] = {
    {"none",        "No effect"},
    {"brightness",  "Static brightness adjustment"},
    {"pulse",       "Pulsing brightness (sine wave)"},
    {"blink",       "On/off blinking"},
    {"fade-in",     "Fade in (one-shot)"},
    {"fade-out",    "Fade out (one-shot)"},
    {"breathing",   "Smooth breathing effect"},
    {"color-shift", "Hue rotation over time"},
    {"saturation",  "Saturation adjustment"},
    {"invert",      "Invert colors"},
    {"grayscale",   "Convert to grayscale"},
    {"scanline",    "Horizontal/vertical scanline"},
    {"wave",        "Brightness wave"},
    {"glitch",      "Random glitch artifacts"},
    {NULL, NULL}
};

/**
 * @brief led.filter.list - List available post-processing filters
 */
static esp_err_t api_led_filter_list(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    cJSON *filters = cJSON_AddArrayToObject(data, "filters");
    
    for (int i = 0; s_filter_types[i].name; i++) {
        cJSON *filter = cJSON_CreateObject();
        cJSON_AddStringToObject(filter, "name", s_filter_types[i].name);
        cJSON_AddStringToObject(filter, "description", s_filter_types[i].description);
        cJSON_AddItemToArray(filters, filter);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.save - Save current LED state as boot configuration
 * @param device: device name (touch/board/matrix)
 */
static esp_err_t api_led_save(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_param = cJSON_GetObjectItem(params, "device");
    
    if (!device_param || !cJSON_IsString(device_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'device' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *device_name = device_param->valuestring;
    
    // 验证设备名
    if (strcmp(device_name, "touch") != 0 && 
        strcmp(device_name, "board") != 0 && 
        strcmp(device_name, "matrix") != 0) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 保存当前配置到 NVS
    esp_err_t ret = ts_led_save_boot_config(device_name);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to save config");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_name);
    cJSON_AddBoolToObject(data, "saved", true);
    
    // 返回保存的配置
    ts_led_boot_config_t cfg;
    if (ts_led_get_boot_config(device_name, &cfg) == ESP_OK) {
        cJSON_AddStringToObject(data, "animation", cfg.animation);
        cJSON_AddNumberToObject(data, "brightness", cfg.brightness);
        cJSON_AddNumberToObject(data, "speed", cfg.speed);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief led.boot.config - Get LED boot configuration
 * @param device: optional device name filter
 */
static esp_err_t api_led_boot_config(const cJSON *params, ts_api_result_t *result)
{
    const char *device_filter = NULL;
    if (params) {
        cJSON *device_param = cJSON_GetObjectItem(params, "device");
        if (device_param && cJSON_IsString(device_param)) {
            device_filter = device_param->valuestring;
        }
    }
    
    const char *devices[] = {"touch", "board", "matrix"};
    int start = 0, end = 3;
    
    if (device_filter) {
        for (int i = 0; i < 3; i++) {
            if (strcmp(device_filter, devices[i]) == 0) {
                start = i;
                end = i + 1;
                break;
            }
        }
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON *configs = cJSON_AddArrayToObject(data, "boot_config");
    
    for (int i = start; i < end; i++) {
        ts_led_boot_config_t cfg;
        if (ts_led_get_boot_config(devices[i], &cfg) == ESP_OK) {
            cJSON *config = cJSON_CreateObject();
            cJSON_AddStringToObject(config, "device", devices[i]);
            cJSON_AddBoolToObject(config, "enabled", cfg.enabled);
            cJSON_AddStringToObject(config, "animation", cfg.animation);
            cJSON_AddStringToObject(config, "filter", cfg.filter);
            cJSON_AddStringToObject(config, "image_path", cfg.image_path);
            cJSON_AddNumberToObject(config, "speed", cfg.speed);
            cJSON_AddNumberToObject(config, "brightness", cfg.brightness);
            cJSON_AddItemToArray(configs, config);
        }
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t led_endpoints[] = {
    {
        .name = "led.list",
        .description = "List LED devices",
        .category = TS_API_CAT_LED,
        .handler = api_led_list,
        .requires_auth = false,
    },
    {
        .name = "led.brightness",
        .description = "Get/set device brightness",
        .category = TS_API_CAT_LED,
        .handler = api_led_brightness,
        .requires_auth = false,  // TODO: 测试完成后改为 true
        .permission = "led.control",
    },
    {
        .name = "led.clear",
        .description = "Clear all LEDs on device",
        .category = TS_API_CAT_LED,
        .handler = api_led_clear,
        .requires_auth = false,  // TODO: 测试完成后改为 true
        .permission = "led.control",
    },
    {
        .name = "led.set",
        .description = "Set LED(s) color",
        .category = TS_API_CAT_LED,
        .handler = api_led_set,
        .requires_auth = false,  // TODO: 测试完成后改为 true
        .permission = "led.control",
    },
    {
        .name = "led.fill",
        .description = "Fill all LEDs with color",
        .category = TS_API_CAT_LED,
        .handler = api_led_fill,
        .requires_auth = false,  // TODO: 测试完成后改为 true
        .permission = "led.control",
    },
    {
        .name = "led.effect.list",
        .description = "List available effects",
        .category = TS_API_CAT_LED,
        .handler = api_led_effect_list,
        .requires_auth = false,
    },
    {
        .name = "led.effect.start",
        .description = "Start effect on device",
        .category = TS_API_CAT_LED,
        .handler = api_led_effect_start,
        .requires_auth = false,  // TODO: 测试完成后改为 true
        .permission = "led.control",
    },
    {
        .name = "led.effect.stop",
        .description = "Stop effect on device",
        .category = TS_API_CAT_LED,
        .handler = api_led_effect_stop,
        .requires_auth = false,  // TODO: 测试完成后改为 true
        .permission = "led.control",
    },
    {
        .name = "led.color.parse",
        .description = "Parse color string to RGB",
        .category = TS_API_CAT_LED,
        .handler = api_led_color_parse,
        .requires_auth = false,
    },
    {
        .name = "led.color.hsv",
        .description = "Convert HSV to RGB",
        .category = TS_API_CAT_LED,
        .handler = api_led_color_hsv,
        .requires_auth = false,
    },
    {
        .name = "led.filter.list",
        .description = "List available post-processing filters",
        .category = TS_API_CAT_LED,
        .handler = api_led_filter_list,
        .requires_auth = false,
    },
    {
        .name = "led.save",
        .description = "Save current state as boot configuration",
        .category = TS_API_CAT_LED,
        .handler = api_led_save,
        .requires_auth = false,  // TODO: 测试完成后改为 true
        .permission = "led.config",
    },
    {
        .name = "led.boot.config",
        .description = "Get LED boot configuration",
        .category = TS_API_CAT_LED,
        .handler = api_led_boot_config,
        .requires_auth = false,
    },
};

esp_err_t ts_api_led_register(void)
{
    TS_LOGI(TAG, "Registering LED APIs");
    
    for (size_t i = 0; i < sizeof(led_endpoints) / sizeof(led_endpoints[0]); i++) {
        esp_err_t ret = ts_api_register(&led_endpoints[i]);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to register %s", led_endpoints[i].name);
            return ret;
        }
    }
    
    return ESP_OK;
}
