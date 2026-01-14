/**
 * @file ts_led_effects.c
 * @brief Built-in LED Effects
 */

#include "ts_led.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

/* Effect: Rainbow */
static void effect_rainbow(ts_led_layer_t layer, uint32_t time_ms, void *data)
{
    uint16_t count = layer->device->config.led_count;
    uint8_t offset = (time_ms / 20) & 0xFF;
    
    for (int i = 0; i < count; i++) {
        ts_led_rgb_t c = ts_led_color_wheel((i * 256 / count + offset) & 0xFF);
        ts_led_set_pixel(layer, i, c);
    }
}

/* Effect: Breathing */
static void effect_breathing(ts_led_layer_t layer, uint32_t time_ms, void *data)
{
    ts_led_rgb_t *color = (ts_led_rgb_t *)data;
    ts_led_rgb_t c = color ? *color : TS_LED_WHITE;
    
    float phase = (time_ms % 2000) / 2000.0f * 3.14159f * 2;
    uint8_t brightness = (uint8_t)((sinf(phase) + 1.0f) * 127);
    
    ts_led_rgb_t scaled = ts_led_scale_color(c, brightness);
    ts_led_fill(layer, scaled);
}

/* Effect: Chase */
static void effect_chase(ts_led_layer_t layer, uint32_t time_ms, void *data)
{
    uint16_t count = layer->device->config.led_count;
    uint16_t pos = (time_ms / 50) % count;
    
    ts_led_fill(layer, TS_LED_BLACK);
    
    for (int i = 0; i < 3; i++) {
        uint16_t idx = (pos + i) % count;
        uint8_t fade = 255 - i * 80;
        ts_led_set_pixel(layer, idx, ts_led_scale_color(TS_LED_WHITE, fade));
    }
}

/* Effect: Fire */
static void effect_fire(ts_led_layer_t layer, uint32_t time_ms, void *data)
{
    uint16_t count = layer->device->config.led_count;
    static uint8_t heat[256];
    
    for (int i = 0; i < count; i++) {
        heat[i] = (heat[i] > 30) ? heat[i] - (esp_random() & 31) : 0;
    }
    
    for (int i = count - 1; i >= 2; i--) {
        heat[i] = (heat[i - 1] + heat[i - 2] + heat[i - 2]) / 3;
    }
    
    if ((esp_random() & 0xFF) < 120) {
        int y = esp_random() % 7;
        heat[y] = heat[y] + 160 + (esp_random() & 63);
    }
    
    for (int i = 0; i < count; i++) {
        uint8_t t = heat[i];
        ts_led_rgb_t c;
        if (t < 85) {
            c = TS_LED_RGB(t * 3, 0, 0);
        } else if (t < 170) {
            c = TS_LED_RGB(255, (t - 85) * 3, 0);
        } else {
            c = TS_LED_RGB(255, 255, (t - 170) * 3);
        }
        ts_led_set_pixel(layer, i, c);
    }
}

/* Effect: Sparkle */
static void effect_sparkle(ts_led_layer_t layer, uint32_t time_ms, void *data)
{
    uint16_t count = layer->device->config.led_count;
    
    for (int i = 0; i < count; i++) {
        layer->buffer[i] = ts_led_scale_color(layer->buffer[i], 200);
    }
    
    if ((esp_random() & 0x0F) == 0) {
        int pos = esp_random() % count;
        ts_led_set_pixel(layer, pos, TS_LED_WHITE);
    }
}

/* Effect: Solid Color */
static void effect_solid(ts_led_layer_t layer, uint32_t time_ms, void *data)
{
    ts_led_rgb_t *color = (ts_led_rgb_t *)data;
    ts_led_fill(layer, color ? *color : TS_LED_WHITE);
}

/* Built-in effects table */
static const ts_led_effect_t s_builtin_effects[] = {
    {"rainbow", effect_rainbow, 20, NULL},
    {"breathing", effect_breathing, 20, NULL},
    {"chase", effect_chase, 50, NULL},
    {"fire", effect_fire, 30, NULL},
    {"sparkle", effect_sparkle, 30, NULL},
    {"solid", effect_solid, 100, NULL},
    {NULL, NULL, 0, NULL}
};

const ts_led_effect_t *ts_led_effect_get_builtin(const char *name)
{
    for (int i = 0; s_builtin_effects[i].name; i++) {
        if (strcmp(s_builtin_effects[i].name, name) == 0) {
            return &s_builtin_effects[i];
        }
    }
    return NULL;
}

size_t ts_led_effect_list_builtin(const char **names, size_t max_names)
{
    size_t count = 0;
    for (int i = 0; s_builtin_effects[i].name && count < max_names; i++) {
        if (names) names[count] = s_builtin_effects[i].name;
        count++;
    }
    return count;
}

esp_err_t ts_led_effect_start(ts_led_layer_t layer, const ts_led_effect_t *effect)
{
    if (!layer || !effect) return ESP_ERR_INVALID_ARG;
    
    layer->effect_fn = effect->func;
    layer->effect_data = effect->user_data;
    layer->effect_interval = effect->frame_interval_ms;
    layer->effect_last_time = 0;
    
    return ESP_OK;
}

esp_err_t ts_led_effect_stop(ts_led_layer_t layer)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    layer->effect_fn = NULL;
    return ESP_OK;
}
