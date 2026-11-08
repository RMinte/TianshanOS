#include "ts_auth_layout.h"
#include "esp_random.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/sha256.h"
#include "mbedtls/platform_util.h"
#include <string.h>

/* Stored parameters bound both verification cost and accepted formats. */
static bool credential_format_valid(const ts_auth_user_credential_t *credential)
{
    return credential->format == TS_AUTH_CREDENTIAL_MAGIC &&
        credential->rounds >= TS_AUTH_KDF_MIN_ROUNDS &&
        credential->rounds <= TS_AUTH_KDF_MAX_ROUNDS;
}

esp_err_t ts_auth_credential_decode(const void *bytes, size_t length,
                                    ts_auth_user_credential_t *credential)
{
    if (!bytes || !credential) return ESP_ERR_INVALID_ARG;
    if (length != TS_AUTH_LEGACY_CREDENTIAL_SIZE && length != sizeof(*credential))
        return ESP_ERR_INVALID_SIZE;
    /* Validate the persisted bool representation before accessing it as bool. */
    if (((const uint8_t *)bytes)[offsetof(ts_auth_user_credential_t, password_changed)] > 1)
        return ESP_ERR_INVALID_STATE;
    memmove(credential, bytes, length);
    if (length == TS_AUTH_LEGACY_CREDENTIAL_SIZE)
        memset((unsigned char *)credential + length, 0, sizeof(*credential) - length);
    if (length == TS_AUTH_LEGACY_CREDENTIAL_SIZE || credential_format_valid(credential))
        return ESP_OK;
    mbedtls_platform_zeroize(credential, sizeof(*credential));
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t credential_hash(const ts_auth_user_credential_t *credential,
                                 const char *password, uint8_t hash[TS_AUTH_HASH_LEN])
{
    size_t length = strlen(password);
    if (length > 64) return ESP_ERR_INVALID_ARG;
    if (credential_format_valid(credential))
        return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
            (const unsigned char *)password, length, credential->salt, TS_AUTH_SALT_LEN,
            credential->rounds, TS_AUTH_HASH_LEN, hash) == 0 ? ESP_OK : ESP_FAIL;
    if (credential->format || credential->rounds) return ESP_ERR_INVALID_STATE;
    /* Original v3 password scheme; no new storage format is emitted. */
    unsigned char material[TS_AUTH_SALT_LEN + 64];
    memcpy(material, credential->salt, TS_AUTH_SALT_LEN);
    memcpy(material + TS_AUTH_SALT_LEN, password, length);
    int result = mbedtls_sha256(material, TS_AUTH_SALT_LEN + length, hash, 0);
    mbedtls_platform_zeroize(material, sizeof(material));
    return result == 0 ? ESP_OK : ESP_FAIL;
}


esp_err_t ts_auth_credential_create(const char *password, bool changed,
                                    ts_auth_user_credential_t *credential)
{
    if (!password || !credential || strlen(password) < 4 || strlen(password) > 64)
        return ESP_ERR_INVALID_ARG;
    memset(credential, 0, sizeof(*credential));
    /* Original v3 salt + SHA-256 layout, also readable by stock 0.6.4. */
    esp_fill_random(credential->salt, TS_AUTH_SALT_LEN);
    esp_err_t ret = credential_hash(credential, password, credential->hash);
    if (ret != ESP_OK) mbedtls_platform_zeroize(credential, sizeof(*credential));
    else credential->password_changed = changed;
    return ret;
}

esp_err_t ts_auth_credential_verify(const ts_auth_user_credential_t *credential,
                                    const char *password, bool *matches)
{
    if (!credential || !password || !matches) return ESP_ERR_INVALID_ARG;
    *matches = false;
    uint8_t computed[TS_AUTH_HASH_LEN];
    esp_err_t ret = credential_hash(credential, password, computed);
    if (ret == ESP_OK) {
        unsigned char difference = 0;
        for (size_t i = 0; i < sizeof(computed); ++i)
            difference |= computed[i] ^ credential->hash[i];
        *matches = difference == 0;
    }
    mbedtls_platform_zeroize(computed, sizeof(computed));
    return ret;
}
