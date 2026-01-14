/**
 * @file ts_led_color.c
 * @brief Color Utilities
 */

#include "ts_led.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

ts_led_rgb_t ts_led_hsv_to_rgb(ts_led_hsv_t hsv)
{
    ts_led_rgb_t rgb = {0};
    uint8_t region, remainder, p, q, t;
    
    if (hsv.s == 0) {
        rgb.r = rgb.g = rgb.b = hsv.v;
        return rgb;
    }
    
    region = hsv.h / 60;
    remainder = (hsv.h - (region * 60)) * 255 / 60;
    
    p = (hsv.v * (255 - hsv.s)) >> 8;
    q = (hsv.v * (255 - ((hsv.s * remainder) >> 8))) >> 8;
    t = (hsv.v * (255 - ((hsv.s * (255 - remainder)) >> 8))) >> 8;
    
    switch (region) {
        case 0:  rgb.r = hsv.v; rgb.g = t; rgb.b = p; break;
        case 1:  rgb.r = q; rgb.g = hsv.v; rgb.b = p; break;
        case 2:  rgb.r = p; rgb.g = hsv.v; rgb.b = t; break;
        case 3:  rgb.r = p; rgb.g = q; rgb.b = hsv.v; break;
        case 4:  rgb.r = t; rgb.g = p; rgb.b = hsv.v; break;
        default: rgb.r = hsv.v; rgb.g = p; rgb.b = q; break;
    }
    return rgb;
}

ts_led_hsv_t ts_led_rgb_to_hsv(ts_led_rgb_t rgb)
{
    ts_led_hsv_t hsv = {0};
    uint8_t min, max, delta;
    
    min = rgb.r < rgb.g ? (rgb.r < rgb.b ? rgb.r : rgb.b) : (rgb.g < rgb.b ? rgb.g : rgb.b);
    max = rgb.r > rgb.g ? (rgb.r > rgb.b ? rgb.r : rgb.b) : (rgb.g > rgb.b ? rgb.g : rgb.b);
    
    hsv.v = max;
    delta = max - min;
    
    if (max == 0 || delta == 0) {
        hsv.s = 0;
        hsv.h = 0;
        return hsv;
    }
    
    hsv.s = (delta * 255) / max;
    
    if (rgb.r == max) {
        hsv.h = 60 * (((int)rgb.g - rgb.b) / delta);
    } else if (rgb.g == max) {
        hsv.h = 60 * (2 + ((int)rgb.b - rgb.r) / delta);
    } else {
        hsv.h = 60 * (4 + ((int)rgb.r - rgb.g) / delta);
    }
    
    if (hsv.h < 0) hsv.h += 360;
    return hsv;
}

ts_led_rgb_t ts_led_blend_colors(ts_led_rgb_t c1, ts_led_rgb_t c2, uint8_t amount)
{
    return (ts_led_rgb_t){
        .r = c1.r + ((c2.r - c1.r) * amount) / 255,
        .g = c1.g + ((c2.g - c1.g) * amount) / 255,
        .b = c1.b + ((c2.b - c1.b) * amount) / 255
    };
}

ts_led_rgb_t ts_led_scale_color(ts_led_rgb_t color, uint8_t scale)
{
    return (ts_led_rgb_t){
        .r = (color.r * scale) >> 8,
        .g = (color.g * scale) >> 8,
        .b = (color.b * scale) >> 8
    };
}

ts_led_rgb_t ts_led_color_wheel(uint8_t pos)
{
    pos = 255 - pos;
    if (pos < 85) {
        return TS_LED_RGB(255 - pos * 3, 0, pos * 3);
    } else if (pos < 170) {
        pos -= 85;
        return TS_LED_RGB(0, pos * 3, 255 - pos * 3);
    } else {
        pos -= 170;
        return TS_LED_RGB(pos * 3, 255 - pos * 3, 0);
    }
}

static const struct { const char *name; ts_led_rgb_t color; } s_colors[] = {
    {"black", {0, 0, 0}}, {"white", {255, 255, 255}},
    {"red", {255, 0, 0}}, {"green", {0, 255, 0}}, {"blue", {0, 0, 255}},
    {"yellow", {255, 255, 0}}, {"cyan", {0, 255, 255}}, {"magenta", {255, 0, 255}},
    {"orange", {255, 165, 0}}, {"purple", {128, 0, 128}}, {"pink", {255, 192, 203}},
    {NULL, {0, 0, 0}}
};

esp_err_t ts_led_parse_color(const char *str, ts_led_rgb_t *color)
{
    if (!str || !color) return ESP_ERR_INVALID_ARG;
    
    if (str[0] == '#' && strlen(str) == 7) {
        unsigned int r, g, b;
        if (sscanf(str + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            *color = TS_LED_RGB(r, g, b);
            return ESP_OK;
        }
    }
    
    for (int i = 0; s_colors[i].name; i++) {
        if (strcasecmp(str, s_colors[i].name) == 0) {
            *color = s_colors[i].color;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}
