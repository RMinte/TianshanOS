/**
 * @file ts_led_driver.c
 * @brief WS2812 LED Driver using RMT
 */

#include "ts_led.h"
#include "ts_log.h"
#include "led_strip.h"
#include <string.h>

#define TAG "led_driver"

typedef struct ts_led_device ts_led_device_impl_t;

/* Access internal device structure */
extern ts_led_rgb_t *ts_led_device_get_fb(ts_led_device_impl_t *dev);
extern uint16_t ts_led_device_get_led_count(ts_led_device_impl_t *dev);
extern uint8_t ts_led_device_get_dev_brightness(ts_led_device_impl_t *dev);

esp_err_t ts_led_driver_init(ts_led_device_impl_t *dev)
{
    if (!dev) return ESP_ERR_INVALID_ARG;
    
    int gpio = dev->config.gpio_pin;
    int count = dev->config.led_count;
    
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio,
        .max_leds = count,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = dev->config.use_dma,
    };
    
    led_strip_handle_t strip;
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }
    
    dev->driver_handle = strip;
    led_strip_clear(strip);
    
    TS_LOGI(TAG, "LED driver initialized: GPIO %d, %d LEDs", gpio, count);
    return ESP_OK;
}

esp_err_t ts_led_driver_send(ts_led_device_impl_t *dev)
{
    if (!dev || !dev->driver_handle) return ESP_ERR_INVALID_STATE;
    
    led_strip_handle_t strip = (led_strip_handle_t)dev->driver_handle;
    uint8_t brightness = dev->brightness;
    
    for (int i = 0; i < dev->config.led_count; i++) {
        ts_led_rgb_t *px = &dev->framebuffer[i];
        uint8_t r = (px->r * brightness) >> 8;
        uint8_t g = (px->g * brightness) >> 8;
        uint8_t b = (px->b * brightness) >> 8;
        led_strip_set_pixel(strip, i, r, g, b);
    }
    
    return led_strip_refresh(strip);
}

void ts_led_driver_deinit(ts_led_device_impl_t *dev)
{
    if (dev && dev->driver_handle) {
        led_strip_del((led_strip_handle_t)dev->driver_handle);
        dev->driver_handle = NULL;
    }
}
