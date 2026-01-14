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

/*===========================================================================*/
/*                          RSA/EC Key Pair                                   */
/*===========================================================================*/

/** Crypto key types (for key pair operations) */
typedef enum {
    TS_CRYPTO_KEY_RSA_2048,
    TS_CRYPTO_KEY_RSA_4096,
    TS_CRYPTO_KEY_EC_P256,
    TS_CRYPTO_KEY_EC_P384
} ts_crypto_key_type_t;

/** Key pair handle */
typedef struct ts_keypair_s *ts_keypair_t;

/**
 * @brief Generate a key pair
 * 
 * @param type Key type (RSA or EC)
 * @param keypair Output keypair handle
 * @return ESP_OK on success
 */
esp_err_t ts_crypto_keypair_generate(ts_crypto_key_type_t type, ts_keypair_t *keypair);

/**
 * @brief Free a key pair
 */
void ts_crypto_keypair_free(ts_keypair_t keypair);

/**
 * @brief Export public key in PEM format
 */
esp_err_t ts_crypto_keypair_export_public(ts_keypair_t keypair, char *pem, size_t *pem_len);

/**
 * @brief Export private key in PEM format
 */
esp_err_t ts_crypto_keypair_export_private(ts_keypair_t keypair, char *pem, size_t *pem_len);

/**
 * @brief Import key pair from PEM
 */
esp_err_t ts_crypto_keypair_import(const char *pem, size_t pem_len, ts_keypair_t *keypair);

/**
 * @brief RSA sign (PKCS#1 v1.5)
 */
esp_err_t ts_crypto_rsa_sign(ts_keypair_t keypair, ts_hash_algo_t hash_algo,
                              const void *hash, size_t hash_len,
                              void *signature, size_t *sig_len);

/**
 * @brief RSA verify
 */
esp_err_t ts_crypto_rsa_verify(ts_keypair_t keypair, ts_hash_algo_t hash_algo,
                                const void *hash, size_t hash_len,
                                const void *signature, size_t sig_len);

/**
 * @brief ECDSA sign
 */
esp_err_t ts_crypto_ecdsa_sign(ts_keypair_t keypair, 
                                const void *hash, size_t hash_len,
                                void *signature, size_t *sig_len);

/**
 * @brief ECDSA verify
 */
esp_err_t ts_crypto_ecdsa_verify(ts_keypair_t keypair,
                                  const void *hash, size_t hash_len,
                                  const void *signature, size_t sig_len);

#ifdef __cplusplus
}
#endif
