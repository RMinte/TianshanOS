/**
 * @file ts_https_api.c
 * @brief HTTPS API 权限测试端点
 * 
 * 这些端点仅用于测试 mTLS 权限系统，不包含任何实际功能。
 * 每个端点只返回调用者的认证信息和权限检查结果。
 */

#include "ts_https.h"
#include "ts_https_api.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ts_https_api";

/*===========================================================================*/
/*                      通用权限测试响应生成                                  */
/*===========================================================================*/

/**
 * @brief 生成统一的权限测试响应
 * 
 * @param req 请求上下文
 * @param endpoint_name 端点名称
 * @param required_role 该端点要求的最低角色
 * @return esp_err_t 
 */
static esp_err_t send_permission_test_response(ts_https_req_t *req, 
                                                const char *endpoint_name,
                                                const char *required_role)
{
    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"endpoint\":\"%s\","
        "\"required_role\":\"%s\","
        "\"access\":\"granted\","
        "\"auth\":{"
            "\"authenticated\":%s,"
            "\"username\":\"%s\","
            "\"role\":\"%s\","
            "\"organization\":\"%s\""
        "},"
        "\"message\":\"Permission test passed\""
        "}",
        endpoint_name,
        required_role,
        req->auth.authenticated ? "true" : "false",
        req->auth.username,
        ts_https_role_to_str(req->auth.role),
        req->auth.organization
    );
    
    ESP_LOGI(TAG, "[%s] Access granted to %s (role=%s)", 
             endpoint_name, req->auth.username, ts_https_role_to_str(req->auth.role));
    
    return ts_https_send_json(req, 200, json);
}

/*===========================================================================*/
/*                         权限测试端点                                       */
/*===========================================================================*/

/**
 * @brief GET / - 健康检查（无需认证）
 */
static esp_err_t api_test_anonymous(ts_https_req_t *req)
{
    return send_permission_test_response(req, "/", "anonymous");
}

/**
 * @brief GET /api/test/viewer - Viewer 权限测试
 */
static esp_err_t api_test_viewer(ts_https_req_t *req)
{
    return send_permission_test_response(req, "/api/test/viewer", "viewer");
}

/**
 * @brief GET /api/test/operator - Operator 权限测试
 */
static esp_err_t api_test_operator(ts_https_req_t *req)
{
    return send_permission_test_response(req, "/api/test/operator", "operator");
}

/**
 * @brief GET /api/test/admin - Admin 权限测试
 */
static esp_err_t api_test_admin(ts_https_req_t *req)
{
    return send_permission_test_response(req, "/api/test/admin", "admin");
}

/**
 * @brief POST /api/test/admin-action - Admin 写操作权限测试
 */
static esp_err_t api_test_admin_action(ts_https_req_t *req)
{
    return send_permission_test_response(req, "/api/test/admin-action", "admin");
}

/**
 * @brief GET /api/auth/whoami - 当前用户信息
 */
static esp_err_t api_auth_whoami(ts_https_req_t *req)
{
    char json[256];
    snprintf(json, sizeof(json),
        "{"
        "\"authenticated\":%s,"
        "\"username\":\"%s\","
        "\"organization\":\"%s\","
        "\"role\":\"%s\","
        "\"cert_days_remaining\":%d"
        "}",
        req->auth.authenticated ? "true" : "false",
        req->auth.username,
        req->auth.organization,
        ts_https_role_to_str(req->auth.role),
        req->auth.cert_days_remaining
    );
    
    return ts_https_send_json(req, 200, json);
}

/*===========================================================================*/
/*                         端点注册表                                         */
/*===========================================================================*/

static const ts_https_endpoint_t s_default_endpoints[] = {
    // 健康检查 - 无需认证
    {
        .uri = "/",
        .method = HTTP_GET,
        .min_role = TS_HTTPS_ROLE_ANONYMOUS,
        .handler = api_test_anonymous,
        .description = "Health check (no auth)"
    },
    // 用户信息 - viewer 权限
    {
        .uri = "/api/auth/whoami",
        .method = HTTP_GET,
        .min_role = TS_HTTPS_ROLE_VIEWER,
        .handler = api_auth_whoami,
        .description = "Current user info"
    },
    // Viewer 权限测试
    {
        .uri = "/api/test/viewer",
        .method = HTTP_GET,
        .min_role = TS_HTTPS_ROLE_VIEWER,
        .handler = api_test_viewer,
        .description = "Viewer permission test"
    },
    // Operator 权限测试
    {
        .uri = "/api/test/operator",
        .method = HTTP_GET,
        .min_role = TS_HTTPS_ROLE_OPERATOR,
        .handler = api_test_operator,
        .description = "Operator permission test"
    },
    // Admin 读权限测试
    {
        .uri = "/api/test/admin",
        .method = HTTP_GET,
        .min_role = TS_HTTPS_ROLE_ADMIN,
        .handler = api_test_admin,
        .description = "Admin permission test (read)"
    },
    // Admin 写权限测试
    {
        .uri = "/api/test/admin-action",
        .method = HTTP_POST,
        .min_role = TS_HTTPS_ROLE_ADMIN,
        .handler = api_test_admin_action,
        .description = "Admin permission test (write)"
    },
    // Terminator
    { .uri = NULL }
};

esp_err_t ts_https_register_default_api(void)
{
    ESP_LOGI(TAG, "Registering permission test endpoints...");
    return ts_https_register_endpoints(s_default_endpoints);
}
