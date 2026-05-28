/**
 * @file ts_api_lpmu_access.c
 * @brief LPMU upper-network access API
 */

#include "ts_api.h"
#include "ts_core.h"
#include "ts_keystore.h"
#include "ts_known_hosts.h"
#include "ts_scp.h"
#include "ts_ssh_client.h"
#include "ts_ssh_hosts_config.h"
#include "ts_usb_mux.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "api_lpmu"

#define LPMU_HOST              "10.10.99.99"
#define LPMU_PORT              22
#define LPMU_REMOTE_TARBALL    "lpmu-agx-network-setup.tar.gz"
#define LPMU_STACK_SIZE        8192
#define LPMU_OUTPUT_TAIL_MAX   1024

extern const uint8_t lpmu_archive_start[] asm("_binary_lpmu_agx_network_setup_tar_gz_start");
extern const uint8_t lpmu_archive_end[] asm("_binary_lpmu_agx_network_setup_tar_gz_end");

typedef enum {
    LPMU_STAGE_IDLE = 0,
    LPMU_STAGE_QUEUED,
    LPMU_STAGE_FINDING_HOST,
    LPMU_STAGE_LOADING_KEY,
    LPMU_STAGE_CONNECTING,
    LPMU_STAGE_VERIFYING_HOST,
    LPMU_STAGE_CHECKING_REMOTE,
    LPMU_STAGE_UPLOADING,
    LPMU_STAGE_EXTRACTING,
    LPMU_STAGE_CHMOD,
    LPMU_STAGE_RUNNING_SCRIPT,
    LPMU_STAGE_SUCCESS,
    LPMU_STAGE_FAILED,
} lpmu_stage_t;

typedef struct {
    uint32_t run_id;
    bool running;
    lpmu_stage_t stage;
    esp_err_t esp_error;
    int exit_code;
    uint32_t bytes_transferred;
    uint32_t bytes_total;
    uint32_t output_bytes;
    size_t output_len;
    char last_error[160];
    char output_tail[LPMU_OUTPUT_TAIL_MAX];
} lpmu_status_t;

static SemaphoreHandle_t s_lpmu_mutex = NULL;
static TaskHandle_t s_lpmu_task = NULL;
static uint32_t s_next_run_id = 0;
static lpmu_status_t s_status = {
    .stage = LPMU_STAGE_IDLE,
    .exit_code = -1,
};

static size_t lpmu_archive_size(void)
{
    return (size_t)(lpmu_archive_end - lpmu_archive_start);
}

static void lpmu_secure_zero(void *ptr, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) {
        *p++ = 0;
    }
}

static const char *lpmu_stage_str(lpmu_stage_t stage)
{
    switch (stage) {
        case LPMU_STAGE_IDLE: return "idle";
        case LPMU_STAGE_QUEUED: return "queued";
        case LPMU_STAGE_FINDING_HOST: return "finding_host";
        case LPMU_STAGE_LOADING_KEY: return "loading_key";
        case LPMU_STAGE_CONNECTING: return "connecting";
        case LPMU_STAGE_VERIFYING_HOST: return "verifying_host";
        case LPMU_STAGE_CHECKING_REMOTE: return "checking_remote";
        case LPMU_STAGE_UPLOADING: return "uploading";
        case LPMU_STAGE_EXTRACTING: return "extracting";
        case LPMU_STAGE_CHMOD: return "chmod";
        case LPMU_STAGE_RUNNING_SCRIPT: return "running_script";
        case LPMU_STAGE_SUCCESS: return "success";
        case LPMU_STAGE_FAILED: return "failed";
        default: return "unknown";
    }
}

static esp_err_t lpmu_ensure_mutex(void)
{
    if (s_lpmu_mutex) {
        return ESP_OK;
    }

    s_lpmu_mutex = xSemaphoreCreateMutex();
    return s_lpmu_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static void lpmu_lock(void)
{
    if (s_lpmu_mutex) {
        xSemaphoreTake(s_lpmu_mutex, portMAX_DELAY);
    }
}

static void lpmu_unlock(void)
{
    if (s_lpmu_mutex) {
        xSemaphoreGive(s_lpmu_mutex);
    }
}

static void lpmu_set_stage(lpmu_stage_t stage)
{
    lpmu_lock();
    s_status.stage = stage;
    lpmu_unlock();
}

static void lpmu_set_error_locked(esp_err_t err, int exit_code, const char *fmt, va_list args)
{
    s_status.stage = LPMU_STAGE_FAILED;
    s_status.esp_error = err;
    s_status.exit_code = exit_code;
    vsnprintf(s_status.last_error, sizeof(s_status.last_error), fmt, args);
}

static void lpmu_fail(esp_err_t err, int exit_code, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    lpmu_lock();
    lpmu_set_error_locked(err, exit_code, fmt, args);
    lpmu_unlock();
    va_end(args);
}

static void lpmu_success(int exit_code)
{
    lpmu_lock();
    s_status.stage = LPMU_STAGE_SUCCESS;
    s_status.esp_error = ESP_OK;
    s_status.exit_code = exit_code;
    s_status.last_error[0] = '\0';
    lpmu_unlock();
}

static void lpmu_append_tail_locked(const char *data, size_t len)
{
    const size_t cap = sizeof(s_status.output_tail) - 1;

    if (!data || len == 0 || cap == 0) {
        return;
    }

    if (UINT32_MAX - s_status.output_bytes < len) {
        s_status.output_bytes = UINT32_MAX;
    } else {
        s_status.output_bytes += (uint32_t)len;
    }

    if (len >= cap) {
        memcpy(s_status.output_tail, data + len - cap, cap);
        s_status.output_len = cap;
        s_status.output_tail[cap] = '\0';
        return;
    }

    if (s_status.output_len + len > cap) {
        size_t drop = s_status.output_len + len - cap;
        memmove(s_status.output_tail, s_status.output_tail + drop, s_status.output_len - drop);
        s_status.output_len -= drop;
    }

    memcpy(s_status.output_tail + s_status.output_len, data, len);
    s_status.output_len += len;
    s_status.output_tail[s_status.output_len] = '\0';
}

static void lpmu_output_cb(const char *data, size_t len, bool is_stderr, void *user_data)
{
    (void)is_stderr;
    (void)user_data;

    lpmu_lock();
    lpmu_append_tail_locked(data, len);
    lpmu_unlock();
}

static esp_err_t lpmu_run_command(ts_ssh_session_t session, const char *command, int *exit_code)
{
    int code = -1;
    esp_err_t ret = ts_ssh_exec_stream(session, command, lpmu_output_cb, NULL, &code);
    if (exit_code) {
        *exit_code = code;
    }
    return ret;
}

static bool lpmu_verify_known_host(ts_ssh_session_t session, char *err, size_t err_size)
{
    ts_host_verify_result_t verify_result = TS_HOST_VERIFY_ERROR;
    ts_known_host_t host_info = {0};
    esp_err_t ret = ts_known_hosts_verify(session, &verify_result, &host_info);

    if (ret != ESP_OK) {
        snprintf(err, err_size, "known-host verification failed: %s", esp_err_to_name(ret));
        return false;
    }

    if (verify_result == TS_HOST_VERIFY_OK) {
        return true;
    }

    if (verify_result == TS_HOST_VERIFY_NOT_FOUND) {
        snprintf(err, err_size, "known-host missing for %s:%d", LPMU_HOST, LPMU_PORT);
    } else if (verify_result == TS_HOST_VERIFY_MISMATCH) {
        snprintf(err, err_size, "known-host mismatch for %s:%d", LPMU_HOST, LPMU_PORT);
    } else {
        snprintf(err, err_size, "known-host verification error for %s:%d", LPMU_HOST, LPMU_PORT);
    }
    return false;
}

static void lpmu_task(void *arg)
{
    (void)arg;

    ts_ssh_session_t session = NULL;
    char *private_key = NULL;
    size_t private_key_len = 0;
    ts_ssh_host_config_t host_cfg = {0};
    int exit_code = -1;
    esp_err_t ret;
    char verify_error[128] = {0};

    lpmu_set_stage(LPMU_STAGE_FINDING_HOST);
    ret = ts_ssh_hosts_config_find(LPMU_HOST, LPMU_PORT, NULL, &host_cfg);
    if (ret != ESP_OK) {
        lpmu_fail(ret, -1, "SSH host %s:%d is not configured", LPMU_HOST, LPMU_PORT);
        goto cleanup;
    }

    if (!host_cfg.enabled || host_cfg.auth_type != TS_SSH_HOST_AUTH_KEY || !host_cfg.keyid[0]) {
        lpmu_fail(ESP_ERR_INVALID_STATE, -1, "SSH host %s:%d must use enabled key auth", LPMU_HOST, LPMU_PORT);
        goto cleanup;
    }

    lpmu_set_stage(LPMU_STAGE_LOADING_KEY);
    ret = ts_keystore_load_private_key(host_cfg.keyid, &private_key, &private_key_len);
    if (ret != ESP_OK) {
        lpmu_fail(ret, -1, "failed to load SSH key '%s': %s", host_cfg.keyid, esp_err_to_name(ret));
        goto cleanup;
    }

    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host_cfg.host;
    config.port = host_cfg.port ? host_cfg.port : LPMU_PORT;
    config.username = host_cfg.username;
    config.auth_method = TS_SSH_AUTH_PUBLICKEY;
    config.auth.key.private_key = (const uint8_t *)private_key;
    config.auth.key.private_key_len = private_key_len;
    config.auth.key.private_key_path = NULL;
    config.auth.key.passphrase = NULL;
    config.timeout_ms = 30000;
    config.verify_host_key = false;

    lpmu_set_stage(LPMU_STAGE_CONNECTING);
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        lpmu_fail(ret, -1, "failed to create SSH session: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        lpmu_fail(ret, -1, "SSH connect failed: %s", ts_ssh_get_error(session));
        goto cleanup;
    }

    lpmu_set_stage(LPMU_STAGE_VERIFYING_HOST);
    if (!lpmu_verify_known_host(session, verify_error, sizeof(verify_error))) {
        lpmu_fail(ESP_ERR_INVALID_STATE, -1, "%s", verify_error);
        goto cleanup;
    }

    lpmu_set_stage(LPMU_STAGE_CHECKING_REMOTE);
    ret = lpmu_run_command(session,
                           "test -f \"$HOME/lpmu-agx-network-setup/lpmu/setup-smart-route.sh\"",
                           &exit_code);
    if (ret != ESP_OK) {
        lpmu_fail(ret, exit_code, "remote check failed: %s", ts_ssh_get_error(session));
        goto cleanup;
    }

    if (exit_code != 0) {
        lpmu_set_stage(LPMU_STAGE_UPLOADING);
        lpmu_lock();
        s_status.bytes_transferred = 0;
        s_status.bytes_total = (uint32_t)lpmu_archive_size();
        lpmu_unlock();

        ret = ts_scp_send_buffer(session, lpmu_archive_start, lpmu_archive_size(),
                                 LPMU_REMOTE_TARBALL, 0644);
        if (ret != ESP_OK) {
            lpmu_fail(ret, -1, "SCP upload failed: %s", esp_err_to_name(ret));
            goto cleanup;
        }

        lpmu_lock();
        s_status.bytes_transferred = (uint32_t)lpmu_archive_size();
        lpmu_unlock();

        lpmu_set_stage(LPMU_STAGE_EXTRACTING);
        ret = lpmu_run_command(session,
                               "mkdir -p \"$HOME/lpmu-agx-network-setup\" && "
                               "tar -xzf \"$HOME/lpmu-agx-network-setup.tar.gz\" "
                               "-C \"$HOME/lpmu-agx-network-setup\" --strip-components=1",
                               &exit_code);
        if (ret != ESP_OK || exit_code != 0) {
            lpmu_fail(ret != ESP_OK ? ret : ESP_FAIL, exit_code,
                      "remote extract failed (exit=%d): %s", exit_code,
                      ret != ESP_OK ? ts_ssh_get_error(session) : "tar command failed");
            goto cleanup;
        }
    }

    lpmu_set_stage(LPMU_STAGE_CHMOD);
    ret = lpmu_run_command(session,
                           "chmod +x \"$HOME/lpmu-agx-network-setup/lpmu/\"*.sh",
                           &exit_code);
    if (ret != ESP_OK || exit_code != 0) {
        lpmu_fail(ret != ESP_OK ? ret : ESP_FAIL, exit_code,
                  "remote chmod failed (exit=%d): %s", exit_code,
                  ret != ESP_OK ? ts_ssh_get_error(session) : "chmod command failed");
        goto cleanup;
    }

    lpmu_set_stage(LPMU_STAGE_RUNNING_SCRIPT);
    ret = lpmu_run_command(session,
                           "cd \"$HOME/lpmu-agx-network-setup/lpmu\" && "
                           "sudo -n ./setup-smart-route.sh",
                           &exit_code);
    if (ret != ESP_OK || exit_code != 0) {
        lpmu_fail(ret != ESP_OK ? ret : ESP_FAIL, exit_code,
                  "setup-smart-route failed (exit=%d): %s", exit_code,
                  ret != ESP_OK ? ts_ssh_get_error(session) : "remote script failed");
        goto cleanup;
    }

    lpmu_success(exit_code);

cleanup:
    if (session) {
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
    }
    if (private_key) {
        lpmu_secure_zero(private_key, private_key_len);
        free(private_key);
    }

    lpmu_lock();
    s_status.running = false;
    s_lpmu_task = NULL;
    lpmu_unlock();

    vTaskDelete(NULL);
}

static esp_err_t api_lpmu_access_start(const cJSON *params, ts_api_result_t *result)
{
    (void)params;

    esp_err_t ret = lpmu_ensure_mutex();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Failed to initialize LPMU access state");
        return ret;
    }

    uint32_t run_id;
    lpmu_lock();
    if (s_status.running || s_lpmu_task != NULL) {
        lpmu_unlock();
        ts_api_result_error(result, TS_API_ERR_BUSY, "LPMU access task is already running");
        return ESP_ERR_INVALID_STATE;
    }

    if (!ts_usb_mux_is_configured()) {
        lpmu_unlock();
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "USB MUX 未配置，无法确认是否已切换到 LPMU");
        return ESP_ERR_INVALID_STATE;
    }

    if (ts_usb_mux_get_target() != TS_USB_MUX_LPMU) {
        lpmu_unlock();
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "请先将 USB 切换到 LPMU 后再接入上层网络");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_status, 0, sizeof(s_status));
    s_status.run_id = ++s_next_run_id;
    if (s_next_run_id == 0) {
        s_next_run_id = 1;
        s_status.run_id = 1;
    }
    s_status.running = true;
    s_status.stage = LPMU_STAGE_QUEUED;
    s_status.exit_code = -1;
    s_status.bytes_total = (uint32_t)lpmu_archive_size();
    run_id = s_status.run_id;
    lpmu_unlock();

    BaseType_t task_ret = xTaskCreate(lpmu_task, "lpmu_access", LPMU_STACK_SIZE,
                                      NULL, 5, &s_lpmu_task);
    if (task_ret != pdPASS) {
        lpmu_fail(ESP_ERR_NO_MEM, -1, "failed to create LPMU access task");
        lpmu_lock();
        s_status.running = false;
        s_lpmu_task = NULL;
        lpmu_unlock();
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Failed to create LPMU access task");
        return ESP_ERR_NO_MEM;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "run_id", run_id);
    cJSON_AddStringToObject(data, "stage", lpmu_stage_str(LPMU_STAGE_QUEUED));
    cJSON_AddBoolToObject(data, "running", true);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

static esp_err_t api_lpmu_access_status(const cJSON *params, ts_api_result_t *result)
{
    (void)params;

    esp_err_t ret = lpmu_ensure_mutex();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Failed to initialize LPMU access state");
        return ret;
    }

    lpmu_lock();
    lpmu_status_t snap = s_status;
    lpmu_unlock();

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "run_id", snap.run_id);
    cJSON_AddBoolToObject(data, "running", snap.running);
    cJSON_AddStringToObject(data, "stage", lpmu_stage_str(snap.stage));
    cJSON_AddNumberToObject(data, "exit_code", snap.exit_code);
    cJSON_AddNumberToObject(data, "esp_error", snap.esp_error);
    cJSON_AddNumberToObject(data, "bytes_transferred", snap.bytes_transferred);
    cJSON_AddNumberToObject(data, "bytes_total", snap.bytes_total);
    cJSON_AddNumberToObject(data, "output_bytes", snap.output_bytes);
    cJSON_AddBoolToObject(data, "output_truncated", snap.output_bytes >= LPMU_OUTPUT_TAIL_MAX);
    cJSON_AddStringToObject(data, "last_error", snap.last_error);
    cJSON_AddStringToObject(data, "output_tail", snap.output_tail);
    cJSON_AddStringToObject(data, "host", LPMU_HOST);
    cJSON_AddNumberToObject(data, "port", LPMU_PORT);

    ts_api_result_ok(result, data);
    return ESP_OK;
}

static const ts_api_endpoint_t lpmu_access_endpoints[] = {
    {
        .name = "network.lpmu_access.start",
        .description = "Start LPMU upper-network access setup",
        .category = TS_API_CAT_NETWORK,
        .handler = api_lpmu_access_start,
        .requires_auth = true,
        .permission = "network.config",
    },
    {
        .name = "network.lpmu_access.status",
        .description = "Get LPMU upper-network access setup status",
        .category = TS_API_CAT_NETWORK,
        .handler = api_lpmu_access_status,
        .requires_auth = true,
        .permission = "network.view",
    },
};

esp_err_t ts_api_lpmu_access_register(void)
{
    esp_err_t ret = lpmu_ensure_mutex();
    if (ret != ESP_OK) {
        return ret;
    }

    return ts_api_register_multiple(lpmu_access_endpoints,
                                    sizeof(lpmu_access_endpoints) / sizeof(lpmu_access_endpoints[0]));
}
