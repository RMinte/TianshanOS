/**
 * @file ts_led_effect.h
 * @brief TianShanOS LED Post-Processing Effects
 * 
 * Post-processing effects (filters) that modify the output of a layer
 * without generating new content. These are applied during the composition
 * phase, after content rendering but before final output.
 * 
 * Key difference from Animation:
 * - Animation GENERATES content (rainbow, fire, etc.)
 * - Effect MODIFIES content (brightness, blink, fade, etc.)
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_LED_EFFECT_H
#define TS_LED_EFFECT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for layer handle */
#ifndef TS_LED_LAYER_DEFINED
#define TS_LED_LAYER_DEFINED
typedef struct ts_led_layer *ts_led_layer_t;
#endif

/* Forward declaration for RGB */
#ifndef TS_LED_RGB_DEFINED
#define TS_LED_RGB_DEFINED
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ts_led_rgb_t;
#endif

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief Effect type enumeration
 * 
 * Post-processing effects that can be applied to layers.
 */
typedef enum {
    TS_LED_EFFECT_NONE = 0,      /**< No effect */
    
    /* Brightness effects */
    TS_LED_EFFECT_BRIGHTNESS,     /**< Static brightness adjustment */
    TS_LED_EFFECT_PULSE,          /**< Pulsing brightness (sine wave) */
    TS_LED_EFFECT_BLINK,          /**< On/off blinking */
    TS_LED_EFFECT_FADE_IN,        /**< Fade in (one-shot) */
    TS_LED_EFFECT_FADE_OUT,       /**< Fade out (one-shot) */
    TS_LED_EFFECT_BREATHING,      /**< Smooth breathing effect */
    
    /* Color effects */
    TS_LED_EFFECT_COLOR_SHIFT,    /**< Hue rotation over time */
    TS_LED_EFFECT_SATURATION,     /**< Saturation adjustment */
    TS_LED_EFFECT_INVERT,         /**< Invert colors */
    TS_LED_EFFECT_GRAYSCALE,      /**< Convert to grayscale */
    TS_LED_EFFECT_COLOR_TEMP,     /**< Color temperature (warm/cool) */
    
    /* Spatial effects (matrix only) */
    TS_LED_EFFECT_SCANLINE,       /**< Horizontal/vertical scanline */
    TS_LED_EFFECT_WAVE,           /**< Brightness wave */
    TS_LED_EFFECT_BLUR,           /**< Simple blur (3x3) */
    TS_LED_EFFECT_PIXELATE,       /**< Pixelation */
    TS_LED_EFFECT_MIRROR,         /**< Mirror effect */
    
    /* Glitch/artistic effects */
    TS_LED_EFFECT_GLITCH,         /**< Random glitch artifacts */
    TS_LED_EFFECT_NOISE,          /**< Add noise */
    TS_LED_EFFECT_STROBE,         /**< Fast strobe */
    
    /* Cool effects */
    TS_LED_EFFECT_RAINBOW,        /**< Rainbow color shift */
    TS_LED_EFFECT_SPARKLE,        /**< Random sparkles */
    TS_LED_EFFECT_PLASMA,         /**< Plasma wave */
    
    /* Basic filters */
    TS_LED_EFFECT_SEPIA,          /**< Sepia tone (vintage) */
    TS_LED_EFFECT_POSTERIZE,      /**< Posterization (color levels) */
    TS_LED_EFFECT_CONTRAST,       /**< Contrast adjustment */
    
    TS_LED_EFFECT_MAX
} ts_led_effect_type_t;

/**
 * @brief Effect direction (for directional effects)
 */
typedef enum {
    TS_LED_EFFECT_DIR_HORIZONTAL = 0,
    TS_LED_EFFECT_DIR_VERTICAL,
    TS_LED_EFFECT_DIR_DIAGONAL_L,
    TS_LED_EFFECT_DIR_DIAGONAL_R
} ts_led_effect_direction_t;

/**
 * @brief Effect parameters
 * 
 * Union of parameters for different effect types.
 */
typedef struct {
    ts_led_effect_type_t type;    /**< Effect type */
    
    union {
        /* Brightness effect params */
        struct {
            uint8_t level;        /**< Brightness level 0-255 */
        } brightness;
        
        /* Pulse effect params */
        struct {
            float frequency;      /**< Frequency in Hz (0.1 - 10.0) */
            uint8_t min_level;    /**< Minimum brightness */
            uint8_t max_level;    /**< Maximum brightness */
        } pulse;
        
        /* Blink effect params */
        struct {
            uint16_t on_time_ms;  /**< On duration in ms */
            uint16_t off_time_ms; /**< Off duration in ms */
        } blink;
        
        /* Fade effect params */
        struct {
            uint16_t duration_ms; /**< Fade duration */
            bool auto_remove;     /**< Remove effect after fade complete */
        } fade;
        
        /* Breathing effect params */
        struct {
            float frequency;      /**< Breath frequency in Hz */
            uint8_t min_level;    /**< Minimum brightness */
            uint8_t max_level;    /**< Maximum brightness */
        } breathing;
        
        /* Color shift params */
        struct {
            int16_t static_shift; /**< Static hue shift (-180 to 180) */
            float speed;          /**< Rotation speed (degrees/sec) */
        } color_shift;
        
        /* Saturation params */
        struct {
            float level;          /**< 0.0 = grayscale, 1.0 = normal, 2.0 = saturated */
        } saturation;
        
        /* Color temperature params */
        struct {
            int8_t temperature;   /**< -100 (cool) to +100 (warm) */
        } color_temp;
        
        /* Scanline params */
        struct {
            float angle;          /**< Rotation angle in degrees (0-360) */
            float speed;          /**< Pixels per second */
            uint8_t width;        /**< Scanline width in pixels (1-16, affects blur range) */
            uint8_t intensity;    /**< Brightness boost (0-255, multiplier for center brightness) */
        } scanline;
        
        /* Wave params */
        struct {
            float angle;          /**< Wave propagation angle in degrees (0-360), 0°=horizontal right, 90°=vertical up */
            float wavelength;     /**< Wave length in pixels */
            float speed;          /**< Wave speed (pixels/sec) */
            uint8_t amplitude;    /**< Amplitude 0-255 */
        } wave;
        
        /* Blur params */
        struct {
            uint8_t radius;       /**< Blur radius (1-3) */
        } blur;
        
        /* Pixelate params */
        struct {
            uint8_t block_size;   /**< Block size in pixels */
        } pixelate;
        
        /* Mirror params */
        struct {
            bool horizontal;      /**< Mirror horizontally */
            bool vertical;        /**< Mirror vertically */
        } mirror;
        
        /* Glitch params */
        struct {
            uint8_t intensity;    /**< Glitch intensity 0-255 */
            uint8_t frequency;    /**< How often glitches occur 0-255 */
        } glitch;
        
        /* Noise params */
        struct {
            uint8_t amount;       /**< Noise amount 0-255 */
            bool monochrome;      /**< Monochrome noise */
        } noise;
        
        /* Strobe params */
        struct {
            uint8_t frequency;    /**< Strobe frequency Hz (1-30) */
        } strobe;
        
        /* Rainbow params */
        struct {
            float speed;          /**< Color rotation speed */
            uint8_t saturation;   /**< Saturation 0-255 */
        } rainbow;
        
        /* Sparkle params */
        struct {
            float speed;          /**< Animation speed (0.1-100), lower=slower spawn rate */
            uint8_t density;      /**< Sparkle density 0-255, higher=more simultaneous sparkles */
            uint8_t decay;        /**< Fade speed 0-255, lower=longer afterglow (推荐50-200) */
        } sparkle;
        
        /* Plasma params */
        struct {
            float speed;          /**< Animation speed */
            uint8_t scale;        /**< Plasma scale */
        } plasma;
        
        /* Posterize params */
        struct {
            uint8_t levels;       /**< Color levels per channel (2-16) */
        } posterize;
        
        /* Contrast params */
        struct {
            int8_t amount;        /**< Contrast adjustment -100 to +100 */
        } contrast;
    } params;
    
} ts_led_effect_config_t;

/**
 * @brief Effect instance (opaque handle)
 */
typedef struct ts_led_effect *ts_led_effect_t;

/*===========================================================================*/
/*                              Default Values                                */
/*===========================================================================*/

/** Default pulse effect: 1Hz, 50-255 brightness */
#define TS_LED_EFFECT_PULSE_DEFAULT { \
    .type = TS_LED_EFFECT_PULSE, \
    .params.pulse = { .frequency = 1.0f, .min_level = 50, .max_level = 255 } \
}

/** Default blink effect: 500ms on/500ms off */
#define TS_LED_EFFECT_BLINK_DEFAULT { \
    .type = TS_LED_EFFECT_BLINK, \
    .params.blink = { .on_time_ms = 500, .off_time_ms = 500 } \
}

/** Default breathing effect: 0.5Hz */
#define TS_LED_EFFECT_BREATHING_DEFAULT { \
    .type = TS_LED_EFFECT_BREATHING, \
    .params.breathing = { .frequency = 0.5f, .min_level = 20, .max_level = 255 } \
}

/** Default fade in: 1 second */
#define TS_LED_EFFECT_FADE_IN_DEFAULT { \
    .type = TS_LED_EFFECT_FADE_IN, \
    .params.fade = { .duration_ms = 1000, .auto_remove = true } \
}

/** Default fade out: 1 second */
#define TS_LED_EFFECT_FADE_OUT_DEFAULT { \
    .type = TS_LED_EFFECT_FADE_OUT, \
    .params.fade = { .duration_ms = 1000, .auto_remove = true } \
}

/*===========================================================================*/
/*                              API Functions                                 */
/*===========================================================================*/

/**
 * @brief Apply a post-processing effect to a layer
 * 
 * The effect modifies the layer's output during composition.
 * Only one effect can be active on a layer at a time.
 * Call this again to replace the current effect.
 * 
 * @param layer Layer handle
 * @param config Effect configuration
 * @return ESP_OK on success
 */
esp_err_t ts_led_effect_apply(ts_led_layer_t layer, const ts_led_effect_config_t *config);

/**
 * @brief Remove effect from layer
 * 
 * @param layer Layer handle
 * @return ESP_OK on success
 */
esp_err_t ts_led_effect_remove(ts_led_layer_t layer);

/**
 * @brief Check if layer has an active effect
 * 
 * @param layer Layer handle
 * @return true if effect is active
 */
bool ts_led_effect_is_active(ts_led_layer_t layer);

/**
 * @brief Get current effect type on layer
 * 
 * @param layer Layer handle
 * @return Effect type, or TS_LED_EFFECT_NONE if no effect
 */
ts_led_effect_type_t ts_led_effect_get_type(ts_led_layer_t layer);

/**
 * @brief Get effect name string
 * 
 * @param type Effect type
 * @return Effect name string or "none"
 */
const char *ts_led_effect_type_to_name(ts_led_effect_type_t type);

/**
 * @brief Parse effect type from name
 * 
 * @param name Effect name string
 * @return Effect type, or TS_LED_EFFECT_NONE if not found
 */
ts_led_effect_type_t ts_led_effect_name_to_type(const char *name);

/**
 * @brief List available effect names
 * 
 * @param names Array to store names (can be NULL to just count)
 * @param max_names Maximum names to retrieve
 * @return Number of effects
 */
size_t ts_led_effect_list(const char **names, size_t max_names);

/**
 * @brief Process effect on pixel buffer
 * 
 * Internal function called during layer composition.
 * 
 * @param layer Layer handle
 * @param buffer Pixel buffer to modify
 * @param count Number of pixels
 * @param width Buffer width (for 2D effects)
 * @param height Buffer height (for 2D effects)
 * @param time_ms Current time in milliseconds
 */
void ts_led_effect_process(ts_led_layer_t layer, ts_led_rgb_t *buffer, 
                           size_t count, uint16_t width, uint16_t height,
                           uint32_t time_ms);

#ifdef __cplusplus
}
#endif

#endif /* TS_LED_EFFECT_H */
