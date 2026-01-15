/**
 * @file ts_cmd_led.c
 * @brief LED Console Commands
 * 
 * 实现 led 命令族：
 * - led --status         显示 LED 设备状态
 * - led --brightness     设置亮度
 * - led --clear          清除 LED
 * - led --list-effects   列出特效
 * - led --parse-color    解析颜色
 * - led --save           保存当前状态为开机配置
 * - led --image          显示图像文件
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_console.h"
#include "ts_led.h"
#include "ts_led_effect.h"
#include "ts_led_preset.h"
#include "ts_led_image.h"
#include "ts_led_qrcode.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdlib.h>

#define TAG "cmd_led"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *status;
    struct arg_lit *brightness;
    struct arg_lit *clear;
    struct arg_lit *on;
    struct arg_lit *off;
    struct arg_lit *effect;
    struct arg_lit *stop_effect;
    struct arg_lit *list_effects;
    struct arg_lit *filter;
    struct arg_lit *stop_filter;
    struct arg_lit *list_filters;
    struct arg_str *filter_name;
    struct arg_lit *parse_color;
    struct arg_lit *save;
    struct arg_lit *clear_boot;
    struct arg_lit *show_boot;
    struct arg_lit *image;
    struct arg_lit *qrcode;
    struct arg_str *text;
    struct arg_str *ecc;
    struct arg_lit *test;
    struct arg_str *file;
    struct arg_str *device;
    struct arg_str *center;
    struct arg_int *value;
    struct arg_str *color_val;
    struct arg_str *effect_name;
    struct arg_int *speed;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_led_args;

/* 当前加载的图像（用于持续显示） */
static ts_led_image_t s_current_image = NULL;

/*===========================================================================*/
/*                          Command: led --status                             */
/*===========================================================================*/

/* 设备名称映射：用户友好名 -> 内部名 */
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

static int do_led_status(const char *device_name, bool json)
{
    /* 使用内部设备名 */
    const char *device_names[] = {"led_touch", "led_board", "led_matrix"};
    const char *display_names[] = {"touch", "board", "matrix"};
    size_t num_devices = sizeof(device_names) / sizeof(device_names[0]);
    
    if (device_name) {
        // 单个设备状态
        const char *internal_name = resolve_device_name(device_name);
        ts_led_device_t dev = ts_led_device_get(internal_name);
        if (!dev) {
            ts_console_error("Device '%s' not found\n", device_name);
            return 1;
        }
        
        if (json) {
            ts_console_printf("{\"device\":\"%s\",\"count\":%u,\"brightness\":%u}\n",
                device_name,
                ts_led_device_get_count(dev),
                ts_led_device_get_brightness(dev));
        } else {
            ts_console_printf("LED Device: %s\n", device_name);
            ts_console_printf("  Count:      %u\n", ts_led_device_get_count(dev));
            ts_console_printf("  Brightness: %u\n", ts_led_device_get_brightness(dev));
        }
    } else {
        // 所有设备状态
        if (json) {
            ts_console_printf("{\"devices\":[");
            bool first = true;
            for (size_t i = 0; i < num_devices; i++) {
                ts_led_device_t dev = ts_led_device_get(device_names[i]);
                if (dev) {
                    if (!first) ts_console_printf(",");
                    ts_console_printf("{\"name\":\"%s\",\"count\":%u,\"brightness\":%u}",
                        display_names[i],
                        ts_led_device_get_count(dev),
                        ts_led_device_get_brightness(dev));
                    first = false;
                }
            }
            ts_console_printf("]}\n");
        } else {
            ts_console_printf("LED Devices:\n\n");
            ts_console_printf("%-12s  %6s  %10s\n", "NAME", "COUNT", "BRIGHTNESS");
            ts_console_printf("------------------------------------\n");
            
            bool found = false;
            for (size_t i = 0; i < num_devices; i++) {
                ts_led_device_t dev = ts_led_device_get(device_names[i]);
                if (dev) {
                    ts_console_printf("%-12s  %6u  %10u\n", 
                        display_names[i],
                        ts_led_device_get_count(dev),
                        ts_led_device_get_brightness(dev));
                    found = true;
                }
            }
            
            if (!found) {
                ts_console_printf("  (no devices initialized)\n");
            }
            ts_console_printf("\n");
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                       Command: led --brightness                            */
/*===========================================================================*/

static int do_led_brightness(const char *device_name, int value, bool set)
{
    if (!device_name) {
        ts_console_error("--device required\n");
        return 1;
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    if (set) {
        if (value < 0 || value > 255) {
            ts_console_error("Brightness must be 0-255\n");
            return 1;
        }
        
        esp_err_t ret = ts_led_device_set_brightness(dev, (uint8_t)value);
        if (ret != ESP_OK) {
            ts_console_error("Failed to set brightness\n");
            return 1;
        }
        ts_console_success("Brightness set to %d\n", value);
    } else {
        ts_console_printf("Brightness: %u\n", ts_led_device_get_brightness(dev));
    }
    
    return 0;
}

/*===========================================================================*/
/*                         Command: led --clear                               */
/*===========================================================================*/

static int do_led_clear(const char *device_name)
{
    if (!device_name) {
        ts_console_error("--device required\n");
        return 1;
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    /* Stop any running effect/animation first */
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (layer) {
        ts_led_animation_stop(layer);
        ts_led_image_animate_stop(layer);
        /* Clear layer buffer too */
        ts_led_layer_clear(layer);
    }
    
    /* Free current image if any */
    if (s_current_image) {
        ts_led_image_free(s_current_image);
        s_current_image = NULL;
    }
    
    /* Clear image path record */
    ts_led_preset_clear_current_image(device_name);
    
    esp_err_t ret = ts_led_device_clear(dev);
    if (ret != ESP_OK) {
        ts_console_error("Failed to clear device\n");
        return 1;
    }
    
    ret = ts_led_device_refresh(dev);
    if (ret != ESP_OK) {
        ts_console_error("Failed to refresh device\n");
        return 1;
    }
    
    ts_console_success("Device '%s' cleared\n", device_name);
    return 0;
}

/*===========================================================================*/
/*                         Command: led --effect                              */
/*===========================================================================*/

/* 静态存储用于特效颜色参数（因为特效使用指针） */
static ts_led_rgb_t s_effect_color;
static bool s_effect_color_valid = false;

static int do_led_effect(const char *device_name, const char *effect_name, int speed, const char *color_str)
{
    if (!device_name) {
        ts_console_error("--device required\n");
        return 1;
    }
    
    if (!effect_name) {
        ts_console_error("--name required (e.g. rainbow, breathing, chase, fire, sparkle, solid)\n");
        return 1;
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    // 获取内置动画
    const ts_led_animation_def_t *effect = ts_led_animation_get_builtin(effect_name);
    if (!effect) {
        ts_console_error("Effect '%s' not found\n", effect_name);
        ts_console_printf("Use 'led --list-effects' to see available effects\n");
        return 1;
    }
    
    // 获取设备的默认层（layer 0）
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (!layer) {
        ts_console_error("Failed to get layer\n");
        return 1;
    }
    
    // 解析颜色参数（用于传递给特效）
    if (color_str) {
        if (ts_led_parse_color(color_str, &s_effect_color) == ESP_OK) {
            s_effect_color_valid = true;
        } else {
            ts_console_error("Invalid color: %s\n", color_str);
            return 1;
        }
    } else {
        s_effect_color_valid = false;
    }
    
    // 创建修改后的动画（速度和/或颜色）
    ts_led_animation_def_t modified_effect = *effect;
    
    if (speed > 0) {
        // 速度 1-100 映射到帧间隔：速度1=200ms, 速度100=5ms
        uint32_t interval = 200 - (speed - 1) * 195 / 99;
        if (interval < 5) interval = 5;
        modified_effect.frame_interval_ms = interval;
    }
    
    // 如果有颜色参数，传递给特效
    if (s_effect_color_valid) {
        modified_effect.user_data = &s_effect_color;
    }
    
    // 启动动画
    esp_err_t ret = ts_led_animation_start(layer, &modified_effect);
    if (ret != ESP_OK) {
        ts_console_error("Failed to start effect: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    // 记录当前特效（供后续保存使用）
    ts_led_preset_set_current_effect(device_name, effect_name, speed > 0 ? (uint8_t)speed : 0);
    
    // 清除图像路径记录（特效和图像互斥）
    ts_led_preset_clear_current_image(device_name);
    
    // 记录颜色（如果有）
    if (s_effect_color_valid) {
        ts_led_preset_set_current_color(device_name, s_effect_color);
    } else {
        ts_led_preset_clear_current_color(device_name);
    }
    
    if (speed > 0 && color_str) {
        ts_console_success("Effect '%s' started on '%s' (speed=%d, color=%s)\n", effect_name, device_name, speed, color_str);
    } else if (speed > 0) {
        ts_console_success("Effect '%s' started on '%s' (speed=%d)\n", effect_name, device_name, speed);
    } else if (color_str) {
        ts_console_success("Effect '%s' started on '%s' (color=%s)\n", effect_name, device_name, color_str);
    } else {
        ts_console_success("Effect '%s' started on '%s'\n", effect_name, device_name);
    }
    return 0;
}

static int do_led_stop_effect(const char *device_name)
{
    if (!device_name) {
        ts_console_error("--device required\n");
        return 1;
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (!layer) {
        ts_console_error("Failed to get layer\n");
        return 1;
    }
    
    esp_err_t ret = ts_led_animation_stop(layer);
    if (ret != ESP_OK) {
        ts_console_error("Failed to stop animation\n");
        return 1;
    }
    
    // 清除记录的特效
    ts_led_preset_set_current_effect(device_name, NULL, 0);
    
    ts_console_success("Effect stopped on '%s'\n", device_name);
    return 0;
}

/*===========================================================================*/
/*                      Command: led --filter (Post-Processing)               */
/*===========================================================================*/

/**
 * @brief 后处理效果类型名称映射
 */
static const struct {
    const char *name;
    ts_led_effect_type_t type;
    const char *description;
} s_filter_types[] = {
    {"none",        TS_LED_EFFECT_NONE,        "No effect"},
    {"brightness",  TS_LED_EFFECT_BRIGHTNESS,  "Static brightness adjustment"},
    {"pulse",       TS_LED_EFFECT_PULSE,       "Pulsing brightness (sine wave)"},
    {"blink",       TS_LED_EFFECT_BLINK,       "On/off blinking"},
    {"fade-in",     TS_LED_EFFECT_FADE_IN,     "Fade in (one-shot)"},
    {"fade-out",    TS_LED_EFFECT_FADE_OUT,    "Fade out (one-shot)"},
    {"breathing",   TS_LED_EFFECT_BREATHING,   "Smooth breathing effect"},
    {"color-shift", TS_LED_EFFECT_COLOR_SHIFT, "Hue rotation over time"},
    {"saturation",  TS_LED_EFFECT_SATURATION,  "Saturation adjustment"},
    {"invert",      TS_LED_EFFECT_INVERT,      "Invert colors"},
    {"grayscale",   TS_LED_EFFECT_GRAYSCALE,   "Convert to grayscale"},
    {"scanline",    TS_LED_EFFECT_SCANLINE,    "Horizontal/vertical scanline"},
    {"wave",        TS_LED_EFFECT_WAVE,        "Brightness wave"},
    {"glitch",      TS_LED_EFFECT_GLITCH,      "Random glitch artifacts"},
    {NULL, 0, NULL}
};

static ts_led_effect_type_t filter_name_to_type(const char *name)
{
    if (!name) return TS_LED_EFFECT_NONE;
    for (int i = 0; s_filter_types[i].name; i++) {
        if (strcmp(s_filter_types[i].name, name) == 0) {
            return s_filter_types[i].type;
        }
    }
    return TS_LED_EFFECT_NONE;
}

static int do_led_filter(const char *device_name, const char *filter_name, int speed)
{
    if (!device_name) {
        ts_console_error("--device required\n");
        return 1;
    }
    
    if (!filter_name) {
        ts_console_error("--filter-name required (e.g. pulse, blink, breathing, fade-in)\n");
        return 1;
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    ts_led_effect_type_t type = filter_name_to_type(filter_name);
    if (type == TS_LED_EFFECT_NONE && strcmp(filter_name, "none") != 0) {
        ts_console_error("Filter '%s' not found\n", filter_name);
        ts_console_printf("Use 'led --list-filters' to see available filters\n");
        return 1;
    }
    
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (!layer) {
        ts_console_error("Failed to get layer\n");
        return 1;
    }
    
    // 配置后处理效果
    ts_led_effect_config_t config = {
        .type = type,
        .params = {
            .brightness = {.level = 255}
        }
    };
    
    // 根据速度参数调整效果参数
    if (speed > 0) {
        // 速度 1-100 映射到频率：速度1=0.2Hz, 速度100=5Hz
        float freq = 0.2f + (speed - 1) * 4.8f / 99.0f;
        
        switch (type) {
            case TS_LED_EFFECT_PULSE:
                config.params.pulse.frequency = freq;
                config.params.pulse.min_level = 20;
                config.params.pulse.max_level = 255;
                break;
            case TS_LED_EFFECT_BLINK: {
                uint16_t period_ms = (uint16_t)(1000.0f / freq);
                config.params.blink.on_time_ms = period_ms / 2;
                config.params.blink.off_time_ms = period_ms / 2;
                break;
            }
            case TS_LED_EFFECT_BREATHING:
                config.params.breathing.frequency = freq;
                config.params.breathing.min_level = 10;
                config.params.breathing.max_level = 255;
                break;
            case TS_LED_EFFECT_FADE_IN:
            case TS_LED_EFFECT_FADE_OUT:
                config.params.fade.duration_ms = (uint16_t)(1000.0f / freq);
                config.params.fade.auto_remove = false;
                break;
            case TS_LED_EFFECT_COLOR_SHIFT:
                config.params.color_shift.speed = speed * 3.6f;  // 度/秒
                config.params.color_shift.static_shift = 0;
                break;
            case TS_LED_EFFECT_SCANLINE:
                config.params.scanline.speed = (float)speed;
                config.params.scanline.width = 3;
                config.params.scanline.direction = TS_LED_EFFECT_DIR_HORIZONTAL;
                config.params.scanline.intensity = 200;
                break;
            case TS_LED_EFFECT_WAVE:
                config.params.wave.speed = (float)speed;
                config.params.wave.wavelength = 8.0f;
                config.params.wave.amplitude = 128;
                config.params.wave.direction = TS_LED_EFFECT_DIR_HORIZONTAL;
                break;
            default:
                break;
        }
    } else {
        // 使用默认参数
        switch (type) {
            case TS_LED_EFFECT_PULSE:
                config.params.pulse.frequency = 0.5f;
                config.params.pulse.min_level = 20;
                config.params.pulse.max_level = 255;
                break;
            case TS_LED_EFFECT_BLINK:
                config.params.blink.on_time_ms = 500;
                config.params.blink.off_time_ms = 500;
                break;
            case TS_LED_EFFECT_BREATHING:
                config.params.breathing.frequency = 0.3f;
                config.params.breathing.min_level = 10;
                config.params.breathing.max_level = 255;
                break;
            case TS_LED_EFFECT_FADE_IN:
            case TS_LED_EFFECT_FADE_OUT:
                config.params.fade.duration_ms = 1000;
                config.params.fade.auto_remove = false;
                break;
            case TS_LED_EFFECT_COLOR_SHIFT:
                config.params.color_shift.speed = 90.0f;
                config.params.color_shift.static_shift = 0;
                break;
            case TS_LED_EFFECT_SCANLINE:
                config.params.scanline.speed = 50.0f;
                config.params.scanline.width = 3;
                config.params.scanline.direction = TS_LED_EFFECT_DIR_HORIZONTAL;
                config.params.scanline.intensity = 200;
                break;
            case TS_LED_EFFECT_WAVE:
                config.params.wave.speed = 50.0f;
                config.params.wave.wavelength = 8.0f;
                config.params.wave.amplitude = 128;
                config.params.wave.direction = TS_LED_EFFECT_DIR_HORIZONTAL;
                break;
            case TS_LED_EFFECT_GLITCH:
                config.params.glitch.intensity = 50;
                config.params.glitch.frequency = 10;
                break;
            default:
                break;
        }
    }
    
    esp_err_t ret = ts_led_layer_set_effect(layer, &config);
    if (ret != ESP_OK) {
        ts_console_error("Failed to apply filter: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    // 记录当前 filter（供保存用）
    ts_led_preset_set_current_filter(internal_name, filter_name);
    
    if (speed > 0) {
        ts_console_success("Filter '%s' applied on '%s' (speed=%d)\n", filter_name, device_name, speed);
    } else {
        ts_console_success("Filter '%s' applied on '%s'\n", filter_name, device_name);
    }
    return 0;
}

static int do_led_stop_filter(const char *device_name)
{
    if (!device_name) {
        ts_console_error("--device required\n");
        return 1;
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (!layer) {
        ts_console_error("Failed to get layer\n");
        return 1;
    }
    
    esp_err_t ret = ts_led_layer_clear_effect(layer);
    if (ret != ESP_OK) {
        ts_console_error("Failed to clear filter\n");
        return 1;
    }
    
    // 清除 filter 记录
    ts_led_preset_set_current_filter(internal_name, NULL);
    
    ts_console_success("Filter cleared on '%s'\n", device_name);
    return 0;
}

static int do_led_list_filters(bool json)
{
    if (json) {
        ts_console_printf("{\"filters\":[");
        bool first = true;
        for (int i = 0; s_filter_types[i].name; i++) {
            if (!first) ts_console_printf(",");
            ts_console_printf("{\"name\":\"%s\",\"description\":\"%s\"}",
                              s_filter_types[i].name, s_filter_types[i].description);
            first = false;
        }
        ts_console_printf("]}\n");
    } else {
        ts_console_printf("\n╭─ Post-Processing Filters ───────────────────────────────╮\n");
        for (int i = 0; s_filter_types[i].name; i++) {
            ts_console_printf("│ %-14s  %-40s │\n", 
                              s_filter_types[i].name, 
                              s_filter_types[i].description);
        }
        ts_console_printf("╰──────────────────────────────────────────────────────────╯\n");
        ts_console_printf("\nUsage: led --filter -d <device> --filter-name <name> [--speed <1-100>]\n");
        ts_console_printf("       led --stop-filter -d <device>\n");
    }
    return 0;
}

/*===========================================================================*/
/*                         Command: led --on/--off                            */
/*===========================================================================*/

static int do_led_on(const char *device_name, const char *color_str)
{
    if (!device_name) {
        ts_console_error("--device required\n");
        return 1;
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    // 解析颜色，默认白色
    ts_led_rgb_t color = {255, 255, 255};  // 默认白色
    if (color_str) {
        if (ts_led_parse_color(color_str, &color) != ESP_OK) {
            ts_console_error("Invalid color: %s\n", color_str);
            return 1;
        }
    }
    
    // 直接填充设备 framebuffer（绕过 layer 系统）
    esp_err_t ret = ts_led_device_fill(dev, color);
    if (ret != ESP_OK) {
        ts_console_error("Failed to fill color\n");
        return 1;
    }
    
    // 立即刷新到硬件
    ret = ts_led_device_refresh(dev);
    if (ret != ESP_OK) {
        ts_console_error("Failed to refresh LED\n");
        return 1;
    }
    
    ts_console_success("LED '%s' on: #%02X%02X%02X\n", 
        device_name, color.r, color.g, color.b);
    return 0;
}

static int do_led_off(const char *device_name)
{
    return do_led_clear(device_name);
}

/*===========================================================================*/
/*                        Command: led --list-effects                         */
/*===========================================================================*/

static int do_led_list_effects(const char *device_name, bool json)
{
    const char *names[32];
    
    if (device_name) {
        // 按设备类型列出适用的特效
        ts_led_layout_t layout;
        
        if (strcmp(device_name, "touch") == 0) {
            layout = TS_LED_LAYOUT_STRIP;  // 点光源视为 strip
        } else if (strcmp(device_name, "board") == 0) {
            layout = TS_LED_LAYOUT_RING;
        } else if (strcmp(device_name, "matrix") == 0) {
            layout = TS_LED_LAYOUT_MATRIX;
        } else {
            ts_console_error("Unknown device: %s\n", device_name);
            return 1;
        }
        
        size_t count = ts_led_animation_list_for_device(layout, names, 32);
        
        if (json) {
            ts_console_printf("{\"device\":\"%s\",\"effects\":[", device_name);
            for (size_t i = 0; i < count; i++) {
                ts_console_printf("%s\"%s\"", i > 0 ? "," : "", names[i]);
            }
            ts_console_printf("]}\n");
        } else {
            ts_console_printf("Effects for '%s':\n", device_name);
            for (size_t i = 0; i < count; i++) {
                ts_console_printf("  - %s\n", names[i]);
            }
        }
    } else {
        // 分类列出所有特效
        if (json) {
            ts_console_printf("{");
            
            // Touch 特效
            size_t count = ts_led_animation_list_for_device(TS_LED_LAYOUT_STRIP, names, 32);
            ts_console_printf("\"touch\":[");
            for (size_t i = 0; i < count; i++) {
                ts_console_printf("%s\"%s\"", i > 0 ? "," : "", names[i]);
            }
            ts_console_printf("],");
            
            // Board 特效
            count = ts_led_animation_list_for_device(TS_LED_LAYOUT_RING, names, 32);
            ts_console_printf("\"board\":[");
            for (size_t i = 0; i < count; i++) {
                ts_console_printf("%s\"%s\"", i > 0 ? "," : "", names[i]);
            }
            ts_console_printf("],");
            
            // Matrix 特效
            count = ts_led_animation_list_for_device(TS_LED_LAYOUT_MATRIX, names, 32);
            ts_console_printf("\"matrix\":[");
            for (size_t i = 0; i < count; i++) {
                ts_console_printf("%s\"%s\"", i > 0 ? "," : "", names[i]);
            }
            ts_console_printf("]}\n");
        } else {
            ts_console_printf("Available Effects by Device Type:\n\n");
            
            // Touch 特效 (点光源)
            ts_console_printf("Touch (point light, 1 LED):\n");
            size_t count = ts_led_animation_list_for_device(TS_LED_LAYOUT_STRIP, names, 32);
            for (size_t i = 0; i < count; i++) {
                ts_console_printf("  - %s\n", names[i]);
            }
            
            // Board 特效 (环形)
            ts_console_printf("\nBoard (ring, 28 LEDs):\n");
            count = ts_led_animation_list_for_device(TS_LED_LAYOUT_RING, names, 32);
            for (size_t i = 0; i < count; i++) {
                ts_console_printf("  - %s\n", names[i]);
            }
            
            // Matrix 特效 (矩阵)
            ts_console_printf("\nMatrix (32x32 panel):\n");
            count = ts_led_animation_list_for_device(TS_LED_LAYOUT_MATRIX, names, 32);
            for (size_t i = 0; i < count; i++) {
                ts_console_printf("  - %s\n", names[i]);
            }
            ts_console_printf("\n");
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                        Command: led --parse-color                          */
/*===========================================================================*/

static int do_led_parse_color(const char *color_str, bool json)
{
    if (!color_str) {
        ts_console_error("--color required\n");
        return 1;
    }
    
    ts_led_rgb_t color;
    if (ts_led_parse_color(color_str, &color) != ESP_OK) {
        ts_console_error("Invalid color: %s\n", color_str);
        ts_console_printf("Use format: #RRGGBB or color name (red, green, blue, etc.)\n");
        return 1;
    }
    
    // 转换为 HSV
    ts_led_hsv_t hsv = ts_led_rgb_to_hsv(color);
    
    if (json) {
        ts_console_printf(
            "{\"input\":\"%s\",\"rgb\":{\"r\":%u,\"g\":%u,\"b\":%u},"
            "\"hex\":\"#%02X%02X%02X\",\"hsv\":{\"h\":%u,\"s\":%u,\"v\":%u}}\n",
            color_str, color.r, color.g, color.b,
            color.r, color.g, color.b,
            hsv.h, hsv.s, hsv.v);
    } else {
        ts_console_printf("Color: %s\n", color_str);
        ts_console_printf("  RGB: (%3u, %3u, %3u)\n", color.r, color.g, color.b);
        ts_console_printf("  Hex: #%02X%02X%02X\n", color.r, color.g, color.b);
        ts_console_printf("  HSV: (%3u, %3u, %3u)\n", hsv.h, hsv.s, hsv.v);
    }
    
    return 0;
}

/*===========================================================================*/
/*                       Command: led --save/--show-boot                      */
/*===========================================================================*/

static int do_led_save(const char *device_name)
{
    esp_err_t ret;
    
    if (device_name) {
        ret = ts_led_save_boot_config(device_name);
        if (ret != ESP_OK) {
            ts_console_error("Failed to save boot config: %s\n", esp_err_to_name(ret));
            return 1;
        }
        ts_console_success("Boot config saved for '%s'\n", device_name);
    } else {
        ret = ts_led_save_all_boot_config();
        if (ret != ESP_OK) {
            ts_console_error("Failed to save boot config\n");
            return 1;
        }
        ts_console_success("Boot config saved for all LED devices\n");
    }
    
    return 0;
}

static int do_led_clear_boot(const char *device_name)
{
    esp_err_t ret = ts_led_clear_boot_config(device_name);
    if (ret != ESP_OK) {
        ts_console_error("Failed to clear boot config\n");
        return 1;
    }
    
    if (device_name) {
        ts_console_success("Boot config cleared for '%s'\n", device_name);
    } else {
        ts_console_success("Boot config cleared for all devices\n");
    }
    return 0;
}

static int do_led_show_boot(const char *device_name, bool json)
{
    const char *devices[] = {"touch", "board", "matrix"};
    int start = 0, end = 3;
    
    if (device_name) {
        // 单个设备
        for (int i = 0; i < 3; i++) {
            if (strcmp(device_name, devices[i]) == 0) {
                start = i;
                end = i + 1;
                break;
            }
        }
    }
    
    if (json) {
        ts_console_printf("{\"boot_config\":[");
        bool first = true;
        for (int i = start; i < end; i++) {
            ts_led_boot_config_t cfg;
            if (ts_led_get_boot_config(devices[i], &cfg) == ESP_OK) {
                if (!first) ts_console_printf(",");
                ts_console_printf("{\"device\":\"%s\",\"enabled\":%s,\"animation\":\"%s\","
                    "\"filter\":\"%s\",\"image\":\"%s\",\"speed\":%d,\"brightness\":%d}",
                    devices[i],
                    cfg.enabled ? "true" : "false",
                    cfg.animation,
                    cfg.filter,
                    cfg.image_path,
                    cfg.speed,
                    cfg.brightness);
                first = false;
            }
        }
        ts_console_printf("]}\n");
    } else {
        ts_console_printf("Boot Configuration:\n\n");
        ts_console_printf("%-10s  %-8s  %-15s  %-12s  %-25s  %6s  %10s\n", 
            "DEVICE", "ENABLED", "ANIMATION", "FILTER", "IMAGE", "SPEED", "BRIGHTNESS");
        ts_console_printf("------------------------------------------------------------------------------------------------------\n");
        
        for (int i = start; i < end; i++) {
            ts_led_boot_config_t cfg;
            if (ts_led_get_boot_config(devices[i], &cfg) == ESP_OK && cfg.enabled) {
                ts_console_printf("%-10s  %-8s  %-15s  %-12s  %-25s  %6d  %10d\n",
                    devices[i],
                    cfg.enabled ? "yes" : "no",
                    cfg.animation[0] ? cfg.animation : "(none)",
                    cfg.filter[0] ? cfg.filter : "(none)",
                    cfg.image_path[0] ? cfg.image_path : "(none)",
                    cfg.speed,
                    cfg.brightness);
            } else {
                ts_console_printf("%-10s  %-8s  %-15s  %-12s  %-25s  %6s  %10s\n",
                    devices[i], "no", "-", "-", "-", "-", "-");
            }
        }
        ts_console_printf("\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                         Command: led --test                                */
/*===========================================================================*/

static int do_led_test(const char *device_name, int mode)
{
    if (!device_name) {
        device_name = "matrix";
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (!layer) {
        ts_console_error("Failed to get layer\n");
        return 1;
    }
    
    // 停止当前动画
    ts_led_animation_stop(layer);
    
    // 获取设备信息（假设矩阵32x32）
    uint16_t w = 32, h = 32;
    uint16_t count = ts_led_device_get_count(dev);
    if (count == 1024) { w = h = 32; }
    else if (count == 28) { w = 28; h = 1; }
    else { w = count; h = 1; }
    
    ts_console_printf("Testing %s (%ux%u, %u LEDs), mode=%d\n", 
                      device_name, w, h, count, mode);
    
    // 测试模式：
    // mode 0: 渐变（左上角红，右下角蓝）- 检查整体方向
    // mode 1: 前3行不同颜色 - 检查行方向
    // mode 2: 前3列不同颜色 - 检查列方向
    // mode 3: 左上角4x4红块 - 检查原点
    
    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            ts_led_rgb_t color = {0, 0, 0};
            
            switch (mode) {
                case 0: // 渐变
                    color.r = (uint8_t)(x * 255 / (w > 1 ? w - 1 : 1));
                    color.g = 0;
                    color.b = (uint8_t)(y * 255 / (h > 1 ? h - 1 : 1));
                    break;
                    
                case 1: // 行测试：前3行不同颜色
                    if (y == 0) color = (ts_led_rgb_t){255, 0, 0};       // 第0行=红
                    else if (y == 1) color = (ts_led_rgb_t){0, 255, 0};  // 第1行=绿
                    else if (y == 2) color = (ts_led_rgb_t){0, 0, 255};  // 第2行=蓝
                    break;
                    
                case 2: // 列测试：前3列不同颜色
                    if (x == 0) color = (ts_led_rgb_t){255, 0, 0};       // 第0列=红
                    else if (x == 1) color = (ts_led_rgb_t){0, 255, 0};  // 第1列=绿
                    else if (x == 2) color = (ts_led_rgb_t){0, 0, 255};  // 第2列=蓝
                    break;
                    
                case 3: // 原点测试：左上角4x4红块
                    if (x < 4 && y < 4) color = (ts_led_rgb_t){255, 0, 0};
                    break;
                    
                default:
                    color.r = (uint8_t)(x * 255 / (w > 1 ? w - 1 : 1));
                    color.b = (uint8_t)(y * 255 / (h > 1 ? h - 1 : 1));
                    break;
            }
            
            ts_led_set_pixel_xy(layer, x, y, color);
        }
    }
    
    ts_led_device_refresh(dev);
    
    const char *mode_desc[] = {
        "渐变: 左上红->右上粉->左下紫->右下蓝",
        "行测试: 顶部3行=红/绿/蓝",
        "列测试: 左侧3列=红/绿/蓝", 
        "原点: 左上角4x4红块"
    };
    ts_console_success("Test pattern %d: %s\n", mode, mode_desc[mode % 4]);
    
    return 0;
}

/*===========================================================================*/
/*                         Command: led --image                               */
/*===========================================================================*/

static int do_led_image(const char *device_name, const char *file_path, const char *center_mode)
{
    if (!device_name) {
        ts_console_error("--device required (only matrix supported)\n");
        return 1;
    }
    
    if (!file_path) {
        ts_console_error("--file required\n");
        return 1;
    }
    
    // 只支持 matrix 设备
    if (strcmp(device_name, "matrix") != 0 && strcmp(device_name, "led_matrix") != 0) {
        ts_console_error("Image display only supported on matrix device\n");
        return 1;
    }
    
    const char *internal_name = resolve_device_name(device_name);
    ts_led_device_t dev = ts_led_device_get(internal_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    // 获取 layer 并停止当前动画/特效（必须在释放图像前！）
    ts_led_layer_t layer = ts_led_layer_get(dev, 0);
    if (layer) {
        ts_led_image_animate_stop(layer);
        ts_led_animation_stop(layer);
    }
    
    // 释放之前的图像（现在安全了，动画已停止）
    if (s_current_image) {
        ts_led_image_free(s_current_image);
        s_current_image = NULL;
    }
    
    // 加载图像
    ts_console_printf("Loading image: %s\n", file_path);
    esp_err_t ret = ts_led_image_load(file_path, TS_LED_IMG_FMT_AUTO, &s_current_image);
    if (ret != ESP_OK) {
        ts_console_error("Failed to load image: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    // 获取图像信息
    ts_led_image_info_t info;
    ts_led_image_get_info(s_current_image, &info);
    ts_console_printf("Image: %ux%u, %d frame(s)\n", info.width, info.height, info.frame_count);
    
    // 显示图像
    ts_led_image_options_t opts = TS_LED_IMAGE_DEFAULT_OPTIONS();
    opts.scale = TS_LED_IMG_SCALE_FIT;
    
    // 解析居中模式
    if (center_mode) {
        if (strcmp(center_mode, "image") == 0 || strcmp(center_mode, "img") == 0) {
            opts.center = TS_LED_IMG_CENTER_IMAGE;
            ts_console_printf("Center mode: image\n");
        } else if (strcmp(center_mode, "content") == 0 || strcmp(center_mode, "auto") == 0) {
            opts.center = TS_LED_IMG_CENTER_CONTENT;
            ts_console_printf("Center mode: content\n");
        } else {
            ts_console_error("Unknown center mode: %s (use 'image' or 'content')\n", center_mode);
            return 1;
        }
    }
    
    if (info.frame_count > 1) {
        // GIF 动画
        ret = ts_led_image_animate_start(layer, s_current_image, &opts);
        if (ret != ESP_OK) {
            ts_console_error("Failed to start animation: %s\n", esp_err_to_name(ret));
            return 1;
        }
        ts_console_success("Animation started on '%s'\n", device_name);
    } else {
        // 静态图像
        ret = ts_led_image_display(layer, s_current_image, &opts);
        if (ret != ESP_OK) {
            ts_console_error("Failed to display image: %s\n", esp_err_to_name(ret));
            return 1;
        }
        ts_console_success("Image displayed on '%s'\n", device_name);
    }
    
    // 记录当前图像路径（供 --save 使用）
    ts_led_preset_set_current_image(device_name, file_path);
    
    return 0;
}

/*===========================================================================*/
/*                          Command: led --qrcode                             */
/*===========================================================================*/

static int do_led_qrcode(const char *device_name, const char *text, 
                          const char *ecc_str, const char *fg_color_str)
{
    if (!device_name) {
        device_name = "matrix";  // 默认使用 matrix 设备
    }
    
    if (!text || strlen(text) == 0) {
        ts_console_error("--text required for QR code generation\n");
        return 1;
    }
    
    // 只支持 matrix 设备
    if (strcmp(device_name, "matrix") != 0 && strcmp(device_name, "led_matrix") != 0) {
        ts_console_error("QR code only supported on matrix device (32x32)\n");
        return 1;
    }
    
    // 解析设备名称（别名 → 内部名称）
    const char *internal_name = resolve_device_name(device_name);
    
    // 解析 ECC 等级
    ts_led_qr_ecc_t ecc = TS_LED_QR_ECC_MEDIUM;  // 默认 Medium
    if (ecc_str) {
        if (ts_led_qr_ecc_parse(ecc_str, &ecc) != ESP_OK) {
            ts_console_error("Invalid ECC level: %s (use L, M, Q, or H)\n", ecc_str);
            return 1;
        }
    }
    
    // 解析前景色
    ts_led_rgb_t fg_color = TS_LED_WHITE;  // 默认白色
    if (fg_color_str) {
        if (ts_led_parse_color(fg_color_str, &fg_color) != ESP_OK) {
            ts_console_error("Invalid color: %s\n", fg_color_str);
            return 1;
        }
    }
    
    // 配置 QR 码
    ts_led_qr_config_t config = TS_LED_QR_DEFAULT_CONFIG();
    config.text = text;
    config.ecc = ecc;
    config.fg_color = fg_color;
    config.bg_color = TS_LED_BLACK;
    config.center = true;
    // 强制使用 v4（33x33 模块）以获得最大容量
    config.version_min = 4;
    config.version_max = 4;
    
    // 生成并显示
    ts_led_qr_result_t result;
    esp_err_t ret = ts_led_qrcode_show_on_device(internal_name, &config, &result);
    
    if (ret == ESP_ERR_INVALID_SIZE) {
        ts_console_error("Text too long for QR code v4 (max ~50 alphanumeric chars)\n");
        return 1;
    }
    if (ret != ESP_OK) {
        ts_console_error("Failed to generate QR code: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("QR code v%d (%dx%d) displayed, ECC=%s\n", 
                       result.version, result.size, result.size,
                       ts_led_qr_ecc_name(ecc));
    ts_console_printf("  Text: %s\n", text);
    ts_console_printf("  Remaining capacity: %d chars\n", result.data_capacity);
    
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_led(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_led_args);
    
    // 显示帮助
    if (s_led_args.help->count > 0) {
        ts_console_printf("Usage: led [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  -s, --status           Show LED device status\n");
        ts_console_printf("      --on               Turn on LED with color\n");
        ts_console_printf("      --off              Turn off LED\n");
        ts_console_printf("  -b, --brightness       Get/set brightness\n");
        ts_console_printf("  -c, --clear            Clear all LEDs on device\n");
        ts_console_printf("  -e, --effect           Start LED animation effect\n");
        ts_console_printf("      --stop-effect      Stop running animation\n");
        ts_console_printf("      --list-effects     List available animations\n");
        ts_console_printf("      --filter           Apply post-processing filter\n");
        ts_console_printf("      --stop-filter      Remove post-processing filter\n");
        ts_console_printf("      --list-filters     List available filters\n");
        ts_console_printf("      --filter-name      Filter name (pulse, blink, etc.)\n");
        ts_console_printf("      --parse-color      Parse color info\n");
        ts_console_printf("      --image            Display image on matrix\n");
        ts_console_printf("      --qrcode           Generate and display QR code\n");
        ts_console_printf("      --text <string>    Text content for QR code\n");
        ts_console_printf("      --ecc <L|M|Q|H>    QR error correction level\n");
        ts_console_printf("      --file <path>      Image file path\n");
        ts_console_printf("  -d, --device <name>    Device: touch, board, matrix\n");
        ts_console_printf("  -n, --name <effect>    Animation name\n");
        ts_console_printf("  -v, --value <0-255>    Brightness value\n");
        ts_console_printf("      --color <color>    Color: #RRGGBB or name\n");
        ts_console_printf("      --speed <1-100>    Effect speed (1=slow, 100=fast)\n");
        ts_console_printf("  -j, --json             JSON output\n");
        ts_console_printf("  -h, --help             Show this help\n\n");
        ts_console_printf("Devices:\n");
        ts_console_printf("  touch   - Single indicator LED (point light)\n");
        ts_console_printf("  board   - PCB edge ring LEDs (28 LEDs, circular)\n");
        ts_console_printf("  matrix  - LED matrix panel (32x32, grid)\n\n");
        ts_console_printf("Effects (by device type):\n");
        ts_console_printf("  Common:  rainbow, breathing, solid, sparkle\n");
        ts_console_printf("  Touch:   pulse, heartbeat, color_cycle\n");
        ts_console_printf("  Board:   chase, comet, spin, breathe_wave\n");
        ts_console_printf("  Matrix:  fire, rain, plasma, ripple\n");
        ts_console_printf("\n  Use 'led --list-effects' for all, or\n");
        ts_console_printf("       'led --list-effects --device <name>' for specific device\n\n");
        ts_console_printf("Boot Configuration:\n");
        ts_console_printf("  --save                 Save current state as boot config\n");
        ts_console_printf("  --show-boot            Show saved boot config\n");
        ts_console_printf("  --clear-boot           Clear boot config\n\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  led --status\n");
        ts_console_printf("  led --on --device touch                  (white)\n");
        ts_console_printf("  led --on --device touch --color red\n");
        ts_console_printf("  led --on --device touch --color #FF0000\n");
        ts_console_printf("  led --off --device touch\n");
        ts_console_printf("  led --brightness --device touch --value 128\n");
        ts_console_printf("  led --effect --device touch --name heartbeat\n");
        ts_console_printf("  led --effect --device board --name spin --speed 50\n");
        ts_console_printf("  led --effect --device matrix --name fire\n");
        ts_console_printf("  led --stop-effect --device touch\n");
        ts_console_printf("  led --save --device touch                (save touch)\n");
        ts_console_printf("  led --save                               (save all)\n");
        ts_console_printf("  led --show-boot                          (show saved)\n");
        ts_console_printf("  led --image --device matrix --file /sdcard/logo.png\n");
        ts_console_printf("  led --filter --device matrix --filter-name pulse\n");
        ts_console_printf("  led --filter --device matrix --filter-name blink --speed 80\n");
        ts_console_printf("  led --stop-filter --device matrix\n");
        ts_console_printf("  led --list-filters\n");
        ts_console_printf("  led --qrcode --text \"https://tianshan.io\"\n");
        ts_console_printf("  led --qrcode --text \"HELLO\" --ecc H\n");
        ts_console_printf("  led --qrcode --text \"192.168.1.1\" --color green\n");
        ts_console_printf("\nSupported image formats: PNG, BMP, JPG, GIF (animated)\n");
        ts_console_printf("\nQR Code v4 capacity (alphanumeric):\n");
        ts_console_printf("  ECC L (~7%% recovery):  114 chars\n");
        ts_console_printf("  ECC M (~15%% recovery): 90 chars\n");
        ts_console_printf("  ECC Q (~25%% recovery): 67 chars\n");
        ts_console_printf("  ECC H (~30%% recovery): 50 chars\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, s_led_args.end, argv[0]);
        return 1;
    }
    
    bool json = s_led_args.json->count > 0;
    const char *device = s_led_args.device->count > 0 ? 
                         s_led_args.device->sval[0] : NULL;
    int value = s_led_args.value->count > 0 ? 
                s_led_args.value->ival[0] : -1;
    const char *color = s_led_args.color_val->count > 0 ? 
                        s_led_args.color_val->sval[0] : NULL;
    const char *effect_name = s_led_args.effect_name->count > 0 ?
                              s_led_args.effect_name->sval[0] : NULL;
    int speed = s_led_args.speed->count > 0 ?
                s_led_args.speed->ival[0] : -1;
    const char *file_path = s_led_args.file->count > 0 ?
                            s_led_args.file->sval[0] : NULL;
    const char *center_mode = s_led_args.center->count > 0 ?
                              s_led_args.center->sval[0] : NULL;
    const char *filter_name_val = s_led_args.filter_name->count > 0 ?
                                  s_led_args.filter_name->sval[0] : NULL;
    const char *qr_text = s_led_args.text->count > 0 ?
                          s_led_args.text->sval[0] : NULL;
    const char *qr_ecc = s_led_args.ecc->count > 0 ?
                         s_led_args.ecc->sval[0] : NULL;
    
    // 生成 QR 码
    if (s_led_args.qrcode->count > 0) {
        return do_led_qrcode(device, qr_text, qr_ecc, color);
    }
    
    // 显示图像
    if (s_led_args.image->count > 0) {
        return do_led_image(device, file_path, center_mode);
    }
    
    // 应用后处理效果
    if (s_led_args.filter->count > 0) {
        return do_led_filter(device, filter_name_val, speed);
    }
    
    // 停止后处理效果
    if (s_led_args.stop_filter->count > 0) {
        return do_led_stop_filter(device);
    }
    
    // 列出后处理效果
    if (s_led_args.list_filters->count > 0) {
        return do_led_list_filters(json);
    }
    
    // 测试模式
    if (s_led_args.test->count > 0) {
        int test_mode = (value >= 0) ? value : 0;
        return do_led_test(device, test_mode);
    }
    
    // 启动特效
    if (s_led_args.effect->count > 0) {
        return do_led_effect(device, effect_name, speed, color);
    }
    
    // 停止特效
    if (s_led_args.stop_effect->count > 0) {
        return do_led_stop_effect(device);
    }
    
    // 点亮 LED
    if (s_led_args.on->count > 0) {
        return do_led_on(device, color);
    }
    
    // 关闭 LED
    if (s_led_args.off->count > 0) {
        return do_led_off(device);
    }
    
    // 亮度
    if (s_led_args.brightness->count > 0) {
        return do_led_brightness(device, value, value >= 0);
    }
    
    // 清除
    if (s_led_args.clear->count > 0) {
        return do_led_clear(device);
    }
    
    // 列出特效 (可选指定设备过滤)
    if (s_led_args.list_effects->count > 0) {
        return do_led_list_effects(device, json);
    }
    
    // 颜色解析
    if (s_led_args.parse_color->count > 0) {
        return do_led_parse_color(color, json);
    }
    
    // 保存启动配置
    if (s_led_args.save->count > 0) {
        return do_led_save(device);
    }
    
    // 清除启动配置
    if (s_led_args.clear_boot->count > 0) {
        return do_led_clear_boot(device);
    }
    
    // 显示启动配置
    if (s_led_args.show_boot->count > 0) {
        return do_led_show_boot(device, json);
    }
    
    // 默认显示状态
    return do_led_status(device, json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_led_register(void)
{
    s_led_args.status       = arg_lit0("s", "status", "Show status");
    s_led_args.on           = arg_lit0(NULL, "on", "Turn on LED");
    s_led_args.off          = arg_lit0(NULL, "off", "Turn off LED");
    s_led_args.brightness   = arg_lit0("b", "brightness", "Get/set brightness");
    s_led_args.clear        = arg_lit0("c", "clear", "Clear LEDs");
    s_led_args.effect       = arg_lit0("e", "effect", "Start animation");
    s_led_args.stop_effect  = arg_lit0(NULL, "stop-effect", "Stop animation");
    s_led_args.list_effects = arg_lit0(NULL, "list-effects", "List animations");
    s_led_args.filter       = arg_lit0(NULL, "filter", "Apply post-processing filter");
    s_led_args.stop_filter  = arg_lit0(NULL, "stop-filter", "Remove filter");
    s_led_args.list_filters = arg_lit0(NULL, "list-filters", "List filters");
    s_led_args.filter_name  = arg_str0(NULL, "filter-name", "<name>", "Filter name");
    s_led_args.parse_color  = arg_lit0(NULL, "parse-color", "Parse color info");
    s_led_args.image        = arg_lit0(NULL, "image", "Display image");
    s_led_args.qrcode       = arg_lit0(NULL, "qrcode", "Generate QR code");
    s_led_args.text         = arg_str0(NULL, "text", "<string>", "QR code text content");
    s_led_args.ecc          = arg_str0(NULL, "ecc", "<L|M|Q|H>", "QR error correction");
    s_led_args.test         = arg_lit0("t", "test", "Show test pattern");
    s_led_args.save         = arg_lit0(NULL, "save", "Save as boot config");
    s_led_args.clear_boot   = arg_lit0(NULL, "clear-boot", "Clear boot config");
    s_led_args.show_boot    = arg_lit0(NULL, "show-boot", "Show boot config");
    s_led_args.device       = arg_str0("d", "device", "<name>", "Device name");
    s_led_args.file         = arg_str0("f", "file", "<path>", "Image file path");
    s_led_args.center       = arg_str0(NULL, "center", "<mode>", "Center mode: image, content");
    s_led_args.value        = arg_int0("v", "value", "<0-255>", "Value/test mode");
    s_led_args.color_val    = arg_str0(NULL, "color", "<color>", "Color value");
    s_led_args.effect_name  = arg_str0("n", "name", "<effect>", "Effect name");
    s_led_args.speed        = arg_int0(NULL, "speed", "<1-100>", "Effect speed (1=slow, 100=fast)");
    s_led_args.json         = arg_lit0("j", "json", "JSON output");
    s_led_args.help         = arg_lit0("h", "help", "Show help");
    s_led_args.end          = arg_end(20);
    
    const ts_console_cmd_t cmd = {
        .command = "led",
        .help = "LED control and effects",
        .hint = NULL,
        .category = TS_CMD_CAT_LED,
        .func = cmd_led,
        .argtable = &s_led_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "LED commands registered");
    }
    
    return ret;
}
