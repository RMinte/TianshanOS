/**
 * @file ts_cert.c
 * @brief TianShanOS PKI Certificate Management Implementation
 * 
 * 实现 X.509 证书和 CSR 操作，包括：
 * - ECDSA P-256 密钥对生成与存储
 * - CSR 生成（带 SAN IP 扩展）
 * - 证书安装与验证
 * - NVS 持久化存储
 * - CA 链保存到 SD 卡（供用户下载信任）
 * 
 * 内存分配优先使用 PSRAM
 */

#include "ts_cert.h"
#include "ts_crypto.h"
#include "ts_core.h"
#include "ts_time_sync.h"

#include "mbedtls/pk.h"
#include "mbedtls/x509_csr.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/oid.h"
#include "mbedtls/asn1write.h"
#include "mbedtls/error.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/inet.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>

static const char *TAG = "ts_cert";

/*===========================================================================*/
/*                              NVS Keys                                      */
/*===========================================================================*/

#define NVS_NAMESPACE           "ts_pki"
#define NVS_KEY_PRIVKEY         "privkey"
#define NVS_KEY_CERT            "cert"
#define NVS_KEY_CA_CHAIN        "ca_chain"
#define NVS_KEY_STATUS          "status"

/** CA chain file path on SD card for user download */
#define CA_CHAIN_SDCARD_PATH    "/sdcard/pki/ca-chain.crt"
#define CA_CHAIN_SDCARD_DIR     "/sdcard/pki"

/*===========================================================================*/
/*                          Static Variables                                  */
/*===========================================================================*/

static bool s_initialized = false;
static nvs_handle_t s_nvs_handle = 0;

/* Cached credentials (loaded on init) */
static char *s_private_key_pem = NULL;
static char *s_certificate_pem = NULL;
static char *s_ca_chain_pem = NULL;
static ts_cert_status_t s_status = TS_CERT_STATUS_NOT_INITIALIZED;

/* RNG context */
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_ctr_drbg;
static bool s_rng_initialized = false;

/*===========================================================================*/
/*                          Internal Functions                                */
/*===========================================================================*/

static esp_err_t init_rng(void)
{
    if (s_rng_initialized) return ESP_OK;
    
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_ctr_drbg);
    
    const char *pers = "ts_cert_csr";
    int ret = mbedtls_ctr_drbg_seed(&s_ctr_drbg, mbedtls_entropy_func, &s_entropy,
                                     (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "RNG seed failed: %s", err_buf);
        return ESP_FAIL;
    }
    
    s_rng_initialized = true;
    return ESP_OK;
}

static esp_err_t nvs_read_string(const char *key, char **out_str)
{
    size_t required_size = 0;
    esp_err_t err = nvs_get_str(s_nvs_handle, key, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out_str = NULL;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;
    
    *out_str = heap_caps_malloc(required_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*out_str) {
        *out_str = malloc(required_size);  /* Fallback to DRAM */
    }
    if (!*out_str) return ESP_ERR_NO_MEM;
    
    return nvs_get_str(s_nvs_handle, key, *out_str, &required_size);
}

static esp_err_t nvs_write_string(const char *key, const char *str)
{
    esp_err_t err = nvs_set_str(s_nvs_handle, key, str);
    if (err != ESP_OK) return err;
    return nvs_commit(s_nvs_handle);
}

static void update_status(void)
{
    if (s_private_key_pem && s_certificate_pem) {
        /* 
         * Check if certificate is expired
         * 但如果系统时间未同步，跳过过期检查（使用统一的 ts_time_sync API）
         */
        if (ts_time_sync_needs_sync()) {
            /* 时间未同步，假设证书有效（这是正常的启动顺序，NTP 同步在网络就绪后完成） */
            ESP_LOGI(TAG, "Time not synced yet, deferring cert expiry check");
            s_status = TS_CERT_STATUS_ACTIVATED;
        } else {
            ts_cert_info_t info;
            if (ts_cert_get_info(&info) == ESP_OK && info.is_valid) {
                s_status = TS_CERT_STATUS_ACTIVATED;
            } else {
                s_status = TS_CERT_STATUS_EXPIRED;
            }
        }
    } else if (s_private_key_pem) {
        s_status = TS_CERT_STATUS_KEY_GENERATED;
    } else {
        s_status = TS_CERT_STATUS_NOT_INITIALIZED;
    }
    
    nvs_set_u8(s_nvs_handle, NVS_KEY_STATUS, (uint8_t)s_status);
    nvs_commit(s_nvs_handle);
}

/*===========================================================================*/
/*                              SAN Extension                                 */
/*===========================================================================*/

/**
 * @brief Build SAN extension with IP addresses
 * 
 * ASN.1 structure:
 * SubjectAltName ::= GeneralNames
 * GeneralNames ::= SEQUENCE SIZE (1..MAX) OF GeneralName
 * GeneralName ::= CHOICE {
 *     iPAddress  [7] OCTET STRING
 * }
 */
static int build_san_extension(const ts_cert_csr_opts_t *opts, 
                                unsigned char *buf, size_t buf_size,
                                size_t *olen)
{
    unsigned char *p = buf + buf_size;
    int ret;
    size_t len = 0;
    
    /* Build from end of buffer (mbedTLS style) */
    
    /* Add IP addresses (GeneralName with tag [7]) */
    for (int i = opts->ip_san_count - 1; i >= 0; i--) {
        uint32_t ip = opts->ip_sans[i];
        unsigned char ip_bytes[4];
        
        /* Convert to network byte order (big-endian) */
        ip_bytes[0] = (ip >> 24) & 0xFF;
        ip_bytes[1] = (ip >> 16) & 0xFF;
        ip_bytes[2] = (ip >> 8) & 0xFF;
        ip_bytes[3] = ip & 0xFF;
        
        /* Write IP address octets */
        MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_raw_buffer(&p, buf, ip_bytes, 4));
        
        /* Context tag [7] for iPAddress */
        MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_len(&p, buf, 4));
        MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_tag(&p, buf, 
            MBEDTLS_ASN1_CONTEXT_SPECIFIC | 7));
    }
    
    /* Add DNS names (GeneralName with tag [2]) */
    for (int i = opts->dns_san_count - 1; i >= 0; i--) {
        if (opts->dns_sans[i]) {
            size_t dns_len = strlen(opts->dns_sans[i]);
            MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_raw_buffer(&p, buf, 
                (const unsigned char *)opts->dns_sans[i], dns_len));
            MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_len(&p, buf, dns_len));
            MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_tag(&p, buf,
                MBEDTLS_ASN1_CONTEXT_SPECIFIC | 2));
        }
    }
    
    /* Wrap in SEQUENCE */
    MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_len(&p, buf, len));
    MBEDTLS_ASN1_CHK_ADD(len, mbedtls_asn1_write_tag(&p, buf, 
        MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE));
    
    /* Move to start of buffer */
    memmove(buf, p, len);
    *olen = len;
    
    return 0;
}

/*===========================================================================*/
/*                           Public Functions                                 */
/*===========================================================================*/

esp_err_t ts_cert_init(void)
{
    if (s_initialized) return ESP_OK;
    
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Load existing credentials */
    err = nvs_read_string(NVS_KEY_PRIVKEY, &s_private_key_pem);
    if (err == ESP_OK && s_private_key_pem) {
        ESP_LOGI(TAG, "Loaded private key from NVS (%d bytes)", strlen(s_private_key_pem));
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read private key: %s", esp_err_to_name(err));
    }
    
    err = nvs_read_string(NVS_KEY_CERT, &s_certificate_pem);
    if (err == ESP_OK && s_certificate_pem) {
        ESP_LOGI(TAG, "Loaded certificate from NVS (%d bytes)", strlen(s_certificate_pem));
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read certificate: %s", esp_err_to_name(err));
    }
    
    err = nvs_read_string(NVS_KEY_CA_CHAIN, &s_ca_chain_pem);
    if (err == ESP_OK && s_ca_chain_pem) {
        ESP_LOGI(TAG, "Loaded CA chain from NVS (%d bytes)", strlen(s_ca_chain_pem));
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read CA chain: %s", esp_err_to_name(err));
    }
    
    /* 必须先设置 initialized，因为 update_status 会调用 ts_cert_get_info */
    s_initialized = true;
    
    update_status();
    
    ESP_LOGI(TAG, "Initialized, status: %s, has_key=%d, has_cert=%d", 
             ts_cert_status_to_str(s_status),
             s_private_key_pem != NULL,
             s_certificate_pem != NULL);
    
    return ESP_OK;
}

void ts_cert_deinit(void)
{
    if (!s_initialized) return;
    
    free(s_private_key_pem);
    free(s_certificate_pem);
    free(s_ca_chain_pem);
    s_private_key_pem = NULL;
    s_certificate_pem = NULL;
    s_ca_chain_pem = NULL;
    
    if (s_nvs_handle) {
        nvs_close(s_nvs_handle);
        s_nvs_handle = 0;
    }
    
    if (s_rng_initialized) {
        mbedtls_ctr_drbg_free(&s_ctr_drbg);
        mbedtls_entropy_free(&s_entropy);
        s_rng_initialized = false;
    }
    
    s_initialized = false;
}

/*===========================================================================*/
/*                         Key Pair Management                                */
/*===========================================================================*/

esp_err_t ts_cert_generate_keypair(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    esp_err_t err = init_rng();
    if (err != ESP_OK) return err;
    
    ESP_LOGI(TAG, "Generating ECDSA P-256 key pair...");
    
    /* Generate key pair using ts_crypto */
    ts_keypair_t keypair = NULL;
    err = ts_crypto_keypair_generate(TS_CRYPTO_KEY_EC_P256, &keypair);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Key generation failed: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Export private key to PEM */
    char *key_pem = heap_caps_malloc(TS_CERT_KEY_MAX_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!key_pem) {
        key_pem = malloc(TS_CERT_KEY_MAX_LEN);
    }
    if (!key_pem) {
        ts_crypto_keypair_free(keypair);
        return ESP_ERR_NO_MEM;
    }
    
    size_t key_len = TS_CERT_KEY_MAX_LEN;
    err = ts_crypto_keypair_export_private(keypair, key_pem, &key_len);
    ts_crypto_keypair_free(keypair);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Key export failed: %s", esp_err_to_name(err));
        free(key_pem);
        return err;
    }
    
    /* Store in NVS */
    err = nvs_write_string(NVS_KEY_PRIVKEY, key_pem);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store key: %s", esp_err_to_name(err));
        free(key_pem);
        return err;
    }
    
    /* Update cache */
    free(s_private_key_pem);
    s_private_key_pem = key_pem;
    
    /* Clear existing certificate (key changed) */
    nvs_erase_key(s_nvs_handle, NVS_KEY_CERT);
    nvs_commit(s_nvs_handle);
    free(s_certificate_pem);
    s_certificate_pem = NULL;
    
    update_status();
    
    ESP_LOGI(TAG, "Key pair generated and stored");
    return ESP_OK;
}

bool ts_cert_has_keypair(void)
{
    return s_private_key_pem != NULL;
}

esp_err_t ts_cert_delete_keypair(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    nvs_erase_key(s_nvs_handle, NVS_KEY_PRIVKEY);
    nvs_erase_key(s_nvs_handle, NVS_KEY_CERT);
    nvs_commit(s_nvs_handle);
    
    /* Securely clear memory */
    if (s_private_key_pem) {
        memset(s_private_key_pem, 0, strlen(s_private_key_pem));
        free(s_private_key_pem);
        s_private_key_pem = NULL;
    }
    
    free(s_certificate_pem);
    s_certificate_pem = NULL;
    
    update_status();
    
    ESP_LOGI(TAG, "Key pair deleted");
    return ESP_OK;
}

/*===========================================================================*/
/*                           CSR Generation                                   */
/*===========================================================================*/

esp_err_t ts_cert_generate_csr(const ts_cert_csr_opts_t *opts, 
                                char *csr_pem, size_t *csr_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!opts || !csr_pem || !csr_len) return ESP_ERR_INVALID_ARG;
    if (!opts->device_id || strlen(opts->device_id) == 0) return ESP_ERR_INVALID_ARG;
    if (!s_private_key_pem) {
        ESP_LOGE(TAG, "No private key, generate first");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err = init_rng();
    if (err != ESP_OK) return err;
    
    int ret;
    char err_buf[128];
    mbedtls_pk_context pk;
    mbedtls_x509write_csr csr;
    
    mbedtls_pk_init(&pk);
    mbedtls_x509write_csr_init(&csr);
    
    /* Parse private key */
    ret = mbedtls_pk_parse_key(&pk, (const unsigned char *)s_private_key_pem,
                                strlen(s_private_key_pem) + 1, NULL, 0,
                                mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) {
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "Failed to parse private key: %s", err_buf);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    /* Set CSR parameters */
    mbedtls_x509write_csr_set_key(&csr, &pk);
    mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);
    
    /* Build subject DN */
    char subject[256];
    int subject_len = snprintf(subject, sizeof(subject), "CN=%s", opts->device_id);
    
    if (opts->organization && strlen(opts->organization) > 0) {
        subject_len += snprintf(subject + subject_len, sizeof(subject) - subject_len,
                                ",O=%s", opts->organization);
    }
    if (opts->org_unit && strlen(opts->org_unit) > 0) {
        subject_len += snprintf(subject + subject_len, sizeof(subject) - subject_len,
                                ",OU=%s", opts->org_unit);
    }
    
    ret = mbedtls_x509write_csr_set_subject_name(&csr, subject);
    if (ret != 0) {
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "Failed to set subject: %s", err_buf);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    /* Add SAN extension if IP addresses specified */
    if (opts->ip_san_count > 0 || opts->dns_san_count > 0) {
        unsigned char san_buf[256];
        size_t san_len = 0;
        
        ret = build_san_extension(opts, san_buf, sizeof(san_buf), &san_len);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to build SAN extension");
            err = ESP_FAIL;
            goto cleanup;
        }
        
        /* OID for Subject Alternative Name: 2.5.29.17 */
        ret = mbedtls_x509write_csr_set_extension(&csr,
            MBEDTLS_OID_SUBJECT_ALT_NAME, MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME),
            0,  /* not critical */
            san_buf, san_len);
        if (ret != 0) {
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            ESP_LOGE(TAG, "Failed to set SAN extension: %s", err_buf);
            err = ESP_FAIL;
            goto cleanup;
        }
        
        ESP_LOGI(TAG, "Added SAN extension with %d IP(s), %d DNS name(s)",
                 opts->ip_san_count, opts->dns_san_count);
    }
    
    /* Generate CSR PEM */
    ret = mbedtls_x509write_csr_pem(&csr, (unsigned char *)csr_pem, *csr_len,
                                     mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) {
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "Failed to write CSR PEM: %s", err_buf);
        err = ESP_FAIL;
        goto cleanup;
    }
    
    *csr_len = strlen(csr_pem) + 1;
    s_status = TS_CERT_STATUS_CSR_PENDING;
    nvs_set_u8(s_nvs_handle, NVS_KEY_STATUS, (uint8_t)s_status);
    nvs_commit(s_nvs_handle);
    
    ESP_LOGI(TAG, "CSR generated for %s", opts->device_id);
    err = ESP_OK;
    
cleanup:
    mbedtls_x509write_csr_free(&csr);
    mbedtls_pk_free(&pk);
    return err;
}

esp_err_t ts_cert_generate_csr_default(char *csr_pem, size_t *csr_len)
{
    /* Get device IP address */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    }
    
    uint32_t ip_addr = 0;
    if (netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            /* Convert to host byte order for our SAN builder */
            ip_addr = ntohl(ip_info.ip.addr);
        }
    }
    
    /* TODO: Get device ID from configuration */
    const char *device_id = "TIANSHAN-DEVICE-001";
    
    ts_cert_csr_opts_t opts = {
        .device_id = device_id,
        .organization = "TianShanOS",
        .org_unit = "Device",
        .ip_san_count = ip_addr ? 1 : 0,
        .dns_san_count = 0
    };
    opts.ip_sans[0] = ip_addr;
    
    return ts_cert_generate_csr(&opts, csr_pem, csr_len);
}

/*===========================================================================*/
/*                        Certificate Management                              */
/*===========================================================================*/

esp_err_t ts_cert_install_certificate(const char *cert_pem, size_t cert_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!cert_pem || cert_len == 0) return ESP_ERR_INVALID_ARG;
    if (!s_private_key_pem) {
        ESP_LOGE(TAG, "No private key, cannot install certificate");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Parse and validate certificate */
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    
    int ret = mbedtls_x509_crt_parse(&crt, (const unsigned char *)cert_pem, cert_len);
    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "Failed to parse certificate: %s", err_buf);
        mbedtls_x509_crt_free(&crt);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Verify certificate matches private key */
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    
    esp_err_t err = init_rng();
    if (err != ESP_OK) {
        mbedtls_x509_crt_free(&crt);
        return err;
    }
    
    ret = mbedtls_pk_parse_key(&pk, (const unsigned char *)s_private_key_pem,
                                strlen(s_private_key_pem) + 1, NULL, 0,
                                mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to parse private key for verification");
        mbedtls_x509_crt_free(&crt);
        mbedtls_pk_free(&pk);
        return ESP_FAIL;
    }
    
    /* Check if public keys match */
    unsigned char cert_pub[256], key_pub[256];
    size_t cert_pub_len, key_pub_len;
    
    ret = mbedtls_pk_write_pubkey_der(&crt.pk, cert_pub, sizeof(cert_pub));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to extract certificate public key");
        err = ESP_FAIL;
        goto verify_cleanup;
    }
    cert_pub_len = ret;
    
    ret = mbedtls_pk_write_pubkey_der(&pk, key_pub, sizeof(key_pub));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to extract private key public component");
        err = ESP_FAIL;
        goto verify_cleanup;
    }
    key_pub_len = ret;
    
    /* Public keys are written at the end of the buffer */
    if (cert_pub_len != key_pub_len || 
        memcmp(cert_pub + sizeof(cert_pub) - cert_pub_len,
               key_pub + sizeof(key_pub) - key_pub_len,
               cert_pub_len) != 0) {
        ESP_LOGE(TAG, "Certificate does not match private key");
        err = ESP_ERR_INVALID_STATE;
        goto verify_cleanup;
    }
    
    /* Store certificate */
    err = nvs_write_string(NVS_KEY_CERT, cert_pem);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store certificate: %s", esp_err_to_name(err));
        goto verify_cleanup;
    }
    
    /* Update cache */
    free(s_certificate_pem);
    s_certificate_pem = strdup(cert_pem);
    
    update_status();
    
    ESP_LOGI(TAG, "Certificate installed successfully");
    err = ESP_OK;
    
verify_cleanup:
    mbedtls_x509_crt_free(&crt);
    mbedtls_pk_free(&pk);
    return err;
}

esp_err_t ts_cert_install_ca_chain(const char *ca_chain_pem, size_t ca_chain_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!ca_chain_pem || ca_chain_len == 0) return ESP_ERR_INVALID_ARG;
    
    /* Validate CA chain can be parsed */
    mbedtls_x509_crt ca;
    mbedtls_x509_crt_init(&ca);
    
    int ret = mbedtls_x509_crt_parse(&ca, (const unsigned char *)ca_chain_pem, ca_chain_len);
    mbedtls_x509_crt_free(&ca);
    
    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        ESP_LOGE(TAG, "Invalid CA chain: %s", err_buf);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Store CA chain in NVS */
    esp_err_t err = nvs_write_string(NVS_KEY_CA_CHAIN, ca_chain_pem);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store CA chain: %s", esp_err_to_name(err));
        return err;
    }
    
    /* Update cache */
    free(s_ca_chain_pem);
    s_ca_chain_pem = strdup(ca_chain_pem);
    
    /* Save CA chain to SD card for user download */
    /* Create directory if not exists */
    struct stat st;
    if (stat(CA_CHAIN_SDCARD_DIR, &st) != 0) {
        if (mkdir(CA_CHAIN_SDCARD_DIR, 0755) != 0) {
            ESP_LOGW(TAG, "Failed to create %s directory (SD card may not be mounted)", CA_CHAIN_SDCARD_DIR);
            /* Continue anyway, NVS storage succeeded */
        }
    }
    
    /* Write CA chain file */
    FILE *f = fopen(CA_CHAIN_SDCARD_PATH, "w");
    if (f) {
        size_t written = fwrite(ca_chain_pem, 1, strlen(ca_chain_pem), f);
        fclose(f);
        if (written == strlen(ca_chain_pem)) {
            ESP_LOGI(TAG, "CA chain saved to %s for user download", CA_CHAIN_SDCARD_PATH);
        } else {
            ESP_LOGW(TAG, "Partial write to SD card: %zu/%zu bytes", written, strlen(ca_chain_pem));
        }
    } else {
        ESP_LOGW(TAG, "Could not save CA chain to SD card (SD card may not be mounted)");
    }
    
    ESP_LOGI(TAG, "CA chain installed");
    return ESP_OK;
}

esp_err_t ts_cert_get_certificate(char *cert_pem, size_t *cert_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!cert_pem || !cert_len) return ESP_ERR_INVALID_ARG;
    
    if (!s_certificate_pem) {
        return ESP_ERR_NOT_FOUND;
    }
    
    size_t required = strlen(s_certificate_pem) + 1;
    if (*cert_len < required) {
        *cert_len = required;
        return ESP_ERR_INVALID_SIZE;
    }
    
    strcpy(cert_pem, s_certificate_pem);
    *cert_len = required;
    return ESP_OK;
}

esp_err_t ts_cert_get_private_key(char *key_pem, size_t *key_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!key_pem || !key_len) return ESP_ERR_INVALID_ARG;
    
    if (!s_private_key_pem) {
        return ESP_ERR_NOT_FOUND;
    }
    
    size_t required = strlen(s_private_key_pem) + 1;
    if (*key_len < required) {
        *key_len = required;
        return ESP_ERR_INVALID_SIZE;
    }
    
    strcpy(key_pem, s_private_key_pem);
    *key_len = required;
    return ESP_OK;
}

esp_err_t ts_cert_get_ca_chain(char *ca_chain_pem, size_t *ca_chain_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!ca_chain_pem || !ca_chain_len) return ESP_ERR_INVALID_ARG;
    
    if (!s_ca_chain_pem) {
        return ESP_ERR_NOT_FOUND;
    }
    
    size_t required = strlen(s_ca_chain_pem) + 1;
    if (*ca_chain_len < required) {
        *ca_chain_len = required;
        return ESP_ERR_INVALID_SIZE;
    }
    
    strcpy(ca_chain_pem, s_ca_chain_pem);
    *ca_chain_len = required;
    return ESP_OK;
}

/*===========================================================================*/
/*                           Status & Info                                    */
/*===========================================================================*/

esp_err_t ts_cert_refresh_status(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    ts_cert_status_t old_status = s_status;
    update_status();
    
    if (old_status != s_status) {
        ESP_LOGI(TAG, "PKI status updated: %s -> %s",
                 ts_cert_status_to_str(old_status),
                 ts_cert_status_to_str(s_status));
    } else {
        ESP_LOGD(TAG, "PKI status refreshed: %s (unchanged)", 
                 ts_cert_status_to_str(s_status));
    }
    
    return ESP_OK;
}

esp_err_t ts_cert_get_status(ts_cert_pki_status_t *status)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!status) return ESP_ERR_INVALID_ARG;
    
    status->status = s_status;
    status->has_private_key = (s_private_key_pem != NULL);
    status->has_certificate = (s_certificate_pem != NULL);
    status->has_ca_chain = (s_ca_chain_pem != NULL);
    
    if (status->has_certificate) {
        ts_cert_get_info(&status->cert_info);
    } else {
        memset(&status->cert_info, 0, sizeof(status->cert_info));
    }
    
    return ESP_OK;
}

esp_err_t ts_cert_get_info(ts_cert_info_t *info)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!info) return ESP_ERR_INVALID_ARG;
    if (!s_certificate_pem) return ESP_ERR_NOT_FOUND;
    
    return ts_cert_parse_certificate(s_certificate_pem, strlen(s_certificate_pem) + 1, info);
}

bool ts_cert_is_valid(void)
{
    if (!s_certificate_pem) return false;
    
    ts_cert_info_t info;
    if (ts_cert_get_info(&info) != ESP_OK) return false;
    
    return info.is_valid;
}

int ts_cert_days_until_expiry(void)
{
    if (!s_certificate_pem) return INT32_MAX;
    
    ts_cert_info_t info;
    if (ts_cert_get_info(&info) != ESP_OK) return INT32_MAX;
    
    return info.days_until_expiry;
}

/*===========================================================================*/
/*                          Factory Reset                                     */
/*===========================================================================*/

esp_err_t ts_cert_factory_reset(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    /* Erase all NVS keys */
    nvs_erase_key(s_nvs_handle, NVS_KEY_PRIVKEY);
    nvs_erase_key(s_nvs_handle, NVS_KEY_CERT);
    nvs_erase_key(s_nvs_handle, NVS_KEY_CA_CHAIN);
    nvs_erase_key(s_nvs_handle, NVS_KEY_STATUS);
    nvs_commit(s_nvs_handle);
    
    /* Securely clear memory */
    if (s_private_key_pem) {
        memset(s_private_key_pem, 0, strlen(s_private_key_pem));
        free(s_private_key_pem);
        s_private_key_pem = NULL;
    }
    
    free(s_certificate_pem);
    s_certificate_pem = NULL;
    
    free(s_ca_chain_pem);
    s_ca_chain_pem = NULL;
    
    s_status = TS_CERT_STATUS_NOT_INITIALIZED;
    
    ESP_LOGI(TAG, "Factory reset complete");
    return ESP_OK;
}

/*===========================================================================*/
/*                          Utility Functions                                 */
/*===========================================================================*/

const char *ts_cert_status_to_str(ts_cert_status_t status)
{
    switch (status) {
        case TS_CERT_STATUS_NOT_INITIALIZED: return "not_initialized";
        case TS_CERT_STATUS_KEY_GENERATED:   return "key_generated";
        case TS_CERT_STATUS_CSR_PENDING:     return "csr_pending";
        case TS_CERT_STATUS_ACTIVATED:       return "activated";
        case TS_CERT_STATUS_EXPIRED:         return "expired";
        case TS_CERT_STATUS_ERROR:           return "error";
        default:                             return "unknown";
    }
}

esp_err_t ts_cert_parse_certificate(const char *cert_pem, size_t cert_len, 
                                     ts_cert_info_t *info)
{
    if (!cert_pem || !info) return ESP_ERR_INVALID_ARG;
    
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    
    int ret = mbedtls_x509_crt_parse(&crt, (const unsigned char *)cert_pem, cert_len);
    if (ret != 0) {
        mbedtls_x509_crt_free(&crt);
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Extract subject CN */
    const mbedtls_x509_name *name = &crt.subject;
    info->subject_cn[0] = '\0';
    info->subject_ou[0] = '\0';
    while (name) {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) == 0) {
            size_t len = name->val.len;
            if (len >= sizeof(info->subject_cn)) len = sizeof(info->subject_cn) - 1;
            memcpy(info->subject_cn, name->val.p, len);
            info->subject_cn[len] = '\0';
        }
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_ORG_UNIT, &name->oid) == 0) {
            size_t len = name->val.len;
            if (len >= sizeof(info->subject_ou)) len = sizeof(info->subject_ou) - 1;
            memcpy(info->subject_ou, name->val.p, len);
            info->subject_ou[len] = '\0';
        }
        name = name->next;
    }
    
    /* Extract issuer CN */
    name = &crt.issuer;
    info->issuer_cn[0] = '\0';
    while (name) {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) == 0) {
            size_t len = name->val.len;
            if (len >= sizeof(info->issuer_cn)) len = sizeof(info->issuer_cn) - 1;
            memcpy(info->issuer_cn, name->val.p, len);
            info->issuer_cn[len] = '\0';
            break;
        }
        name = name->next;
    }
    
    /* Convert validity times */
    struct tm tm_from, tm_to;
    tm_from.tm_year = crt.valid_from.year - 1900;
    tm_from.tm_mon = crt.valid_from.mon - 1;
    tm_from.tm_mday = crt.valid_from.day;
    tm_from.tm_hour = crt.valid_from.hour;
    tm_from.tm_min = crt.valid_from.min;
    tm_from.tm_sec = crt.valid_from.sec;
    info->not_before = mktime(&tm_from);
    
    tm_to.tm_year = crt.valid_to.year - 1900;
    tm_to.tm_mon = crt.valid_to.mon - 1;
    tm_to.tm_mday = crt.valid_to.day;
    tm_to.tm_hour = crt.valid_to.hour;
    tm_to.tm_min = crt.valid_to.min;
    tm_to.tm_sec = crt.valid_to.sec;
    info->not_after = mktime(&tm_to);
    
    /* Serial number (hex) */
    for (size_t i = 0; i < crt.serial.len && i < 32; i++) {
        sprintf(info->serial + i * 2, "%02X", crt.serial.p[i]);
    }
    
    /* Calculate validity */
    time_t now;
    time(&now);
    
    info->is_valid = (now >= info->not_before && now <= info->not_after);
    info->days_until_expiry = (int)((info->not_after - now) / 86400);
    
    mbedtls_x509_crt_free(&crt);
    return ESP_OK;
}
