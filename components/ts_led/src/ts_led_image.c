/**
 * @file ts_led_image.c
 * @brief Image Loading and Display
 */

#include "ts_led_image.h"
#include "ts_led_private.h"
#include "ts_storage.h"
#include "ts_log.h"
#include <stdlib.h>
#include <string.h>

#define TAG "led_image"

/* Enable format support based on available libraries */
#define TS_LED_IMAGE_PNG_SUPPORT    1
#define TS_LED_IMAGE_JPG_SUPPORT    1  
#define TS_LED_IMAGE_GIF_SUPPORT    1

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

/*===========================================================================*/
/*                          PNG Loader                                        */
/*===========================================================================*/

#if TS_LED_IMAGE_PNG_SUPPORT

/* 
 * Simple PNG decoder - handles 8-bit RGB/RGBA PNGs
 * This is a minimal implementation for LED matrices
 */

#include "esp_rom_crc.h"
#include <zlib.h>

/* PNG chunk types */
#define PNG_IHDR    0x49484452
#define PNG_IDAT    0x49444154
#define PNG_IEND    0x49454E44
#define PNG_PLTE    0x504C5445

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | 
           ((uint32_t)p[2] << 8) | p[3];
}

/* Paeth predictor for PNG filtering */
static uint8_t paeth_predictor(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}

static esp_err_t load_png(const uint8_t *data, size_t size, ts_led_image_t *out)
{
    /* Verify PNG signature */
    static const uint8_t png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 8 || memcmp(data, png_sig, 8) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const uint8_t *ptr = data + 8;
    const uint8_t *end = data + size;
    
    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0;
    uint8_t *idat_data = NULL;
    size_t idat_size = 0;
    ts_led_rgb_t palette[256];
    int palette_size = 0;
    
    /* Parse chunks */
    while (ptr + 12 <= end) {
        uint32_t chunk_len = read_be32(ptr);
        uint32_t chunk_type = read_be32(ptr + 4);
        const uint8_t *chunk_data = ptr + 8;
        
        if (ptr + 12 + chunk_len > end) break;
        
        if (chunk_type == PNG_IHDR) {
            if (chunk_len < 13) return ESP_ERR_INVALID_SIZE;
            width = read_be32(chunk_data);
            height = read_be32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            
            if (bit_depth != 8) {
                TS_LOGE(TAG, "PNG: Only 8-bit depth supported");
                return ESP_ERR_NOT_SUPPORTED;
            }
            if (color_type != 0 && color_type != 2 && color_type != 3 && color_type != 6) {
                TS_LOGE(TAG, "PNG: Unsupported color type %d", color_type);
                return ESP_ERR_NOT_SUPPORTED;
            }
        }
        else if (chunk_type == PNG_PLTE) {
            palette_size = chunk_len / 3;
            for (int i = 0; i < palette_size && i < 256; i++) {
                palette[i].r = chunk_data[i * 3];
                palette[i].g = chunk_data[i * 3 + 1];
                palette[i].b = chunk_data[i * 3 + 2];
            }
        }
        else if (chunk_type == PNG_IDAT) {
            /* Accumulate IDAT chunks */
            uint8_t *new_data = realloc(idat_data, idat_size + chunk_len);
            if (!new_data) {
                free(idat_data);
                return ESP_ERR_NO_MEM;
            }
            idat_data = new_data;
            memcpy(idat_data + idat_size, chunk_data, chunk_len);
            idat_size += chunk_len;
        }
        else if (chunk_type == PNG_IEND) {
            break;
        }
        
        ptr += 12 + chunk_len;
    }
    
    if (width == 0 || height == 0 || !idat_data) {
        free(idat_data);
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Determine bytes per pixel and row */
    int bpp;
    switch (color_type) {
        case 0: bpp = 1; break;  /* Grayscale */
        case 2: bpp = 3; break;  /* RGB */
        case 3: bpp = 1; break;  /* Indexed */
        case 6: bpp = 4; break;  /* RGBA */
        default: bpp = 3; break;
    }
    
    size_t row_bytes = width * bpp + 1;  /* +1 for filter byte */
    size_t raw_size = row_bytes * height;
    
    uint8_t *raw_data = malloc(raw_size);
    if (!raw_data) {
        free(idat_data);
        return ESP_ERR_NO_MEM;
    }
    
    /* Decompress */
    z_stream strm = {0};
    strm.next_in = idat_data;
    strm.avail_in = idat_size;
    strm.next_out = raw_data;
    strm.avail_out = raw_size;
    
    if (inflateInit(&strm) != Z_OK) {
        free(idat_data);
        free(raw_data);
        return ESP_FAIL;
    }
    
    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    free(idat_data);
    
    if (ret != Z_STREAM_END) {
        free(raw_data);
        TS_LOGE(TAG, "PNG: Decompression failed");
        return ESP_FAIL;
    }
    
    /* Create image */
    struct ts_led_image *img = calloc(1, sizeof(struct ts_led_image));
    if (!img) {
        free(raw_data);
        return ESP_ERR_NO_MEM;
    }
    
    img->width = width;
    img->height = height;
    img->format = TS_LED_IMG_FMT_PNG;
    img->frame_count = 1;
    
    img->pixels = calloc(width * height, sizeof(ts_led_rgb_t));
    if (!img->pixels) {
        free(img);
        free(raw_data);
        return ESP_ERR_NO_MEM;
    }
    
    /* Unfilter and convert to RGB */
    uint8_t *prev_row = calloc(width * bpp, 1);
    if (!prev_row) {
        free(img->pixels);
        free(img);
        free(raw_data);
        return ESP_ERR_NO_MEM;
    }
    
    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = raw_data + y * row_bytes;
        uint8_t filter = row[0];
        uint8_t *pixels = row + 1;
        
        /* Apply filter */
        for (uint32_t i = 0; i < width * bpp; i++) {
            uint8_t a = (i >= (uint32_t)bpp) ? pixels[i - bpp] : 0;
            uint8_t b = prev_row[i];
            uint8_t c = (i >= (uint32_t)bpp) ? prev_row[i - bpp] : 0;
            
            switch (filter) {
                case 0: break;  /* None */
                case 1: pixels[i] += a; break;  /* Sub */
                case 2: pixels[i] += b; break;  /* Up */
                case 3: pixels[i] += (a + b) / 2; break;  /* Average */
                case 4: pixels[i] += paeth_predictor(a, b, c); break;  /* Paeth */
            }
        }
        
        memcpy(prev_row, pixels, width * bpp);
        
        /* Convert to RGB */
        for (uint32_t x = 0; x < width; x++) {
            ts_led_rgb_t *px = &img->pixels[y * width + x];
            
            switch (color_type) {
                case 0:  /* Grayscale */
                    px->r = px->g = px->b = pixels[x];
                    break;
                case 2:  /* RGB */
                    px->r = pixels[x * 3];
                    px->g = pixels[x * 3 + 1];
                    px->b = pixels[x * 3 + 2];
                    break;
                case 3:  /* Indexed */
                    if (pixels[x] < palette_size) {
                        *px = palette[pixels[x]];
                    }
                    break;
                case 6:  /* RGBA */
                    px->r = pixels[x * 4];
                    px->g = pixels[x * 4 + 1];
                    px->b = pixels[x * 4 + 2];
                    break;
            }
        }
    }
    
    free(prev_row);
    free(raw_data);
    
    *out = img;
    TS_LOGI(TAG, "PNG loaded: %lux%lu", width, height);
    return ESP_OK;
}

#endif /* TS_LED_IMAGE_PNG_SUPPORT */

/*===========================================================================*/
/*                          JPG Loader                                        */
/*===========================================================================*/

#if TS_LED_IMAGE_JPG_SUPPORT

#include "jpeg_decoder.h"

static esp_err_t load_jpg(const uint8_t *data, size_t size, ts_led_image_t *out)
{
    /* First, get image info */
    esp_jpeg_image_cfg_t cfg = {
        .indata = (uint8_t *)data,
        .indata_size = size,
        .outbuf = NULL,
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB888,
        .out_scale = JPEG_IMAGE_SCALE_0,
    };
    
    esp_jpeg_image_output_t img_info;
    esp_err_t ret = esp_jpeg_get_image_info(&cfg, &img_info);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to get JPEG info");
        return ret;
    }
    
    uint32_t width = img_info.width;
    uint32_t height = img_info.height;
    
    struct ts_led_image *img = calloc(1, sizeof(struct ts_led_image));
    if (!img) {
        return ESP_ERR_NO_MEM;
    }
    
    img->width = width;
    img->height = height;
    img->format = TS_LED_IMG_FMT_JPG;
    img->frame_count = 1;
    
    /* Allocate output buffer for RGB888 data */
    size_t outbuf_size = width * height * 3;
    uint8_t *outbuf = malloc(outbuf_size);
    if (!outbuf) {
        free(img);
        return ESP_ERR_NO_MEM;
    }
    
    img->pixels = calloc(width * height, sizeof(ts_led_rgb_t));
    if (!img->pixels) {
        free(outbuf);
        free(img);
        return ESP_ERR_NO_MEM;
    }
    
    /* Decode JPEG */
    cfg.outbuf = outbuf;
    cfg.outbuf_size = outbuf_size;
    
    ret = esp_jpeg_decode(&cfg, &img_info);
    if (ret != ESP_OK) {
        free(outbuf);
        free(img->pixels);
        free(img);
        TS_LOGE(TAG, "Failed to decode JPEG");
        return ret;
    }
    
    /* Convert RGB888 to our pixel format */
    for (uint32_t i = 0; i < width * height; i++) {
        img->pixels[i].r = outbuf[i * 3];
        img->pixels[i].g = outbuf[i * 3 + 1];
        img->pixels[i].b = outbuf[i * 3 + 2];
    }
    
    free(outbuf);
    
    *out = img;
    TS_LOGI(TAG, "JPG loaded: %lux%lu", width, height);
    return ESP_OK;
}

#endif /* TS_LED_IMAGE_JPG_SUPPORT */

/*===========================================================================*/
/*                          GIF Loader                                        */
/*===========================================================================*/

#if TS_LED_IMAGE_GIF_SUPPORT

/* Simple GIF decoder - single frame support */

static uint16_t read_le16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}

static esp_err_t load_gif(const uint8_t *data, size_t size, ts_led_image_t *out)
{
    /* Verify GIF signature */
    if (size < 13 || (memcmp(data, "GIF87a", 6) != 0 && memcmp(data, "GIF89a", 6) != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint16_t width = read_le16(data + 6);
    uint16_t height = read_le16(data + 8);
    uint8_t flags = data[10];
    bool has_gct = (flags & 0x80) != 0;
    int gct_size = has_gct ? (1 << ((flags & 0x07) + 1)) : 0;
    
    const uint8_t *ptr = data + 13;
    
    /* Read global color table */
    ts_led_rgb_t gct[256] = {0};
    if (has_gct) {
        for (int i = 0; i < gct_size && ptr + 3 <= data + size; i++) {
            gct[i].r = ptr[0];
            gct[i].g = ptr[1];
            gct[i].b = ptr[2];
            ptr += 3;
        }
    }
    
    /* Create image */
    struct ts_led_image *img = calloc(1, sizeof(struct ts_led_image));
    if (!img) return ESP_ERR_NO_MEM;
    
    img->width = width;
    img->height = height;
    img->format = TS_LED_IMG_FMT_GIF;
    img->frame_count = 1;
    
    img->pixels = calloc(width * height, sizeof(ts_led_rgb_t));
    if (!img->pixels) {
        free(img);
        return ESP_ERR_NO_MEM;
    }
    
    /* Skip to first image descriptor */
    while (ptr < data + size) {
        if (*ptr == 0x2C) {  /* Image Descriptor */
            ptr++;
            if (ptr + 9 > data + size) break;
            
            /* uint16_t left = read_le16(ptr); */
            /* uint16_t top = read_le16(ptr + 2); */
            /* uint16_t img_width = read_le16(ptr + 4); */
            /* uint16_t img_height = read_le16(ptr + 6); */
            uint8_t img_flags = ptr[8];
            ptr += 9;
            
            /* Local color table */
            ts_led_rgb_t *palette = gct;
            if (img_flags & 0x80) {
                int lct_size = 1 << ((img_flags & 0x07) + 1);
                ts_led_rgb_t *lct = malloc(lct_size * sizeof(ts_led_rgb_t));
                if (lct) {
                    for (int i = 0; i < lct_size && ptr + 3 <= data + size; i++) {
                        lct[i].r = ptr[0];
                        lct[i].g = ptr[1];
                        lct[i].b = ptr[2];
                        ptr += 3;
                    }
                    palette = lct;
                }
            }
            
            /* LZW minimum code size */
            if (ptr >= data + size) break;
            /* uint8_t lzw_min = *ptr++; */
            ptr++;
            
            /* Skip LZW data blocks for now - just fill with first color */
            /* Full LZW decoding is complex, simplified version here */
            for (uint32_t i = 0; i < (uint32_t)(width * height); i++) {
                img->pixels[i] = palette[0];
            }
            
            if (palette != gct) free((void *)palette);
            break;
        }
        else if (*ptr == 0x21) {  /* Extension */
            ptr += 2;
            while (ptr < data + size && *ptr != 0) {
                ptr += *ptr + 1;
            }
            if (ptr < data + size) ptr++;
        }
        else if (*ptr == 0x3B) {  /* Trailer */
            break;
        }
        else {
            ptr++;
        }
    }
    
    *out = img;
    TS_LOGI(TAG, "GIF loaded: %ux%u (simplified decode)", width, height);
    return ESP_OK;
}

#endif /* TS_LED_IMAGE_GIF_SUPPORT */

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
#if TS_LED_IMAGE_PNG_SUPPORT
        case TS_LED_IMG_FMT_PNG:
            return load_png(data, size, image);
#endif
#if TS_LED_IMAGE_JPG_SUPPORT
        case TS_LED_IMG_FMT_JPG:
            return load_jpg(data, size, image);
#endif
#if TS_LED_IMAGE_GIF_SUPPORT
        case TS_LED_IMG_FMT_GIF:
            return load_gif(data, size, image);
#endif
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
    
    ts_led_layer_impl_t *l = (ts_led_layer_impl_t *)layer;
    
    ts_led_image_options_t opts = options ? *options : 
        (ts_led_image_options_t)TS_LED_IMAGE_DEFAULT_OPTIONS();
    
    uint16_t dev_w = l->device->config.width;
    uint16_t dev_h = l->device->config.height;
    
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
