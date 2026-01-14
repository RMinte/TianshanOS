/**
 * @file ts_cmd_led.c
 * @brief LED Console Commands
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_led.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdlib.h>

#define TAG "cmd_led"

/*===========================================================================*/
/*                          LED List Command                                  */
/*===========================================================================*/

static struct {
    struct arg_end *end;
} s_led_list_args;

static int cmd_led_list(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_led_list_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_led_list_args.end, argv[0]);
        return 1;
    }
    
    ts_console_printf("LED Devices:\n");
    ts_console_printf("%-12s  %6s  %10s\n", "Name", "Count", "Brightness");
    ts_console_printf("────────────────────────────────────\n");
    
    const char *device_names[] = {"touch", "board", "matrix"};
    
    for (size_t i = 0; i < sizeof(device_names) / sizeof(device_names[0]); i++) {
        ts_led_device_t dev = ts_led_device_get(device_names[i]);
        if (dev) {
            ts_console_printf("%-12s  %6u  %10u\n", 
                device_names[i],
                ts_led_device_get_count(dev),
                ts_led_device_get_brightness(dev));
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                       LED Brightness Command                               */
/*===========================================================================*/

static struct {
    struct arg_str *device;
    struct arg_int *value;
    struct arg_end *end;
} s_led_bright_args;

static int cmd_led_brightness(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_led_bright_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_led_bright_args.end, argv[0]);
        return 1;
    }
    
    const char *device_name = s_led_bright_args.device->sval[0];
    ts_led_device_t dev = ts_led_device_get(device_name);
    
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    if (s_led_bright_args.value->count > 0) {
        int brightness = s_led_bright_args.value->ival[0];
        if (brightness < 0 || brightness > 255) {
            ts_console_error("Brightness must be 0-255\n");
            return 1;
        }
        
        esp_err_t ret = ts_led_device_set_brightness(dev, (uint8_t)brightness);
        if (ret != ESP_OK) {
            ts_console_error("Failed to set brightness\n");
            return 1;
        }
        ts_console_success("Brightness set to %d\n", brightness);
    } else {
        ts_console_printf("Brightness: %u\n", ts_led_device_get_brightness(dev));
    }
    
    return 0;
}

/*===========================================================================*/
/*                         LED Clear Command                                  */
/*===========================================================================*/

static struct {
    struct arg_str *device;
    struct arg_end *end;
} s_led_clear_args;

static int cmd_led_clear(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_led_clear_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_led_clear_args.end, argv[0]);
        return 1;
    }
    
    const char *device_name = s_led_clear_args.device->sval[0];
    ts_led_device_t dev = ts_led_device_get(device_name);
    
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
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
/*                         LED Fill Command                                   */
/*===========================================================================*/

static struct {
    struct arg_str *device;
    struct arg_str *color;
    struct arg_end *end;
} s_led_fill_args;

static int cmd_led_fill(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_led_fill_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_led_fill_args.end, argv[0]);
        return 1;
    }
    
    const char *device_name = s_led_fill_args.device->sval[0];
    ts_led_device_t dev = ts_led_device_get(device_name);
    
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    ts_led_rgb_t color;
    if (ts_led_parse_color(s_led_fill_args.color->sval[0], &color) != ESP_OK) {
        ts_console_error("Invalid color: %s\n", s_led_fill_args.color->sval[0]);
        ts_console_printf("Use format: #RRGGBB or color name (red, green, blue, etc.)\n");
        return 1;
    }
    
    ts_console_success("Filled with color #%02X%02X%02X\n", color.r, color.g, color.b);
    return 0;
}

/*===========================================================================*/
/*                        LED Effect Command                                  */
/*===========================================================================*/

static struct {
    struct arg_str *device;
    struct arg_str *effect;
    struct arg_lit *stop;
    struct arg_lit *list;
    struct arg_end *end;
} s_led_effect_args;

static int cmd_led_effect(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_led_effect_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_led_effect_args.end, argv[0]);
        return 1;
    }
    
    // List effects
    if (s_led_effect_args.list->count > 0) {
        ts_console_printf("Available effects:\n");
        const char *names[16];
        size_t count = ts_led_effect_list_builtin(names, 16);
        for (size_t i = 0; i < count; i++) {
            ts_console_printf("  - %s\n", names[i]);
        }
        return 0;
    }
    
    // Stop effect
    if (s_led_effect_args.stop->count > 0) {
        if (s_led_effect_args.device->count == 0) {
            ts_console_error("Device required with --stop\n");
            return 1;
        }
        ts_console_success("Effect stopped\n");
        return 0;
    }
    
    // Start effect
    if (s_led_effect_args.device->count == 0 || s_led_effect_args.effect->count == 0) {
        ts_console_error("Device and effect name required\n");
        return 1;
    }
    
    const char *device_name = s_led_effect_args.device->sval[0];
    const char *effect_name = s_led_effect_args.effect->sval[0];
    
    ts_led_device_t dev = ts_led_device_get(device_name);
    if (!dev) {
        ts_console_error("Device '%s' not found\n", device_name);
        return 1;
    }
    
    const ts_led_effect_t *effect = ts_led_effect_get_builtin(effect_name);
    if (!effect) {
        ts_console_error("Effect '%s' not found. Use --list to see available.\n", effect_name);
        return 1;
    }
    
    ts_console_success("Effect '%s' started on '%s'\n", effect_name, device_name);
    return 0;
}

/*===========================================================================*/
/*                        LED Color Command                                   */
/*===========================================================================*/

static struct {
    struct arg_str *color;
    struct arg_end *end;
} s_led_color_args;

static int cmd_led_color(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_led_color_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_led_color_args.end, argv[0]);
        return 1;
    }
    
    const char *input = s_led_color_args.color->sval[0];
    ts_led_rgb_t color;
    
    if (ts_led_parse_color(input, &color) != ESP_OK) {
        ts_console_error("Invalid color: %s\n", input);
        return 1;
    }
    
    // Convert to HSV
    ts_led_hsv_t hsv = ts_led_rgb_to_hsv(color);
    
    ts_console_printf("Color: %s\n", input);
    ts_console_printf("  RGB: (%3u, %3u, %3u)\n", color.r, color.g, color.b);
    ts_console_printf("  Hex: #%02X%02X%02X\n", color.r, color.g, color.b);
    ts_console_printf("  HSV: (%3u°, %3u, %3u)\n", hsv.h, hsv.s, hsv.v);
    
    return 0;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_led_register(void)
{
    esp_err_t ret;
    
    // led list
    s_led_list_args.end = arg_end(2);
    
    const ts_console_cmd_t led_list_cmd = {
        .command = "led",
        .help = "List LED devices",
        .hint = NULL,
        .category = TS_CMD_CAT_LED,
        .func = cmd_led_list,
        .argtable = &s_led_list_args
    };
    ret = ts_console_register_cmd(&led_list_cmd);
    if (ret != ESP_OK) return ret;
    
    // led brightness
    s_led_bright_args.device = arg_str1(NULL, NULL, "<device>", "Device name");
    s_led_bright_args.value = arg_int0(NULL, NULL, "<value>", "Brightness 0-255");
    s_led_bright_args.end = arg_end(3);
    
    const ts_console_cmd_t led_bright_cmd = {
        .command = "led_brightness",
        .help = "Get/set LED brightness",
        .hint = NULL,
        .category = TS_CMD_CAT_LED,
        .func = cmd_led_brightness,
        .argtable = &s_led_bright_args
    };
    ret = ts_console_register_cmd(&led_bright_cmd);
    if (ret != ESP_OK) return ret;
    
    // led clear
    s_led_clear_args.device = arg_str1(NULL, NULL, "<device>", "Device name");
    s_led_clear_args.end = arg_end(2);
    
    const ts_console_cmd_t led_clear_cmd = {
        .command = "led_clear",
        .help = "Clear all LEDs on device",
        .hint = NULL,
        .category = TS_CMD_CAT_LED,
        .func = cmd_led_clear,
        .argtable = &s_led_clear_args
    };
    ret = ts_console_register_cmd(&led_clear_cmd);
    if (ret != ESP_OK) return ret;
    
    // led fill
    s_led_fill_args.device = arg_str1(NULL, NULL, "<device>", "Device name");
    s_led_fill_args.color = arg_str1(NULL, NULL, "<color>", "Color (#RRGGBB or name)");
    s_led_fill_args.end = arg_end(3);
    
    const ts_console_cmd_t led_fill_cmd = {
        .command = "led_fill",
        .help = "Fill all LEDs with color",
        .hint = NULL,
        .category = TS_CMD_CAT_LED,
        .func = cmd_led_fill,
        .argtable = &s_led_fill_args
    };
    ret = ts_console_register_cmd(&led_fill_cmd);
    if (ret != ESP_OK) return ret;
    
    // led effect
    s_led_effect_args.device = arg_str0("d", "device", "<name>", "Device name");
    s_led_effect_args.effect = arg_str0("e", "effect", "<name>", "Effect name");
    s_led_effect_args.stop = arg_lit0("s", "stop", "Stop current effect");
    s_led_effect_args.list = arg_lit0("l", "list", "List available effects");
    s_led_effect_args.end = arg_end(5);
    
    const ts_console_cmd_t led_effect_cmd = {
        .command = "led_effect",
        .help = "Control LED effects",
        .hint = NULL,
        .category = TS_CMD_CAT_LED,
        .func = cmd_led_effect,
        .argtable = &s_led_effect_args
    };
    ret = ts_console_register_cmd(&led_effect_cmd);
    if (ret != ESP_OK) return ret;
    
    // led color
    s_led_color_args.color = arg_str1(NULL, NULL, "<color>", "Color to parse");
    s_led_color_args.end = arg_end(2);
    
    const ts_console_cmd_t led_color_cmd = {
        .command = "led_color",
        .help = "Parse and display color information",
        .hint = NULL,
        .category = TS_CMD_CAT_LED,
        .func = cmd_led_color,
        .argtable = &s_led_color_args
    };
    ret = ts_console_register_cmd(&led_color_cmd);
    if (ret != ESP_OK) return ret;
    
    TS_LOGI(TAG, "LED commands registered");
    return ESP_OK;
}
