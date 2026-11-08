/**
 * @file ts_auth_layout.h
 * @brief Shared on-flash auth credential layout.
 */

#ifndef TS_AUTH_LAYOUT_H
#define TS_AUTH_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define TS_AUTH_NVS_NAMESPACE           "ts_auth"
#define TS_AUTH_ROOT_CRED_KEY           "cred_root"
#define TS_AUTH_ADMIN_CRED_KEY          "cred_admin"
#define TS_AUTH_CONFIG_VERSION_KEY      "cfg_version"
#define TS_AUTH_DEFAULT_ROOT_PASSWORD   "rm01"
#define TS_AUTH_DEFAULT_ADMIN_PASSWORD  "rm01"
#define TS_AUTH_CONFIG_VERSION          3
#define TS_AUTH_PROTOTYPE_CONFIG_VERSION 4
#define TS_AUTH_LEGACY_CONFIG_VERSION   3
#define TS_AUTH_CREDENTIAL_MAGIC       0x34464b50U
#define TS_AUTH_KDF_MIN_ROUNDS         10000U
#define TS_AUTH_KDF_MAX_ROUNDS         200000U

#define TS_AUTH_SALT_LEN                16
#define TS_AUTH_HASH_LEN                32

typedef struct {
    uint8_t salt[TS_AUTH_SALT_LEN];
    uint8_t hash[TS_AUTH_HASH_LEN];
    bool password_changed;
    uint32_t failed_attempts;
    uint32_t lockout_until;
    uint32_t format;
    uint32_t rounds;
} ts_auth_user_credential_t;

#define TS_AUTH_LEGACY_CREDENTIAL_SIZE 60
#define TS_AUTH_USER_CREDENTIAL_SIZE 60
/* Read bridge for the unreleased API prototype; never written by this firmware. */
#define TS_AUTH_MAX_CREDENTIAL_SIZE 68

#if defined(__cplusplus)
static_assert(offsetof(ts_auth_user_credential_t, salt) == 0, "auth salt offset changed");
static_assert(offsetof(ts_auth_user_credential_t, hash) == 16, "auth hash offset changed");
static_assert(offsetof(ts_auth_user_credential_t, password_changed) == 48, "auth password_changed offset changed");
static_assert(offsetof(ts_auth_user_credential_t, failed_attempts) == 52, "auth failed_attempts offset changed");
static_assert(offsetof(ts_auth_user_credential_t, lockout_until) == 56, "auth lockout_until offset changed");
static_assert(offsetof(ts_auth_user_credential_t, format) == 60, "auth format offset changed");
static_assert(offsetof(ts_auth_user_credential_t, rounds) == 64, "auth rounds offset changed");
static_assert(sizeof(ts_auth_user_credential_t) == TS_AUTH_MAX_CREDENTIAL_SIZE, "auth credential size changed");
#else
_Static_assert(offsetof(ts_auth_user_credential_t, salt) == 0, "auth salt offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, hash) == 16, "auth hash offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, password_changed) == 48, "auth password_changed offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, failed_attempts) == 52, "auth failed_attempts offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, lockout_until) == 56, "auth lockout_until offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, format) == 60, "auth format offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, rounds) == 64, "auth rounds offset changed");
_Static_assert(sizeof(ts_auth_user_credential_t) == TS_AUTH_MAX_CREDENTIAL_SIZE, "auth credential size changed");
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Writes use the original 60-byte v3 format. The internal view can also read
 * the old prototype for conversion after correct password verification. */
esp_err_t ts_auth_credential_decode(const void *bytes, size_t length,
                                    ts_auth_user_credential_t *credential);
esp_err_t ts_auth_credential_create(const char *password, bool changed,
                                    ts_auth_user_credential_t *credential);
esp_err_t ts_auth_credential_verify(const ts_auth_user_credential_t *credential,
                                    const char *password, bool *matches);

#ifdef __cplusplus
}
#endif

#endif /* TS_AUTH_LAYOUT_H */
