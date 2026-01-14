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
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/pem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "ts_crypto"

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

/*===========================================================================*/
/*                          RSA/EC Key Pair Implementation                    */
/*===========================================================================*/

struct ts_keypair_s {
    mbedtls_pk_context pk;
    ts_crypto_key_type_t type;
};

/* Random number generator context */
static mbedtls_entropy_context s_entropy;
static mbedtls_ctr_drbg_context s_ctr_drbg;
static bool s_rng_initialized = false;

static esp_err_t init_rng(void)
{
    if (s_rng_initialized) return ESP_OK;
    
    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_ctr_drbg);
    
    const char *pers = "ts_crypto";
    int ret = mbedtls_ctr_drbg_seed(&s_ctr_drbg, mbedtls_entropy_func, &s_entropy,
                                     (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        return ESP_FAIL;
    }
    
    s_rng_initialized = true;
    return ESP_OK;
}

esp_err_t ts_crypto_keypair_generate(ts_crypto_key_type_t type, ts_keypair_t *keypair)
{
    if (!keypair) return ESP_ERR_INVALID_ARG;
    
    esp_err_t err = init_rng();
    if (err != ESP_OK) return err;
    
    struct ts_keypair_s *kp = calloc(1, sizeof(struct ts_keypair_s));
    if (!kp) return ESP_ERR_NO_MEM;
    
    mbedtls_pk_init(&kp->pk);
    kp->type = type;
    
    int ret;
    
    switch (type) {
        case TS_CRYPTO_KEY_RSA_2048:
        case TS_CRYPTO_KEY_RSA_4096: {
            ret = mbedtls_pk_setup(&kp->pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
            if (ret != 0) goto error;
            
            unsigned int nbits = (type == TS_CRYPTO_KEY_RSA_2048) ? 2048 : 4096;
            ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(kp->pk), 
                                       mbedtls_ctr_drbg_random, &s_ctr_drbg,
                                       nbits, 65537);
            if (ret != 0) goto error;
            break;
        }
        
        case TS_CRYPTO_KEY_EC_P256:
        case TS_CRYPTO_KEY_EC_P384: {
            ret = mbedtls_pk_setup(&kp->pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
            if (ret != 0) goto error;
            
            mbedtls_ecp_group_id grp_id = (type == TS_CRYPTO_KEY_EC_P256) ? 
                                           MBEDTLS_ECP_DP_SECP256R1 : 
                                           MBEDTLS_ECP_DP_SECP384R1;
            ret = mbedtls_ecp_gen_key(grp_id, mbedtls_pk_ec(kp->pk),
                                       mbedtls_ctr_drbg_random, &s_ctr_drbg);
            if (ret != 0) goto error;
            break;
        }
        
        default:
            free(kp);
            return ESP_ERR_NOT_SUPPORTED;
    }
    
    *keypair = kp;
    return ESP_OK;
    
error:
    mbedtls_pk_free(&kp->pk);
    free(kp);
    return ESP_FAIL;
}

void ts_crypto_keypair_free(ts_keypair_t keypair)
{
    if (!keypair) return;
    
    mbedtls_pk_free(&keypair->pk);
    free(keypair);
}

esp_err_t ts_crypto_keypair_export_public(ts_keypair_t keypair, char *pem, size_t *pem_len)
{
    if (!keypair || !pem || !pem_len) return ESP_ERR_INVALID_ARG;
    
    int ret = mbedtls_pk_write_pubkey_pem(&keypair->pk, (unsigned char *)pem, *pem_len);
    if (ret != 0) {
        return ESP_FAIL;
    }
    
    *pem_len = strlen(pem) + 1;
    return ESP_OK;
}

esp_err_t ts_crypto_keypair_export_private(ts_keypair_t keypair, char *pem, size_t *pem_len)
{
    if (!keypair || !pem || !pem_len) return ESP_ERR_INVALID_ARG;
    
    int ret = mbedtls_pk_write_key_pem(&keypair->pk, (unsigned char *)pem, *pem_len);
    if (ret != 0) {
        return ESP_FAIL;
    }
    
    *pem_len = strlen(pem) + 1;
    return ESP_OK;
}

esp_err_t ts_crypto_keypair_import(const char *pem, size_t pem_len, ts_keypair_t *keypair)
{
    if (!pem || !keypair) return ESP_ERR_INVALID_ARG;
    
    esp_err_t err = init_rng();
    if (err != ESP_OK) return err;
    
    struct ts_keypair_s *kp = calloc(1, sizeof(struct ts_keypair_s));
    if (!kp) return ESP_ERR_NO_MEM;
    
    mbedtls_pk_init(&kp->pk);
    
    /* Try parsing as private key first */
    int ret = mbedtls_pk_parse_key(&kp->pk, (const unsigned char *)pem, pem_len,
                                    NULL, 0, mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) {
        /* Try as public key */
        ret = mbedtls_pk_parse_public_key(&kp->pk, (const unsigned char *)pem, pem_len);
        if (ret != 0) {
            mbedtls_pk_free(&kp->pk);
            free(kp);
            return ESP_FAIL;
        }
    }
    
    /* Determine key type */
    mbedtls_pk_type_t pk_type = mbedtls_pk_get_type(&kp->pk);
    if (pk_type == MBEDTLS_PK_RSA) {
        size_t bits = mbedtls_pk_get_bitlen(&kp->pk);
        kp->type = (bits <= 2048) ? TS_CRYPTO_KEY_RSA_2048 : TS_CRYPTO_KEY_RSA_4096;
    } else if (pk_type == MBEDTLS_PK_ECKEY || pk_type == MBEDTLS_PK_ECDSA) {
        /* Use bit length to determine curve type since mbedtls 3.x changed API */
        size_t bits = mbedtls_pk_get_bitlen(&kp->pk);
        kp->type = (bits <= 256) ? TS_CRYPTO_KEY_EC_P256 : TS_CRYPTO_KEY_EC_P384;
    }
    
    *keypair = kp;
    return ESP_OK;
}

esp_err_t ts_crypto_rsa_sign(ts_keypair_t keypair, ts_hash_algo_t hash_algo,
                              const void *hash, size_t hash_len,
                              void *signature, size_t *sig_len)
{
    if (!keypair || !hash || !signature || !sig_len) return ESP_ERR_INVALID_ARG;
    
    if (mbedtls_pk_get_type(&keypair->pk) != MBEDTLS_PK_RSA) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = init_rng();
    if (err != ESP_OK) return err;
    
    mbedtls_md_type_t md_type;
    switch (hash_algo) {
        case TS_HASH_SHA256: md_type = MBEDTLS_MD_SHA256; break;
        case TS_HASH_SHA384: md_type = MBEDTLS_MD_SHA384; break;
        case TS_HASH_SHA512: md_type = MBEDTLS_MD_SHA512; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    
    int ret = mbedtls_pk_sign(&keypair->pk, md_type, hash, hash_len,
                               signature, *sig_len, sig_len,
                               mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t ts_crypto_rsa_verify(ts_keypair_t keypair, ts_hash_algo_t hash_algo,
                                const void *hash, size_t hash_len,
                                const void *signature, size_t sig_len)
{
    if (!keypair || !hash || !signature) return ESP_ERR_INVALID_ARG;
    
    if (mbedtls_pk_get_type(&keypair->pk) != MBEDTLS_PK_RSA) {
        return ESP_ERR_INVALID_ARG;
    }
    
    mbedtls_md_type_t md_type;
    switch (hash_algo) {
        case TS_HASH_SHA256: md_type = MBEDTLS_MD_SHA256; break;
        case TS_HASH_SHA384: md_type = MBEDTLS_MD_SHA384; break;
        case TS_HASH_SHA512: md_type = MBEDTLS_MD_SHA512; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    
    int ret = mbedtls_pk_verify(&keypair->pk, md_type, hash, hash_len,
                                 signature, sig_len);
    if (ret != 0) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t ts_crypto_ecdsa_sign(ts_keypair_t keypair, 
                                const void *hash, size_t hash_len,
                                void *signature, size_t *sig_len)
{
    if (!keypair || !hash || !signature || !sig_len) return ESP_ERR_INVALID_ARG;
    
    mbedtls_pk_type_t pk_type = mbedtls_pk_get_type(&keypair->pk);
    if (pk_type != MBEDTLS_PK_ECKEY && pk_type != MBEDTLS_PK_ECDSA) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = init_rng();
    if (err != ESP_OK) return err;
    
    /* Use SHA256 for signing */
    int ret = mbedtls_pk_sign(&keypair->pk, MBEDTLS_MD_SHA256, hash, hash_len,
                               signature, *sig_len, sig_len,
                               mbedtls_ctr_drbg_random, &s_ctr_drbg);
    if (ret != 0) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t ts_crypto_ecdsa_verify(ts_keypair_t keypair,
                                  const void *hash, size_t hash_len,
                                  const void *signature, size_t sig_len)
{
    if (!keypair || !hash || !signature) return ESP_ERR_INVALID_ARG;
    
    mbedtls_pk_type_t pk_type = mbedtls_pk_get_type(&keypair->pk);
    if (pk_type != MBEDTLS_PK_ECKEY && pk_type != MBEDTLS_PK_ECDSA) {
        return ESP_ERR_INVALID_ARG;
    }
    
    int ret = mbedtls_pk_verify(&keypair->pk, MBEDTLS_MD_SHA256, hash, hash_len,
                                 signature, sig_len);
    if (ret != 0) {
        return ESP_FAIL;
    }
    
    return ESP_OK;
}
