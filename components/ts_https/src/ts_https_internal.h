/**
 * @file ts_https_internal.h
 * @brief Internal definitions for ts_https component
 */

#ifndef TS_HTTPS_INTERNAL_H
#define TS_HTTPS_INTERNAL_H

#include "ts_https.h"
#include "mbedtls/x509_crt.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract authentication info from certificate
 */
esp_err_t ts_https_auth_from_cert(const mbedtls_x509_crt *cert, ts_https_auth_t *auth);

/**
 * @brief Get client certificate auth from SSL context
 */
esp_err_t ts_https_get_client_auth(void *ssl_ctx, ts_https_auth_t *auth);

/**
 * @brief Check if request has sufficient permissions
 */
bool ts_https_check_permission(const ts_https_auth_t *auth, ts_https_role_t min_role);

#ifdef __cplusplus
}
#endif

#endif /* TS_HTTPS_INTERNAL_H */
