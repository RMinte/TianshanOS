/**
 * @file root_password_reset_main.c
 * @brief One-purpose firmware for restoring the root password credential.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ts_auth_layout.h"

#define TAG "root_reset"

#ifndef TIANSHAN_OS_VERSION_FULL
#define TIANSHAN_OS_VERSION_FULL "unknown"
#endif

#ifndef ROOT_PASSWORD_RESET_RUN_ID
#define ROOT_PASSWORD_RESET_RUN_ID "unknown"
#endif

static bool string_contains(const char *haystack, const char *needle)
{
    if (!haystack || !needle) {
        return false;
    }

    return strstr(haystack, needle) != NULL;
}

static void halt_forever(void)
{
    ESP_LOGE(TAG, "Recovery firmware halted. NVS was not erased.");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void halt_on_error(const char *operation, esp_err_t ret)
{
    ESP_LOGE(TAG, "%s failed: %s (0x%x)", operation, esp_err_to_name(ret), (unsigned)ret);
    halt_forever();
}

static esp_err_t compute_root_hash(const uint8_t salt[TS_AUTH_SALT_LEN],
                                   uint8_t hash_out[TS_AUTH_HASH_LEN])
{
    static const char default_password[] = TS_AUTH_DEFAULT_ROOT_PASSWORD;
    uint8_t material[TS_AUTH_SALT_LEN + sizeof(default_password) - 1];

    memcpy(material, salt, TS_AUTH_SALT_LEN);
    memcpy(material + TS_AUTH_SALT_LEN, default_password, sizeof(default_password) - 1);

    int ret = mbedtls_sha256(material, sizeof(material), hash_out, 0);
    memset(material, 0, sizeof(material));

    return ret == 0 ? ESP_OK : ESP_FAIL;
}

static bool constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;

    for (size_t i = 0; i < len; ++i) {
        diff |= a[i] ^ b[i];
    }

    return diff == 0;
}

static esp_err_t verify_auth_config_version(nvs_handle_t handle)
{
    uint8_t stored_version = 0;
    esp_err_t ret = nvs_get_u8(handle, TS_AUTH_CONFIG_VERSION_KEY, &stored_version);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "cfg_version key not found - auth module was never initialized on this device");
        }
        return ret;
    }

    if (stored_version != TS_AUTH_CONFIG_VERSION) {
        ESP_LOGE(TAG, "Auth config version mismatch: expected %u, got %u",
                 (unsigned)TS_AUTH_CONFIG_VERSION, (unsigned)stored_version);
        ESP_LOGE(TAG, "Refusing to proceed because normal firmware may reset all auth users");
        return ESP_ERR_INVALID_VERSION;
    }

    return ESP_OK;
}

static void reset_root_credential(void)
{
    ESP_LOGI(TAG, "Initializing NVS");
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK) {
        halt_on_error("nvs_flash_init", ret);
    }

    nvs_handle_t handle = 0;
    bool handle_open = false;
    ret = nvs_open(TS_AUTH_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        halt_on_error("nvs_open(ts_auth)", ret);
    }
    handle_open = true;

    const char *failed_operation = NULL;

    ret = verify_auth_config_version(handle);
    if (ret != ESP_OK) {
        failed_operation = "verify_auth_config_version";
        goto fail;
    }

    ts_auth_user_credential_t credential = {0};
    esp_fill_random(credential.salt, TS_AUTH_SALT_LEN);

    ret = compute_root_hash(credential.salt, credential.hash);
    if (ret != ESP_OK) {
        failed_operation = "compute_root_hash";
        goto fail;
    }

    credential.password_changed = false;
    credential.failed_attempts = 0;
    credential.lockout_until = 0;

    ESP_LOGI(TAG, "Writing NVS blob %s/%s", TS_AUTH_NVS_NAMESPACE, TS_AUTH_ROOT_CRED_KEY);
    ret = nvs_set_blob(handle, TS_AUTH_ROOT_CRED_KEY, &credential, sizeof(credential));
    if (ret != ESP_OK) {
        failed_operation = "nvs_set_blob(cred_root)";
        goto fail;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        failed_operation = "nvs_commit";
        goto fail;
    }
    ESP_LOGI(TAG, "NVS commit complete");

    ts_auth_user_credential_t readback = {0};
    size_t readback_len = sizeof(readback);
    ret = nvs_get_blob(handle, TS_AUTH_ROOT_CRED_KEY, &readback, &readback_len);
    nvs_close(handle);
    handle_open = false;
    if (ret != ESP_OK) {
        halt_on_error("nvs_get_blob(cred_root)", ret);
    }
    if (readback_len != sizeof(readback)) {
        ESP_LOGE(TAG, "Readback length mismatch: expected %u, got %u",
                 (unsigned)sizeof(readback), (unsigned)readback_len);
        halt_forever();
    }

    uint8_t expected_hash[TS_AUTH_HASH_LEN] = {0};
    ret = compute_root_hash(readback.salt, expected_hash);
    if (ret != ESP_OK) {
        halt_on_error("compute_root_hash(readback)", ret);
    }

    bool hash_ok = constant_time_equal(readback.hash, expected_hash, TS_AUTH_HASH_LEN);
    memset(expected_hash, 0, sizeof(expected_hash));

    if (!hash_ok ||
        readback.password_changed ||
        readback.failed_attempts != 0 ||
        readback.lockout_until != 0) {
        ESP_LOGE(TAG, "Readback verification failed");
        halt_forever();
    }

    ESP_LOGI(TAG, "Root credential restored to default password");
    return;

fail:
    if (handle_open) {
        nvs_close(handle);
    }
    halt_on_error(failed_operation, ret);
}

static bool ota_state_rejected(const esp_partition_t *partition)
{
    esp_ota_img_states_t state = 0;
    esp_err_t ret = esp_ota_get_state_partition(partition, &state);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Target OTA state: %d", state);
        return state == ESP_OTA_IMG_INVALID || state == ESP_OTA_IMG_ABORTED;
    }

    ESP_LOGW(TAG, "Target OTA state unavailable: %s (0x%x)",
             esp_err_to_name(ret), (unsigned)ret);
    return false;
}

static const esp_partition_t *find_return_partition(const esp_partition_t *running)
{
    esp_partition_subtype_t target_subtype;

    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        target_subtype = ESP_PARTITION_SUBTYPE_APP_OTA_1;
    } else if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        target_subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0;
    } else {
        ESP_LOGE(TAG, "Running app is not an OTA slot: subtype=0x%x", running->subtype);
        return NULL;
    }

    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, target_subtype, NULL);
}

static void switch_back_to_normal_firmware(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "Cannot resolve running partition");
        halt_forever();
    }

    ESP_LOGI(TAG, "Running partition: label=%s subtype=0x%x address=0x%lx size=0x%lx",
             running->label, running->subtype,
             (unsigned long)running->address, (unsigned long)running->size);

    const esp_partition_t *target = find_return_partition(running);
    if (!target) {
        ESP_LOGE(TAG, "No alternate OTA app partition found. Flash normal firmware manually.");
        halt_forever();
    }

    esp_app_desc_t target_desc = {0};
    esp_err_t ret = esp_ota_get_partition_description(target, &target_desc);
    if (ret != ESP_OK) {
        halt_on_error("esp_ota_get_partition_description", ret);
    }

    if (ota_state_rejected(target)) {
        ESP_LOGE(TAG, "Refusing to boot target partition because it is invalid or aborted");
        halt_forever();
    }

    ESP_LOGI(TAG, "Return target: label=%s subtype=0x%x address=0x%lx size=0x%lx",
             target->label, target->subtype,
             (unsigned long)target->address, (unsigned long)target->size);
    ESP_LOGI(TAG, "Return target app: project=%s version=%s",
             target_desc.project_name, target_desc.version);
    if (string_contains(target_desc.version, "root-reset")) {
        ESP_LOGE(TAG, "Refusing to boot another root-reset firmware");
        halt_forever();
    }

    ret = esp_ota_set_boot_partition(target);
    if (ret != ESP_OK) {
        halt_on_error("esp_ota_set_boot_partition", ret);
    }

    ESP_LOGI(TAG, "Boot partition updated. Restarting in 3 seconds.");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

void app_main(void)
{
    ESP_LOGI(TAG, "TianShanOS root password reset firmware");
    ESP_LOGI(TAG, "Version: %s", TIANSHAN_OS_VERSION_FULL);
    ESP_LOGI(TAG, "Run ID: %s", ROOT_PASSWORD_RESET_RUN_ID);
    ESP_LOGW(TAG, "Only root credential will be modified; NVS erase is never performed.");

    reset_root_credential();
    switch_back_to_normal_firmware();
}
