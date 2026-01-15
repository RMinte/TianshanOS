/**
 * @file ts_led_layer.c
 * @brief LED Layer Management
 */

#include "ts_led_private.h"
#include "ts_log.h"
#include <stdlib.h>
#include <string.h>

#define TAG "led_layer"

esp_err_t ts_led_layer_create(ts_led_device_t device, 
                               const ts_led_layer_config_t *config,
                               ts_led_layer_t *layer)
{
    if (!device || !layer) return ESP_ERR_INVALID_ARG;
    ts_led_device_impl_t *dev = (ts_led_device_impl_t *)device;
    
    if (dev->layer_count >= TS_LED_MAX_LAYERS) return ESP_ERR_NO_MEM;
    
    ts_led_layer_impl_t *l = calloc(1, sizeof(ts_led_layer_impl_t));
    if (!l) return ESP_ERR_NO_MEM;
    
    l->buffer = calloc(dev->config.led_count, sizeof(ts_led_rgb_t));
    if (!l->buffer) {
        free(l);
        return ESP_ERR_NO_MEM;
    }
    
    l->device = dev;
    l->size = dev->config.led_count;
    if (config) {
        l->blend_mode = config->blend_mode;
        l->opacity = config->opacity;
        l->visible = config->visible;
    } else {
        l->blend_mode = TS_LED_BLEND_NORMAL;
        l->opacity = 255;
        l->visible = true;
    }
    
    dev->layers[dev->layer_count++] = l;
    *layer = l;
    
    return ESP_OK;
}

ts_led_layer_t ts_led_layer_get(ts_led_device_t device, uint8_t index)
{
    if (!device) return NULL;
    ts_led_device_impl_t *dev = (ts_led_device_impl_t *)device;
    
    // Return existing layer if available
    if (index < dev->layer_count) {
        return dev->layers[index];
    }
    
    // Auto-create layer 0 if requested and doesn't exist
    if (index == 0 && dev->layer_count == 0) {
        ts_led_layer_t layer = NULL;
        ts_led_layer_config_t cfg = TS_LED_LAYER_DEFAULT_CONFIG();
        if (ts_led_layer_create(device, &cfg, &layer) == ESP_OK) {
            return layer;
        }
    }
    
    return NULL;
}

esp_err_t ts_led_layer_destroy(ts_led_layer_t layer)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    
    free(l->buffer);
    free(l);
    return ESP_OK;
}

esp_err_t ts_led_layer_set_blend(ts_led_layer_t layer, ts_led_blend_t mode)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    l->blend_mode = mode;
    return ESP_OK;
}

esp_err_t ts_led_layer_set_opacity(ts_led_layer_t layer, uint8_t opacity)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    l->opacity = opacity;
    return ESP_OK;
}

esp_err_t ts_led_layer_set_visible(ts_led_layer_t layer, bool visible)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    l->visible = visible;
    return ESP_OK;
}

esp_err_t ts_led_layer_clear(ts_led_layer_t layer)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    memset(l->buffer, 0, l->device->config.led_count * sizeof(ts_led_rgb_t));
    return ESP_OK;
}

/*===========================================================================*/
/*                      Post-Processing Effect API                            */
/*===========================================================================*/

esp_err_t ts_led_layer_set_effect(ts_led_layer_t layer, const ts_led_effect_config_t *config)
{
    if (!layer || !config) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    
    /* Copy effect configuration */
    l->post_effect = *config;
    l->effect_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    TS_LOGI(TAG, "Layer effect set: type=%d", config->type);
    return ESP_OK;
}

esp_err_t ts_led_layer_clear_effect(ts_led_layer_t layer)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    
    memset(&l->post_effect, 0, sizeof(l->post_effect));
    l->post_effect.type = TS_LED_EFFECT_NONE;
    l->effect_start_time = 0;
    
    TS_LOGI(TAG, "Layer effect cleared");
    return ESP_OK;
}

bool ts_led_layer_has_effect(ts_led_layer_t layer)
{
    if (!layer) return false;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    return l->post_effect.type != TS_LED_EFFECT_NONE;
}

ts_led_effect_type_t ts_led_layer_get_effect_type(ts_led_layer_t layer)
{
    if (!layer) return TS_LED_EFFECT_NONE;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    return l->post_effect.type;
}

/* Drawing operations */
esp_err_t ts_led_set_pixel(ts_led_layer_t layer, uint16_t index, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    if (index >= l->device->config.led_count) return ESP_ERR_INVALID_ARG;
    l->buffer[index] = color;
    return ESP_OK;
}

esp_err_t ts_led_set_pixel_xy(ts_led_layer_t layer, uint16_t x, uint16_t y, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    ts_led_device_impl_t *dev = l->device;
    
    if (x >= dev->config.width || y >= dev->config.height) return ESP_ERR_INVALID_ARG;
    
    uint16_t w = dev->config.width;
    uint16_t h = dev->config.height;
    uint16_t tx = x, ty = y;
    
    // 根据 origin 转换坐标
    switch (dev->config.origin) {
        case TS_LED_ORIGIN_TOP_RIGHT:
            tx = w - 1 - x;
            break;
        case TS_LED_ORIGIN_BOTTOM_LEFT:
            ty = h - 1 - y;
            break;
        case TS_LED_ORIGIN_BOTTOM_RIGHT:
            tx = w - 1 - x;
            ty = h - 1 - y;
            break;
        case TS_LED_ORIGIN_TOP_LEFT:
        default:
            break;
    }
    
    uint16_t index;
    switch (dev->config.scan) {
        case TS_LED_SCAN_ZIGZAG_ROWS:
            // 蛇形布线：根据行号决定方向
            // 偶数行(0,2,4...)正序，奇数行(1,3,5...)反序
            index = (ty % 2 == 0) ? ty * w + tx
                                  : ty * w + (w - 1 - tx);
            break;
        case TS_LED_SCAN_ZIGZAG_COLS:
            // 蛇形布线（列优先）
            index = (tx % 2 == 0) ? tx * h + ty
                                  : tx * h + (h - 1 - ty);
            break;
        case TS_LED_SCAN_COLUMNS:
            index = tx * h + ty;
            break;
        case TS_LED_SCAN_ROWS:
        default:
            index = ty * w + tx;
            break;
    }
    
    return ts_led_set_pixel(layer, index, color);
}

esp_err_t ts_led_fill(ts_led_layer_t layer, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    for (int i = 0; i < l->device->config.led_count; i++) {
        l->buffer[i] = color;
    }
    return ESP_OK;
}

esp_err_t ts_led_fill_range(ts_led_layer_t layer, uint16_t start, uint16_t count, ts_led_rgb_t color)
{
    if (!layer) return ESP_ERR_INVALID_ARG;
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    uint16_t end = start + count;
    if (end > l->device->config.led_count) end = l->device->config.led_count;
    
    for (uint16_t i = start; i < end; i++) {
        l->buffer[i] = color;
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
