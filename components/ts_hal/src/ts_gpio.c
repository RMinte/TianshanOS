/**
 * @file ts_gpio.c
 * @brief TianShanOS GPIO Abstraction Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_hal_gpio.h"
#include "ts_pin_manager.h"
#include "ts_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ts_gpio"

/*===========================================================================*/
/*                              Private Types                                 */
/*===========================================================================*/

struct ts_gpio_s {
    int gpio_num;                       /* Physical GPIO number */
    ts_pin_function_t function;         /* Logical function (if any) */
    ts_gpio_config_t config;            /* Current configuration */
    ts_gpio_isr_callback_t isr_cb;      /* ISR callback */
    void *isr_user_data;                /* ISR callback user data */
    bool configured;                    /* Is configured */
    bool using_function;                /* Using pin manager function */
    char owner[32];                     /* Owner name */
};

/*===========================================================================*/
/*                              Private Data                                  */
/*===========================================================================*/

static bool s_initialized = false;
static SemaphoreHandle_t s_mutex = NULL;
static ts_gpio_handle_t s_handles[CONFIG_TS_HAL_MAX_GPIO_HANDLES];
static bool s_isr_service_installed = false;

/*===========================================================================*/
/*                          Private Functions                                 */
/*===========================================================================*/

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    ts_gpio_handle_t handle = (ts_gpio_handle_t)arg;
    if (handle && handle->isr_cb) {
        handle->isr_cb(handle, handle->isr_user_data);
    }
}

static gpio_mode_t convert_direction(ts_gpio_dir_t dir)
{
    switch (dir) {
        case TS_GPIO_DIR_INPUT:
            return GPIO_MODE_INPUT;
        case TS_GPIO_DIR_OUTPUT:
            return GPIO_MODE_OUTPUT;
        case TS_GPIO_DIR_OUTPUT_OD:
            return GPIO_MODE_OUTPUT_OD;
        case TS_GPIO_DIR_BIDIRECTIONAL:
            return GPIO_MODE_INPUT_OUTPUT;
        default:
            return GPIO_MODE_DISABLE;
    }
}

static gpio_pull_mode_t convert_pull(ts_gpio_pull_t pull)
{
    switch (pull) {
        case TS_GPIO_PULL_UP:
            return GPIO_PULLUP_ONLY;
        case TS_GPIO_PULL_DOWN:
            return GPIO_PULLDOWN_ONLY;
        case TS_GPIO_PULL_UP_DOWN:
            return GPIO_PULLUP_PULLDOWN;
        default:
            return GPIO_FLOATING;
    }
}

static gpio_int_type_t convert_intr(ts_gpio_intr_t intr)
{
    switch (intr) {
        case TS_GPIO_INTR_POSEDGE:
            return GPIO_INTR_POSEDGE;
        case TS_GPIO_INTR_NEGEDGE:
            return GPIO_INTR_NEGEDGE;
        case TS_GPIO_INTR_ANYEDGE:
            return GPIO_INTR_ANYEDGE;
        case TS_GPIO_INTR_LOW_LEVEL:
            return GPIO_INTR_LOW_LEVEL;
        case TS_GPIO_INTR_HIGH_LEVEL:
            return GPIO_INTR_HIGH_LEVEL;
        default:
            return GPIO_INTR_DISABLE;
    }
}

static ts_gpio_handle_t find_free_handle(void)
{
    for (int i = 0; i < CONFIG_TS_HAL_MAX_GPIO_HANDLES; i++) {
        if (s_handles[i] == NULL) {
            return (ts_gpio_handle_t)(intptr_t)(i + 1); /* Return slot marker */
        }
    }
    return NULL;
}

static int get_handle_slot(ts_gpio_handle_t handle)
{
    for (int i = 0; i < CONFIG_TS_HAL_MAX_GPIO_HANDLES; i++) {
        if (s_handles[i] == handle) {
            return i;
        }
    }
    return -1;
}

/*===========================================================================*/
/*                         Public Functions                                   */
/*===========================================================================*/

esp_err_t ts_gpio_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Initializing GPIO subsystem");
    
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    memset(s_handles, 0, sizeof(s_handles));
    s_isr_service_installed = false;
    s_initialized = true;
    
    return ESP_OK;
}

esp_err_t ts_gpio_deinit(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGI(TAG, "Deinitializing GPIO subsystem");
    
    /* Destroy all handles */
    for (int i = 0; i < CONFIG_TS_HAL_MAX_GPIO_HANDLES; i++) {
        if (s_handles[i] != NULL) {
            ts_gpio_destroy(s_handles[i]);
        }
    }
    
    if (s_isr_service_installed) {
        gpio_uninstall_isr_service();
        s_isr_service_installed = false;
    }
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    return ESP_OK;
}

ts_gpio_handle_t ts_gpio_create(ts_pin_function_t function, const char *owner)
{
    if (!s_initialized || owner == NULL) {
        return NULL;
    }
    
    int gpio_num = ts_pin_manager_get_gpio(function);
    if (gpio_num < 0) {
        TS_LOGE(TAG, "Function %d has no GPIO mapping", function);
        return NULL;
    }
    
    /* Acquire pin from manager */
    esp_err_t ret = ts_pin_manager_acquire(function, owner);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to acquire pin for function %d", function);
        return NULL;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_TS_HAL_MAX_GPIO_HANDLES; i++) {
        if (s_handles[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        ts_pin_manager_release(function);
        TS_LOGE(TAG, "No free GPIO handles");
        return NULL;
    }
    
    /* Allocate handle */
    ts_gpio_handle_t handle = calloc(1, sizeof(struct ts_gpio_s));
    if (handle == NULL) {
        xSemaphoreGive(s_mutex);
        ts_pin_manager_release(function);
        return NULL;
    }
    
    handle->gpio_num = gpio_num;
    handle->function = function;
    handle->using_function = true;
    handle->configured = false;
    strncpy(handle->owner, owner, sizeof(handle->owner) - 1);
    
    s_handles[slot] = handle;
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "Created GPIO handle for function %d (GPIO%d), owner: %s",
            function, gpio_num, owner);
    
    return handle;
}

ts_gpio_handle_t ts_gpio_create_raw(int gpio_num, const char *owner)
{
    if (!s_initialized || owner == NULL || gpio_num < 0) {
        return NULL;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_TS_HAL_MAX_GPIO_HANDLES; i++) {
        if (s_handles[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        xSemaphoreGive(s_mutex);
        TS_LOGE(TAG, "No free GPIO handles");
        return NULL;
    }
    
    /* Allocate handle */
    ts_gpio_handle_t handle = calloc(1, sizeof(struct ts_gpio_s));
    if (handle == NULL) {
        xSemaphoreGive(s_mutex);
        return NULL;
    }
    
    handle->gpio_num = gpio_num;
    handle->function = TS_PIN_FUNC_MAX;
    handle->using_function = false;
    handle->configured = false;
    strncpy(handle->owner, owner, sizeof(handle->owner) - 1);
    
    s_handles[slot] = handle;
    
    xSemaphoreGive(s_mutex);
    
    TS_LOGD(TAG, "Created raw GPIO handle for GPIO%d, owner: %s", gpio_num, owner);
    
    return handle;
}

esp_err_t ts_gpio_configure(ts_gpio_handle_t handle, const ts_gpio_config_t *config)
{
    if (!s_initialized || handle == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << handle->gpio_num),
        .mode = convert_direction(config->direction),
        .pull_up_en = (config->pull_mode == TS_GPIO_PULL_UP || 
                       config->pull_mode == TS_GPIO_PULL_UP_DOWN),
        .pull_down_en = (config->pull_mode == TS_GPIO_PULL_DOWN ||
                         config->pull_mode == TS_GPIO_PULL_UP_DOWN),
        .intr_type = convert_intr(config->intr_type),
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "GPIO%d config failed: %s", handle->gpio_num, esp_err_to_name(ret));
        return ret;
    }
    
    /* Set drive strength */
    gpio_set_drive_capability(handle->gpio_num, (gpio_drive_cap_t)config->drive);
    
    /* Set initial level if output */
    if (config->direction == TS_GPIO_DIR_OUTPUT || 
        config->direction == TS_GPIO_DIR_OUTPUT_OD ||
        config->direction == TS_GPIO_DIR_BIDIRECTIONAL) {
        if (config->initial_level >= 0) {
            int level = config->invert ? !config->initial_level : config->initial_level;
            gpio_set_level(handle->gpio_num, level);
        }
    }
    
    handle->config = *config;
    handle->configured = true;
    
    TS_LOGD(TAG, "GPIO%d configured: dir=%d, pull=%d, intr=%d",
            handle->gpio_num, config->direction, config->pull_mode, config->intr_type);
    
    return ESP_OK;
}

esp_err_t ts_gpio_set_level(ts_gpio_handle_t handle, int level)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handle->config.direction == TS_GPIO_DIR_INPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int actual_level = handle->config.invert ? !level : level;
    return gpio_set_level(handle->gpio_num, actual_level);
}

int ts_gpio_get_level(ts_gpio_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return -1;
    }
    
    int level = gpio_get_level(handle->gpio_num);
    return handle->config.invert ? !level : level;
}

esp_err_t ts_gpio_toggle(ts_gpio_handle_t handle)
{
    if (!s_initialized || handle == NULL || !handle->configured) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handle->config.direction == TS_GPIO_DIR_INPUT) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int current = gpio_get_level(handle->gpio_num);
    return gpio_set_level(handle->gpio_num, !current);
}

esp_err_t ts_gpio_set_direction(ts_gpio_handle_t handle, ts_gpio_dir_t direction)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = gpio_set_direction(handle->gpio_num, convert_direction(direction));
    if (ret == ESP_OK) {
        handle->config.direction = direction;
    }
    return ret;
}

esp_err_t ts_gpio_set_pull(ts_gpio_handle_t handle, ts_gpio_pull_t pull_mode)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = gpio_set_pull_mode(handle->gpio_num, convert_pull(pull_mode));
    if (ret == ESP_OK) {
        handle->config.pull_mode = pull_mode;
    }
    return ret;
}

esp_err_t ts_gpio_set_isr_callback(ts_gpio_handle_t handle,
                                    ts_gpio_isr_callback_t callback,
                                    void *user_data)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Install ISR service if not already done */
    if (!s_isr_service_installed) {
        esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            return ret;
        }
        s_isr_service_installed = true;
    }
    
    handle->isr_cb = callback;
    handle->isr_user_data = user_data;
    
    if (callback != NULL) {
        return gpio_isr_handler_add(handle->gpio_num, gpio_isr_handler, handle);
    } else {
        return gpio_isr_handler_remove(handle->gpio_num);
    }
}

esp_err_t ts_gpio_intr_enable(ts_gpio_handle_t handle)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return gpio_intr_enable(handle->gpio_num);
}

esp_err_t ts_gpio_intr_disable(ts_gpio_handle_t handle)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return gpio_intr_disable(handle->gpio_num);
}

int ts_gpio_get_num(ts_gpio_handle_t handle)
{
    if (handle == NULL) {
        return -1;
    }
    return handle->gpio_num;
}

esp_err_t ts_gpio_destroy(ts_gpio_handle_t handle)
{
    if (!s_initialized || handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    /* Find and remove from handles array */
    int slot = get_handle_slot(handle);
    if (slot >= 0) {
        s_handles[slot] = NULL;
    }
    
    xSemaphoreGive(s_mutex);
    
    /* Remove ISR handler if set */
    if (handle->isr_cb != NULL) {
        gpio_isr_handler_remove(handle->gpio_num);
    }
    
    /* Reset GPIO */
    gpio_reset_pin(handle->gpio_num);
    
    /* Release pin if using function */
    if (handle->using_function) {
        ts_pin_manager_release(handle->function);
    }
    
    TS_LOGD(TAG, "Destroyed GPIO handle for GPIO%d", handle->gpio_num);
    
    free(handle);
    return ESP_OK;
}
