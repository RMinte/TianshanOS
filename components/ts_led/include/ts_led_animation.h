/**
 * @file ts_led_animation.h
 * @brief TianShanOS LED Animation System
 * 
 * Animation system for LED devices. Animations are dynamic content generators
 * that produce frames over time. This includes:
 * - Frame-based animations (GIF playback)
 * - Procedural animations (rainbow, fire, rain, etc.)
 * 
 * Note: The "effect" terminology from earlier versions referred to procedural
 * animations. True post-processing effects are defined in ts_led_effect.h.
 * 
 * This header is designed to be included by ts_led.h after base types are defined.
 * It can also be included standalone if the required types are forward-declared.
 * 
 * @author TianShanOS Team
 * @version 2.0.0
 * @date 2026-01-15
 */

#ifndef TS_LED_ANIMATION_H
#define TS_LED_ANIMATION_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration for layer handle */
#ifndef TS_LED_LAYER_DEFINED
#define TS_LED_LAYER_DEFINED
typedef struct ts_led_layer *ts_led_layer_t;
#endif

/* ts_led_layout_t should be defined by ts_led.h before including this header.
 * If not, provide fallback definition (standalone mode) */
#ifndef TS_LED_LAYOUT_DEFINED
#define TS_LED_LAYOUT_DEFINED
typedef enum {
    TS_LED_LAYOUT_STRIP = 0,
    TS_LED_LAYOUT_MATRIX,
    TS_LED_LAYOUT_RING
} ts_led_layout_t;
#endif

/* RGB color - use guard to prevent redefinition */
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
 * @brief Animation function type
 * 
 * Called each frame to generate animation content.
 * 
 * @param layer Layer to render to
 * @param time_ms Elapsed time in milliseconds
 * @param user_data User data pointer
 */
typedef void (*ts_led_animation_fn_t)(ts_led_layer_t layer, uint32_t time_ms, void *user_data);

/**
 * @brief Animation definition
 * 
 * Defines a procedural animation (previously called "effect").
 */
typedef struct {
    const char *name;                 /**< Animation name */
    ts_led_animation_fn_t func;       /**< Animation function */
    uint32_t frame_interval_ms;       /**< Frame interval in ms */
    void *user_data;                  /**< User data */
} ts_led_animation_def_t;

/**
 * @brief Animation playback state
 */
typedef enum {
    TS_LED_ANIMATION_STOPPED = 0,
    TS_LED_ANIMATION_PLAYING,
    TS_LED_ANIMATION_PAUSED
} ts_led_animation_state_t;

/**
 * @brief Animation parameters for customization
 */
typedef struct {
    ts_led_rgb_t color;               /**< Primary color (if applicable) */
    uint8_t speed;                    /**< Speed 1-100 (50 = normal) */
    uint8_t intensity;                /**< Intensity/brightness 0-255 */
    bool reverse;                     /**< Reverse direction */
} ts_led_animation_params_t;

/**
 * @brief Device type flags for animation compatibility
 */
typedef enum {
    TS_LED_ANIM_DEVICE_TOUCH   = 0x01,  /**< Point light source */
    TS_LED_ANIM_DEVICE_BOARD   = 0x02,  /**< Ring/strip */
    TS_LED_ANIM_DEVICE_MATRIX  = 0x04,  /**< 2D matrix */
    TS_LED_ANIM_DEVICE_ALL     = 0x07   /**< All devices */
} ts_led_animation_device_t;

/*===========================================================================*/
/*                           Animation Control                                */
/*===========================================================================*/

/**
 * @brief Start a procedural animation on layer
 * 
 * @param layer Layer handle
 * @param animation Animation definition
 * @return ESP_OK on success
 */
esp_err_t ts_led_animation_start(ts_led_layer_t layer, const ts_led_animation_def_t *animation);

/**
 * @brief Stop animation on layer
 * 
 * @param layer Layer handle
 * @return ESP_OK on success
 */
esp_err_t ts_led_animation_stop(ts_led_layer_t layer);

/**
 * @brief Check if animation is running on layer
 * 
 * @param layer Layer handle
 * @return true if animation is running
 */
bool ts_led_animation_is_running(ts_led_layer_t layer);

/**
 * @brief Get current animation state
 * 
 * @param layer Layer handle
 * @return Animation state
 */
ts_led_animation_state_t ts_led_animation_get_state(ts_led_layer_t layer);

/*===========================================================================*/
/*                         Built-in Animations                                */
/*===========================================================================*/

/**
 * @brief Get built-in animation by name
 * 
 * Built-in animations (procedural):
 * 
 * Universal:
 *   - rainbow: Rainbow gradient cycle
 *   - breathing: Breathing/pulse effect
 *   - solid: Solid color fill
 *   - sparkle: Random sparkle
 * 
 * Touch (point source):
 *   - pulse: Quick flash and fade
 *   - heartbeat: Heartbeat rhythm
 *   - color_cycle: Hue rotation
 * 
 * Board (ring):
 *   - chase: Chase/theater lights
 *   - comet: Comet with tail
 *   - spin: Rotating rainbow
 *   - breathe_wave: Breathing wave
 * 
 * Matrix:
 *   - fire: Fire effect
 *   - rain: Digital rain/falling drops
 *   - coderain: Matrix-style code rain
 *   - plasma: Plasma wave
 *   - ripple: Water ripple
 * 
 * @param name Animation name
 * @return Animation definition or NULL if not found
 */
const ts_led_animation_def_t *ts_led_animation_get_builtin(const char *name);

/**
 * @brief List all built-in animation names
 * 
 * @param names Array to store names (can be NULL to just count)
 * @param max_names Maximum names to retrieve
 * @return Number of animations
 */
size_t ts_led_animation_list_builtin(const char **names, size_t max_names);

/**
 * @brief List animations suitable for a specific device layout
 * 
 * @param layout Device layout type
 * @param names Array to store names (can be NULL to just count)
 * @param max_names Maximum names to retrieve
 * @return Number of suitable animations
 */
size_t ts_led_animation_list_for_device(ts_led_layout_t layout, const char **names, size_t max_names);


#ifdef __cplusplus
}
#endif

#endif /* TS_LED_ANIMATION_H */
