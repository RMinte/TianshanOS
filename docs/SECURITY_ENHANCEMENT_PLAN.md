# TianShanOS 安全增强方案

**日期**：2026年1月24日  
**目标**：实现 HTTPS、权限管理和双向认证

---

## 📋 当前安全状态

### 已实现功能 ✅
- **HTTP 服务器**：基于 ESP-IDF httpd 组件
- **WebSocket**：未加密传输
- **认证机制**：基础 Token 认证（`ts_security` 组件）
- **SSH 客户端**：libssh2 支持，用于连接外部设备

### 安全缺陷 ⚠️
1. **HTTP 明文传输**：所有数据（包括密码）明文传输
2. **WebSocket 未加密**：实时通信可被窃听
3. **单向认证**：仅服务器验证客户端，客户端不验证服务器
4. **权限控制缺失**：Token 有效即拥有所有权限
5. **会话管理简陋**：无超时、无刷新机制
6. **证书管理薄弱**：无证书生成/管理工具

---

## 🎯 Phase 16: HTTPS & 安全增强

### 目标功能

#### 1. HTTPS 支持（TLS/SSL）
**技术栈**：
- ESP-IDF HTTPS 服务器（mbedTLS）
- 自签名证书 或 Let's Encrypt（嵌入式设备通常用自签名）

**实现要点**：
- 服务器端 TLS 证书管理
- 证书持久化存储（NVS 或 SD 卡）
- HTTP → HTTPS 自动重定向
- 支持 HTTP 和 HTTPS 并存（配置选项）

**API 设计**：
```c
// 配置 HTTPS
esp_err_t ts_https_init(const ts_https_config_t *config);
esp_err_t ts_https_set_certificate(const uint8_t *cert_pem, size_t cert_len,
                                     const uint8_t *key_pem, size_t key_len);
esp_err_t ts_https_enable(bool enable);
```

---

#### 2. WebSocket Secure (WSS)
**技术栈**：
- WSS over TLS（基于 HTTPS 服务器）
- 前端自动切换 `ws://` → `wss://`

**实现要点**：
- 复用 HTTPS 证书
- 前端检测 `window.location.protocol`
- 握手升级处理

---

#### 3. 双向认证（Mutual TLS Authentication）
**场景**：企业环境、高安全需求

**实现要点**：
- 客户端证书验证
- 证书白名单管理
- 证书吊销列表（CRL）支持

**配置选项**：
```c
typedef struct {
    bool require_client_cert;      // 是否要求客户端证书
    const uint8_t *ca_cert_pem;   // CA 证书（验证客户端证书）
    size_t ca_cert_len;
} ts_mtls_config_t;
```

---

#### 4. 细粒度权限管理
**角色定义**：
```c
typedef enum {
    TS_ROLE_GUEST = 0,        // 游客（只读系统信息）
    TS_ROLE_USER = 1,         // 普通用户（查看、基础控制）
    TS_ROLE_OPERATOR = 2,     // 操作员（完整设备控制）
    TS_ROLE_ADMIN = 3,        // 管理员（配置、用户管理、OTA）
    TS_ROLE_ROOT = 4,         // 超级管理员（所有权限）
} ts_user_role_t;
```

**权限矩阵**：
| 操作 | GUEST | USER | OPERATOR | ADMIN | ROOT |
|------|-------|------|----------|-------|------|
| 查看系统信息 | ✅ | ✅ | ✅ | ✅ | ✅ |
| 查看日志 | ❌ | ✅ | ✅ | ✅ | ✅ |
| LED 控制 | ❌ | ✅ | ✅ | ✅ | ✅ |
| AGX 电源控制 | ❌ | ❌ | ✅ | ✅ | ✅ |
| 网络配置 | ❌ | ❌ | ❌ | ✅ | ✅ |
| 用户管理 | ❌ | ❌ | ❌ | ✅ | ✅ |
| OTA 升级 | ❌ | ❌ | ❌ | ✅ | ✅ |
| SSH 管理 | ❌ | ❌ | ❌ | ❌ | ✅ |
| 安全配置 | ❌ | ❌ | ❌ | ❌ | ✅ |

**实现方式**：
```c
// 权限检查宏
#define TS_REQUIRE_ROLE(required_role) \
    do { \
        if (current_user_role < required_role) { \
            return ts_api_error(403, "Insufficient permissions"); \
        } \
    } while(0)

// API 使用示例
static esp_err_t api_system_reboot(const cJSON *params, ts_api_result_t *result) {
    TS_REQUIRE_ROLE(TS_ROLE_ADMIN);
    // ... 执行重启
}
```

---

#### 5. 会话管理增强
**功能**：
- Token 自动过期（可配置超时时间）
- Token 刷新机制（refresh token）
- 多设备并发登录管理
- 会话踢出（管理员强制登出其他用户）

**数据结构**：
```c
typedef struct {
    char token[64];               // Access token
    char refresh_token[64];       // Refresh token
    uint64_t expires_at;          // 过期时间戳
    ts_user_role_t role;          // 用户角色
    char username[32];            // 用户名
    char client_ip[16];           // 客户端 IP
    uint32_t session_id;          // 会话 ID
} ts_session_t;
```

**API 设计**：
```c
esp_err_t ts_session_create(const char *username, const char *password, ts_session_t *session);
esp_err_t ts_session_refresh(const char *refresh_token, ts_session_t *new_session);
esp_err_t ts_session_revoke(const char *token);
esp_err_t ts_session_list(ts_session_t *sessions, size_t *count);
esp_err_t ts_session_kick(uint32_t session_id);
```

---

#### 6. 用户数据库
**存储方式**：
- NVS 存储（加密）
- 最多支持 16 个用户

**用户信息**：
```c
typedef struct {
    char username[32];
    char password_hash[64];       // bcrypt/SHA256 哈希
    ts_user_role_t role;
    bool enabled;
    uint64_t created_at;
    uint64_t last_login;
} ts_user_t;
```

**密码哈希**：
- 使用 SHA256 + Salt（mbedTLS 提供）
- Salt 存储在用户记录中

**API 设计**：
```c
esp_err_t ts_user_create(const char *username, const char *password, ts_user_role_t role);
esp_err_t ts_user_delete(const char *username);
esp_err_t ts_user_change_password(const char *username, const char *old_pw, const char *new_pw);
esp_err_t ts_user_set_role(const char *username, ts_user_role_t role);
esp_err_t ts_user_list(ts_user_t *users, size_t *count);
```

---

## 🏗️ 架构设计

### 组件结构

```
components/
├── ts_security/                  # 现有安全组件
│   ├── include/
│   │   ├── ts_security.h        # 公共 API
│   │   ├── ts_https.h           # HTTPS 支持（新增）
│   │   ├── ts_user.h            # 用户管理（新增）
│   │   └── ts_session.h         # 会话管理（新增）
│   └── src/
│       ├── ts_security.c        # 现有实现
│       ├── ts_https.c           # HTTPS 实现（新增）
│       ├── ts_user.c            # 用户管理（新增）
│       └── ts_session.c         # 会话管理（新增）
└── ts_webui/
    ├── src/
    │   ├── ts_webui.c           # 更新：支持 HTTPS
    │   ├── ts_webui_api.c       # 更新：权限检查
    │   └── ts_webui_ws.c        # 更新：WSS 支持
    └── web/
        └── js/
            ├── auth.js          # 更新：Token 刷新
            └── api.js           # 更新：错误处理（403）
```

---

## 🔐 证书管理方案

### 方案 A：自签名证书（推荐用于局域网）

**优点**：
- 无需外部 CA，完全自主
- 适合内网设备
- 实现简单

**缺点**：
- 浏览器会显示"不安全"警告
- 用户需要手动信任证书

**实现**：
```c
// 启动时自动生成证书（如不存在）
esp_err_t ts_cert_generate_self_signed(void) {
    // 使用 mbedTLS 生成 RSA 密钥对
    // 创建自签名证书（CN=TianShanOS, O=TianShan, C=CN）
    // 保存到 NVS 或 SD 卡
}
```

**WebUI 提示**：
```html
<div class="cert-warning">
  ⚠️ 使用自签名证书，浏览器可能显示警告。
  <a href="#" onclick="downloadCert()">下载证书</a> 并添加到系统信任列表。
</div>
```

---

### 方案 B：Let's Encrypt（适合有公网 IP 的设备）

**优点**：
- 浏览器信任，无警告
- 自动续期

**缺点**：
- 需要公网 IP 和域名
- ACME 协议实现复杂
- ESP32 存储空间有限

**实施难度**：高（不推荐优先实现）

---

### 方案 C：企业 CA 证书（适合企业部署）

**优点**：
- 企业内部信任
- 统一管理

**缺点**：
- 需要用户手动导入证书

**实现**：
- WebUI 提供证书上传接口
- 支持 PEM 格式
- 验证证书链完整性

---

## 📱 WebUI 安全功能

### 1. 登录页面增强
```html
<!-- 角色选择（可选） -->
<select id="login-role" disabled>
  <option value="user">普通用户</option>
  <option value="operator">操作员</option>
  <option value="admin">管理员</option>
</select>

<!-- 记住我（保存 refresh token） -->
<label>
  <input type="checkbox" id="remember-me">
  保持登录（30天）
</label>
```

### 2. 用户管理页面（仅 ADMIN/ROOT 可见）
- 用户列表
- 创建/删除用户
- 修改密码
- 角色分配
- 在线会话管理（踢出用户）

### 3. 安全设置页面（仅 ROOT 可见）
- HTTPS 启用/禁用
- 证书管理（上传/下载/重新生成）
- 会话超时配置
- 双向认证启用/禁用
- 安全日志查看

---

## 🔧 配置选项（Kconfig）

```kconfig
menu "TianShanOS Security"
    config TS_SECURITY_HTTPS_ENABLE
        bool "Enable HTTPS Support"
        default y
        help
            Enable HTTPS server with TLS/SSL encryption.
    
    config TS_SECURITY_HTTPS_PORT
        int "HTTPS Port"
        default 443
        depends on TS_SECURITY_HTTPS_ENABLE
    
    config TS_SECURITY_HTTP_REDIRECT
        bool "Redirect HTTP to HTTPS"
        default y
        depends on TS_SECURITY_HTTPS_ENABLE
        help
            Automatically redirect HTTP requests to HTTPS.
    
    config TS_SECURITY_MTLS_ENABLE
        bool "Enable Mutual TLS (Client Certificate)"
        default n
        depends on TS_SECURITY_HTTPS_ENABLE
        help
            Require client certificates for authentication.
    
    config TS_SECURITY_SESSION_TIMEOUT
        int "Session Timeout (seconds)"
        default 3600
        help
            Token expiration time in seconds. 0 = never expire.
    
    config TS_SECURITY_MAX_USERS
        int "Maximum Number of Users"
        default 16
        range 1 32
    
    config TS_SECURITY_MAX_SESSIONS
        int "Maximum Concurrent Sessions"
        default 8
        range 1 32
endmenu
```

---

## 📊 实施计划

### Phase 16.1: HTTPS 基础 (1-2 周)
- [x] 调研 ESP-IDF HTTPS API
- [ ] 实现 `ts_https` 模块
- [ ] 自签名证书生成
- [ ] HTTP 服务器迁移到 HTTPS
- [ ] WebUI 适配（`https://` 和 `wss://`）
- [ ] 测试和优化

### Phase 16.2: 权限管理 (1 周)
- [ ] 设计权限模型和角色定义
- [ ] 实现 `ts_user` 模块（用户数据库）
- [ ] 实现 `ts_session` 模块（会话管理）
- [ ] API 权限检查集成
- [ ] WebUI 权限适配（动态隐藏功能）

### Phase 16.3: 会话增强 (3-5 天)
- [ ] Token 自动过期机制
- [ ] Refresh token 实现
- [ ] 会话列表和踢出功能
- [ ] WebUI 自动刷新 token

### Phase 16.4: WebUI 安全页面 (3-5 天)
- [ ] 用户管理页面
- [ ] 安全设置页面
- [ ] 证书管理界面
- [ ] 在线会话管理

### Phase 16.5: 双向认证（可选，2-3 周）
- [ ] 客户端证书验证
- [ ] 证书白名单管理
- [ ] WebUI 证书上传
- [ ] 测试和文档

---

## ⚠️ 技术挑战

### 1. 内存限制
**问题**：TLS 握手需要大量内存（~40KB）
**解决方案**：
- 限制并发 HTTPS 连接数（默认 4）
- 优化 mbedTLS 配置
- 使用 PSRAM（ESP32-S3 有 8MB）

### 2. 性能影响
**问题**：TLS 加解密消耗 CPU
**解决方案**：
- 使用硬件加速（ESP32-S3 支持 AES/SHA 硬件加速）
- 启用会话复用（TLS session resumption）
- 监控 CPU 使用率

### 3. 证书存储
**问题**：证书和密钥需要安全存储
**解决方案**：
- 使用 NVS 加密分区
- 或存储到 SD 卡（需要文件系统加密）

### 4. 用户体验
**问题**：自签名证书浏览器警告
**解决方案**：
- 提供证书下载和安装指南
- WebUI 提示用户信任证书
- 考虑支持 Let's Encrypt（未来）

---

## 🎯 业务决策点

### 决策 1：HTTPS 部署策略
**选项 A：默认启用 HTTPS，禁用 HTTP**
- 优点：强制安全
- 缺点：用户需要处理证书警告

**选项 B：HTTP 和 HTTPS 并存**（推荐）
- 优点：兼容性好，逐步迁移
- 缺点：存在安全风险

**选项 C：配置选项，用户决定**
- 优点：灵活
- 缺点：增加配置复杂度

**推荐**：选项 B，默认同时启用 HTTP (80) 和 HTTPS (443)，并提供 HTTP → HTTPS 重定向选项

---

### 决策 2：默认用户和角色
**选项 A：单用户模式（简化）**
- 默认用户：`admin` / `tianshan`
- 无角色区分

**选项 B：多用户多角色**（推荐）
- 默认用户：`root` / `tianshan` (ROOT 角色)
- 首次登录强制修改密码
- 可创建其他角色用户

**推荐**：选项 B，提供完整的用户管理功能

---

### 决策 3：会话超时时间
**选项**：
- 15 分钟（高安全）
- 1 小时（平衡）（推荐）
- 8 小时（低安全）
- 永不过期（不推荐）

**推荐**：默认 1 小时，可配置

---

### 决策 4：双向认证 (mTLS) 实施优先级
**选项 A：Phase 16 必须实现**
- 适合企业环境

**选项 B：Phase 17 或更晚实现**（推荐）
- 先完成基础 HTTPS 和权限管理
- mTLS 作为高级功能

**推荐**：选项 B，Phase 16 聚焦 HTTPS 和权限管理

---

### 决策 5：证书管理方式
**选项 A：仅支持自签名证书**（推荐短期）
- 实现简单
- 适合局域网

**选项 B：支持自签名 + 用户上传证书**（推荐中期）
- 兼容企业 CA
- 灵活性高

**选项 C：支持 Let's Encrypt**（长期目标）
- 需要公网和域名
- 实现复杂

**推荐路线图**：
- Phase 16.1：选项 A（自签名）
- Phase 16.4：选项 B（+ 证书上传）
- Phase 17+：选项 C（Let's Encrypt）

---

## 📚 参考资源

### ESP-IDF 文档
- [HTTPS Server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/protocols/esp_https_server.html)
- [mbedTLS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/protocols/mbedtls.html)
- [NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_flash.html#nvs-encryption)

### 安全最佳实践
- [OWASP Embedded Application Security](https://owasp.org/www-project-embedded-application-security/)
- [TLS Best Practices](https://wiki.mozilla.org/Security/Server_Side_TLS)

---

## 下一步行动

请审阅并决策：
1. **HTTPS 部署策略**（决策 1）
2. **用户角色模型**（决策 2）
3. **会话超时配置**（决策 3）
4. **mTLS 实施优先级**（决策 4）
5. **证书管理路线图**（决策 5）

确认后，我将开始 Phase 16.1 的开发工作。
