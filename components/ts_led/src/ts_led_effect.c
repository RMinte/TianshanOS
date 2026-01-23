/**
 * @file ts_led_effect.c
 * @brief TianShanOS LED Post-Processing Effects Implementation
 * 
 * Implements post-processing effects (filters) that modify layer output.
 * These effects are applied during composition, after content rendering.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_led_effect.h"
#include "ts_led.h"
#include "ts_led_private.h"
#include <esp_log.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ts_led_effect";

/*===========================================================================*/
/*                              Effect Name Table                             */
/*===========================================================================*/

static const struct {
    ts_led_effect_type_t type;
    const char *name;
} s_effect_names[] = {
    { TS_LED_EFFECT_NONE,         "none" },
    { TS_LED_EFFECT_BRIGHTNESS,   "brightness" },
    { TS_LED_EFFECT_PULSE,        "pulse" },
    { TS_LED_EFFECT_BLINK,        "blink" },
    { TS_LED_EFFECT_FADE_IN,      "fade_in" },
    { TS_LED_EFFECT_FADE_OUT,     "fade_out" },
    { TS_LED_EFFECT_BREATHING,    "breathing" },
    { TS_LED_EFFECT_COLOR_SHIFT,  "color_shift" },
    { TS_LED_EFFECT_SATURATION,   "saturation" },
    { TS_LED_EFFECT_INVERT,       "invert" },
    { TS_LED_EFFECT_GRAYSCALE,    "grayscale" },
    { TS_LED_EFFECT_COLOR_TEMP,   "color_temp" },
    { TS_LED_EFFECT_SCANLINE,     "scanline" },
    { TS_LED_EFFECT_WAVE,         "wave" },
    { TS_LED_EFFECT_BLUR,         "blur" },
    { TS_LED_EFFECT_PIXELATE,     "pixelate" },
    { TS_LED_EFFECT_MIRROR,       "mirror" },
    { TS_LED_EFFECT_GLITCH,       "glitch" },
    { TS_LED_EFFECT_NOISE,        "noise" },
    { TS_LED_EFFECT_STROBE,       "strobe" },
};

#define NUM_EFFECTS (sizeof(s_effect_names) / sizeof(s_effect_names[0]))

/*===========================================================================*/
/*                              Helper Functions                              */
/*===========================================================================*/

/**
 * @brief Scale RGB color by brightness
 */
static inline ts_led_rgb_t scale_rgb(ts_led_rgb_t color, uint8_t scale)
{
    return (ts_led_rgb_t){
        .r = (uint8_t)((color.r * scale) >> 8),
        .g = (uint8_t)((color.g * scale) >> 8),
        .b = (uint8_t)((color.b * scale) >> 8)
    };
}

/**
 * @brief Clamp value to 0-255
 */
static inline uint8_t clamp_u8(int32_t val)
{
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

/**
 * @brief Simple pseudo-random number generator
 */
static uint32_t s_random_state = 12345;
static inline uint32_t effect_random(void)
{
    s_random_state = s_random_state * 1103515245 + 12345;
    return (s_random_state >> 16) & 0xFFFF;
}

/**
 * @brief Convert RGB to HSV
 */
static void rgb_to_hsv(ts_led_rgb_t rgb, uint16_t *h, uint8_t *s, uint8_t *v)
{
    uint8_t max = rgb.r > rgb.g ? (rgb.r > rgb.b ? rgb.r : rgb.b) 
                                : (rgb.g > rgb.b ? rgb.g : rgb.b);
    uint8_t min = rgb.r < rgb.g ? (rgb.r < rgb.b ? rgb.r : rgb.b) 
                                : (rgb.g < rgb.b ? rgb.g : rgb.b);
    uint8_t delta = max - min;
    
    *v = max;
    
    if (max == 0) {
        *s = 0;
        *h = 0;
        return;
    }
    
    *s = (uint8_t)((delta * 255) / max);
    
    if (delta == 0) {
        *h = 0;
        return;
    }
    
    int16_t hue;
    if (max == rgb.r) {
        hue = 60 * ((rgb.g - rgb.b) * 256 / delta) / 256;
    } else if (max == rgb.g) {
        hue = 120 + 60 * ((rgb.b - rgb.r) * 256 / delta) / 256;
    } else {
        hue = 240 + 60 * ((rgb.r - rgb.g) * 256 / delta) / 256;
    }
    
    if (hue < 0) hue += 360;
    *h = (uint16_t)hue;
}

/**
 * @brief Convert HSV to RGB
 */
static ts_led_rgb_t hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v)
{
    if (s == 0) {
        return (ts_led_rgb_t){v, v, v};
    }
    
    h = h % 360;
    uint8_t region = h / 60;
    uint8_t remainder = (h % 60) * 255 / 60;
    
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
    
    switch (region) {
        case 0: return (ts_led_rgb_t){v, t, p};
        case 1: return (ts_led_rgb_t){q, v, p};
        case 2: return (ts_led_rgb_t){p, v, t};
        case 3: return (ts_led_rgb_t){p, q, v};
        case 4: return (ts_led_rgb_t){t, p, v};
        default: return (ts_led_rgb_t){v, p, q};
    }
}

/*===========================================================================*/
/*                              Effect Processors                             */
/*===========================================================================*/

/**
 * @brief Process brightness effect
 */
static void process_brightness(ts_led_rgb_t *buffer, size_t count,
                               const ts_led_effect_config_t *config, uint32_t time_ms)
{
    (void)time_ms;
    uint8_t level = config->params.brightness.level;
    
    for (size_t i = 0; i < count; i++) {
        buffer[i] = scale_rgb(buffer[i], level);
    }
}

/**
 * @brief Process pulse effect (sine wave brightness modulation)
 */
static void process_pulse(ts_led_rgb_t *buffer, size_t count,
                          const ts_led_effect_config_t *config, uint32_t time_ms)
{
    float freq = config->params.pulse.frequency;
    uint8_t min_level = config->params.pulse.min_level;
    uint8_t max_level = config->params.pulse.max_level;
    
    // Calculate brightness using sine wave
    float phase = (float)time_ms * freq * 0.001f * 2.0f * 3.14159f;
    float sine = (sinf(phase) + 1.0f) * 0.5f; // 0.0 to 1.0
    uint8_t level = min_level + (uint8_t)(sine * (max_level - min_level));
    
    for (size_t i = 0; i < count; i++) {
        buffer[i] = scale_rgb(buffer[i], level);
    }
}

/**
 * @brief Process blink effect
 */
static void process_blink(ts_led_rgb_t *buffer, size_t count,
                          const ts_led_effect_config_t *config, uint32_t time_ms)
{
    uint32_t period = config->params.blink.on_time_ms + config->params.blink.off_time_ms;
    uint32_t phase = time_ms % period;
    
    if (phase >= config->params.blink.on_time_ms) {
        // Off phase - set all to black
        memset(buffer, 0, count * sizeof(ts_led_rgb_t));
    }
    // On phase - keep original colors
}

/**
 * @brief Process fade in effect
 */
static void process_fade_in(ts_led_layer_t layer, ts_led_rgb_t *buffer, size_t count,
                            const ts_led_effect_config_t *config, uint32_t time_ms)
{
    uint32_t start_time = layer->effect_start_time;
    uint32_t elapsed = time_ms - start_time;
    uint16_t duration = config->params.fade.duration_ms;
    
    uint8_t level;
    if (elapsed >= duration) {
        level = 255;
        if (config->params.fade.auto_remove) {
            layer->post_effect.type = TS_LED_EFFECT_NONE;
        }
    } else {
        level = (uint8_t)((elapsed * 255) / duration);
    }
    
    for (size_t i = 0; i < count; i++) {
        buffer[i] = scale_rgb(buffer[i], level);
    }
}

/**
 * @brief Process fade out effect
 */
static void process_fade_out(ts_led_layer_t layer, ts_led_rgb_t *buffer, size_t count,
                             const ts_led_effect_config_t *config, uint32_t time_ms)
{
    uint32_t start_time = layer->effect_start_time;
    uint32_t elapsed = time_ms - start_time;
    uint16_t duration = config->params.fade.duration_ms;
    
    uint8_t level;
    if (elapsed >= duration) {
        level = 0;
        if (config->params.fade.auto_remove) {
            layer->post_effect.type = TS_LED_EFFECT_NONE;
        }
    } else {
        level = 255 - (uint8_t)((elapsed * 255) / duration);
    }
    
    for (size_t i = 0; i < count; i++) {
        buffer[i] = scale_rgb(buffer[i], level);
    }
}

/**
 * @brief Process breathing effect (smoother than pulse)
 */
static void process_breathing(ts_led_rgb_t *buffer, size_t count,
                              const ts_led_effect_config_t *config, uint32_t time_ms)
{
    float freq = config->params.breathing.frequency;
    uint8_t min_level = config->params.breathing.min_level;
    uint8_t max_level = config->params.breathing.max_level;
    
    // Breathing uses (1 - cos) / 2 for smoother feel
    float phase = (float)time_ms * freq * 0.001f * 2.0f * 3.14159f;
    float breath = (1.0f - cosf(phase)) * 0.5f; // 0.0 to 1.0
    uint8_t level = min_level + (uint8_t)(breath * (max_level - min_level));
    
    for (size_t i = 0; i < count; i++) {
        buffer[i] = scale_rgb(buffer[i], level);
    }
}

/**
 * @brief Process color shift (hue rotation)
 */
static void process_color_shift(ts_led_rgb_t *buffer, size_t count,
                                const ts_led_effect_config_t *config, uint32_t time_ms)
{
    int16_t static_shift = config->params.color_shift.static_shift;
    float speed = config->params.color_shift.speed;
    
    // Calculate total hue shift
    int16_t shift = static_shift + (int16_t)(speed * time_ms / 1000.0f);
    shift = ((shift % 360) + 360) % 360; // Normalize to 0-359
    
    for (size_t i = 0; i < count; i++) {
        uint16_t h;
        uint8_t s, v;
        rgb_to_hsv(buffer[i], &h, &s, &v);
        h = (h + shift) % 360;
        buffer[i] = hsv_to_rgb(h, s, v);
    }
}

/**
 * @brief Process saturation adjustment
 */
static void process_saturation(ts_led_rgb_t *buffer, size_t count,
                               const ts_led_effect_config_t *config, uint32_t time_ms)
{
    (void)time_ms;
    float level = config->params.saturation.level;
    
    for (size_t i = 0; i < count; i++) {
        uint16_t h;
        uint8_t s, v;
        rgb_to_hsv(buffer[i], &h, &s, &v);
        
        float new_s = s * level;
        if (new_s > 255) new_s = 255;
        if (new_s < 0) new_s = 0;
        
        buffer[i] = hsv_to_rgb(h, (uint8_t)new_s, v);
    }
}

/**
 * @brief Process invert effect
 */
static void process_invert(ts_led_rgb_t *buffer, size_t count,
                           const ts_led_effect_config_t *config, uint32_t time_ms)
{
    (void)config;
    (void)time_ms;
    
    for (size_t i = 0; i < count; i++) {
        buffer[i].r = 255 - buffer[i].r;
        buffer[i].g = 255 - buffer[i].g;
        buffer[i].b = 255 - buffer[i].b;
    }
}

/**
 * @brief Process grayscale effect
 */
static void process_grayscale(ts_led_rgb_t *buffer, size_t count,
                              const ts_led_effect_config_t *config, uint32_t time_ms)
{
    (void)config;
    (void)time_ms;
    
    for (size_t i = 0; i < count; i++) {
        // Use luminance formula: 0.299*R + 0.587*G + 0.114*B
        uint8_t gray = (uint8_t)((buffer[i].r * 77 + buffer[i].g * 150 + buffer[i].b * 29) >> 8);
        buffer[i].r = gray;
        buffer[i].g = gray;
        buffer[i].b = gray;
    }
}

/**
 * @brief Process color temperature adjustment
 */
static void process_color_temp(ts_led_rgb_t *buffer, size_t count,
                               const ts_led_effect_config_t *config, uint32_t time_ms)
{
    (void)time_ms;
    int8_t temp = config->params.color_temp.temperature;
    
    // Warm: increase red, decrease blue
    // Cool: increase blue, decrease red
    int16_t r_adj = temp;      // -100 to +100
    int16_t b_adj = -temp;
    
    for (size_t i = 0; i < count; i++) {
        buffer[i].r = clamp_u8((int32_t)buffer[i].r + r_adj);
        buffer[i].b = clamp_u8((int32_t)buffer[i].b + b_adj);
    }
}

/**
 * @brief Process scanline effect (matrix only)
 */
static void process_scanline(ts_led_rgb_t *buffer, size_t count,
                             uint16_t width, uint16_t height,
                             const ts_led_effect_config_t *config, uint32_t time_ms)
{
    if (width == 0 || height == 0) {
        ESP_LOGW(TAG, "Scanline effect: invalid dimensions (width=%d, height=%d)", width, height);
        return;
    }
    
    float speed = config->params.scanline.speed;
    uint8_t line_width = config->params.scanline.width;
    uint8_t intensity = config->params.scanline.intensity;
    bool horizontal = (config->params.scanline.direction == TS_LED_EFFECT_DIR_HORIZONTAL);
    
    uint16_t max_pos = horizontal ? height : width;
    float pos = fmodf(speed * time_ms / 1000.0f, (float)max_pos);
    
    for (size_t i = 0; i < count; i++) {
        uint16_t x = i % width;
        uint16_t y = i / width;
        uint16_t coord = horizontal ? y : x;
        
        // Check if within scanline
        float dist = fabsf((float)coord - pos);
        if (dist > max_pos / 2) dist = max_pos - dist; // Wrap around
        
        if (dist < line_width) {
            // 如果像素是黑色，用扫描线颜色填充
            if (buffer[i].r == 0 && buffer[i].g == 0 && buffer[i].b == 0) {
                float fade = 1.0f - dist / line_width;
                uint8_t line_brightness = (uint8_t)(intensity * fade);
                buffer[i].r = line_brightness;
                buffer[i].g = line_brightness;
                buffer[i].b = line_brightness;
            } else {
                // 否则增强现有亮度
                float boost = 1.0f + (float)intensity / 255.0f * (1.0f - dist / line_width);
                buffer[i].r = clamp_u8((int32_t)(buffer[i].r * boost));
                buffer[i].g = clamp_u8((int32_t)(buffer[i].g * boost));
                buffer[i].b = clamp_u8((int32_t)(buffer[i].b * boost));
            }
        }
    }
}

/**
 * @brief Process wave effect (matrix only)
 */
static void process_wave(ts_led_rgb_t *buffer, size_t count,
                         uint16_t width, uint16_t height,
                         const ts_led_effect_config_t *config, uint32_t time_ms)
{
    if (width == 0 || height == 0) {
        ESP_LOGW(TAG, "Wave effect: invalid dimensions (width=%d, height=%d)", width, height);
        return;
    }
    
    float wavelength = config->params.wave.wavelength;
    float speed = config->params.wave.speed;
    uint8_t amplitude = config->params.wave.amplitude;
    bool horizontal = (config->params.wave.direction == TS_LED_EFFECT_DIR_HORIZONTAL);
    
    float time_offset = speed * time_ms / 1000.0f;
    
    for (size_t i = 0; i < count; i++) {
        uint16_t x = i % width;
        uint16_t y = i / width;
        float coord = horizontal ? (float)x : (float)y;
        
        // Calculate wave value
        float phase = (coord + time_offset) * 2.0f * 3.14159f / wavelength;
        float wave = (sinf(phase) + 1.0f) * 0.5f; // 0.0 to 1.0
        
        // 如果像素是黑色，用波浪模式填充
        if (buffer[i].r == 0 && buffer[i].g == 0 && buffer[i].b == 0) {
            uint8_t wave_brightness = (uint8_t)(wave * amplitude);
            buffer[i].r = wave_brightness;
            buffer[i].g = wave_brightness;
            buffer[i].b = wave_brightness;
        } else {
            // 否则调制现有亮度
            uint8_t level = 255 - amplitude + (uint8_t)(wave * amplitude);
            buffer[i] = scale_rgb(buffer[i], level);
        }
    }
}

/**
 * @brief Process strobe effect
 */
static void process_strobe(ts_led_rgb_t *buffer, size_t count,
                           const ts_led_effect_config_t *config, uint32_t time_ms)
{
    uint8_t freq = config->params.strobe.frequency;
    if (freq == 0) freq = 1;
    
    uint32_t period = 1000 / freq;
    uint32_t phase = time_ms % period;
    
    // 10% on-time for strobe effect
    if (phase > period / 10) {
        memset(buffer, 0, count * sizeof(ts_led_rgb_t));
    }
}

/**
 * @brief Process noise effect
 */
static void process_noise(ts_led_rgb_t *buffer, size_t count,
                          const ts_led_effect_config_t *config, uint32_t time_ms)
{
    (void)time_ms;
    uint8_t amount = config->params.noise.amount;
    bool mono = config->params.noise.monochrome;
    
    for (size_t i = 0; i < count; i++) {
        if (mono) {
            int8_t noise = (int8_t)((effect_random() & 0xFF) - 128) * amount / 255;
            buffer[i].r = clamp_u8((int32_t)buffer[i].r + noise);
            buffer[i].g = clamp_u8((int32_t)buffer[i].g + noise);
            buffer[i].b = clamp_u8((int32_t)buffer[i].b + noise);
        } else {
            buffer[i].r = clamp_u8((int32_t)buffer[i].r + 
                                   (int8_t)((effect_random() & 0xFF) - 128) * amount / 255);
            buffer[i].g = clamp_u8((int32_t)buffer[i].g + 
                                   (int8_t)((effect_random() & 0xFF) - 128) * amount / 255);
            buffer[i].b = clamp_u8((int32_t)buffer[i].b + 
                                   (int8_t)((effect_random() & 0xFF) - 128) * amount / 255);
        }
    }
}

/**
 * @brief Process glitch effect
 */
static void process_glitch(ts_led_rgb_t *buffer, size_t count,
                           uint16_t width, uint16_t height,
                           const ts_led_effect_config_t *config, uint32_t time_ms)
{
    (void)time_ms;
    uint8_t intensity = config->params.glitch.intensity;
    uint8_t frequency = config->params.glitch.frequency;
    
    // Decide if glitch happens this frame
    if ((effect_random() & 0xFF) > frequency) return;
    
    // 检查是否有任何非黑色像素
    bool has_content = false;
    for (size_t i = 0; i < count && !has_content; i++) {
        if (buffer[i].r != 0 || buffer[i].g != 0 || buffer[i].b != 0) {
            has_content = true;
        }
    }
    
    if (!has_content) {
        // 如果全黑，生成随机故障像素
        ESP_LOGW(TAG, "Glitch effect: generating random pixels (no content)");
        size_t num_glitches = (intensity * count) / 512;  // intensity 控制故障密度
        for (size_t i = 0; i < num_glitches; i++) {
            size_t pos = (effect_random() * count) >> 16;
            if (pos < count) {
                uint8_t brightness = (effect_random() & 0xFF);
                buffer[pos].r = brightness;
                buffer[pos].g = (effect_random() & 0xFF);
                buffer[pos].b = (effect_random() & 0xFF);
            }
        }
        return;
    }
    
    if (width == 0 || height == 0) {
        ESP_LOGW(TAG, "Glitch effect: using linear mode (width=%d, height=%d)", width, height);
        // Linear glitch - random block
        size_t start = (effect_random() * count) >> 16;
        size_t len = (effect_random() * intensity) >> 16;
        if (start + len > count) len = count - start;
        
        // Color shift
        int16_t shift = (int16_t)(effect_random() & 0xFF) - 128;
        for (size_t i = start; i < start + len; i++) {
            buffer[i].r = clamp_u8((int32_t)buffer[i].r + shift);
        }
    } else {
        // Matrix glitch - random row offset
        uint16_t row = (effect_random() * height) >> 16;
        int16_t offset = (int16_t)((effect_random() & 0x0F) - 8);
        
        // Shift row
        ts_led_rgb_t temp[width];
        memcpy(temp, &buffer[row * width], width * sizeof(ts_led_rgb_t));
        
        for (uint16_t x = 0; x < width; x++) {
            int16_t src = x + offset;
            if (src < 0) src += width;
            if (src >= width) src -= width;
            buffer[row * width + x] = temp[src];
        }
    }
}

/*===========================================================================*/
/*                              Public API                                    */
/*===========================================================================*/

esp_err_t ts_led_effect_apply(ts_led_layer_t layer, const ts_led_effect_config_t *config)
{
    if (!layer || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (config->type >= TS_LED_EFFECT_MAX) {
        ESP_LOGE(TAG, "Invalid effect type: %d", config->type);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Copy configuration
    memcpy(&layer->post_effect, config, sizeof(ts_led_effect_config_t));
    layer->effect_start_time = esp_log_timestamp();
    
    ESP_LOGI(TAG, "Applied effect '%s' to layer %d", 
             ts_led_effect_type_to_name(config->type), layer->id);
    
    return ESP_OK;
}

esp_err_t ts_led_effect_remove(ts_led_layer_t layer)
{
    if (!layer) {
        return ESP_ERR_INVALID_ARG;
    }
    
    layer->post_effect.type = TS_LED_EFFECT_NONE;
    
    return ESP_OK;
}

bool ts_led_effect_is_active(ts_led_layer_t layer)
{
    if (!layer) return false;
    return layer->post_effect.type != TS_LED_EFFECT_NONE;
}

ts_led_effect_type_t ts_led_effect_get_type(ts_led_layer_t layer)
{
    if (!layer) return TS_LED_EFFECT_NONE;
    return layer->post_effect.type;
}

const char *ts_led_effect_type_to_name(ts_led_effect_type_t type)
{
    for (size_t i = 0; i < NUM_EFFECTS; i++) {
        if (s_effect_names[i].type == type) {
            return s_effect_names[i].name;
        }
    }
    return "unknown";
}

ts_led_effect_type_t ts_led_effect_name_to_type(const char *name)
{
    if (!name) return TS_LED_EFFECT_NONE;
    
    for (size_t i = 0; i < NUM_EFFECTS; i++) {
        if (strcasecmp(s_effect_names[i].name, name) == 0) {
            return s_effect_names[i].type;
        }
    }
    return TS_LED_EFFECT_NONE;
}

size_t ts_led_effect_list(const char **names, size_t max_names)
{
    if (!names) {
        return NUM_EFFECTS - 1; // Exclude "none"
    }
    
    size_t count = 0;
    for (size_t i = 1; i < NUM_EFFECTS && count < max_names; i++) {
        names[count++] = s_effect_names[i].name;
    }
    
    return count;
}

void ts_led_effect_process(ts_led_layer_t layer, ts_led_rgb_t *buffer,
                           size_t count, uint16_t width, uint16_t height,
                           uint32_t time_ms)
{
    if (!layer || !buffer || count == 0) return;
    
    const ts_led_effect_config_t *config = &layer->post_effect;
    
    switch (config->type) {
        case TS_LED_EFFECT_NONE:
            break;
            
        case TS_LED_EFFECT_BRIGHTNESS:
            process_brightness(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_PULSE:
            process_pulse(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_BLINK:
            process_blink(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_FADE_IN:
            process_fade_in(layer, buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_FADE_OUT:
            process_fade_out(layer, buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_BREATHING:
            process_breathing(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_COLOR_SHIFT:
            process_color_shift(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_SATURATION:
            process_saturation(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_INVERT:
            process_invert(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_GRAYSCALE:
            process_grayscale(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_COLOR_TEMP:
            process_color_temp(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_SCANLINE:
            process_scanline(buffer, count, width, height, config, time_ms);
            break;
            
        case TS_LED_EFFECT_WAVE:
            process_wave(buffer, count, width, height, config, time_ms);
            break;
            
        case TS_LED_EFFECT_STROBE:
            process_strobe(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_NOISE:
            process_noise(buffer, count, config, time_ms);
            break;
            
        case TS_LED_EFFECT_GLITCH:
            process_glitch(buffer, count, width, height, config, time_ms);
            break;
            
        case TS_LED_EFFECT_BLUR:
        case TS_LED_EFFECT_PIXELATE:
        case TS_LED_EFFECT_MIRROR:
            // These require additional implementation with 2D buffer access
            ESP_LOGW(TAG, "Effect '%s' not yet implemented", 
                     ts_led_effect_type_to_name(config->type));
            break;
            
        default:
            break;
    }
}
