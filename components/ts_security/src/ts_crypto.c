/**
 * @file ts_crypto.c
 * @brief Cryptographic Utilities Implementation
 */

#include "ts_crypto.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/md.h"
#include "mbedtls/gcm.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>

esp_err_t ts_crypto_hash(ts_hash_algo_t algo, const void *data, size_t len,
                          void *hash, size_t hash_len)
{
    if (!data || !hash) return ESP_ERR_INVALID_ARG;
    
    switch (algo) {
        case TS_HASH_SHA256:
            if (hash_len < 32) return ESP_ERR_INVALID_SIZE;
            mbedtls_sha256(data, len, hash, 0);
            break;
        case TS_HASH_SHA384:
            if (hash_len < 48) return ESP_ERR_INVALID_SIZE;
            mbedtls_sha512(data, len, hash, 1);
            break;
        case TS_HASH_SHA512:
            if (hash_len < 64) return ESP_ERR_INVALID_SIZE;
            mbedtls_sha512(data, len, hash, 0);
            break;
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

esp_err_t ts_crypto_hmac(ts_hash_algo_t algo, const void *key, size_t key_len,
                          const void *data, size_t data_len,
                          void *mac, size_t mac_len)
{
    if (!key || !data || !mac) return ESP_ERR_INVALID_ARG;
    
    mbedtls_md_type_t md_type;
    size_t required_len;
    
    switch (algo) {
        case TS_HASH_SHA256:
            md_type = MBEDTLS_MD_SHA256;
            required_len = 32;
            break;
        case TS_HASH_SHA384:
            md_type = MBEDTLS_MD_SHA384;
            required_len = 48;
            break;
        case TS_HASH_SHA512:
            md_type = MBEDTLS_MD_SHA512;
            required_len = 64;
            break;
        default:
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (mac_len < required_len) return ESP_ERR_INVALID_SIZE;
    
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
    int ret = mbedtls_md_hmac(md_info, key, key_len, data, data_len, mac);
    
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ts_crypto_aes_gcm_encrypt(const void *key, size_t key_len,
                                     const void *iv, size_t iv_len,
                                     const void *aad, size_t aad_len,
                                     const void *plaintext, size_t plaintext_len,
                                     void *ciphertext, void *tag)
{
    if (!key || !iv || !plaintext || !ciphertext || !tag) return ESP_ERR_INVALID_ARG;
    
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    
    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, key_len * 8);
    if (ret != 0) {
        mbedtls_gcm_free(&ctx);
        return ESP_FAIL;
    }
    
    ret = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT,
                                     plaintext_len, iv, iv_len,
                                     aad, aad_len,
                                     plaintext, ciphertext,
                                     16, tag);
    
    mbedtls_gcm_free(&ctx);
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ts_crypto_aes_gcm_decrypt(const void *key, size_t key_len,
                                     const void *iv, size_t iv_len,
                                     const void *aad, size_t aad_len,
                                     const void *ciphertext, size_t ciphertext_len,
                                     const void *tag, void *plaintext)
{
    if (!key || !iv || !ciphertext || !tag || !plaintext) return ESP_ERR_INVALID_ARG;
    
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    
    int ret = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, key_len * 8);
    if (ret != 0) {
        mbedtls_gcm_free(&ctx);
        return ESP_FAIL;
    }
    
    ret = mbedtls_gcm_auth_decrypt(&ctx, ciphertext_len,
                                    iv, iv_len,
                                    aad, aad_len,
                                    tag, 16,
                                    ciphertext, plaintext);
    
    mbedtls_gcm_free(&ctx);
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ts_crypto_base64_encode(const void *src, size_t src_len,
                                   char *dst, size_t *dst_len)
{
    if (!src || !dst || !dst_len) return ESP_ERR_INVALID_ARG;
    
    size_t olen;
    int ret = mbedtls_base64_encode((unsigned char *)dst, *dst_len, &olen, src, src_len);
    
    if (ret == 0) {
        *dst_len = olen;
        return ESP_OK;
    } else if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_FAIL;
}

esp_err_t ts_crypto_base64_decode(const char *src, size_t src_len,
                                   void *dst, size_t *dst_len)
{
    if (!src || !dst || !dst_len) return ESP_ERR_INVALID_ARG;
    
    size_t olen;
    int ret = mbedtls_base64_decode(dst, *dst_len, &olen, (const unsigned char *)src, src_len);
    
    if (ret == 0) {
        *dst_len = olen;
        return ESP_OK;
    } else if (ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_FAIL;
}

esp_err_t ts_crypto_hex_encode(const void *src, size_t src_len,
                                char *dst, size_t dst_len)
{
    if (!src || !dst) return ESP_ERR_INVALID_ARG;
    if (dst_len < src_len * 2 + 1) return ESP_ERR_INVALID_SIZE;
    
    const uint8_t *s = src;
    for (size_t i = 0; i < src_len; i++) {
        sprintf(dst + i * 2, "%02x", s[i]);
    }
    dst[src_len * 2] = '\0';
    return ESP_OK;
}

esp_err_t ts_crypto_hex_decode(const char *src, size_t src_len,
                                void *dst, size_t *dst_len)
{
    if (!src || !dst || !dst_len) return ESP_ERR_INVALID_ARG;
    if (src_len % 2 != 0) return ESP_ERR_INVALID_ARG;
    if (*dst_len < src_len / 2) return ESP_ERR_INVALID_SIZE;
    
    uint8_t *d = dst;
    for (size_t i = 0; i < src_len / 2; i++) {
        unsigned int val;
        if (sscanf(src + i * 2, "%2x", &val) != 1) {
            return ESP_ERR_INVALID_ARG;
        }
        d[i] = (uint8_t)val;
    }
    *dst_len = src_len / 2;
    return ESP_OK;
}
