/**
 * @file ts_api_cert.c
 * @brief PKI Certificate Management API Handlers
 * 
 * Provides Core API endpoints for HTTPS certificate management:
 * - cert.status: Get current PKI status
 * - cert.generate_keypair: Generate ECDSA P-256 key pair
 * - cert.generate_csr: Generate Certificate Signing Request
 * - cert.install: Install signed certificate
 * - cert.install_ca: Install CA certificate chain
 * - cert.delete: Delete all PKI credentials
 * - cert.get_csr: Get the last generated CSR
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include "ts_api.h"
#include "ts_cert.h"
#include "ts_log.h"
#include <string.h>
#include <time.h>
#include <esp_heap_caps.h>

#define TAG "api_cert"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief Convert status enum to user-friendly string
 */
static const char *status_to_display(ts_cert_status_t status)
{
    switch (status) {
        case TS_CERT_STATUS_NOT_INITIALIZED:
            return "未初始化";
        case TS_CERT_STATUS_KEY_GENERATED:
            return "密钥已生成，等待 CSR";
        case TS_CERT_STATUS_CSR_PENDING:
            return "CSR 已生成，等待签发";
        case TS_CERT_STATUS_ACTIVATED:
            return "已激活";
        case TS_CERT_STATUS_EXPIRED:
            return "已过期";
        case TS_CERT_STATUS_ERROR:
            return "错误";
        default:
            return "未知";
    }
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief cert.status - Get PKI certificate status
 * 
 * Returns:
 * {
 *   "status": "activated",
 *   "status_display": "已激活",
 *   "has_private_key": true,
 *   "has_certificate": true,
 *   "has_ca_chain": true,
 *   "cert_info": {
 *     "subject_cn": "TIANSHAN-RM01-0001",
 *     "issuer_cn": "TianShanOS CA",
 *     "not_before": 1737705600,
 *     "not_after": 1769241600,
 *     "serial": "01:23:45:67:89",
 *     "is_valid": true,
 *     "days_until_expiry": 365
 *   }
 * }
 */
static esp_err_t api_cert_status(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    TS_LOGI(TAG, "API: cert.status called");
    
    ts_cert_pki_status_t pki_status;
    esp_err_t ret = ts_cert_get_status(&pki_status);
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, 
            "Failed to get PKI status");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }
    
    // 基本状态
    cJSON_AddStringToObject(data, "status", ts_cert_status_to_str(pki_status.status));
    cJSON_AddStringToObject(data, "status_display", status_to_display(pki_status.status));
    cJSON_AddBoolToObject(data, "has_private_key", pki_status.has_private_key);
    cJSON_AddBoolToObject(data, "has_certificate", pki_status.has_certificate);
    cJSON_AddBoolToObject(data, "has_ca_chain", pki_status.has_ca_chain);
    
    // 如果有证书，添加证书信息
    if (pki_status.has_certificate) {
        cJSON *cert_info = cJSON_CreateObject();
        cJSON_AddStringToObject(cert_info, "subject_cn", pki_status.cert_info.subject_cn);
        cJSON_AddStringToObject(cert_info, "issuer_cn", pki_status.cert_info.issuer_cn);
        cJSON_AddNumberToObject(cert_info, "not_before", (double)pki_status.cert_info.not_before);
        cJSON_AddNumberToObject(cert_info, "not_after", (double)pki_status.cert_info.not_after);
        cJSON_AddStringToObject(cert_info, "serial", pki_status.cert_info.serial);
        cJSON_AddBoolToObject(cert_info, "is_valid", pki_status.cert_info.is_valid);
        cJSON_AddNumberToObject(cert_info, "days_until_expiry", pki_status.cert_info.days_until_expiry);
        
        // 计算是否即将过期（30 天内）
        cJSON_AddBoolToObject(cert_info, "expires_soon", 
            pki_status.cert_info.days_until_expiry >= 0 && 
            pki_status.cert_info.days_until_expiry < 30);
        
        cJSON_AddItemToObject(data, "cert_info", cert_info);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief cert.generate_keypair - Generate ECDSA P-256 key pair
 * 
 * Params: none
 * 
 * Returns:
 * {
 *   "success": true,
 *   "message": "Key pair generated successfully"
 * }
 */
static esp_err_t api_cert_generate_keypair(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    TS_LOGI(TAG, "API: cert.generate_keypair called");
    
    // 检查是否已有密钥
    if (ts_cert_has_keypair()) {
        // 不自动覆盖，需要先删除
        const cJSON *force = cJSON_GetObjectItem(params, "force");
        if (!force || !cJSON_IsTrue(force)) {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG,
                "Key pair already exists. Use force=true to overwrite.");
            return ESP_ERR_INVALID_STATE;
        }
        TS_LOGW(TAG, "Overwriting existing key pair");
    }
    
    esp_err_t ret = ts_cert_generate_keypair();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to generate key pair: %s", esp_err_to_name(ret));
        ts_api_result_error(result, TS_API_ERR_INTERNAL,
            "Failed to generate key pair");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "message", "ECDSA P-256 key pair generated successfully");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief cert.generate_csr - Generate Certificate Signing Request
 * 
 * Params:
 * {
 *   "device_id": "TIANSHAN-RM01-0001",  // optional, uses config default
 *   "organization": "HiddenPeak Labs",   // optional
 *   "org_unit": "Device"                 // optional
 * }
 * 
 * Returns:
 * {
 *   "success": true,
 *   "csr_pem": "-----BEGIN CERTIFICATE REQUEST-----\n..."
 * }
 */
static esp_err_t api_cert_generate_csr(const cJSON *params, ts_api_result_t *result)
{
    TS_LOGI(TAG, "API: cert.generate_csr called");
    
    // 检查是否有密钥
    if (!ts_cert_has_keypair()) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG,
            "No private key exists. Generate key pair first.");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 分配 CSR 缓冲区（从 PSRAM）
    char *csr_pem = heap_caps_malloc(TS_CERT_CSR_MAX_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!csr_pem) {
        csr_pem = malloc(TS_CERT_CSR_MAX_LEN);
        if (!csr_pem) {
            ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }
    
    size_t csr_len = TS_CERT_CSR_MAX_LEN;
    esp_err_t ret;
    
    // 如果提供了参数，使用自定义选项
    const cJSON *device_id_obj = cJSON_GetObjectItem(params, "device_id");
    const cJSON *org_obj = cJSON_GetObjectItem(params, "organization");
    const cJSON *ou_obj = cJSON_GetObjectItem(params, "org_unit");
    
    if (device_id_obj || org_obj || ou_obj) {
        ts_cert_csr_opts_t opts = {0};
        opts.device_id = cJSON_IsString(device_id_obj) ? device_id_obj->valuestring : NULL;
        opts.organization = cJSON_IsString(org_obj) ? org_obj->valuestring : NULL;
        opts.org_unit = cJSON_IsString(ou_obj) ? ou_obj->valuestring : NULL;
        
        ret = ts_cert_generate_csr(&opts, csr_pem, &csr_len);
    } else {
        // 使用默认选项
        ret = ts_cert_generate_csr_default(csr_pem, &csr_len);
    }
    
    if (ret != ESP_OK) {
        free(csr_pem);
        TS_LOGE(TAG, "Failed to generate CSR: %s", esp_err_to_name(ret));
        ts_api_result_error(result, TS_API_ERR_INTERNAL, 
            "Failed to generate CSR");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "csr_pem", csr_pem);
    
    free(csr_pem);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief cert.install - Install signed certificate
 * 
 * Params:
 * {
 *   "cert_pem": "-----BEGIN CERTIFICATE-----\n..."
 * }
 */
static esp_err_t api_cert_install(const cJSON *params, ts_api_result_t *result)
{
    TS_LOGI(TAG, "API: cert.install called");
    
    const cJSON *cert_obj = cJSON_GetObjectItem(params, "cert_pem");
    if (!cert_obj || !cJSON_IsString(cert_obj)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG,
            "Missing required parameter: cert_pem");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *cert_pem = cert_obj->valuestring;
    size_t cert_len = strlen(cert_pem);
    
    esp_err_t ret = ts_cert_install_certificate(cert_pem, cert_len);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to install certificate: %s", esp_err_to_name(ret));
        
        const char *err_msg = "Failed to install certificate";
        if (ret == ESP_ERR_INVALID_ARG) {
            err_msg = "Invalid certificate format";
        } else if (ret == ESP_ERR_INVALID_STATE) {
            err_msg = "Certificate does not match private key";
        }
        
        ts_api_result_error(result, TS_API_ERR_INTERNAL, err_msg);
        return ret;
    }
    
    // 获取安装后的证书信息
    ts_cert_info_t info;
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "message", "Certificate installed successfully");
    
    if (ts_cert_get_info(&info) == ESP_OK) {
        cJSON *cert_info = cJSON_CreateObject();
        cJSON_AddStringToObject(cert_info, "subject_cn", info.subject_cn);
        cJSON_AddStringToObject(cert_info, "issuer_cn", info.issuer_cn);
        cJSON_AddNumberToObject(cert_info, "not_before", (double)info.not_before);
        cJSON_AddNumberToObject(cert_info, "not_after", (double)info.not_after);
        cJSON_AddNumberToObject(cert_info, "days_until_expiry", info.days_until_expiry);
        cJSON_AddItemToObject(data, "cert_info", cert_info);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief cert.install_ca - Install CA certificate chain
 * 
 * Params:
 * {
 *   "ca_pem": "-----BEGIN CERTIFICATE-----\n..."
 * }
 */
static esp_err_t api_cert_install_ca(const cJSON *params, ts_api_result_t *result)
{
    TS_LOGI(TAG, "API: cert.install_ca called");
    
    const cJSON *ca_obj = cJSON_GetObjectItem(params, "ca_pem");
    if (!ca_obj || !cJSON_IsString(ca_obj)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG,
            "Missing required parameter: ca_pem");
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *ca_pem = ca_obj->valuestring;
    size_t ca_len = strlen(ca_pem);
    
    esp_err_t ret = ts_cert_install_ca_chain(ca_pem, ca_len);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to install CA chain: %s", esp_err_to_name(ret));
        ts_api_result_error(result, TS_API_ERR_INTERNAL,
            "Failed to install CA certificate chain");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "message", "CA chain installed successfully");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief cert.delete - Delete all PKI credentials
 * 
 * WARNING: This removes private key, certificate and CA chain.
 */
static esp_err_t api_cert_delete(const cJSON *params, ts_api_result_t *result)
{
    TS_LOGW(TAG, "API: cert.delete called - factory reset");
    
    // 需要确认参数
    const cJSON *confirm = cJSON_GetObjectItem(params, "confirm");
    if (!confirm || !cJSON_IsTrue(confirm)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG,
            "Missing confirm=true parameter. This will delete all PKI credentials.");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ts_cert_factory_reset();
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to delete PKI credentials: %s", esp_err_to_name(ret));
        ts_api_result_error(result, TS_API_ERR_INTERNAL,
            "Failed to delete PKI credentials");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", true);
    cJSON_AddStringToObject(data, "message", "All PKI credentials deleted");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief cert.get_certificate - Get the device certificate (PEM)
 */
static esp_err_t api_cert_get_certificate(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    TS_LOGI(TAG, "API: cert.get_certificate called");
    
    char *cert_pem = heap_caps_malloc(TS_CERT_PEM_MAX_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cert_pem) {
        cert_pem = malloc(TS_CERT_PEM_MAX_LEN);
        if (!cert_pem) {
            ts_api_result_error(result, TS_API_ERR_NO_MEM, "Memory allocation failed");
            return ESP_ERR_NO_MEM;
        }
    }
    
    size_t cert_len = TS_CERT_PEM_MAX_LEN;
    esp_err_t ret = ts_cert_get_certificate(cert_pem, &cert_len);
    
    if (ret == ESP_ERR_NOT_FOUND) {
        free(cert_pem);
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND,
            "No certificate installed");
        return ret;
    }
    
    if (ret != ESP_OK) {
        free(cert_pem);
        ts_api_result_error(result, TS_API_ERR_INTERNAL,
            "Failed to get certificate");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "cert_pem", cert_pem);
    
    free(cert_pem);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_api_cert_register(void)
{
    static const ts_api_endpoint_t cert_apis[] = {
        {
            .name = "cert.status",
            .description = "Get PKI certificate status",
            .category = TS_API_CAT_SECURITY,
            .handler = api_cert_status,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "cert.generate_keypair",
            .description = "Generate ECDSA P-256 key pair",
            .category = TS_API_CAT_SECURITY,
            .handler = api_cert_generate_keypair,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "cert.generate_csr",
            .description = "Generate Certificate Signing Request",
            .category = TS_API_CAT_SECURITY,
            .handler = api_cert_generate_csr,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "cert.install",
            .description = "Install signed certificate",
            .category = TS_API_CAT_SECURITY,
            .handler = api_cert_install,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "cert.install_ca",
            .description = "Install CA certificate chain",
            .category = TS_API_CAT_SECURITY,
            .handler = api_cert_install_ca,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "cert.delete",
            .description = "Delete all PKI credentials",
            .category = TS_API_CAT_SECURITY,
            .handler = api_cert_delete,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "cert.get_certificate",
            .description = "Get device certificate PEM",
            .category = TS_API_CAT_SECURITY,
            .handler = api_cert_get_certificate,
            .requires_auth = false,
            .permission = NULL
        }
    };
    
    esp_err_t ret = ts_api_register_multiple(cert_apis, 
        sizeof(cert_apis) / sizeof(cert_apis[0]));
    
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Certificate APIs registered");
    }
    
    return ret;
}
