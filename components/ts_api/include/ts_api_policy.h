#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Zero-initialized entries are trusted internal operations only. External
 * exposure always needs an explicit role and nonzero method declaration. */
typedef enum {
    TS_API_ACCESS_INTERNAL = 0,
    TS_API_ACCESS_LEGACY = TS_API_ACCESS_INTERNAL, /* Source alias for old snapshots; no external fallback. */
    TS_API_ACCESS_PUBLIC,
    TS_API_ACCESS_READ,
    TS_API_ACCESS_WRITE,
    TS_API_ACCESS_ADMIN,
    TS_API_ACCESS_ROOT,
    TS_API_ACCESS_TOKEN
} ts_api_external_access_t;
enum {
    TS_API_METHOD_GET = 1,
    TS_API_METHOD_POST = 2,
    TS_API_METHOD_PUT = 4,
    TS_API_METHOD_DELETE = 8,
    TS_API_METHOD_READ = TS_API_METHOD_GET | TS_API_METHOD_POST,
    TS_API_METHOD_WRITE = TS_API_METHOD_POST | TS_API_METHOD_PUT | TS_API_METHOD_DELETE,
    TS_API_METHOD_ALL = TS_API_METHOD_READ | TS_API_METHOD_WRITE
};
typedef struct {
    bool requires_auth;
    uint8_t minimum_level;
    uint8_t allowed_methods;
    ts_api_external_access_t external_access;
} ts_api_access_t;
