/**
 * @file ts_auth_layout.h
 * @brief Shared on-flash auth credential layout.
 */

#ifndef TS_AUTH_LAYOUT_H
#define TS_AUTH_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TS_AUTH_NVS_NAMESPACE           "ts_auth"
#define TS_AUTH_ROOT_CRED_KEY           "cred_root"
#define TS_AUTH_CONFIG_VERSION_KEY      "cfg_version"
#define TS_AUTH_DEFAULT_ROOT_PASSWORD   "rm01"
#define TS_AUTH_CONFIG_VERSION          3

#define TS_AUTH_SALT_LEN                16
#define TS_AUTH_HASH_LEN                32

typedef struct {
    uint8_t salt[TS_AUTH_SALT_LEN];
    uint8_t hash[TS_AUTH_HASH_LEN];
    bool password_changed;
    uint32_t failed_attempts;
    uint32_t lockout_until;
} ts_auth_user_credential_t;

#define TS_AUTH_USER_CREDENTIAL_SIZE 60

#if defined(__cplusplus)
static_assert(offsetof(ts_auth_user_credential_t, salt) == 0, "auth salt offset changed");
static_assert(offsetof(ts_auth_user_credential_t, hash) == 16, "auth hash offset changed");
static_assert(offsetof(ts_auth_user_credential_t, password_changed) == 48, "auth password_changed offset changed");
static_assert(offsetof(ts_auth_user_credential_t, failed_attempts) == 52, "auth failed_attempts offset changed");
static_assert(offsetof(ts_auth_user_credential_t, lockout_until) == 56, "auth lockout_until offset changed");
static_assert(sizeof(ts_auth_user_credential_t) == TS_AUTH_USER_CREDENTIAL_SIZE, "auth credential size changed");
#else
_Static_assert(offsetof(ts_auth_user_credential_t, salt) == 0, "auth salt offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, hash) == 16, "auth hash offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, password_changed) == 48, "auth password_changed offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, failed_attempts) == 52, "auth failed_attempts offset changed");
_Static_assert(offsetof(ts_auth_user_credential_t, lockout_until) == 56, "auth lockout_until offset changed");
_Static_assert(sizeof(ts_auth_user_credential_t) == TS_AUTH_USER_CREDENTIAL_SIZE, "auth credential size changed");
#endif

#endif /* TS_AUTH_LAYOUT_H */
