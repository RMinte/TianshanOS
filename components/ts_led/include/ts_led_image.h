/**
 * @file ts_led_image.h
 * @brief TianShanOS LED Image Display
 * 
 * Image loading and display support for BMP, PNG, JPG, and GIF.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_LED_IMAGE_H
#define TS_LED_IMAGE_H

#include "ts_led.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief Image format
 */
typedef enum {
    TS_LED_IMG_FMT_AUTO = 0,     /**< Auto-detect format */
    TS_LED_IMG_FMT_BMP,          /**< BMP format */
    TS_LED_IMG_FMT_PNG,          /**< PNG format */
    TS_LED_IMG_FMT_JPG,          /**< JPEG format */
    TS_LED_IMG_FMT_GIF,          /**< GIF format (animated) */
    TS_LED_IMG_FMT_RAW           /**< Raw RGB data */
} ts_led_image_format_t;

/**
 * @brief Image scale mode
 */
typedef enum {
    TS_LED_IMG_SCALE_NONE = 0,   /**< No scaling */
    TS_LED_IMG_SCALE_FIT,        /**< Fit to layer (maintain aspect) */
    TS_LED_IMG_SCALE_FILL,       /**< Fill layer (crop if needed) */
    TS_LED_IMG_SCALE_STRETCH     /**< Stretch to layer size */
} ts_led_image_scale_t;

/**
 * @brief Image center mode
 */
typedef enum {
    TS_LED_IMG_CENTER_IMAGE = 0, /**< Center the scaled image */
    TS_LED_IMG_CENTER_CONTENT    /**< Center based on non-transparent content */
} ts_led_image_center_t;

/**
 * @brief Image handle
 */
typedef struct ts_led_image *ts_led_image_t;

/**
 * @brief Image info
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    ts_led_image_format_t format;
    uint16_t frame_count;        /**< Number of frames (for GIF) */
    uint32_t *frame_delays;      /**< Frame delays in ms (for GIF) */
} ts_led_image_info_t;

/**
 * @brief Image display options
 */
typedef struct {
    int16_t x;                   /**< X offset */
    int16_t y;                   /**< Y offset */
    ts_led_image_scale_t scale;  /**< Scale mode */
    ts_led_image_center_t center; /**< Center mode */
    bool loop;                   /**< Loop animation (for GIF) */
    uint8_t brightness;          /**< Image brightness (0-255) */
} ts_led_image_options_t;

/*===========================================================================*/
/*                         Default Options                                    */
/*===========================================================================*/

#define TS_LED_IMAGE_DEFAULT_OPTIONS() { \
    .x = 0, \
    .y = 0, \
    .scale = TS_LED_IMG_SCALE_FIT, \
    .center = TS_LED_IMG_CENTER_CONTENT, \
    .loop = true, \
    .brightness = 255 \
}

/*===========================================================================*/
/*                              Image API                                     */
/*===========================================================================*/

/**
 * @brief Load image from file
 * 
 * @param path File path
 * @param format Format (or AUTO)
 * @param image Output image handle
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_load(const char *path, ts_led_image_format_t format,
                             ts_led_image_t *image);

/**
 * @brief Load image from memory
 * 
 * @param data Image data
 * @param size Data size
 * @param format Format (or AUTO)
 * @param image Output image handle
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_load_mem(const void *data, size_t size,
                                 ts_led_image_format_t format,
                                 ts_led_image_t *image);

/**
 * @brief Create image from raw RGB data
 * 
 * @param data RGB pixel data
 * @param width Width
 * @param height Height
 * @param image Output image handle
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_create(const ts_led_rgb_t *data, uint16_t width,
                               uint16_t height, ts_led_image_t *image);

/**
 * @brief Free image
 * 
 * @param image Image handle
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_free(ts_led_image_t image);

/**
 * @brief Get image info
 * 
 * @param image Image handle
 * @param info Output info
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_get_info(ts_led_image_t image, ts_led_image_info_t *info);

/**
 * @brief Get pixel color at specified position
 * 
 * @param image Image handle
 * @param x X coordinate
 * @param y Y coordinate
 * @param color Output color
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out of bounds
 */
esp_err_t ts_led_image_get_pixel(ts_led_image_t image, uint16_t x, uint16_t y,
                                  ts_led_rgb_t *color);

/**
 * @brief Display image on layer (static)
 * 
 * @param layer Layer handle
 * @param image Image handle
 * @param options Display options
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_display(ts_led_layer_t layer, ts_led_image_t image,
                                const ts_led_image_options_t *options);

/**
 * @brief Display specific frame (for GIF)
 * 
 * @param layer Layer handle
 * @param image Image handle
 * @param frame Frame index
 * @param options Display options
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_display_frame(ts_led_layer_t layer, ts_led_image_t image,
                                      uint16_t frame,
                                      const ts_led_image_options_t *options);

/**
 * @brief Start animated image playback
 * 
 * @param layer Layer handle
 * @param image Image handle
 * @param options Display options
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_animate_start(ts_led_layer_t layer, ts_led_image_t image,
                                      const ts_led_image_options_t *options);

/**
 * @brief Stop animated image playback
 * 
 * @param layer Layer handle
 * @return ESP_OK on success
 */
esp_err_t ts_led_image_animate_stop(ts_led_layer_t layer);

/**
 * @brief Check if animation is playing
 * 
 * @param layer Layer handle
 * @return true if playing
 */
bool ts_led_image_is_playing(ts_led_layer_t layer);

#ifdef __cplusplus
}
#endif

#endif /* TS_LED_IMAGE_H */
