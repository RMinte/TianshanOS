/**
 * @file ts_crypto.h
 * @brief Cryptographic Utilities
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hash algorithms */
typedef enum {
    TS_HASH_SHA256,
    TS_HASH_SHA384,
    TS_HASH_SHA512
} ts_hash_algo_t;

/**
 * @brief Compute hash
 */
esp_err_t ts_crypto_hash(ts_hash_algo_t algo, const void *data, size_t len,
                          void *hash, size_t hash_len);

/**
 * @brief HMAC
 */
esp_err_t ts_crypto_hmac(ts_hash_algo_t algo, const void *key, size_t key_len,
                          const void *data, size_t data_len,
                          void *mac, size_t mac_len);

/**
 * @brief AES-GCM encrypt
 */
esp_err_t ts_crypto_aes_gcm_encrypt(const void *key, size_t key_len,
                                     const void *iv, size_t iv_len,
                                     const void *aad, size_t aad_len,
                                     const void *plaintext, size_t plaintext_len,
                                     void *ciphertext, void *tag);

/**
 * @brief AES-GCM decrypt
 */
esp_err_t ts_crypto_aes_gcm_decrypt(const void *key, size_t key_len,
                                     const void *iv, size_t iv_len,
                                     const void *aad, size_t aad_len,
                                     const void *ciphertext, size_t ciphertext_len,
                                     const void *tag, void *plaintext);

/**
 * @brief Base64 encode
 */
esp_err_t ts_crypto_base64_encode(const void *src, size_t src_len,
                                   char *dst, size_t *dst_len);

/**
 * @brief Base64 decode
 */
esp_err_t ts_crypto_base64_decode(const char *src, size_t src_len,
                                   void *dst, size_t *dst_len);

/**
 * @brief Hex encode
 */
esp_err_t ts_crypto_hex_encode(const void *src, size_t src_len,
                                char *dst, size_t dst_len);

/**
 * @brief Hex decode
 */
esp_err_t ts_crypto_hex_decode(const char *src, size_t src_len,
                                void *dst, size_t *dst_len);

#ifdef __cplusplus
}
#endif
