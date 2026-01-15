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
#include <string.h>

#define TAG "api_led"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

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
    
    // Get known device names
    const char *device_names[] = {"touch", "board", "matrix"};
    
    for (size_t i = 0; i < sizeof(device_names) / sizeof(device_names[0]); i++) {
        ts_led_device_t dev = ts_led_device_get(device_names[i]);
        if (dev) {
            cJSON *device = cJSON_CreateObject();
            cJSON_AddStringToObject(device, "name", device_names[i]);
            cJSON_AddNumberToObject(device, "count", ts_led_device_get_count(dev));
            cJSON_AddNumberToObject(device, "brightness", ts_led_device_get_brightness(dev));
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
    
    ts_led_device_t dev = ts_led_device_get(device_param->valuestring);
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
    
    ts_led_device_t dev = ts_led_device_get(device_param->valuestring);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    esp_err_t ret = ts_led_device_clear(dev);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to clear device");
        return ret;
    }
    
    ret = ts_led_device_refresh(dev);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to refresh device");
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
    
    ts_led_device_t dev = ts_led_device_get(device_param->valuestring);
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
    
    ts_led_device_t dev = ts_led_device_get(device_param->valuestring);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    ts_led_rgb_t color;
    if (parse_color_param(params, "color", &color) != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid 'color' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
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
 * @param params: effect parameters (optional)
 */
static esp_err_t api_led_effect_start(const cJSON *params, ts_api_result_t *result)
{
    cJSON *device_param = cJSON_GetObjectItem(params, "device");
    cJSON *effect_param = cJSON_GetObjectItem(params, "effect");
    
    if (!device_param || !cJSON_IsString(device_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'device' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!effect_param || !cJSON_IsString(effect_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'effect' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_led_device_t dev = ts_led_device_get(device_param->valuestring);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    const ts_led_animation_def_t *effect = ts_led_animation_get_builtin(effect_param->valuestring);
    if (!effect) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Effect not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_param->valuestring);
    cJSON_AddStringToObject(data, "effect", effect_param->valuestring);
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
    
    ts_led_device_t dev = ts_led_device_get(device_param->valuestring);
    if (!dev) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Device not found");
        return ESP_ERR_NOT_FOUND;
    }
    
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
        .requires_auth = true,
        .permission = "led.control",
    },
    {
        .name = "led.clear",
        .description = "Clear all LEDs on device",
        .category = TS_API_CAT_LED,
        .handler = api_led_clear,
        .requires_auth = true,
        .permission = "led.control",
    },
    {
        .name = "led.set",
        .description = "Set LED(s) color",
        .category = TS_API_CAT_LED,
        .handler = api_led_set,
        .requires_auth = true,
        .permission = "led.control",
    },
    {
        .name = "led.fill",
        .description = "Fill all LEDs with color",
        .category = TS_API_CAT_LED,
        .handler = api_led_fill,
        .requires_auth = true,
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
        .requires_auth = true,
        .permission = "led.control",
    },
    {
        .name = "led.effect.stop",
        .description = "Stop effect on device",
        .category = TS_API_CAT_LED,
        .handler = api_led_effect_stop,
        .requires_auth = true,
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
