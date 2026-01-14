/**
 * @file ts_led_preset.c
 * @brief Preset LED Device Instances
 */

#include "ts_led_preset.h"
#include "ts_led.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "ts_event.h"

#define TAG "led_preset"

static ts_led_device_t s_touch = NULL;
static ts_led_device_t s_board = NULL;
static ts_led_device_t s_matrix = NULL;
static ts_led_layer_t s_status_layer = NULL;

esp_err_t ts_led_touch_init(void)
{
    int gpio = ts_pin_manager_get_gpio(TS_PIN_FUNC_LED_TOUCH);
    if (gpio < 0) gpio = 45;
    
    ts_led_config_t cfg = TS_LED_DEFAULT_CONFIG();
    cfg.name = TS_LED_TOUCH_NAME;
    cfg.gpio_pin = gpio;
#ifdef CONFIG_TS_LED_TOUCH_COUNT
    cfg.led_count = CONFIG_TS_LED_TOUCH_COUNT;
#else
    cfg.led_count = 1;  // RM01: Single WS2812 touch indicator
#endif
#ifdef CONFIG_TS_LED_TOUCH_DEFAULT_BRIGHTNESS
    cfg.brightness = CONFIG_TS_LED_TOUCH_DEFAULT_BRIGHTNESS;
#else
    cfg.brightness = 80;
#endif
    cfg.layout = TS_LED_LAYOUT_STRIP;
    
    esp_err_t ret = ts_led_device_create(&cfg, &s_touch);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Touch LED initialized: %d LEDs on GPIO %d", cfg.led_count, gpio);
    }
    return ret;
}

esp_err_t ts_led_board_init(void)
{
    int gpio = ts_pin_manager_get_gpio(TS_PIN_FUNC_LED_BOARD);
    if (gpio < 0) gpio = 42;
    
    ts_led_config_t cfg = TS_LED_DEFAULT_CONFIG();
    cfg.name = TS_LED_BOARD_NAME;
    cfg.gpio_pin = gpio;
#ifdef CONFIG_TS_LED_BOARD_COUNT
    cfg.led_count = CONFIG_TS_LED_BOARD_COUNT;
#else
    cfg.led_count = 28;  // RM01: 28 LED strip
#endif
#ifdef CONFIG_TS_LED_BOARD_DEFAULT_BRIGHTNESS
    cfg.brightness = CONFIG_TS_LED_BOARD_DEFAULT_BRIGHTNESS;
#else
    cfg.brightness = 60;
#endif
    cfg.layout = TS_LED_LAYOUT_STRIP;
    
    esp_err_t ret = ts_led_device_create(&cfg, &s_board);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Board LED initialized: %d LEDs on GPIO %d", cfg.led_count, gpio);
    }
    return ret;
}

esp_err_t ts_led_matrix_init(void)
{
    int gpio = ts_pin_manager_get_gpio(TS_PIN_FUNC_LED_MATRIX);
    if (gpio < 0) gpio = 9;
    
    ts_led_config_t cfg = TS_LED_DEFAULT_CONFIG();
    cfg.name = TS_LED_MATRIX_NAME;
    cfg.gpio_pin = gpio;
    cfg.layout = TS_LED_LAYOUT_MATRIX;
    
#ifdef CONFIG_TS_LED_MATRIX_WIDTH
    cfg.width = CONFIG_TS_LED_MATRIX_WIDTH;
#else
    cfg.width = 32;  // RM01: 32x32 matrix
#endif
#ifdef CONFIG_TS_LED_MATRIX_HEIGHT
    cfg.height = CONFIG_TS_LED_MATRIX_HEIGHT;
#else
    cfg.height = 32;  // RM01: 32x32 matrix
#endif
    cfg.led_count = cfg.width * cfg.height;
    cfg.scan = TS_LED_SCAN_ZIGZAG_ROWS;
    
#ifdef CONFIG_TS_LED_MATRIX_DEFAULT_BRIGHTNESS
    cfg.brightness = CONFIG_TS_LED_MATRIX_DEFAULT_BRIGHTNESS;
#else
    cfg.brightness = 50;
#endif
    
    esp_err_t ret = ts_led_device_create(&cfg, &s_matrix);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Matrix LED initialized: %dx%d on GPIO %d", 
                cfg.width, cfg.height, gpio);
    }
    return ret;
}

esp_err_t ts_led_preset_init_all(void)
{
    esp_err_t ret;
    
    ret = ts_led_touch_init();
    if (ret != ESP_OK) TS_LOGW(TAG, "Touch LED init failed");
    
    ret = ts_led_board_init();
    if (ret != ESP_OK) TS_LOGW(TAG, "Board LED init failed");
    
    ret = ts_led_matrix_init();
    if (ret != ESP_OK) TS_LOGW(TAG, "Matrix LED init failed");
    
    return ESP_OK;
}

ts_led_device_t ts_led_touch_get(void) { return s_touch; }
ts_led_device_t ts_led_board_get(void) { return s_board; }
ts_led_device_t ts_led_matrix_get(void) { return s_matrix; }

/* Status indicator colors */
static const ts_led_rgb_t s_status_colors[] = {
    [TS_LED_STATUS_IDLE]    = {0, 0, 64},
    [TS_LED_STATUS_BUSY]    = {64, 64, 0},
    [TS_LED_STATUS_SUCCESS] = {0, 64, 0},
    [TS_LED_STATUS_ERROR]   = {64, 0, 0},
    [TS_LED_STATUS_WARNING] = {64, 32, 0},
    [TS_LED_STATUS_NETWORK] = {0, 32, 64},
    [TS_LED_STATUS_USB]     = {32, 0, 64},
    [TS_LED_STATUS_BOOT]    = {64, 64, 64},
};

esp_err_t ts_led_set_status(ts_led_status_t status)
{
    if (status >= TS_LED_STATUS_MAX) return ESP_ERR_INVALID_ARG;
    
    ts_led_device_t dev = ts_led_touch_get();
    if (!dev) return ESP_ERR_INVALID_STATE;
    
    if (!s_status_layer) {
        ts_led_layer_config_t cfg = TS_LED_LAYER_DEFAULT_CONFIG();
        ts_led_layer_create(dev, &cfg, &s_status_layer);
    }
    
    if (s_status_layer) {
        ts_led_fill(s_status_layer, s_status_colors[status]);
    }
    
    return ESP_OK;
}

esp_err_t ts_led_clear_status(void)
{
    return ts_led_set_status(TS_LED_STATUS_IDLE);
}

esp_err_t ts_led_bind_event_status(uint32_t event_id, ts_led_status_t status,
                                    uint32_t duration_ms)
{
    return ESP_ERR_NOT_SUPPORTED;
}
