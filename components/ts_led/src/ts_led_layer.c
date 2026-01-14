/**
 * @file ts_led_layer.c
 * @brief LED Layer Management
 */

#include "ts_led.h"
#include "ts_log.h"
#include <stdlib.h>
#include <string.h>

#define TAG "led_layer"

typedef struct ts_led_device ts_led_device_impl_t;

struct ts_led_layer {
    ts_led_device_impl_t *device;
    ts_led_rgb_t *buffer;
    ts_led_layer_config_t config;
    ts_led_effect_fn_t effect_fn;
    void *effect_data;
    uint32_t effect_interval;
    uint32_t effect_last_time;
    bool used;
};

esp_err_t ts_led_layer_create(ts_led_device_t device, 
                               const ts_led_layer_config_t *config,
                               ts_led_layer_t *layer)
{
    if (!device || !layer) return ESP_ERR_INVALID_ARG;
    ts_led_device_impl_t *dev = (ts_led_device_impl_t *)device;
    
    if (dev->layer_count >= TS_LED_MAX_LAYERS) return ESP_ERR_NO_MEM;
    
    struct ts_led_layer *l = calloc(1, sizeof(struct ts_led_layer));
    if (!l) return ESP_ERR_NO_MEM;
    
    l->buffer = calloc(dev->config.led_count, sizeof(ts_led_rgb_t));
    if (!l->buffer) {
        free(l);
        return ESP_ERR_NO_MEM;
    }
    
    l->device = dev;
    l->config = config ? *config : (ts_led_layer_config_t)TS_LED_LAYER_DEFAULT_CONFIG();
    l->used = true;
    
    dev->layers[dev->layer_count++] = l;
    *layer = l;
    
    return ESP_OK;
}

esp_err_t ts_led_layer_destroy(ts_led_layer_t layer)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    struct ts_led_layer *l = layer;
    
    free(l->buffer);
    free(l);
    return ESP_OK;
}

esp_err_t ts_led_layer_set_blend(ts_led_layer_t layer, ts_led_blend_t mode)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    layer->config.blend_mode = mode;
    return ESP_OK;
}

esp_err_t ts_led_layer_set_opacity(ts_led_layer_t layer, uint8_t opacity)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    layer->config.opacity = opacity;
    return ESP_OK;
}

esp_err_t ts_led_layer_set_visible(ts_led_layer_t layer, bool visible)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    layer->config.visible = visible;
    return ESP_OK;
}

esp_err_t ts_led_layer_clear(ts_led_layer_t layer)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    memset(layer->buffer, 0, layer->device->config.led_count * sizeof(ts_led_rgb_t));
    return ESP_OK;
}

/* Drawing operations */
esp_err_t ts_led_set_pixel(ts_led_layer_t layer, uint16_t index, ts_led_rgb_t color)
{
    if (!layer || index >= layer->device->config.led_count) return ESP_ERR_INVALID_ARG;
    layer->buffer[index] = color;
    return ESP_OK;
}

esp_err_t ts_led_set_pixel_xy(ts_led_layer_t layer, uint16_t x, uint16_t y, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_device_impl_t *dev = layer->device;
    
    if (x >= dev->config.width || y >= dev->config.height) return ESP_ERR_INVALID_ARG;
    
    uint16_t index;
    switch (dev->config.scan) {
        case TS_LED_SCAN_ZIGZAG_ROWS:
            index = (y % 2 == 0) ? y * dev->config.width + x 
                                 : y * dev->config.width + (dev->config.width - 1 - x);
            break;
        default:
            index = y * dev->config.width + x;
    }
    
    return ts_led_set_pixel(layer, index, color);
}

esp_err_t ts_led_fill(ts_led_layer_t layer, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < layer->device->config.led_count; i++) {
        layer->buffer[i] = color;
    }
    return ESP_OK;
}

esp_err_t ts_led_fill_range(ts_led_layer_t layer, uint16_t start, uint16_t count, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    uint16_t end = start + count;
    if (end > layer->device->config.led_count) end = layer->device->config.led_count;
    
    for (uint16_t i = start; i < end; i++) {
        layer->buffer[i] = color;
    }
    return ESP_OK;
}

esp_err_t ts_led_fill_rect(ts_led_layer_t layer, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    for (uint16_t dy = 0; dy < h; dy++) {
        for (uint16_t dx = 0; dx < w; dx++) {
            ts_led_set_pixel_xy(layer, x + dx, y + dy, color);
        }
    }
    return ESP_OK;
}

esp_err_t ts_led_draw_line(ts_led_layer_t layer, int16_t x0, int16_t y0,
                            int16_t x1, int16_t y1, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    
    int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;
    
    while (1) {
        ts_led_set_pixel_xy(layer, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return ESP_OK;
}

esp_err_t ts_led_draw_circle(ts_led_layer_t layer, int16_t cx, int16_t cy,
                              int16_t r, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    
    int16_t x = -r, y = 0, err = 2 - 2 * r;
    do {
        ts_led_set_pixel_xy(layer, cx - x, cy + y, color);
        ts_led_set_pixel_xy(layer, cx - y, cy - x, color);
        ts_led_set_pixel_xy(layer, cx + x, cy - y, color);
        ts_led_set_pixel_xy(layer, cx + y, cy + x, color);
        r = err;
        if (r <= y) err += ++y * 2 + 1;
        if (r > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
    
    return ESP_OK;
}

esp_err_t ts_led_gradient(ts_led_layer_t layer, uint16_t start, uint16_t count,
                           ts_led_rgb_t color1, ts_led_rgb_t color2)
{
    if (!layer || count == 0) return ESP_ERR_INVALID_ARG;
    
    for (uint16_t i = 0; i < count; i++) {
        uint8_t t = (i * 255) / (count - 1);
        ts_led_rgb_t c = {
            .r = color1.r + ((color2.r - color1.r) * t) / 255,
            .g = color1.g + ((color2.g - color1.g) * t) / 255,
            .b = color1.b + ((color2.b - color1.b) * t) / 255
        };
        ts_led_set_pixel(layer, start + i, c);
    }
    return ESP_OK;
}
