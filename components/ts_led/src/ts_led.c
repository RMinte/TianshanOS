/**
 * @file ts_led.c
 * @brief TianShanOS LED Core Implementation
 */

#include "ts_led.h"
#include "ts_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ts_led"

#ifndef CONFIG_TS_LED_MAX_DEVICES
#define CONFIG_TS_LED_MAX_DEVICES 8
#endif

typedef struct ts_led_device {
    char name[TS_LED_MAX_NAME];
    ts_led_config_t config;
    ts_led_rgb_t *framebuffer;
    ts_led_layer_t layers[TS_LED_MAX_LAYERS];
    uint8_t layer_count;
    uint8_t brightness;
    void *driver_handle;
    bool used;
    SemaphoreHandle_t mutex;
} ts_led_device_impl_t;

static struct {
    bool initialized;
    ts_led_device_impl_t devices[CONFIG_TS_LED_MAX_DEVICES];
    SemaphoreHandle_t mutex;
    TaskHandle_t render_task;
    bool render_running;
} s_led = {0};

/* Forward declarations */
esp_err_t ts_led_driver_init(ts_led_device_impl_t *dev);
esp_err_t ts_led_driver_send(ts_led_device_impl_t *dev);
void ts_led_driver_deinit(ts_led_device_impl_t *dev);

static void render_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(1000 / 60);
    
    while (s_led.render_running) {
        for (int i = 0; i < CONFIG_TS_LED_MAX_DEVICES; i++) {
            if (s_led.devices[i].used) {
                ts_led_device_refresh(&s_led.devices[i]);
            }
        }
        vTaskDelayUntil(&last_wake, interval);
    }
    vTaskDelete(NULL);
}

esp_err_t ts_led_init(void)
{
    if (s_led.initialized) return ESP_ERR_INVALID_STATE;
    
    s_led.mutex = xSemaphoreCreateMutex();
    if (!s_led.mutex) return ESP_ERR_NO_MEM;
    
    memset(s_led.devices, 0, sizeof(s_led.devices));
    s_led.initialized = true;
    
    s_led.render_running = true;
    xTaskCreate(render_task, "led_render", 4096, NULL, 5, &s_led.render_task);
    
    TS_LOGI(TAG, "LED subsystem initialized");
    return ESP_OK;
}

esp_err_t ts_led_deinit(void)
{
    if (!s_led.initialized) return ESP_ERR_INVALID_STATE;
    
    s_led.render_running = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    
    for (int i = 0; i < CONFIG_TS_LED_MAX_DEVICES; i++) {
        if (s_led.devices[i].used) {
            ts_led_device_destroy(&s_led.devices[i]);
        }
    }
    
    vSemaphoreDelete(s_led.mutex);
    s_led.initialized = false;
    return ESP_OK;
}

esp_err_t ts_led_device_create(const ts_led_config_t *config, ts_led_device_t *device)
{
    if (!config || !device || config->led_count == 0) return ESP_ERR_INVALID_ARG;
    
    xSemaphoreTake(s_led.mutex, portMAX_DELAY);
    
    ts_led_device_impl_t *dev = NULL;
    for (int i = 0; i < CONFIG_TS_LED_MAX_DEVICES; i++) {
        if (!s_led.devices[i].used) {
            dev = &s_led.devices[i];
            break;
        }
    }
    
    if (!dev) {
        xSemaphoreGive(s_led.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    memset(dev, 0, sizeof(*dev));
    dev->config = *config;
    strncpy(dev->name, config->name ? config->name : "led", TS_LED_MAX_NAME - 1);
    dev->brightness = config->brightness;
    
    dev->framebuffer = calloc(config->led_count, sizeof(ts_led_rgb_t));
    if (!dev->framebuffer) {
        xSemaphoreGive(s_led.mutex);
        return ESP_ERR_NO_MEM;
    }
    
    dev->mutex = xSemaphoreCreateMutex();
    esp_err_t ret = ts_led_driver_init(dev);
    if (ret != ESP_OK) {
        free(dev->framebuffer);
        vSemaphoreDelete(dev->mutex);
        xSemaphoreGive(s_led.mutex);
        return ret;
    }
    
    dev->used = true;
    *device = dev;
    
    xSemaphoreGive(s_led.mutex);
    TS_LOGI(TAG, "Created LED device '%s' with %d LEDs", dev->name, config->led_count);
    return ESP_OK;
}

esp_err_t ts_led_device_destroy(ts_led_device_t device)
{
    if (!device) return ESP_ERR_INVALID_ARG;
    ts_led_device_impl_t *dev = (ts_led_device_impl_t *)device;
    
    ts_led_driver_deinit(dev);
    free(dev->framebuffer);
    vSemaphoreDelete(dev->mutex);
    memset(dev, 0, sizeof(*dev));
    return ESP_OK;
}

ts_led_device_t ts_led_device_get(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < CONFIG_TS_LED_MAX_DEVICES; i++) {
        if (s_led.devices[i].used && strcmp(s_led.devices[i].name, name) == 0) {
            return &s_led.devices[i];
        }
    }
    return NULL;
}

esp_err_t ts_led_device_set_brightness(ts_led_device_t device, uint8_t brightness)
{
    if (!device) return ESP_ERR_INVALID_ARG;
    ((ts_led_device_impl_t *)device)->brightness = brightness;
    return ESP_OK;
}

uint8_t ts_led_device_get_brightness(ts_led_device_t device)
{
    return device ? ((ts_led_device_impl_t *)device)->brightness : 0;
}

uint16_t ts_led_device_get_count(ts_led_device_t device)
{
    return device ? ((ts_led_device_impl_t *)device)->config.led_count : 0;
}

esp_err_t ts_led_device_refresh(ts_led_device_t device)
{
    if (!device) return ESP_ERR_INVALID_ARG;
    ts_led_device_impl_t *dev = (ts_led_device_impl_t *)device;
    
    xSemaphoreTake(dev->mutex, portMAX_DELAY);
    esp_err_t ret = ts_led_driver_send(dev);
    xSemaphoreGive(dev->mutex);
    return ret;
}

esp_err_t ts_led_device_clear(ts_led_device_t device)
{
    if (!device) return ESP_ERR_INVALID_ARG;
    ts_led_device_impl_t *dev = (ts_led_device_impl_t *)device;
    
    xSemaphoreTake(dev->mutex, portMAX_DELAY);
    memset(dev->framebuffer, 0, dev->config.led_count * sizeof(ts_led_rgb_t));
    xSemaphoreGive(dev->mutex);
    return ESP_OK;
}
