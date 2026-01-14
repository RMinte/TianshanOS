/**
 * @file ts_led_image.c
 * @brief Image Loading and Display
 */

#include "ts_led_image.h"
#include "ts_storage.h"
#include "ts_log.h"
#include <stdlib.h>
#include <string.h>

#define TAG "led_image"

struct ts_led_image {
    ts_led_rgb_t *pixels;
    uint16_t width;
    uint16_t height;
    ts_led_image_format_t format;
    uint16_t frame_count;
    uint16_t current_frame;
    ts_led_rgb_t **frames;
    uint32_t *frame_delays;
};

static ts_led_image_format_t detect_format(const uint8_t *data, size_t size)
{
    if (size < 4) return TS_LED_IMG_FMT_RAW;
    
    if (data[0] == 'B' && data[1] == 'M') return TS_LED_IMG_FMT_BMP;
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') 
        return TS_LED_IMG_FMT_PNG;
    if (data[0] == 0xFF && data[1] == 0xD8) return TS_LED_IMG_FMT_JPG;
    if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F') return TS_LED_IMG_FMT_GIF;
    
    return TS_LED_IMG_FMT_RAW;
}

/* Simple BMP loader for 24-bit uncompressed BMP */
static esp_err_t load_bmp(const uint8_t *data, size_t size, ts_led_image_t *out)
{
    if (size < 54) return ESP_ERR_INVALID_SIZE;
    
    uint32_t offset = *(uint32_t *)(data + 10);
    int32_t width = *(int32_t *)(data + 18);
    int32_t height = *(int32_t *)(data + 22);
    uint16_t bpp = *(uint16_t *)(data + 28);
    
    if (bpp != 24) {
        TS_LOGE(TAG, "Only 24-bit BMP supported");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    bool flip = height > 0;
    if (height < 0) height = -height;
    
    struct ts_led_image *img = calloc(1, sizeof(struct ts_led_image));
    if (!img) return ESP_ERR_NO_MEM;
    
    img->width = width;
    img->height = height;
    img->format = TS_LED_IMG_FMT_BMP;
    img->frame_count = 1;
    
    img->pixels = calloc(width * height, sizeof(ts_led_rgb_t));
    if (!img->pixels) {
        free(img);
        return ESP_ERR_NO_MEM;
    }
    
    int row_size = ((width * 3 + 3) / 4) * 4;
    const uint8_t *px = data + offset;
    
    for (int y = 0; y < height; y++) {
        int dst_y = flip ? (height - 1 - y) : y;
        for (int x = 0; x < width; x++) {
            int src_idx = y * row_size + x * 3;
            int dst_idx = dst_y * width + x;
            img->pixels[dst_idx].b = px[src_idx];
            img->pixels[dst_idx].g = px[src_idx + 1];
            img->pixels[dst_idx].r = px[src_idx + 2];
        }
    }
    
    *out = img;
    return ESP_OK;
}

esp_err_t ts_led_image_load(const char *path, ts_led_image_format_t format,
                             ts_led_image_t *image)
{
    if (!path || !image) return ESP_ERR_INVALID_ARG;
    
    ssize_t size = ts_storage_size(path);
    if (size < 0) return ESP_ERR_NOT_FOUND;
    
    uint8_t *data = malloc(size);
    if (!data) return ESP_ERR_NO_MEM;
    
    if (ts_storage_read_file(path, data, size) != size) {
        free(data);
        return ESP_FAIL;
    }
    
    esp_err_t ret = ts_led_image_load_mem(data, size, format, image);
    free(data);
    return ret;
}

esp_err_t ts_led_image_load_mem(const void *data, size_t size,
                                 ts_led_image_format_t format,
                                 ts_led_image_t *image)
{
    if (!data || !image) return ESP_ERR_INVALID_ARG;
    
    if (format == TS_LED_IMG_FMT_AUTO) {
        format = detect_format(data, size);
    }
    
    switch (format) {
        case TS_LED_IMG_FMT_BMP:
            return load_bmp(data, size, image);
        default:
            TS_LOGW(TAG, "Format %d not implemented", format);
            return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t ts_led_image_create(const ts_led_rgb_t *data, uint16_t width,
                               uint16_t height, ts_led_image_t *image)
{
    if (!data || !image) return ESP_ERR_INVALID_ARG;
    
    struct ts_led_image *img = calloc(1, sizeof(struct ts_led_image));
    if (!img) return ESP_ERR_NO_MEM;
    
    img->width = width;
    img->height = height;
    img->format = TS_LED_IMG_FMT_RAW;
    img->frame_count = 1;
    
    size_t px_size = width * height * sizeof(ts_led_rgb_t);
    img->pixels = malloc(px_size);
    if (!img->pixels) {
        free(img);
        return ESP_ERR_NO_MEM;
    }
    
    memcpy(img->pixels, data, px_size);
    *image = img;
    return ESP_OK;
}

esp_err_t ts_led_image_free(ts_led_image_t image)
{
    if (!image) return ESP_ERR_INVALID_ARG;
    
    free(image->pixels);
    free(image->frame_delays);
    if (image->frames) {
        for (int i = 0; i < image->frame_count; i++) {
            free(image->frames[i]);
        }
        free(image->frames);
    }
    free(image);
    return ESP_OK;
}

esp_err_t ts_led_image_get_info(ts_led_image_t image, ts_led_image_info_t *info)
{
    if (!image || !info) return ESP_ERR_INVALID_ARG;
    
    info->width = image->width;
    info->height = image->height;
    info->format = image->format;
    info->frame_count = image->frame_count;
    info->frame_delays = image->frame_delays;
    return ESP_OK;
}

esp_err_t ts_led_image_display(ts_led_layer_t layer, ts_led_image_t image,
                                const ts_led_image_options_t *options)
{
    if (!layer || !image) return ESP_ERR_INVALID_ARG;
    
    ts_led_image_options_t opts = options ? *options : 
        (ts_led_image_options_t)TS_LED_IMAGE_DEFAULT_OPTIONS();
    
    uint16_t dev_w = layer->device->config.width;
    uint16_t dev_h = layer->device->config.height;
    
    for (int y = 0; y < image->height && y + opts.y < dev_h; y++) {
        for (int x = 0; x < image->width && x + opts.x < dev_w; x++) {
            if (x + opts.x >= 0 && y + opts.y >= 0) {
                ts_led_rgb_t px = image->pixels[y * image->width + x];
                if (opts.brightness < 255) {
                    px = ts_led_scale_color(px, opts.brightness);
                }
                ts_led_set_pixel_xy(layer, x + opts.x, y + opts.y, px);
            }
        }
    }
    return ESP_OK;
}

esp_err_t ts_led_image_display_frame(ts_led_layer_t layer, ts_led_image_t image,
                                      uint16_t frame,
                                      const ts_led_image_options_t *options)
{
    return ts_led_image_display(layer, image, options);
}

esp_err_t ts_led_image_animate_start(ts_led_layer_t layer, ts_led_image_t image,
                                      const ts_led_image_options_t *options)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ts_led_image_animate_stop(ts_led_layer_t layer)
{
    return ESP_OK;
}

bool ts_led_image_is_playing(ts_led_layer_t layer)
{
    return false;
}
