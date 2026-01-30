/**
 * @file ts_api_ui.c
 * @brief UI Configuration API Handlers
 * 
 * WebUI 配置持久化 API
 * - ui.widgets.get: 获取数据监控组件配置
 * - ui.widgets.set: 保存数据监控组件配置
 * 
 * 配置优先级：SD卡 > NVS > 默认值
 * 
 * 加载逻辑：
 * 1. SD卡有配置 → 使用 SD 卡配置
 * 2. SD卡没有但 NVS 有 → 使用 NVS 配置，并同步复制到 SD 卡
 * 3. 都没有 → 返回默认空配置
 * 
 * 保存逻辑：同时写入 SD卡 + NVS（双写，确保持久化）
 * 
 * 存储位置：
 * - SD卡: /sdcard/config/ui_widgets.json
 * - NVS: ts_ui 命名空间，key="widgets"
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_api.h"
#include "ts_log.h"
#include "ts_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define TAG "api_ui"

/* 配置文件路径 */
#define UI_WIDGETS_FILE     "/sdcard/config/ui_widgets.json"
#define UI_WIDGETS_NVS_NS   "ts_ui"
#define UI_WIDGETS_NVS_KEY  "widgets"

/* 最大配置大小 (NVS blob 限制) */
#define UI_WIDGETS_MAX_SIZE 4000

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 从 SD 卡加载 widgets 配置
 * @return cJSON* 配置对象，失败返回 NULL（调用者负责释放）
 */
static cJSON *load_widgets_from_sdcard(void)
{
    FILE *f = fopen(UI_WIDGETS_FILE, "r");
    if (!f) {
        TS_LOGD(TAG, "SD card file not found: %s", UI_WIDGETS_FILE);
        return NULL;
    }
    
    /* 获取文件大小 */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0 || size > UI_WIDGETS_MAX_SIZE * 2) {
        TS_LOGW(TAG, "Invalid file size: %ld", size);
        fclose(f);
        return NULL;
    }
    
    /* 读取文件内容 */
    char *content = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
    if (!content) {
        content = malloc(size + 1);
    }
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(content, 1, size, f);
    fclose(f);
    content[read] = '\0';
    
    /* 解析 JSON */
    cJSON *json = cJSON_Parse(content);
    free(content);
    
    if (!json) {
        TS_LOGW(TAG, "Failed to parse SD card config");
        return NULL;
    }
    
    TS_LOGI(TAG, "Loaded widgets from SD card");
    return json;
}

/**
 * @brief 从 NVS 加载 widgets 配置
 * @return cJSON* 配置对象，失败返回 NULL（调用者负责释放）
 */
static cJSON *load_widgets_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(UI_WIDGETS_NVS_NS, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        TS_LOGD(TAG, "NVS namespace not found");
        return NULL;
    }
    
    /* 获取数据大小 */
    size_t size = 0;
    ret = nvs_get_blob(handle, UI_WIDGETS_NVS_KEY, NULL, &size);
    if (ret != ESP_OK || size == 0) {
        nvs_close(handle);
        return NULL;
    }
    
    /* 读取数据 */
    char *content = heap_caps_malloc(size + 1, MALLOC_CAP_SPIRAM);
    if (!content) {
        content = malloc(size + 1);
    }
    if (!content) {
        nvs_close(handle);
        return NULL;
    }
    
    ret = nvs_get_blob(handle, UI_WIDGETS_NVS_KEY, content, &size);
    nvs_close(handle);
    
    if (ret != ESP_OK) {
        free(content);
        return NULL;
    }
    content[size] = '\0';
    
    /* 解析 JSON */
    cJSON *json = cJSON_Parse(content);
    free(content);
    
    if (!json) {
        TS_LOGW(TAG, "Failed to parse NVS config");
        return NULL;
    }
    
    TS_LOGI(TAG, "Loaded widgets from NVS");
    return json;
}

/**
 * @brief 保存 widgets 配置到 SD 卡
 * @param json 配置 JSON
 * @return ESP_OK 成功
 */
static esp_err_t save_widgets_to_sdcard(cJSON *json)
{
    /* 确保目录存在 */
    struct stat st;
    if (stat("/sdcard/config", &st) != 0) {
        if (mkdir("/sdcard/config", 0755) != 0) {
            TS_LOGW(TAG, "Failed to create config dir: %s", strerror(errno));
            return ESP_FAIL;
        }
    }
    
    /* 生成 JSON 字符串 */
    char *content = cJSON_PrintUnformatted(json);
    if (!content) {
        return ESP_ERR_NO_MEM;
    }
    
    /* 写入文件 */
    FILE *f = fopen(UI_WIDGETS_FILE, "w");
    if (!f) {
        TS_LOGW(TAG, "Failed to open file for writing: %s", strerror(errno));
        cJSON_free(content);
        return ESP_FAIL;
    }
    
    size_t written = fwrite(content, 1, strlen(content), f);
    fclose(f);
    cJSON_free(content);
    
    if (written == 0) {
        return ESP_FAIL;
    }
    
    TS_LOGI(TAG, "Saved widgets to SD card (%zu bytes)", written);
    return ESP_OK;
}

/**
 * @brief 保存 widgets 配置到 NVS
 * @param json 配置 JSON
 * @return ESP_OK 成功
 */
static esp_err_t save_widgets_to_nvs(cJSON *json)
{
    /* 生成 JSON 字符串 */
    char *content = cJSON_PrintUnformatted(json);
    if (!content) {
        return ESP_ERR_NO_MEM;
    }
    
    size_t len = strlen(content);
    if (len > UI_WIDGETS_MAX_SIZE) {
        TS_LOGW(TAG, "Config too large for NVS: %zu > %d", len, UI_WIDGETS_MAX_SIZE);
        cJSON_free(content);
        return ESP_ERR_INVALID_SIZE;
    }
    
    /* 打开 NVS */
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(UI_WIDGETS_NVS_NS, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        cJSON_free(content);
        return ret;
    }
    
    /* 写入 blob */
    ret = nvs_set_blob(handle, UI_WIDGETS_NVS_KEY, content, len);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    
    nvs_close(handle);
    cJSON_free(content);
    
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Saved widgets to NVS (%zu bytes)", len);
    }
    
    return ret;
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief ui.widgets.get - 获取数据监控组件配置
 * 
 * 加载优先级：SD卡 > NVS > 空配置
 * 
 * @return JSON:
 * {
 *   "widgets": [...],
 *   "refresh_interval": 5000,
 *   "source": "sdcard" | "nvs" | "default"
 * }
 */
static esp_err_t api_ui_widgets_get(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    cJSON *config = NULL;
    const char *source = "default";
    
    /* 配置优先级：SD卡 > NVS > 默认值 */
    /* 1. 优先从 SD 卡加载 */
    config = load_widgets_from_sdcard();
    if (config) {
        source = "sdcard";
    } else {
        /* 2. SD卡没有，尝试从 NVS 加载 */
        config = load_widgets_from_nvs();
        if (config) {
            source = "nvs";
            /* 同步 NVS 配置到 SD 卡，方便后续编辑 */
            if (save_widgets_to_sdcard(config) == ESP_OK) {
                TS_LOGI(TAG, "Synced NVS config to SD card");
            }
        }
    }
    
    /* 构建响应 */
    cJSON *data = cJSON_CreateObject();
    
    if (config) {
        /* 提取 widgets 数组 */
        cJSON *widgets = cJSON_GetObjectItem(config, "widgets");
        if (widgets && cJSON_IsArray(widgets)) {
            cJSON_AddItemToObject(data, "widgets", cJSON_Duplicate(widgets, true));
        } else {
            cJSON_AddArrayToObject(data, "widgets");
        }
        
        /* 提取刷新间隔 */
        cJSON *interval = cJSON_GetObjectItem(config, "refresh_interval");
        if (interval && cJSON_IsNumber(interval)) {
            cJSON_AddNumberToObject(data, "refresh_interval", interval->valueint);
        } else {
            cJSON_AddNumberToObject(data, "refresh_interval", 5000);
        }
        
        cJSON_Delete(config);
    } else {
        /* 默认空配置 */
        cJSON_AddArrayToObject(data, "widgets");
        cJSON_AddNumberToObject(data, "refresh_interval", 5000);
    }
    
    cJSON_AddStringToObject(data, "source", source);
    
    ts_api_result_ok(result, data);
    TS_LOGI(TAG, "Loaded UI widgets config (source: %s)", source);
    return ESP_OK;
}

/**
 * @brief ui.widgets.set - 保存数据监控组件配置
 * 
 * 双写：SD卡 + NVS
 * 
 * @param params JSON:
 * {
 *   "widgets": [...],
 *   "refresh_interval": 5000
 * }
 */
static esp_err_t api_ui_widgets_set(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 构建配置对象 */
    cJSON *config = cJSON_CreateObject();
    
    /* 复制 widgets 数组 */
    cJSON *widgets = cJSON_GetObjectItem(params, "widgets");
    if (widgets && cJSON_IsArray(widgets)) {
        cJSON_AddItemToObject(config, "widgets", cJSON_Duplicate(widgets, true));
    } else {
        cJSON_AddArrayToObject(config, "widgets");
    }
    
    /* 复制刷新间隔 */
    cJSON *interval = cJSON_GetObjectItem(params, "refresh_interval");
    if (interval && cJSON_IsNumber(interval)) {
        cJSON_AddNumberToObject(config, "refresh_interval", interval->valueint);
    } else {
        cJSON_AddNumberToObject(config, "refresh_interval", 5000);
    }
    
    /* 双写：SD卡 + NVS */
    esp_err_t sd_ret = save_widgets_to_sdcard(config);
    esp_err_t nvs_ret = save_widgets_to_nvs(config);
    
    cJSON_Delete(config);
    
    /* 响应 */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "sdcard_saved", sd_ret == ESP_OK);
    cJSON_AddBoolToObject(data, "nvs_saved", nvs_ret == ESP_OK);
    
    if (sd_ret != ESP_OK && nvs_ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to save config");
        cJSON_Delete(data);
        return ESP_FAIL;
    }
    
    ts_api_result_ok(result, data);
    TS_LOGI(TAG, "Saved UI widgets config (sdcard=%s, nvs=%s)", 
            sd_ret == ESP_OK ? "ok" : "fail",
            nvs_ret == ESP_OK ? "ok" : "fail");
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t ui_endpoints[] = {
    {
        .name = "ui.widgets.get",
        .description = "Get data widgets configuration",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_ui_widgets_get,
        .requires_auth = false,
    },
    {
        .name = "ui.widgets.set",
        .description = "Save data widgets configuration",
        .category = TS_API_CAT_SYSTEM,
        .handler = api_ui_widgets_set,
        .requires_auth = false,  /* WebUI 内部使用，暂不要求认证 */
    },
};

esp_err_t ts_api_ui_register(void)
{
    TS_LOGI(TAG, "Registering UI APIs");
    
    for (size_t i = 0; i < sizeof(ui_endpoints) / sizeof(ui_endpoints[0]); i++) {
        esp_err_t ret = ts_api_register(&ui_endpoints[i]);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to register %s", ui_endpoints[i].name);
            return ret;
        }
    }
    
    return ESP_OK;
}
