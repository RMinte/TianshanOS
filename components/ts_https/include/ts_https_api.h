/**
 * @file ts_https_api.h
 * @brief Default HTTPS API Endpoints
 * 
 * Provides standard API endpoints for system status, PKI info, etc.
 */

#ifndef TS_HTTPS_API_H
#define TS_HTTPS_API_H

#include "ts_https.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register default API endpoints
 * 
 * Registers the following endpoints:
 * - GET /api/system/info     (viewer)  - System information
 * - GET /api/system/memory   (viewer)  - Memory statistics
 * - GET /api/pki/status      (viewer)  - PKI status
 * - GET /api/auth/whoami     (viewer)  - Current user info
 * - POST /api/system/reboot  (admin)   - Reboot device
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_https_register_default_api(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_HTTPS_API_H */
