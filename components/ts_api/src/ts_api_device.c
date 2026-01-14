/**
 * @file ts_api_device.c
 * @brief Device Control API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_api.h"
#include "ts_log.h"
#include "ts_device_ctrl.h"
#include "ts_fan.h"
#include "ts_power.h"
#include "ts_usb_mux.h"
#include <string.h>

#define TAG "api_device"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static const char *device_state_to_str(ts_device_state_t state)
{
    switch (state) {
        case TS_DEVICE_STATE_OFF:     return "off";
        case TS_DEVICE_STATE_STANDBY: return "standby";
        case TS_DEVICE_STATE_ON:      return "on";
        case TS_DEVICE_STATE_BOOTING: return "booting";
        case TS_DEVICE_STATE_ERROR:   return "error";
        default: return "unknown";
    }
}

static const char *fan_mode_to_str(ts_fan_mode_t mode)
{
    switch (mode) {
        case TS_FAN_MODE_OFF:    return "off";
        case TS_FAN_MODE_MANUAL: return "manual";
        case TS_FAN_MODE_AUTO:   return "auto";
        default: return "unknown";
    }
}

static const char *usb_target_to_str(ts_usb_target_t target)
{
    switch (target) {
        case TS_USB_TARGET_HOST:       return "host";
        case TS_USB_TARGET_DEVICE:     return "device";
        case TS_USB_TARGET_DISCONNECT: return "disconnected";
        default: return "unknown";
    }
}

/*===========================================================================*/
/*                          Device Control APIs                               */
/*===========================================================================*/

/**
 * @brief device.status - Get device status
 * @param device: device name ("agx", "lpmu")
 */
static esp_err_t api_device_status(const cJSON *params, ts_api_result_t *result)
{
    ts_device_id_t device_id = TS_DEVICE_AGX;
    
    cJSON *device = cJSON_GetObjectItem(params, "device");
    if (device && cJSON_IsString(device)) {
        if (strcmp(device->valuestring, "lpmu") == 0) {
            device_id = TS_DEVICE_LPMU;
        }
    }
    
    ts_device_status_t status;
    esp_err_t ret = ts_device_get_status(device_id, &status);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to get device status");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_id == TS_DEVICE_AGX ? "agx" : "lpmu");
    cJSON_AddStringToObject(data, "state", device_state_to_str(status.state));
    cJSON_AddBoolToObject(data, "power_good", status.power_good);
    cJSON_AddNumberToObject(data, "uptime_ms", status.uptime_ms);
    cJSON_AddNumberToObject(data, "boot_count", status.boot_count);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief device.power - Control device power
 * @param device: device name
 * @param action: "on", "off", "force_off"
 */
static esp_err_t api_device_power(const cJSON *params, ts_api_result_t *result)
{
    cJSON *action = cJSON_GetObjectItem(params, "action");
    if (!action || !cJSON_IsString(action)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'action' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_device_id_t device_id = TS_DEVICE_AGX;
    cJSON *device = cJSON_GetObjectItem(params, "device");
    if (device && cJSON_IsString(device)) {
        if (strcmp(device->valuestring, "lpmu") == 0) {
            device_id = TS_DEVICE_LPMU;
        }
    }
    
    esp_err_t ret = ESP_OK;
    const char *action_str = action->valuestring;
    
    if (strcmp(action_str, "on") == 0) {
        ret = ts_device_power_on(device_id);
    } else if (strcmp(action_str, "off") == 0) {
        ret = ts_device_power_off(device_id);
    } else if (strcmp(action_str, "force_off") == 0) {
        ret = ts_device_force_off(device_id);
    } else {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid action");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Power control failed");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_id == TS_DEVICE_AGX ? "agx" : "lpmu");
    cJSON_AddStringToObject(data, "action", action_str);
    cJSON_AddBoolToObject(data, "success", true);
    
    ts_api_result_ok(result, data);
    TS_LOGI(TAG, "Device %s: %s", device_id == TS_DEVICE_AGX ? "AGX" : "LPMU", action_str);
    return ESP_OK;
}

/**
 * @brief device.reset - Reset device
 * @param device: device name
 */
static esp_err_t api_device_reset(const cJSON *params, ts_api_result_t *result)
{
    ts_device_id_t device_id = TS_DEVICE_AGX;
    cJSON *device = cJSON_GetObjectItem(params, "device");
    if (device && cJSON_IsString(device)) {
        if (strcmp(device->valuestring, "lpmu") == 0) {
            device_id = TS_DEVICE_LPMU;
        }
    }
    
    esp_err_t ret = ts_device_reset(device_id);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Reset failed");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "device", device_id == TS_DEVICE_AGX ? "agx" : "lpmu");
    cJSON_AddBoolToObject(data, "reset", true);
    
    ts_api_result_ok(result, data);
    TS_LOGI(TAG, "Device %s reset", device_id == TS_DEVICE_AGX ? "AGX" : "LPMU");
    return ESP_OK;
}

/*===========================================================================*/
/*                          Fan Control APIs                                  */
/*===========================================================================*/

/**
 * @brief device.fan.status - Get fan status
 * @param fan: fan id (0-3) or "all"
 */
static esp_err_t api_device_fan_status(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    cJSON *fans = cJSON_AddArrayToObject(data, "fans");
    
    cJSON *fan_param = cJSON_GetObjectItem(params, "fan");
    int start = 0, end = TS_FAN_MAX;
    
    if (fan_param && cJSON_IsNumber(fan_param)) {
        int fan_id = (int)cJSON_GetNumberValue(fan_param);
        if (fan_id >= 0 && fan_id < TS_FAN_MAX) {
            start = fan_id;
            end = fan_id + 1;
        }
    }
    
    for (int i = start; i < end; i++) {
        ts_fan_status_t status;
        if (ts_fan_get_status(i, &status) == ESP_OK) {
            cJSON *fan = cJSON_CreateObject();
            cJSON_AddNumberToObject(fan, "id", i);
            cJSON_AddStringToObject(fan, "mode", fan_mode_to_str(status.mode));
            cJSON_AddNumberToObject(fan, "duty", status.duty_percent);
            cJSON_AddNumberToObject(fan, "rpm", status.rpm);
            cJSON_AddNumberToObject(fan, "temp", status.temp / 10.0);
            cJSON_AddBoolToObject(fan, "running", status.is_running);
            cJSON_AddItemToArray(fans, fan);
        }
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief device.fan.set - Set fan parameters
 * @param fan: fan id
 * @param mode: "off", "manual", "auto"
 * @param duty: duty cycle (0-100) for manual mode
 */
static esp_err_t api_device_fan_set(const cJSON *params, ts_api_result_t *result)
{
    cJSON *fan_param = cJSON_GetObjectItem(params, "fan");
    if (!fan_param || !cJSON_IsNumber(fan_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'fan' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    int fan_id = (int)cJSON_GetNumberValue(fan_param);
    if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid fan ID");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    cJSON *mode = cJSON_GetObjectItem(params, "mode");
    if (mode && cJSON_IsString(mode)) {
        ts_fan_mode_t fan_mode;
        if (strcmp(mode->valuestring, "off") == 0) {
            fan_mode = TS_FAN_MODE_OFF;
        } else if (strcmp(mode->valuestring, "manual") == 0) {
            fan_mode = TS_FAN_MODE_MANUAL;
        } else if (strcmp(mode->valuestring, "auto") == 0) {
            fan_mode = TS_FAN_MODE_AUTO;
        } else {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid mode");
            return ESP_ERR_INVALID_ARG;
        }
        ret = ts_fan_set_mode(fan_id, fan_mode);
    }
    
    cJSON *duty = cJSON_GetObjectItem(params, "duty");
    if (ret == ESP_OK && duty && cJSON_IsNumber(duty)) {
        int duty_val = (int)cJSON_GetNumberValue(duty);
        if (duty_val >= 0 && duty_val <= 100) {
            ret = ts_fan_set_duty(fan_id, duty_val);
        }
    }
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Fan control failed");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "fan", fan_id);
    cJSON_AddBoolToObject(data, "success", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Power Monitoring APIs                             */
/*===========================================================================*/

static const char *power_rail_names[] = {
    "vin", "5v", "3v3", "12v", "vbat"
};

/**
 * @brief device.power.status - Get power measurements
 * @param rail: rail name or "all"
 */
static esp_err_t api_device_power_status(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    cJSON *rails = cJSON_AddArrayToObject(data, "rails");
    
    ts_power_data_t power_data[TS_POWER_RAIL_MAX];
    ts_power_read_all(power_data);
    
    for (int i = 0; i < TS_POWER_RAIL_MAX; i++) {
        cJSON *rail = cJSON_CreateObject();
        cJSON_AddStringToObject(rail, "name", power_rail_names[i]);
        cJSON_AddNumberToObject(rail, "voltage_mv", power_data[i].voltage_mv);
        if (power_data[i].current_ma >= 0) {
            cJSON_AddNumberToObject(rail, "current_ma", power_data[i].current_ma);
        }
        if (power_data[i].power_mw >= 0) {
            cJSON_AddNumberToObject(rail, "power_mw", power_data[i].power_mw);
        }
        cJSON_AddItemToArray(rails, rail);
    }
    
    int32_t total_mw;
    if (ts_power_get_total(&total_mw) == ESP_OK && total_mw >= 0) {
        cJSON_AddNumberToObject(data, "total_power_mw", total_mw);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          USB MUX APIs                                      */
/*===========================================================================*/

/**
 * @brief device.usb.status - Get USB MUX status
 */
static esp_err_t api_device_usb_status(const cJSON *params, ts_api_result_t *result)
{
    cJSON *data = cJSON_CreateObject();
    cJSON *muxes = cJSON_AddArrayToObject(data, "muxes");
    
    for (int i = 0; i < TS_USB_MUX_MAX; i++) {
        ts_usb_mux_status_t status;
        if (ts_usb_mux_get_status(i, &status) == ESP_OK) {
            cJSON *mux = cJSON_CreateObject();
            cJSON_AddNumberToObject(mux, "id", i);
            cJSON_AddStringToObject(mux, "target", usb_target_to_str(status.target));
            cJSON_AddBoolToObject(mux, "enabled", status.enabled);
            cJSON_AddItemToArray(muxes, mux);
        }
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief device.usb.set - Set USB MUX target
 * @param mux: mux id (0-1)
 * @param target: "host", "device", "disconnect"
 */
static esp_err_t api_device_usb_set(const cJSON *params, ts_api_result_t *result)
{
    cJSON *mux_param = cJSON_GetObjectItem(params, "mux");
    cJSON *target_param = cJSON_GetObjectItem(params, "target");
    
    if (!mux_param || !cJSON_IsNumber(mux_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'mux' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!target_param || !cJSON_IsString(target_param)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'target' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    int mux_id = (int)cJSON_GetNumberValue(mux_param);
    if (mux_id < 0 || mux_id >= TS_USB_MUX_MAX) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid mux ID");
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_usb_target_t target;
    const char *target_str = target_param->valuestring;
    if (strcmp(target_str, "host") == 0) {
        target = TS_USB_TARGET_HOST;
    } else if (strcmp(target_str, "device") == 0) {
        target = TS_USB_TARGET_DEVICE;
    } else if (strcmp(target_str, "disconnect") == 0) {
        target = TS_USB_TARGET_DISCONNECT;
    } else {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid target");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ts_usb_mux_set_target(mux_id, target);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "USB MUX control failed");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "mux", mux_id);
    cJSON_AddStringToObject(data, "target", target_str);
    cJSON_AddBoolToObject(data, "success", true);
    
    ts_api_result_ok(result, data);
    TS_LOGI(TAG, "USB MUX %d set to %s", mux_id, target_str);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t device_endpoints[] = {
    {
        .name = "device.status",
        .description = "Get device power status",
        .category = TS_API_CAT_DEVICE,
        .handler = api_device_status,
        .requires_auth = false,
    },
    {
        .name = "device.power",
        .description = "Control device power (on/off/force_off)",
        .category = TS_API_CAT_DEVICE,
        .handler = api_device_power,
        .requires_auth = true,
        .permission = "device.control",
    },
    {
        .name = "device.reset",
        .description = "Reset device",
        .category = TS_API_CAT_DEVICE,
        .handler = api_device_reset,
        .requires_auth = true,
        .permission = "device.control",
    },
    {
        .name = "device.fan.status",
        .description = "Get fan status",
        .category = TS_API_CAT_FAN,
        .handler = api_device_fan_status,
        .requires_auth = false,
    },
    {
        .name = "device.fan.set",
        .description = "Set fan mode and duty",
        .category = TS_API_CAT_FAN,
        .handler = api_device_fan_set,
        .requires_auth = true,
        .permission = "device.control",
    },
    {
        .name = "device.power.status",
        .description = "Get power measurements",
        .category = TS_API_CAT_POWER,
        .handler = api_device_power_status,
        .requires_auth = false,
    },
    {
        .name = "device.usb.status",
        .description = "Get USB MUX status",
        .category = TS_API_CAT_DEVICE,
        .handler = api_device_usb_status,
        .requires_auth = false,
    },
    {
        .name = "device.usb.set",
        .description = "Set USB MUX target",
        .category = TS_API_CAT_DEVICE,
        .handler = api_device_usb_set,
        .requires_auth = true,
        .permission = "device.control",
    },
};

esp_err_t ts_api_device_register(void)
{
    TS_LOGI(TAG, "Registering device APIs");
    
    for (size_t i = 0; i < sizeof(device_endpoints) / sizeof(device_endpoints[0]); i++) {
        esp_err_t ret = ts_api_register(&device_endpoints[i]);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to register %s", device_endpoints[i].name);
            return ret;
        }
    }
    
    return ESP_OK;
}
