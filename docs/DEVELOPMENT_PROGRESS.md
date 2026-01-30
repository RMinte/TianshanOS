# TianShanOS 开发进度跟踪

> **项目**：TianShanOS（天山操作系统）  
> **版本**：0.3.6  
> **最后更新**：2026年1月30日  
> **代码统计**：116+ 个 C 源文件，86+ 个头文件

---

## 📊 总体进度

| 阶段 | 状态 | 进度 | 预计完成 |
|-----|------|------|---------|
| Phase 0: 规划与设计 | ✅ 完成 | 100% | 2026-01-15 |
| Phase 1: 基础架构 | ✅ 完成 | 100% | 2026-01-15 |
| Phase 2: 硬件抽象层 | ✅ 完成 | 100% | 2026-01-15 |
| Phase 3: 核心服务 | ✅ 完成 | 100% | 2026-01-15 |
| Phase 4: LED 系统 | ✅ 完成 | 100% | 2026-01-15 |
| Phase 5: 设备驱动 | ✅ 完成 | 100% | 2026-01-15 |
| Phase 6: 网络与安全 | ✅ 完成 | 100% | 2026-01-15 |
| Phase 7: WebUI | ✅ 完成 | 100% | 2026-01-15 |
| Phase 8: 文档与测试 | ✅ 完成 | 100% | 2026-01-15 |
| Phase 9: 统一配置系统 | ✅ 完成 | 100% | 2026-01-19 |
| Phase 10: WebUI 增强 & SSH Shell | ✅ 完成 | 100% | 2026-01-21 |
| Phase 11: OTA 固件升级 | ✅ 完成 | 100% | 2026-01-22 |
| Phase 12: OTA 增强 & Bug修复 | ✅ 完成 | 100% | 2026-01-23 |
| Phase 13: 日志系统增强 | ✅ 完成 | 100% | 2026-01-23 |
| Phase 14: LED 滤镜系统优化 | ✅ 完成 | 100% | 2026-01-23 |
| Phase 15: WebUI 日志流修复 & 导航栏清理 | ✅ 完成 | 100% | 2026-01-24 |
| Phase 16: DRAM 碎片优化 | ✅ 完成 | 100% | 2026-01-24 |
| Phase 17: 自动化引擎实现 | ✅ 完成 | 100% | 2026-01-27 |
| Phase 18: HTTPS 证书管理 | ✅ 完成 | 100% | 2026-01-27 |
| Phase 19: 主机管理增强 | ✅ 完成 | 100% | 2026-01-27 |
| Phase 20: 变量系统实现 | ✅ 完成 | 100% | 2026-01-27 |
| Phase 21: 动作模板系统 | ✅ 完成 | 100% | 2026-01-28 |
| Phase 22: 规则引擎增强 | ✅ 完成 | 100% | 2026-01-28 |
| Phase 23: 自动化 UI 增强 & 代码清理 | ✅ 完成 | 100% | 2026-01-29 |
| Phase 24: 风扇曲线控制增强 | ✅ 完成 | 100% | 2026-01-30 |
| Phase 25: 配置系统修复 & 内存优化 | ✅ 完成 | 100% | 2026-01-30 |
| Phase 26: WebUI 认证系统 | ✅ 完成 | 100% | 2026-01-30 |
| Phase 27: UI Widget 持久化 | ✅ 完成 | 100% | 2026-01-30 |

---

## 📋 Phase 27: UI Widget 持久化 ✅

**时间**：2026年1月30日  
**目标**：实现 WebUI 数据监控组件（Data Widgets）的后端持久化存储

### 问题背景

WebUI 首页的数据监控组件（显示 AGX 状态、功耗、温度等）原先只存储在浏览器 localStorage 中，导致：
- 更换浏览器/设备后配置丢失
- 无法在多设备间同步配置
- 不符合 TianShanOS 配置持久化原则

### 解决方案

实现完整的后端 API，支持 SD 卡 + NVS 双存储：

#### 配置优先级

```
SD 卡文件 > NVS 持久化 > 默认空配置
```

#### 加载逻辑

1. **SD 卡有配置** → 使用 SD 卡配置，返回 `source: "sdcard"`
2. **SD 卡无，NVS 有** → 使用 NVS 配置，**并自动复制到 SD 卡**，返回 `source: "nvs"`
3. **都没有** → 返回空配置，`source: "default"`

#### 保存逻辑

同时写入 SD 卡 + NVS（双写，确保持久化和可编辑性）

### 新增 API

#### 1. ui.widgets.get - 获取组件配置

```json
GET /api/v1/call
{
    "method": "ui.widgets.get"
}

Response:
{
    "code": 0,
    "data": {
        "widgets": [
            {"id": "w1", "type": "power", "config": {...}},
            {"id": "w2", "type": "temp", "config": {...}}
        ],
        "refresh_interval": 5000,
        "source": "sdcard"
    }
}
```

#### 2. ui.widgets.set - 保存组件配置

```json
POST /api/v1/call
{
    "method": "ui.widgets.set",
    "params": {
        "widgets": [...],
        "refresh_interval": 5000
    }
}

Response:
{
    "code": 0,
    "data": {
        "sdcard_saved": true,
        "nvs_saved": true
    }
}
```

### 存储位置

| 存储 | 路径/Key | 限制 |
|------|---------|------|
| SD 卡 | `/sdcard/config/ui_widgets.json` | 无限制 |
| NVS | namespace: `ts_ui`, key: `widgets` | 4000 字节 |

### 技术实现

#### 后端（ts_api_ui.c）

```c
// 从 SD 卡加载
static cJSON *load_widgets_from_sdcard(void) {
    char *content = ts_storage_read_file(WIDGETS_SD_PATH);
    return cJSON_Parse(content);
}

// 从 NVS 加载
static cJSON *load_widgets_from_nvs(void) {
    nvs_handle_t handle;
    nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    nvs_get_blob(handle, NVS_KEY, buffer, &len);
    return cJSON_Parse(buffer);
}

// SD 卡无配置时，从 NVS 同步到 SD 卡
if (!config_from_sdcard && config_from_nvs) {
    save_widgets_to_sdcard(config_from_nvs);
}
```

#### 前端（app.js）

```javascript
async function loadDataWidgets() {
    const result = await api.call('ui.widgets.get');
    if (result.code === 0 && result.data?.widgets) {
        widgets = result.data.widgets;
        refreshInterval = result.data.refresh_interval || 5000;
    }
}

async function saveDataWidgets() {
    await api.call('ui.widgets.set', {
        widgets: widgets,
        refresh_interval: refreshInterval
    });
}
```

### 代码变更

| 文件 | 变更内容 |
|------|---------|
| `ts_api_ui.c` | **新增**：UI 配置 API 处理器（408 行） |
| `ts_api.c` | 新增 `ts_api_ui_register()` 调用 |
| `ts_api.h` | 新增函数声明 |
| `CMakeLists.txt` | 添加 `ts_api_ui.c` 源文件 |
| `app.js` | 修改 `loadDataWidgets()` 和 `saveDataWidgets()` 使用后端 API |

### 设计优势

1. **SD 卡优先**：方便用户直接编辑 JSON 文件修改配置
2. **自动同步**：NVS 配置自动复制到 SD 卡，确保可见可编辑
3. **双写保障**：同时写入两个存储，防止数据丢失
4. **兼容性**：前端保留 localStorage 作为最终 fallback

---

## 📋 Phase 26: WebUI 认证系统 ✅

**时间**：2026年1月30日  
**目标**：实现 WebUI 用户认证、会话管理和权限控制

### 功能概述

本阶段实现了完整的 WebUI 认证系统：
- **双用户体系**：`admin`（管理员）和 `root`（超级用户）
- **密码安全**：SHA256 + 随机 Salt 哈希存储
- **会话管理**：24 小时 Token 有效期，最多 8 个并发会话
- **权限控制**：root 专属页面（终端、自动化、指令）
- **登录保护**：5 次失败锁定 5 分钟

### 用户权限

| 用户 | 权限级别 | 可访问页面 | 默认密码 |
|------|---------|-----------|---------|
| admin | TS_PERM_ADMIN (3) | 系统、网络、文件、安全 | rm01 |
| root | TS_PERM_ROOT (4) | 所有页面（含终端、自动化、指令）| rm01 |

### 新增 API

#### 1. auth.login - 用户登录

```json
POST /api/v1/auth/login
{
    "username": "admin",
    "password": "rm01"
}

Response:
{
    "code": 0,
    "data": {
        "token": "abc123...",
        "username": "admin",
        "level": "admin",
        "expires_in": 86400,
        "password_changed": false
    }
}
```

#### 2. auth.logout - 用户登出

```json
POST /api/v1/auth/logout
{
    "token": "abc123..."
}
```

#### 3. auth.status - 检查认证状态

```json
POST /api/v1/auth/status
{
    "token": "abc123..."
}

Response:
{
    "code": 0,
    "data": {
        "valid": true,
        "username": "admin",
        "level": "admin",
        "expires_in": 3600,
        "password_changed": false
    }
}
```

#### 4. auth.change_password - 修改密码

```json
POST /api/v1/auth/change_password
{
    "token": "abc123...",
    "old_password": "rm01",
    "new_password": "newpassword"
}
```

### 技术实现

#### 密码存储（ts_auth.c）

```c
typedef struct {
    uint8_t salt[16];           // 随机 Salt
    uint8_t hash[32];           // SHA256(salt + password)
    bool password_changed;       // 是否已修改默认密码
    uint32_t failed_attempts;    // 失败尝试次数
    uint32_t lockout_until;      // 锁定截止时间
} user_credential_t;
```

#### 版本控制强制重置

```c
#define AUTH_CONFIG_VERSION 3

// 版本变化时强制重置所有用户密码
if (stored_version != AUTH_CONFIG_VERSION) {
    force_create_user("admin", TS_PERM_ADMIN);
    force_create_user("root", TS_PERM_ROOT);
}
```

#### 前端权限控制（router.js）

```javascript
// 需要 root 权限的页面
this.rootOnlyPages = ['/terminal', '/automation', '/commands'];

// 导航菜单可见性
updateNavVisibility() {
    document.querySelectorAll('.nav-link').forEach(link => {
        if (link.hasAttribute('data-requires-root')) {
            link.style.display = api.isRoot() ? '' : 'none';
        }
    });
}
```

### 代码变更

| 文件 | 变更内容 |
|------|---------|
| `ts_auth.c` | 完全重写：SHA256+Salt 哈希、NVS 存储、登录锁定 |
| `ts_api_auth.c` | 新增：auth.* API 处理器 |
| `ts_webui_api.c` | 修改：登录响应格式、权限级别字符串化 |
| `ts_security.h` | 新增：ts_auth_* 函数声明 |
| `api.js` | 重写：登录/登出、Token 管理、权限检查 |
| `router.js` | 新增：页面访问权限检查、导航可见性控制 |
| `app.js` | 新增：认证 UI 更新、密码修改提醒 |
| `index.html` | 优化：登录表单、data-requires-root 属性 |
| `style.css` | 新增：.form-error 样式 |

### 安全特性

1. **密码哈希**：SHA256 + 16字节随机 Salt，防止彩虹表攻击
2. **恒定时间比较**：防止时序攻击
3. **登录锁定**：5 次失败后锁定 5 分钟
4. **Token 有效期**：24 小时自动过期
5. **会话限制**：最多 8 个并发会话
6. **敏感数据清除**：密码验证后立即清除内存中的明文

### 配置选项

```kconfig
# sdkconfig.defaults
CONFIG_TS_SECURITY_TOKEN_EXPIRE_SEC=86400  # Token 有效期 24 小时
```

---

## 📋 Phase 25: 配置系统修复 & 内存优化 ✅

**时间**：2026年1月30日  
**目标**：修复配置系统优先级问题、解决大型结构体栈溢出、消除启动警告

### 问题背景

在 NVS 配置同步到 SD 卡的过程中，发现多个严重问题：
1. **`ESP_ERR_NVS_VALUE_TOO_LONG`**：模板数组序列化为巨大字符串导致 NVS 写入失败
2. **配置优先级错误**：schema-less 模块（TEMP/RULES/ACTIONS/SOURCES）的加载顺序错误
3. **栈溢出崩溃**：大型结构体（>1KB）在循环中栈分配导致 main 任务栈溢出
4. **WebSocket 超时警告**：未配置超时参数导致启动时警告

### 配置优先级修正

**正确的配置优先级**（从高到低）：

```
SD 卡文件 > NVS 持久化 > 代码默认值
```

**Schema-based 模块**（8个）：NET, DHCP, WIFI, NAT, LED, FAN, DEVICE, SYSTEM
- 使用 `ts_config_module_persist()` 统一管理
- 文件后端自动处理优先级

**Schema-less 模块**（4个）：TEMP, RULES, ACTIONS, SOURCES
- 使用自定义 NVS 格式，需专门处理
- 修复前：先加载 NVS，SD 卡文件被忽略
- 修复后：优先检查 SD 卡，无则回退 NVS

### 核心修复

#### 1. SD 卡优先级修复

**修复的模块**：
- `ts_action_manager.c` - Action 模板加载
- `ts_rule_engine.c` - 规则加载
- `ts_source_manager.c` - 数据源加载
- `ts_temp_source.c` - 温度源配置

**修复模式**：
```c
// 修复前（错误）
load_from_nvs();  // 先加载 NVS
if (!loaded) {
    load_from_sd_card();  // SD 卡作为 fallback
}

// 修复后（正确）
if (load_from_sd_card()) {  // 优先 SD 卡
    save_to_nvs();  // 同步到 NVS
} else {
    load_from_nvs();  // SD 卡无文件时用 NVS
}
```

#### 2. 栈溢出修复（PSRAM 堆分配）

**问题**：`ts_action_template_t` (~1KB) 和 `ts_auto_rule_t` 在循环中栈分配

```c
// 错误：栈分配大型结构体
for (int i = 0; i < count; i++) {
    ts_action_template_t temp;  // ~1KB 在栈上！
    // 多次循环导致栈溢出
}

// 正确：PSRAM 堆分配
ts_action_template_t *temp = heap_caps_malloc(
    sizeof(ts_action_template_t), 
    MALLOC_CAP_SPIRAM
);
if (!temp) {
    temp = malloc(sizeof(ts_action_template_t));  // Fallback
}
// 使用后释放
free(temp);
```

#### 3. WebSocket 超时配置

**修复位置**：`ts_source_manager.c`

```c
esp_websocket_client_config_t ws_cfg = {
    .uri = ws_url,
    .buffer_size = 2048,
    .reconnect_timeout_ms = 10000,  // 新增
    .network_timeout_ms = 10000,    // 新增
    .ping_interval_sec = 0,         // 新增：禁用心跳
};
```

#### 4. automation.json 加载

**新增**：`ts_automation.c` 中实现 `load_config()` 函数
- 解析 `/sdcard/config/automation.json`
- 文件不存在时使用 DEBUG 级别日志（不再是 WARNING）

### 代码变更

| 文件 | 变更内容 |
|------|---------|
| `ts_action_manager.c` | SD卡优先级修复 + PSRAM堆分配 |
| `ts_rule_engine.c` | SD卡优先级修复 + PSRAM堆分配 |
| `ts_source_manager.c` | SD卡优先级修复 + WebSocket超时配置 |
| `ts_temp_source.c` | SD卡优先级修复 |
| `ts_automation.c` | 实现 load_config() 函数 |
| `ts_config_file.c` | 跳过 schema-less 文件的通用加载 |

### 验证结果

启动日志确认修复成功：
```log
I (3222) ts_source_mgr: Loaded 1 sources from SD card: /sdcard/config/sources.json
I (3289) ts_rule_engine: Loaded 5 rules from SD card: /sdcard/config/rules.json
I (3430) ts_action_mgr: Loaded 16 action templates from SD card: /sdcard/config/actions.json
I (5155) websocket_client: Started  ← 无超时警告
```

**系统状态**：
- 12 个服务全部正常运行
- 启动时间：4061 ms
- 无 `ESP_ERR_NVS_VALUE_TOO_LONG` 错误
- 无栈溢出崩溃
- 无 WebSocket 超时警告

### 内存分配规范总结

| 大小 | 分配方式 | 说明 |
|------|---------|------|
| < 128 字节 | `malloc()` | 小分配用 DRAM |
| ≥ 128 字节 | `heap_caps_malloc(MALLOC_CAP_SPIRAM)` | 中大分配用 PSRAM |
| 循环中的大型结构体 | 必须用堆分配 | 避免栈溢出 |
| DMA 缓冲区 | `heap_caps_malloc(MALLOC_CAP_DMA)` | 必须用 DRAM |

---

## 📋 Phase 24: 风扇曲线控制增强 ✅

**时间**：2026年1月30日  
**目标**：增强风扇温度曲线控制系统，支持变量绑定温度源、占空比限制和测试温度功能

### 功能概述

本阶段完善了风扇温度控制系统：
- **温度变量绑定**：支持将系统变量（如 `agx.cpu_temp`）绑定为温度源
- **占空比限制**：新增 `fan.limits` API 设置风扇最小/最大占空比
- **测试温度**：支持手动设置测试温度用于调试曲线
- **WebUI 增强**：曲线编辑器支持设置占空比限制

### 新增 API

#### 1. fan.limits - 设置占空比限制

**请求**:
```json
POST /api/v1/fan.limits
{
  "id": 0,
  "min_duty": 10,
  "max_duty": 100
}
```

**用途**：设置风扇在曲线模式下的占空比上下限，解决曲线最低点被 min_duty 限制的问题。

#### 2. temp.bind - 绑定温度变量

**请求**:
```json
POST /api/v1/temp.bind
{
  "variable": "agx.cpu_temp"
}
```

**用途**：将系统变量绑定为温度源，实现风扇根据 AGX GPU/CPU 温度自动调速。

#### 3. temp.manual - 测试温度

**请求**:
```json
POST /api/v1/temp.manual
{
  "enable": true,
  "temperature": 45.0
}
```

**用途**：调试风扇曲线时设置虚拟温度，验证曲线配置是否正确。

### CLI 命令增强

```bash
# 设置占空比限制
fan --limits --id 0 --min 10 --max 100

# 绑定温度变量
temp --bind --variable "agx.cpu_temp"

# 设置测试温度
temp --manual --enable --value 45
temp --manual --disable
```

### WebUI 改进

曲线管理模态框新增：
- **最小占空比**：设置风扇最低转速限制（0-100%）
- **最大占空比**：设置风扇最高转速限制（0-100%）
- **温度变量绑定**：选择系统变量作为温度源
- **测试温度**：输入测试温度验证曲线效果

### 修复的问题

1. **曲线不生效**：曲线计算出的占空比被 `min_duty` 限制，现在可以通过 `fan.limits` 调整
2. **温度不同步**：清除测试温度后有效温度未恢复到绑定变量值，添加 `manual_mode` 自动禁用逻辑
3. **target_duty 不更新**：在 `ts_fan_get_status()` 中增加实时温度获取和曲线计算

### 代码变更

| 文件 | 变更内容 |
|------|---------|
| `ts_fan.c` | 新增 `ts_fan_set_limits()` 函数 |
| `ts_fan.h` | 新增 `ts_fan_set_limits()` 声明 |
| `ts_api_fan.c` | 新增 `api_fan_limits()` 处理函数 |
| `ts_cmd_fan.c` | 新增 `--limits`, `--min`, `--max` 参数 |
| `ts_temp_source.c` | 修复 `manual_mode` 切换逻辑 |
| `app.js` | 曲线模态框增加占空比限制设置 |

---

## 📋 Phase 23: 自动化 UI 增强 & 代码清理 ✅

**时间**：2026年1月29日  
**目标**：增强自动化模块 WebUI 体验，修复变量选择器、统计数据显示问题，清理冗余页面

### 功能概述

本阶段专注于自动化模块的 WebUI 优化和代码清理：
- **变量选择器统一**：规则条件和动作条件都使用变量选择器
- **统计数据修复**：修复自动化引擎顶部统计数据显示为零的问题
- **监控页面移除**：删除冗余的设备监控页面，简化导航

### 核心修复

#### 1. 变量选择器 Modal ID 不匹配

**问题**：点击变量选择器按钮无响应

**原因**：
```javascript
// 错误：使用了不存在的 modal ID
const modal = document.getElementById('variable-selector-modal');
```

**修复**：
```javascript
// 正确：使用实际存在的 modal ID
const modal = document.getElementById('variable-select-modal');
```

#### 2. 触发条件变量选择器

**问题**：规则的触发条件只有文本输入框，需要变量选择器

**修复**：为条件行添加变量选择器按钮
```javascript
// addConditionRow() 修改
<input type="hidden" class="cond-var-name" id="cond-var-${rowId}" value="">
<button type="button" class="btn btn-sm btn-outline-secondary" 
        onclick="openConditionVarSelector('${rowId}')">
    <i class="bi bi-list-ul"></i> 选择变量
</button>
<span class="cond-var-display ms-2 text-muted" id="cond-var-display-${rowId}">
    未选择变量
</span>
```

**新增函数**：
- `openConditionVarSelector(rowId)` - 打开条件变量选择器
- `handleConditionVarSelect(varName)` - 处理条件变量选择结果

#### 3. 自动化引擎统计数据全为零

**问题**：自动化页面顶部的规则数、变量数、数据源数等统计全部显示为 0

**原因**：前端字段名与 API 响应字段名不匹配
```javascript
// 错误：使用了错误的字段名
data.rule_count     // API 返回的是 rules_count
data.variable_count // API 返回的是 variables_count
```

**修复**：
```javascript
// 正确：使用正确的字段名
document.getElementById('stat-rules').textContent = data.rules_count || 0;
document.getElementById('stat-variables').textContent = data.variables_count || 0;
document.getElementById('stat-sources').textContent = data.sources_count || 0;
document.getElementById('stat-triggers').textContent = data.rule_triggers || 0;
document.getElementById('stat-uptime').textContent = 
    Math.floor((data.uptime_ms || 0) / 1000) + 's';
```

#### 4. 监控页面移除

**删除内容**：
- 导航链接：`<a href="#/device" class="nav-link">监控</a>`
- 路由注册：`router.register('/device', loadDevicePage)`
- 相关函数：`loadDevicePage()`, `refreshDevicePageOnce()`, `updateDeviceUI()`, 
  `updateAgxMonitorData()`, `devicePower()`, `deviceReset()`, `deviceForceOff()`

### 变量选择器回调机制

使用 `modal.dataset.callback` 区分不同的选择场景：

| 回调值 | 场景 | 处理函数 |
|-------|------|----------|
| `actionCondition` | 动作级条件变量选择 | `handleActionConditionVarSelect()` |
| `ruleCondition` | 规则触发条件变量选择 | `handleConditionVarSelect()` |
| (无) | 普通变量选择 | 默认处理逻辑 |

```javascript
function selectVariable(varName) {
    const varSelectModal = document.getElementById('variable-select-modal');
    const callback = varSelectModal?.dataset?.callback;
    
    if (callback === 'actionCondition') {
        handleActionConditionVarSelect(varName);
    } else if (callback === 'ruleCondition') {
        handleConditionVarSelect(varName);
    } else {
        // 默认处理
    }
}
```

### 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `app.js` | 变量选择器修复、条件变量选择器、统计数据字段名修复、删除监控页面代码 |
| `index.html` | 删除监控页面导航链接 |

### 导航栏结构（更新后）

```
┌─────────────────────────────────────────────────────────────┐
│  系统  │  网络  │  自动化  │  文件  │  终端  │  指令  │  安全  │
└─────────────────────────────────────────────────────────────┘
```

已移除：~~监控~~

### 窗口导出函数

```javascript
window.openConditionVarSelector = openConditionVarSelector;
window.handleConditionVarSelect = handleConditionVarSelect;
```

---

## 📋 Phase 22: 规则引擎增强 ✅

**时间**：2026年1月28日  
**目标**：完善规则引擎，支持规则持久化、编辑、手动触发模式

### 功能概述

规则引擎是自动化系统的核心，本阶段实现了：
- **规则 NVS 持久化**：规则保存到 NVS，重启后自动恢复
- **规则编辑功能**：WebUI 支持编辑现有规则
- **手动触发模式**：支持创建无条件规则，仅通过手动触发执行
- **SSH/CLI 动作执行**：完整支持 SSH 命令引用和 CLI 命令动作

### 核心修复

#### 1. 规则注册深拷贝问题

**问题**：`ts_rule_register()` 使用浅拷贝导致悬空指针

**原因**：
```c
// 错误：浅拷贝只复制指针，不复制数据
memcpy(&s_rule_ctx.rules[idx], rule, sizeof(ts_auto_rule_t));
```

**修复**：改为深拷贝
```c
// 正确：分别分配内存并复制数据
new_rule->conditions.conditions = heap_caps_calloc(...);
memcpy(new_rule->conditions.conditions, rule->conditions.conditions, ...);
new_rule->actions = heap_caps_calloc(...);
memcpy(new_rule->actions, rule->actions, ...);
```

#### 2. 空条件规则自动触发问题

**问题**：没有条件的规则会被自动评估触发

**原因**：
```c
// 错误：空条件返回 true 导致自动触发
if (!group || group->count == 0) {
    return true;
}
```

**修复**：空条件返回 false
```c
// 正确：空条件不满足，仅支持手动触发
if (!group || group->count == 0) {
    return false;  // 仅手动触发的规则
}
```

#### 3. 动作类型执行缺失

**问题**：`TS_AUTO_ACT_SSH_CMD_REF`（类型 7）和 `TS_AUTO_ACT_CLI` 未实现

**修复**：在 `ts_action_execute()` 中添加处理分支
```c
case TS_AUTO_ACT_SSH_CMD_REF:
    ret = execute_ssh_ref_action(action);  // 调用 ts_action_exec_ssh_ref()
    break;

case TS_AUTO_ACT_CLI:
    ret = execute_cli_action(action);      // 调用 ts_action_exec_cli()
    break;
```

### 规则持久化设计

**NVS 存储结构**：
```
NVS Namespace: auto_rules
├── count      → 规则数量 (uint8_t)
├── rule_0     → 第一条规则 JSON
├── rule_1     → 第二条规则 JSON
└── ...
```

**JSON 序列化格式**：
```json
{
  "id": "rule_power_off",
  "name": "低电压关机",
  "enabled": true,
  "cooldown_ms": 60000,
  "conditions": {
    "logic": "and",
    "items": [
      {"variable": "power.voltage", "operator": "lt", "value": 12.6}
    ]
  },
  "actions": [
    {"type": "ssh_cmd_ref", "cmd_id": "agx_shutdown", "delay_ms": 0},
    {"type": "log", "message": "低电压保护触发", "level": 2}
  ]
}
```

### API 端点

| 端点 | 说明 | 参数 |
|------|------|------|
| `automation.rules.list` | 列出所有规则 | - |
| `automation.rules.add` | 创建规则 | id, name, conditions, actions |
| `automation.rules.get` | 获取规则详情 | id |
| `automation.rules.delete` | 删除规则 | id |
| `automation.rules.enable` | 启用规则 | id |
| `automation.rules.disable` | 禁用规则 | id |
| `automation.rules.trigger` | 手动触发规则 | id |

### WebUI 增强

**规则列表新增功能**：
- ✏️ 编辑按钮：点击打开编辑弹窗
- ▶️ 触发按钮：手动执行规则

**添加/编辑规则弹窗**：
- 支持"仅手动触发"复选框
- 勾选后隐藏条件配置区域
- 动作模板下拉选择

### 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `ts_rule_engine.c` | NVS 持久化、深拷贝、空条件处理、SSH/CLI 动作执行 |
| `ts_rule_engine.h` | 添加 `ts_rules_save()` / `ts_rules_load()` 声明 |
| `ts_api_automation.c` | 添加 `automation.rules.get` API |
| `app.js` | 规则编辑 UI、手动触发复选框、editRule() 函数 |

### 规则执行流程

```
创建规则 (WebUI/API)
    ↓
ts_rule_register() → 深拷贝到内部存储 → ts_rules_save() 保存 NVS
    ↓
规则评估循环（每 100ms）:
    ↓
ts_rule_evaluate_all()
    ↓
ts_rule_eval_condition_group()
    ↓ (条件满足)
ts_action_execute_array() → 执行动作序列
    ↓
execute_ssh_ref_action() / execute_cli_action() / ...
```

---

## 📋 Phase 21: 动作模板系统 ✅

**时间**：2026年1月28日  
**目标**：实现动作模板管理系统，支持创建、执行、持久化可复用的自动化动作

### 功能概述

动作模板是自动化引擎的核心功能之一，允许用户：
- **创建动作模板**：定义可复用的动作配置
- **执行动作**：手动触发或由规则引擎自动触发
- **持久化存储**：模板保存到 NVS，重启后自动恢复

### 支持的动作类型

| 类型 | 说明 | 配置参数 |
|------|------|----------|
| **CLI** | 执行本地控制台命令 | command, var_name, timeout_ms |
| **SSH** | 执行已配置的 SSH 命令 | cmd_id（引用 SSH 命令配置） |
| **LED** | 控制 LED 设备 | device, color, effect, brightness |
| **Log** | 记录日志消息 | level, message |
| **Set Variable** | 设置变量值 | variable, value |
| **Webhook** | 发送 HTTP 请求 | url, method, body_template |

### SSH 命令执行流程

**设计理念**：动作模板引用已配置的 SSH 命令，而非重复配置

```
SSH 管理页面配置命令 → 动作模板引用命令 → 执行时查找并运行
      (主机+命令+变量)        (cmd_id)          (keystore 认证)
```

**SSH 认证机制**：
- 自动从 `ts_keystore` 加载私钥到内存
- 使用内存认证而非文件路径
- 支持 fallback 到文件认证

### API 端点

| 端点 | 说明 | 参数 |
|------|------|------|
| `automation.actions.list` | 列出所有动作模板 | - |
| `automation.actions.add` | 创建动作模板 | id, name, type, ... |
| `automation.actions.get` | 获取模板详情 | id |
| `automation.actions.delete` | 删除模板 | id |
| `automation.actions.execute` | 执行模板 | id |

### NVS 持久化

动作模板自动保存到 NVS（`action_tpl` namespace）：
- **添加模板**：立即保存
- **删除模板**：立即更新
- **更新模板**：立即保存
- **初始化时**：自动从 NVS 加载

**存储格式**：
```
NVS Namespace: action_tpl
├── count     → 模板数量 (uint8_t)
├── tpl_0     → 第一个模板 JSON
├── tpl_1     → 第二个模板 JSON
└── ...
```

### WebUI 集成

**自动化页面新增"动作模板"区域**：
- 卡片式动作类型选择
- 类型特定的配置表单
- SSH 命令下拉选择（引用已配置命令）
- LED 设备配置（支持 touch/board/matrix）
- 实时执行与结果反馈

### 修改的文件

| 文件 | 修改内容 |
|------|----------|
| `ts_action_manager.c` | 添加 NVS 持久化功能 |
| `ts_action_manager.h` | 添加 save/load 函数声明 |
| `ts_api_automation.c` | 添加 actions.get API |
| `app.js` | 动作模板 UI（创建/编辑/执行） |

### 核心代码变更

**ts_action_manager.c 新增功能**：
```c
// NVS 持久化
esp_err_t ts_action_templates_save(void);  // 保存所有模板到 NVS
esp_err_t ts_action_templates_load(void);  // 从 NVS 加载模板

// 初始化时自动加载
esp_err_t ts_action_manager_init(void) {
    // ... 创建 mutex/queue/task ...
    ts_action_templates_load();  // 从 NVS 恢复模板
}

// 添加/删除/更新后自动保存
esp_err_t ts_action_template_add(...) {
    // ... 添加模板 ...
    ts_action_templates_save();  // 持久化
}
```

---

## 📋 Phase 20: 变量系统统一 ✅

**时间**：2026年1月27日  
**目标**：统一变量存储系统，废弃旧 `ts_var` 组件，使用 `ts_variable`（位于 `ts_automation` 内）作为唯一变量系统

### 系统概述

变量系统是自动化引擎的核心数据层，提供：
- **类型安全**：bool/int/float/string 四种类型
- **SSH 结果存储**：自动保存 7 个标准变量（status/exit_code/extracted/expect_matched/fail_matched/host/timestamp）
- **线程安全**：FreeRTOS mutex 保护
- **PSRAM 优先**：大容量变量存储优先使用 PSRAM
- **事件通知**：变量变化通过事件总线通知

> **架构变更（2026-01-27）**：原 `ts_var` 组件已删除，统一使用 `ts_variable`（位于 `components/ts_automation/`）。

### 组件实现

#### ts_variable 统一变量系统 (`components/ts_automation/`)

**文件结构**：
```
components/ts_automation/
├── CMakeLists.txt
├── include/
│   └── ts_variable.h     # 统一变量 API
└── src/
    └── ts_variable.c     # 实现
```

> **注意**：原 `components/ts_var/` 目录已删除（2026-01-27）。

**内存分配**：
- 动态变量数量（最大 256 个，可配置）
- **优先分配到 PSRAM**
- 自动 fallback 到 DRAM（如 PSRAM 不可用）

**变量类型**（类型安全）：
| 类型 | 枚举值 | 说明 |
|------|--------|------|
| 布尔 | `TS_VAR_TYPE_BOOL` | true/false |
| 整数 | `TS_VAR_TYPE_INT` | 32 位有符号整数 |
| 浮点 | `TS_VAR_TYPE_FLOAT` | 单精度浮点 |
| 字符串 | `TS_VAR_TYPE_STRING` | UTF-8 字符串 |

**变量来源**（通过 source_id 分组）：
- SSH 命令结果按 source_id 自动分组
- 支持按 source_id 批量删除

#### 核心 API

```c
#include "ts_variable.h"

// 初始化（ts_automation 服务启动时自动调用）
esp_err_t ts_variable_init(void);

// 类型安全的设置 API
esp_err_t ts_variable_set_string(const char *name, const char *value, const char *source_id);
esp_err_t ts_variable_set_int(const char *name, int value, const char *source_id);
esp_err_t ts_variable_set_float(const char *name, float value, const char *source_id);
esp_err_t ts_variable_set_bool(const char *name, bool value, const char *source_id);

// 类型安全的获取 API
esp_err_t ts_variable_get(const char *name, ts_auto_value_t *value);
const char* ts_variable_get_string(const char *name, const char *default_val);
int ts_variable_get_int(const char *name, int default_val);
float ts_variable_get_float(const char *name, float default_val);
bool ts_variable_get_bool(const char *name, bool default_val);

// 检查存在性
bool ts_variable_exists(const char *name);

// 枚举变量
esp_err_t ts_variable_iterate(ts_variable_iterate_fn callback, void *user_data);

// 按源删除
esp_err_t ts_variable_delete_by_source(const char *source_id);
```

### SSH 命令结果存储

SSH 命令执行后，WebUI 自动存储 7 个标准变量（通过 `source_id` 分组）：

| 变量名 | 类型 | 说明 |
|--------|------|------|
| `<source_id>.status` | STRING | 执行状态：running/completed/cancelled/failed |
| `<source_id>.exit_code` | INT | 退出码（0=成功） |
| `<source_id>.extracted` | STRING | 正则提取的内容 |
| `<source_id>.expect_matched` | BOOL | 是否匹配成功模式 |
| `<source_id>.fail_matched` | BOOL | 是否匹配失败模式 |
| `<source_id>.host` | STRING | 执行主机 |
| `<source_id>.timestamp` | INT | 执行时间戳 |

**WebUI SSH Exec 集成**：

WebSocket 消息支持 `var_name` 字段：
```json
{
  "action": "ssh_exec",
  "host_id": "agx@10.10.99.100",
  "command": "cat /sys/class/thermal/thermal_zone0/temp",
  "var_name": "cpu_temp",
  "expect_pattern": "\\d+",
  "timeout_ms": 30000
}
```

执行完成后自动创建：
- `cpu_temp.extracted` = "45000" (STRING)
- `cpu_temp.status` = "completed" (STRING)
- `cpu_temp.exit_code` = 0 (INT)
- `cpu_temp.expect_matched` = true (BOOL)

### API 端点

通过 `automation.variables.*` API 访问变量：

| 端点 | 说明 | 参数 |
|------|------|------|
| `automation.variables.get` | 获取变量值 | `name` |
| `automation.variables.set` | 设置变量 | `name`, `value`, `type` |
| `automation.variables.delete` | 删除变量 | `name` |
| `automation.variables.list` | 列出所有变量 | `prefix` (可选) |

> **注意**：原 `var.*` API 已废弃（2026-01-27）。

### 与自动化引擎集成

ts_variable 是自动化引擎的核心数据层：

#### 1. 数据源变量存储

```c
// ts_source_manager 将数据源值映射为变量
ts_variable_set_float("agx.cpu_temp", temp_value, "agx_monitor");
```

#### 2. 规则引擎条件评估

```c
// ts_rule_engine 使用类型安全 API 获取值
float temp = ts_variable_get_float("agx.cpu_temp", 0.0f);
if (temp > 80.0f) {
    // 触发动作
}
```

### 架构变更记录（2026-01-27）

#### 删除的文件

| 文件/目录 | 说明 |
|------|------|
| `components/ts_var/` | 整个目录删除，功能统一到 ts_variable |
| `components/ts_api/src/ts_api_var.c` | 旧 var.* API 处理器删除 |

#### 修改的文件

| 文件 | 修改内容 |
|------|------|
| `components/ts_webui/CMakeLists.txt` | 移除 ts_var 依赖 |
| `components/ts_automation/CMakeLists.txt` | 移除 ts_var 依赖 |
| `components/ts_api/CMakeLists.txt` | 移除 ts_api_var.c 和 ts_var 依赖 |
| `components/ts_api/include/ts_api.h` | 移除 ts_api_var_register() 声明 |
| `components/ts_api/src/ts_api.c` | 移除 ts_api_var_register() 调用 |
| `components/ts_webui/src/ts_webui_ws.c` | 使用 ts_variable.h 替代 ts_var.h |
| `components/ts_automation/src/ts_source_manager.c` | 重写 read_variable_source() 使用统一 API |

### 新增/保留文件

| 文件 | 描述 |
|------|------|
| `components/ts_automation/src/ts_variable.c` | 统一变量系统实现 |
| `components/ts_automation/include/ts_variable.h` | 统一变量 API |
| `components/ts_webui/www/pages/automation/variables.html` | 变量管理页面 |

### 内存优化效果

| 指标 | 优化前 | 优化后 |
|------|--------|--------|
| DRAM .bss | 59,952 bytes | 37,936 bytes |
| PSRAM 使用 | - | +21,504 bytes |
| 节省 DRAM | - | ~22KB |

---

## 📋 Phase 19: 主机管理增强 ✅

**时间**：2026年1月27日  
**目标**：完善 SSH 主机管理功能，解决密钥删除后已部署主机记录残留的问题

### 问题背景

删除 SSH 密钥后，已部署主机记录仍然存在，导致：
- 无法撤销公钥（因为密钥已不存在）
- 无法删除本地记录
- 无法更新服务器指纹

### 解决方案

#### 1. CLI `hosts` 命令增强 🔧

**新增已部署主机管理**：
```bash
# 列出所有已部署主机
hosts --deployed

# 移除已部署主机记录（仅删除本地记录）
hosts --deployed --remove --id agx@10.10.99.100

# 清空所有已部署主机记录
hosts --deployed --clear --yes
```

**原有指纹管理保持不变**：
```bash
hosts --list           # 列出已知主机指纹
hosts --remove --host <ip>  # 移除指纹
hosts --clear --yes    # 清空所有指纹
```

#### 2. WebUI 安全页面增强 🖥️

**已部署主机表格**：
- 新增"移除"按钮（仅删除本地记录，不撤销远程公钥）
- 原有"撤销"按钮保持（撤销远程公钥并删除本地记录）

**新增"已知主机指纹"管理区域**：
- 显示所有 SSH 服务器指纹
- 支持查看完整指纹
- 支持删除指纹（服务器重装后需要）

#### 3. 内存优化 💾

**PSRAM 分配迁移**：
- `ts_api_key.c`: `ts_keystore_key_info_t` 数组改用 `heap_caps_malloc(SPIRAM)` 
- `ts_cmd_key.c`: 同上
- `ts_cmd_hosts.c`: `ts_ssh_host_config_t` 数组改用 PSRAM

### 修改文件

| 文件 | 修改内容 |
|------|----------|
| `ts_cmd_hosts.c` | 添加 `--deployed` 模式和相关处理函数 |
| `ts_api_key.c` | 使用 PSRAM 分配避免栈溢出 |
| `ts_cmd_key.c` | 使用 PSRAM 分配避免栈溢出 |
| `app.js` | 安全页面添加"已知主机指纹"区域和"移除"按钮 |

### 安全页面新结构

```
安全与连接
├── 🔑 密钥管理
│   ├── SSH 密钥（ts_keystore）
│   └── HTTPS 密钥（ts_cert）
├── 🖥️ 已部署主机
│   ├── 测试连接
│   ├── 撤销公钥（并删除记录）
│   └── 移除记录（仅本地） ✨ 新增
├── 🔐 已知主机指纹 ✨ 新增
│   ├── 查看完整指纹
│   └── 删除指纹
└── 🔒 HTTPS 证书
```

---

## 📋 Phase 18: HTTPS 证书管理 ✅

**时间**：2026年1月27日  
**目标**：实现完整的 HTTPS/mTLS 证书管理系统，支持设备端 PKI 操作和 WebUI 管理界面

### 核心功能

#### 1. ts_cert 证书管理组件 🔐

**实现特性**：
- ECDSA P-256 密钥对生成（存储在 PSRAM，安全清零）
- X.509 CSR 生成（符合 RFC 2986）
- 证书链验证（Root CA → Intermediate CA → Device Cert）
- NVS 持久化存储（私钥/证书/CA链）
- 证书状态查询（有效期、主体、签发者）

**存储架构**：
```
NVS namespace: ts_cert
├── priv_key    - ECDSA P-256 私钥 (PEM)
├── cert        - 设备证书 (PEM)
└── ca_chain    - CA 证书链 (PEM)
```

#### 2. Core API 层 (ts_api_cert) 🌐

**7 个 REST API 端点**：

| API | 方法 | 功能 |
|-----|------|------|
| `cert.status` | GET | 获取证书状态（有效期、CN、签发者等） |
| `cert.generate_key` | POST | 生成新的 ECDSA P-256 密钥对 |
| `cert.generate_csr` | POST | 生成 CSR（需提供 CN） |
| `cert.get_csr` | GET | 获取已生成的 CSR |
| `cert.install` | POST | 安装签发的证书（PEM 格式） |
| `cert.install_ca` | POST | 安装 CA 证书链 |
| `cert.delete` | POST | 删除所有证书和密钥 |

**返回格式示例**：
```json
{
  "success": true,
  "data": {
    "has_private_key": true,
    "has_certificate": true,
    "has_ca_chain": true,
    "cert_info": {
      "subject_cn": "TIANSHAN-DEVICE-001",
      "issuer_cn": "TianShanOS Intermediate CA",
      "not_before": "2026-01-27 00:00:00",
      "not_after": "2027-01-27 00:00:00",
      "serial": "01",
      "is_valid": true,
      "days_until_expiry": 363
    }
  }
}
```

#### 3. WebUI 安全页面增强 🎨

**统一密钥管理表格**：
- SSH 密钥（来自 ts_keystore）
- HTTPS 密钥（来自 ts_cert）
- 状态展示：🔐 已生成 / 🔒 未生成
- 操作按钮：生成密钥、查看证书、生成 CSR、删除

**HTTPS 证书状态卡片**：
- 证书有效性指示（✅ 已激活 / ⚠️ 即将过期 / ❌ 已过期）
- 详细信息：主体 CN、签发者、有效期、序列号
- 操作按钮：生成 CSR、安装证书、安装 CA、删除

**模态框交互**：
- CSR 生成：输入 CN → 调用 API → 显示 PEM（可复制）
- 证书安装：粘贴 PEM → 调用 API → 刷新状态
- CA 安装：粘贴 CA 链 PEM → 调用 API → 验证完整性

### 技术细节

#### API 端点限制调整
将 `CONFIG_TS_API_MAX_ENDPOINTS` 从 200 增加到 256，支持新增的 7 个证书 API。

#### 前端字段映射修复
修复 API 返回字段与前端代码不匹配的问题：
- `has_keypair` → `has_private_key`
- `certificate` → `cert_info`

### 新增文件

| 文件 | 描述 |
|------|------|
| `components/ts_api/src/ts_api_cert.c` | 证书 API 实现（7 个端点） |
| `components/ts_webui/web/js/api.js` | 新增 `CertAPI` 命名空间 |
| `components/ts_webui/web/js/app.js` | 安全页面 HTTPS 证书管理 UI |

### 安全页面结构

```
安全与连接
├── 🔑 密钥管理
│   ├── SSH 密钥（ts_keystore）
│   └── HTTPS 密钥（ts_cert） ✨ 新增
├── 🖥️ 已部署主机
└── 🔒 HTTPS 证书 ✨ 新增
    ├── 状态卡片（有效期/CN/签发者）
    └── 操作按钮（CSR/安装/删除）
```

---

## 📋 Phase 17: 自动化引擎实现 ✅

**时间**：2026年1月25日 - 1月27日  
**目标**：实现完整的自动化引擎，支持多数据源监控和 WebUI Dashboard

### 核心功能

#### 1. 数据源管理系统 (ts_source_manager) 🔌

**支持的数据源协议**：
- **Socket.IO**: 实时 WebSocket 数据流（如 AGX tegrastats）
- **REST API**: HTTP 轮询数据源（如 LPMU 状态）
- **WebSocket**: 原生 WebSocket 连接

**配置统一化**：
```json
{
  "sources": [{
    "id": "agx",
    "type": "socketio",
    "url": "http://10.10.99.98:58090",
    "events": ["tegrastats_update"],
    "mappings": [
      {"json_path": "cpu[0].usage", "var_name": "agx.cpu0_usage"},
      {"json_path": "temperature.cpu", "var_name": "agx.cpu_temp"}
    ]
  }]
}
```

**自动发现 (auto_discover)**：
- Socket.IO / REST 源均支持 `auto_discover: true`
- 自动从 JSON 响应中提取所有字段创建变量
- 首次接收数据时自动注册，支持嵌套对象/数组

#### 2. 变量存储系统 (ts_variable) 📊

**实现特性**：
- 支持 bool/int/float/string 四种类型
- 线程安全（FreeRTOS mutex）
- 优先使用 PSRAM 分配
- 变量变化通过事件总线通知
- 支持按前缀枚举、按源批量删除

#### 3. JSONPath 解析器 (ts_jsonpath) 🔍

**轻量级实现**：
```c
// 支持的语法
cJSON *val = ts_jsonpath_get(root, "cpu[0].usage");      // 数组索引
cJSON *arr = ts_jsonpath_get(root, "cpu[*].freq");       // 通配符展开
cJSON *last = ts_jsonpath_get(root, "history[-1].value"); // 负索引
```

**便捷 API**：
```c
double temp = ts_jsonpath_get_number(data, "temperature.cpu", 0.0);
char *name = ts_jsonpath_get_string(data, "device.name");  // 调用方需 free
```

#### 4. 算力设备监控 (ts_compute_monitor) 🖥️

**独立模块**（从 ts_source_manager 分离）：
- 专门监控 Jetson AGX 等算力设备
- Socket.IO 协议完整实现（握手/probe/upgrade/ping-pong）
- 自动重连（指数退避）
- 温度数据自动推送到 `ts_temp_source`

**CLI 命令**：
```bash
compute --status           # 连接状态
compute --data --json      # 最新数据（JSON 格式）
compute --start/--stop     # 启动/停止监控
compute --config --server 10.10.99.98 --port 58090
```

#### 5. WebUI Dashboard 增强 🎨

**数据源配置页面** (`/automation/sources`)：
- 源列表展示（状态、协议、连接信息）
- 新增/编辑源配置（表单 + JSON 高级模式）
- 变量浏览器（实时值、类型、更新时间）

**模态框 CSS 修复**：
- 修复 `.modal` 默认隐藏逻辑与 JS 的 `hidden` 类不匹配问题
- 统一使用 `hidden` 类控制显示/隐藏

### 技术优化

#### 日志级别调整
将 `ts_source_manager.c` 中的频繁操作日志从 INFO 改为 DEBUG：
- Socket.IO 事件接收/匹配
- 值更新通知
- 协议握手细节（probe/upgrade/connect）
- mapping 处理和 auto-discover 结果

**效果**：CLI 可正常交互，不被日志刷屏

#### sdkconfig.defaults 同步
新增配置项：
```kconfig
# Automation Engine
CONFIG_TS_AUTOMATION_ENABLED=y
CONFIG_TS_AUTOMATION_CONFIG_PATH="/sdcard/config/automation.json"
CONFIG_TS_AUTOMATION_MAX_SOURCES=16
CONFIG_TS_AUTOMATION_MAX_RULES=32
CONFIG_TS_AUTOMATION_MAX_VARIABLES=256

# HTTPS/mTLS 控制通道
CONFIG_TS_HTTPS_ENABLED=y
CONFIG_TS_HTTPS_REQUIRE_CLIENT_CERT=y

# WebUI
CONFIG_TS_WEBUI_ENABLE=y
CONFIG_TS_WEBUI_WS_ENABLE=y
CONFIG_TS_WEBUI_CORS_ENABLE=y

# LED Matrix
CONFIG_TS_LED_MATRIX_WIDTH=32
CONFIG_TS_LED_MATRIX_HEIGHT=32
```

### 新增文件

| 文件 | 描述 |
|------|------|
| `components/ts_automation/src/ts_variable.c` | 变量存储实现 |
| `components/ts_automation/src/ts_source_manager.c` | 数据源管理 |
| `components/ts_jsonpath/` | JSONPath 解析组件 |
| `components/ts_drivers/src/ts_compute_monitor.c` | 算力设备监控 |
| `components/ts_console/commands/ts_cmd_compute.c` | compute 命令 |

### 相关文档

- `docs/AUTOMATION_ENGINE.md` - 自动化引擎设计文档
- `sdkconfig.defaults` - 项目配置同步更新

---

## 📋 Phase 16: DRAM 碎片优化 ✅

**时间**：2026年1月24日  
**目标**：降低 DRAM 碎片率，提升最大连续块大小以满足 DMA 需求

### 优化背景

**问题**：DRAM 碎片率高达 ~60%，最大连续块仅 ~40KB，影响 DMA 和大缓冲区分配。

**目标**：碎片率降至 <50%，最大连续块 >60KB。

### 优化过程

#### 第一轮：PSRAM 基础配置
```
碎片率: 60% → 51.6%
最大块: ~40KB → 46KB
```
- `SPIRAM_MALLOC_ALWAYSINTERNAL`: 16384 → 512
- `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`: 启用
- `NVS_ALLOCATE_CACHE_IN_SPIRAM`: 启用

#### 第二轮：激进优化
```
碎片率: 51.6% → 44.5%
最大块: 46KB → 60KB
```
- `SPIRAM_MALLOC_ALWAYSINTERNAL`: 512 → 256
- `FREERTOS_TIMER_TASK_STACK_DEPTH`: 4096 → 3072
- `ESP_WIFI_CACHE_TX_BUFFER_NUM`: 32 → 16
- 任务栈优化（console/led/event）

#### 第三轮：进一步压缩
```
碎片率: 44.5% → 43.2%
最大块: 60KB → 64KB
```
- `SPIRAM_MALLOC_ALWAYSINTERNAL`: 256 → 128
- WiFi 管理帧缓冲区减少
- LWIP RECVMBOX 优化
- ⚠️ HTTPD_MAX_REQ_HDR_LEN=512 过于激进，已回退至 1024

#### 最终结果
```
碎片率: 43.2% → 42.1%
最大块: 64KB → 68KB
```

### 最终配置（sdkconfig.defaults）

```kconfig
# PSRAM 核心配置
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=128    # 关键：仅 <128B 用 DRAM
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=8192
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y

# WiFi 缓冲区优化
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=16
CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM=16
CONFIG_ESP_WIFI_MGMT_SBUF_NUM=8

# LWIP 优化
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=4096
CONFIG_LWIP_TCP_WND_DEFAULT=4096
CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=8

# 任务栈优化
CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=3072
CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE=3584
CONFIG_TS_CONSOLE_TASK_STACK_SIZE=6144
CONFIG_TS_LED_TASK_STACK_SIZE=3072

# 其他 PSRAM 迁移
CONFIG_NVS_ALLOCATE_CACHE_IN_SPIRAM=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
```

### 优化效果总结

| 指标 | 优化前 | 优化后 | 改善 |
|------|--------|--------|------|
| DRAM 碎片率 | ~60% | 42.1% | ↓18% |
| DRAM 最大块 | ~40KB | 68KB | ↑70% |
| DRAM 分配块数 | ~1000 | 842 | ↓16% |
| PSRAM 碎片率 | ~5% | 2.7% | ↓2.3% |

### 配置影响分类

**只影响性能（安全）**：
- `SPIRAM_MALLOC_ALWAYSINTERNAL` - PSRAM 访问略慢但功能正常
- WiFi 缓冲区减少 - 吞吐量可能降低
- LWIP 缓冲区减少 - TCP 传输速度降低

**可能导致功能失效（需注意）**：
- ~~`HTTPD_MAX_REQ_HDR_LEN=512`~~ - 已回退，HTTP 头太长会被拒绝
- `HTTPD_MAX_URI_LEN=256` - URI 超长会失败（一般够用）

### 关键经验

1. **`SPIRAM_MALLOC_ALWAYSINTERNAL` 是最有效的配置**
   - 默认 16KB → 128 字节
   - 将数百个中等分配从 DRAM 转移到 PSRAM
   - 性能代价很小（PSRAM 仅慢 2-3 倍）

2. **WiFi/LWIP 缓冲区优化有收益但有限**
   - 主要影响网络吞吐量
   - 对嵌入式管理场景影响不大

3. **HTTP 服务器缓冲区不宜过小**
   - `HTTPD_MAX_REQ_HDR_LEN` 最小建议 1024
   - 浏览器请求头通常 500-800 字节

### 相关文件

- `sdkconfig.defaults` - 所有配置修改
- `docs/MEMORY_STATUS.md` - 内存状态报告
- `docs/MEMORY_OPTIMIZATION.md` - 优化方案文档

---

## 📋 Phase 15: WebUI 日志流修复 & 导航栏清理 ✅

**时间**：2026年1月24日  
**目标**：修复 WebSocket 日志流根本问题，优化 WebUI 用户界面

### 问题诊断与修复

#### 1. WebSocket 日志流 Bug 修复 🐛
**问题症状**：
- WebSocket 订阅成功（`log_subscribe` → `log_subscribed`）
- 历史日志加载正常（433 条）
- 但**没有实时日志消息推送到前端**

**根本原因**：
```c
// 错误代码（components/ts_webui/src/ts_webui_ws.c:1242）
ts_log_add_callback(log_ws_callback, TS_LOG_ERROR, NULL, &s_log_callback_handle);
//                                     ^^^^^^^^^^^
// 使用 TS_LOG_ERROR 导致只接收 ERROR 级别的日志
```

**修复方案**：
```c
// 正确代码
ts_log_add_callback(log_ws_callback, TS_LOG_VERBOSE, NULL, &s_log_callback_handle);
//                                     ^^^^^^^^^^^^^
// 使用 TS_LOG_VERBOSE 接收所有级别，然后在回调中根据客户端需求过滤
```

**调试日志增强**：
- 订阅处理：`Client %d subscribed to logs (minLevel=%d)`
- 流状态管理：`update_log_stream_state: need_streaming=%d, current=%d`
- 流启用/禁用：`Log streaming enabled (receiving all levels)`
- 回调过滤：`log_ws_callback: No clients received log (level=%d)`

#### 2. WebUI 导航栏清理 🎨
**删除的导航链接**：
- ❌ `#/logs`（日志页面）- 功能已整合到终端页面模态框
- ❌ `#/ota`（OTA 页面）- 降低误操作风险

**优化后导航**（9个 → 7个链接）：
```
系统 | 网络 | 监控 | 文件 | 终端 | 配置 | 安全
```

**向后兼容**：
```javascript
// 旧书签 #/logs 自动重定向到终端页面并打开日志模态框
router.register('/logs', () => {
    window.location.hash = '#/terminal';
    setTimeout(() => showTerminalLogsModal(), 100);
});
```

#### 3. 代码清理 🧹
**删除代码**：
- 删除 `loadLogsPage()` 函数及其所有依赖（~580 行）
- 删除日志页面 HTML 模板和 CSS 样式
- 删除日志页面相关的工具函数

**保留功能**：
- 终端页面的日志模态框完整保留
- WebSocket 日志流基础设施保留
- 日志相关 API 和后端功能完整保留

### 文档更新

#### 新增/更新文档
1. **TROUBLESHOOTING.md**
   - 新增章节：WebSocket 日志流不工作 - 日志回调级别配置错误
   - 记录完整 debug 过程和解决方案
   - 提供最佳实践和代码示例

2. **WEBUI_CLEANUP_PROPOSAL.md**（新文档）
   - 日志页面删除可行性评估
   - OTA 导航链接删除方案
   - 导航栏优化建议
   - 实施优先级和兼容性考虑

3. **QUICK_START.md**
   - 新增"WebUI 页面导航"章节
   - 新增"日志查看"说明（通过终端页面）
   - 更新"固件升级 (OTA)"访问方式

### 技术收益

**用户体验**：
- ✅ 更简洁的导航栏（7 个核心功能）
- ✅ 日志查看无需页面切换（模态框）
- ✅ 实时日志流正常工作

**代码质量**：
- ✅ 减少重复代码（~580 行）
- ✅ 统一日志查看入口
- ✅ 降低维护成本

**性能优化**：
- ✅ WebSocket 连接数优化（模态框与终端共享连接）
- ✅ 日志回调正确配置，接收所有级别

### 相关文件

**修改文件**：
- `components/ts_webui/src/ts_webui_ws.c` - 日志回调级别修复
- `components/ts_webui/web/index.html` - 删除导航链接
- `components/ts_webui/web/js/app.js` - 删除日志页面代码，添加路由重定向
- `docs/TROUBLESHOOTING.md` - 新增日志流问题排查
- `docs/QUICK_START.md` - 更新 WebUI 使用说明

**新增文档**：
- `docs/WEBUI_CLEANUP_PROPOSAL.md` - WebUI 清理方案

---

## 📋 Phase 14: LED 滤镜系统优化 ✅

**时间**：2026年1月23日  
**目标**：优化 LED 后处理滤镜系统，增强用户体验

### 已完成功能

#### 1. 滤镜参数持久化 🎯
- [x] 实现 `ts_led_effect_config_t` 完整参数保存
- [x] NVS blob 存储（368 bytes per device）
- [x] 启动自动加载并应用滤镜配置
- [x] 调试日志完善（save/load 追踪）

**技术细节**：
```c
// 全局配置存储
static ts_led_effect_config_t s_current_filter_config[3];  // touch, board, matrix

// NVS 存储键
// led.tou.fpm (touch filter params)
// led.brd.fpm (board filter params)
// led.mat.fpm (matrix filter params)
```

#### 2. Scanline 滤镜增强 🌊
- [x] **离散方向 → 连续角度**：支持 0-360° 任意角度旋转
- [x] **旋转矩阵算法**：使用向量投影计算扫描线位置
- [x] **参数增强**：
  - `width` (1-16): 扫描线宽度，影响光晕范围
  - `intensity` (0-255): 中心亮度增益，使用二次衰减曲线
  - `angle` (0-360°): 旋转角度
- [x] **默认值优化**：width=3, intensity=150

**算法改进**：
```c
// 旧算法：4个离散方向（水平/垂直/对角线）
bool horizontal = (direction == HORIZONTAL);
float coord = horizontal ? x : y;

// 新算法：任意角度投影
float angle_rad = angle * M_PI / 180.0f;
float cos_a = cosf(angle_rad);
float sin_a = sinf(angle_rad);
float perp_dist = dx * cos_a + dy * sin_a;

// 增强亮度公式（二次衰减）
float boost = 1.0 + (intensity/255.0) * 3.0 * fade * fade;
```

#### 3. Wave 滤镜增强 🌀
- [x] **方向枚举 → 连续角度**：0-360° 波浪传播方向
- [x] **向量投影算法**：沿任意角度传播波浪
- [x] **振幅修正**：amplitude 范围 0-255（修复 WebUI 错误的 1-16px）
- [x] **默认值优化**：amplitude=128

**Bug 修复**：
- ❌ 旧问题：WebUI amplitude 默认值为 5，波浪效果几乎不可见
- ✅ 修复：amplitude=128，效果明显可见

#### 4. Sparkle 滤镜优化 ✨
- [x] **速度控制优化**：简化概率计算，线性映射
- [x] **参数影响增强**：
  - `speed` (0.1-100): 闪烁频率，推荐 1-10
  - `density` (0-255): 同时闪烁像素数，推荐 50-150
  - `decay` (0-255): 余晖衰减速度，推荐 100-200
- [x] **默认值优化**：speed=5, decay=150

**算法简化**：
```c
// 旧算法：复杂的平方根映射
float speed_factor = sqrtf(speed) * 0.1f;
uint32_t spawn_chance = (uint32_t)(speed_factor * density * 2.56f);

// 新算法：直接线性映射（更直观）
uint32_t spawn_chance = (uint32_t)(speed * density);
```

### 三层架构实现

| 层级 | 修改文件 | 关键变更 |
|------|---------|---------|
| **Core (ts_led_effect)** | `ts_led_effect.h`, `ts_led_effect.c` | - scanline/wave 结构体改为 `float angle`<br>- 重写旋转投影算法<br>- sparkle 简化概率计算 |
| **API (ts_api_led)** | `ts_api_led.c` | - 添加 `angle`, `wavelength`, `amplitude` 参数提取<br>- 调整默认值 |
| **CLI (ts_cmd_led)** | `ts_cmd_led.c` | - `--direction` 参数支持 0-360 角度值<br>- sparkle 速度映射调整 |
| **WebUI (app.js)** | `app.js` | - filterConfig 参数更新<br>- paramLabels 范围修正<br>- 帮助文本完善 |

### 参数对比表

| 滤镜 | 参数 | 旧范围 | 新范围 | 说明 |
|------|------|-------|-------|------|
| **scanline** | direction | 0-3 枚举 | angle: 0-360° | 连续角度旋转 |
| **scanline** | intensity | - | 0-255 | 二次衰减曲线 |
| **wave** | direction | 0-3 枚举 | angle: 0-360° | 任意方向传播 |
| **wave** | amplitude | 1-16 (px) | 0-255 | 修正单位错误 |
| **sparkle** | speed | 50 默认 | 5 默认 | 更慢的起始速度 |
| **sparkle** | decay | 230 默认 | 150 默认 | 更明显的余晖 |

### 文档更新

- [x] `docs/COMMANDS.md`：更新 filter 参数说明和示例
- [x] `docs/LED_ARCHITECTURE.md`：更新 Effect 参数详解
- [x] `docs/DEVELOPMENT_PROGRESS.md`：添加 Phase 14 记录

### 测试验证

```bash
# Scanline 角度测试
led --filter scanline --device matrix --direction 0     # 0° 水平
led --filter scanline --device matrix --direction 45    # 45° 斜向
led --filter scanline --device matrix --direction 90    # 90° 垂直

# Wave 角度测试
led --filter wave --device matrix --direction 90 --amplitude 200

# Sparkle 速度测试
led --filter sparkle --device matrix --speed 1 --density 100 --decay 100   # 极慢
led --filter sparkle --device matrix --speed 5 --density 150 --decay 150   # 推荐
```

### 技术债务

- ✅ 滤镜参数持久化（已解决）
- ✅ Scanline/Wave 方向灵活性（已解决）
- ✅ Sparkle 速度控制问题（已解决）
- ✅ Wave amplitude 参数错误（已解决）

---

## 📋 Phase 0: 规划与设计 ✅

### 已完成
- [x] 问题分析文档 (REFACTORING_ANALYSIS.md)
- [x] 原项目描述文档 (PROJECT_DESCRIPTION.md)
- [x] 架构设计文档 (docs/ARCHITECTURE_DESIGN.md)
- [x] 命令规范文档 (docs/COMMAND_SPECIFICATION.md)
- [x] 技术选型确定
- [x] 项目目录结构创建

### 关键决策记录
| 决策项 | 决策结果 |
|-------|---------|
| 平台支持 | ESP32S3 + ESP32P4 |
| 抽象策略 | 编译时确定芯片，运行时加载引脚配置 |
| 引脚配置 | NVS + 外部文件 + 默认值 |
| 命令格式 | 参数风格，基于 esp_console |
| 组件通信 | 增强的事件总线 + 发布订阅 |
| 服务管理 | 阶段化启动 + 依赖图 |
| SSH | SSH-2.0 客户端 (mbedtls) |
| 图像格式 | BMP/PNG/JPG/GIF |
| 文件系统 | FAT32(SD卡) + SPIFFS(内部) |

---

## 📋 Phase 1: 基础架构

### ts_core/ts_config - 配置管理
- [x] 基础 API 实现
- [x] NVS 后端
- [x] 文件后端
- [x] 配置变更通知
- [x] 配置验证
- [ ] 单元测试

### ts_core/ts_log - 日志系统
- [x] 日志级别管理
- [x] 多输出目标（串口、文件、内存）
- [x] 日志格式化
- [ ] 单元测试

### ts_core/ts_event - 事件系统
- [x] 事件总线实现
- [x] 发布/订阅机制
- [x] 异步事件处理
- [x] 事件优先级
- [x] 事务支持
- [ ] 单元测试

### ts_core/ts_service - 服务管理
- [x] 服务注册机制
- [x] 依赖管理器
- [x] 生命周期管理
- [x] 健康检查
- [x] 阶段化启动
- [ ] 单元测试

---

## 📋 Phase 2: 硬件抽象层 ✅

### ts_hal - 硬件抽象
- [x] 引脚管理器 (ts_pin_manager) - JSON配置加载、NVS持久化、冲突检测
- [x] GPIO 抽象 (ts_hal_gpio) - 方向/上下拉/中断配置、ISR支持
- [x] PWM 抽象 (ts_hal_pwm) - LEDC驱动、定时器自动分配、渐变支持
- [x] SPI 抽象 (ts_hal_spi) - 总线/设备分离、DMA支持
- [x] I2C 抽象 (ts_hal_i2c) - 新版i2c_master API、设备扫描
- [x] UART 抽象 (ts_hal_uart) - 事件队列、回调支持、行读取
- [x] ADC 抽象 (ts_hal_adc) - oneshot模式、校准支持
- [x] ESP32S3 平台适配 (ts_platform_s3)
- [x] ESP32P4 平台适配占位 (ts_platform_p4)
- [ ] 单元测试

### 板级配置
- [x] rm01_esp32s3 配置文件
  - board.json - 板级特性配置
  - pins.json - 引脚映射定义
  - services.json - 服务配置
- [ ] rm01_esp32p4 配置文件（预留）

---

## 📋 Phase 3: 核心服务 ✅

### ts_console - 控制台
- [x] 基于 esp_console 实现
- [x] 参数解析器 (argtable3)
- [x] 命令分类管理
- [x] 帮助系统
- [x] 内置命令 (help, version, sysinfo, tasks, free, reboot, clear, echo, log, lang)
- [x] 历史记录管理
- [x] 彩色输出支持
- [x] 多语言支持 (ts_i18n - 英文/简中/繁中/日文/韩文)
- [x] 脚本引擎 (变量/条件/命令执行, run/eval 命令)
- [ ] 单元测试

### ts_api - Core API
- [x] API 框架
- [x] 端点注册/调用机制
- [x] 权限标记
- [x] 结果格式化 (JSON)
- [x] 系统 API (system.info, system.memory, system.tasks, system.reboot, system.log.level)
- [x] 配置 API (config.get, config.set, config.delete, config.list, config.save)
- [x] 设备 API (device.info, device.set, device.fan, device.power)
- [x] LED API (led.list, led.info, led.brightness, led.clear, led.fill, led.effect)
- [x] 网络 API (net.status, net.wifi.scan, net.wifi.connect, net.info)
- [ ] 单元测试

### ts_storage - 存储管理
- [x] SPIFFS 支持 (挂载/卸载/格式化/统计)
- [x] SD 卡驱动 (SPI/SDIO 1-bit/4-bit)
- [x] FAT32 文件系统
- [x] 文件操作 API (读/写/删除/复制/重命名)
- [x] 目录操作 API (创建/删除/遍历)
- [ ] 单元测试

---

## 📋 Phase 4: LED 系统 ✅

### ts_led - LED 控制
- [x] WS2812 驱动 (RMT/led_strip)
- [x] 渲染器 (FreeRTOS 任务, 60Hz)
- [x] 图层管理器 (多图层, 混合模式, 透明度)
- [x] 动画引擎 (效果帧回调)
- [x] 特效库 (rainbow, breathing, chase, sparkle, fire, solid)
- [x] 颜色工具 (HSV/RGB转换, 调色盘, 颜色解析)
- [x] 图像解码（BMP - 24位）
- [x] 图像解码（PNG）- libpng
- [x] 图像解码（JPG）- esp_jpeg
- [x] 图像解码（GIF）- 简化解码
- [x] 设备实例（touch/board/matrix 预设）
- [x] 状态指示绑定
- [x] LED 命令 (led, led_brightness, led_clear, led_fill, led_effect, led_color)
- [x] 字体加载器 (ts_led_font) - SD卡动态加载, LRU缓存, 二分查找
- [x] 文本渲染器 (ts_led_text) - UTF-8解析, 对齐, 滚动, 覆盖层
- [x] 文本覆盖层系统 - 独立Layer渲染, 反色叠加, 循环滚动

---

## 📋 Phase 5: 设备驱动 ✅

### ts_drivers/fan - 风扇控制
- [x] 风扇驱动 (PWM, 4路)
- [x] 温度曲线 (自动模式)
- [x] 转速测量 (Tach中断)
- [x] 紧急全速模式

### ts_drivers/power - 电源监控
- [x] ADC 电压监测
- [x] 电压警报 (高低阈值)
- [x] INA226 I2C芯片支持
- [x] INA3221 I2C芯片支持 (3通道)
- [x] UART 电源芯片 (PZEM-004T Modbus RTU 协议)

### ts_drivers/device - 设备控制
- [x] AGX 电源控制 (开/关/复位/强制关机)
- [x] 电源状态检测 (power_good)
- [x] 关机请求处理 (中断)
- [x] 启动次数统计

### ts_drivers/usb_mux - USB MUX
- [x] MUX 切换控制 (Host/Device)
- [x] OE 使能控制
- [x] 状态查询

---

## 📋 Phase 6: 网络与安全 ✅

### ts_net - 网络
- [x] 网络子系统框架 (esp_netif)
- [x] 以太网驱动（W5500 SPI）
- [x] WiFi 管理器 (STA/AP/APSTA模式)
- [x] WiFi 扫描
- [x] DHCP 服务器 (lwIP集成)
- [x] HTTP 服务器 (esp_http_server)
- [x] 路由注册/处理
- [x] 静态文件服务
- [x] WebSocket (ts_webui_ws)
- [x] HTTPS/mTLS (ts_https_server - TLS 1.2/1.3, 自签名证书, mTLS客户端验证)

### ts_security - 安全
- [x] 随机数生成 (esp_random)
- [x] 密钥生成/存储/加载 (NVS)
- [x] 证书存储
- [x] 会话管理 (8路并发)
- [x] 权限检查 (5级权限)
- [x] Token生成/验证
- [x] 密码验证
- [x] SHA256/SHA384/SHA512 哈希
- [x] HMAC
- [x] AES-GCM 加解密
- [x] Base64/Hex 编解码
- [x] RSA/EC 密钥对 (RSA-2048/4096, EC-P256/P384, PEM导入导出, 签名验证)
- [x] SSH 客户端 (ts_ssh_client - SSH-2.0协议, 密码/公钥认证, 远程命令执行)
- [x] SSH Known Hosts (ts_ssh_known_hosts - 主机密钥验证, SHA256指纹, TOFU策略, NVS存储)
- [x] SSH 端口转发 (ts_port_forward - 本地转发, 多并发连接, libssh2隧道)
- [x] SSH 交互式 Shell (ts_ssh_shell - PTY分配, 多终端类型, 双向I/O, 信号处理)
- [x] SSH 密钥生成 (CLI --keygen - RSA/ECDSA生成, PEM私钥导出, OpenSSH公钥格式)
- [x] SSH 公钥部署 (CLI --copyid - 自动部署公钥到远程服务器, 权限设置, 验证)
- [x] SSH 安全密钥存储 (ts_keystore - NVS加密存储私钥, 支持导入/生成/删除/列表)
- [x] SSH 使用安全存储密钥 (--keyid 选项从安全存储加载密钥进行认证)
- [x] 安全加固 L1 (私钥内存清零, 禁止私钥导出, exportable标记, NVS 48KB扩容)
- [x] SSH 主机密钥验证 (TOFU策略, 指纹变化警告, NVS存储)
- [x] SSH 公钥撤销 (ssh --revoke 从远程删除已部署公钥)
- [x] Known Hosts 管理 (hosts命令 - list/info/remove/clear)
- [x] SFTP 文件传输 (ts_sftp - ls/get/put/rm/mkdir/stat)
- [ ] 安全加固 L2 (NVS 加密) - 配置已就绪，待功能开发完成后测试
- [ ] 安全加固 L3/L4 (Secure Boot, Flash 加密) - 生产阶段

---

## 📋 Phase 7: WebUI ✅

### ts_webui - Web 界面
- [x] HTTP 服务器集成
- [x] REST API 端点 (通用API网关)
- [x] 登录/登出 API
- [x] Token 认证
- [x] WebSocket 支持 (广播/事件)
- [x] 静态文件服务
- [x] SPA 路由支持
- [x] CORS 配置
- [x] 前端界面
  - index.html 仪表盘
  - CSS 样式框架
  - API 客户端库
  - WebSocket 客户端
  - 登录模态框
- [x] WebUI 服务集成 (ts_services.c)
- [x] SPA 路由系统 (router.js)
- [x] Web 终端 (terminal.js + xterm.js)
- [x] SSH Shell WebSocket 支持 (ts_webui_ws.c)
- [x] 电压保护事件 WebSocket 广播
- [x] 完整的 7 个页面：
  - **系统**（首页，合并原仪表盘+系统：系统信息/时间/内存/网络/电源/风扇控制/服务列表/重启）
  - LED 控制（设备/亮度/颜色/特效/图像）
  - 网络配置（以太网/WiFi/DHCP/NAT）
  - **设备控制**（AGX/LPMU 电源控制）
  - 终端（Web CLI + SSH Shell）
  - 安全（SSH 测试、密钥管理、已知主机）
  - 配置（配置列表/编辑/删除）
  - 文件管理（SD卡/SPIFFS 浏览、上传/下载、新建/删除/重命名、**挂载/卸载 SD 卡**）

---

## 📋 Phase 8: 文档与测试 ✅

- [x] 快速入门指南 (docs/QUICK_START.md)
- [x] API 参考文档 (docs/API_REFERENCE.md)
- [x] 板级配置指南 (docs/BOARD_CONFIGURATION.md)
- [x] 测试计划文档 (docs/TEST_PLAN.md)
- [ ] 集成测试用例 - 后续版本
- [ ] 性能测试 - 后续版本
- [ ] 稳定性测试 - 后续版本

---

## 📋 Phase 9: 统一配置系统 ✅

### ts_config_module - 模块化配置管理
- [x] 模块枚举定义 (NET/DHCP/WIFI/LED/FAN/DEVICE/SYSTEM)
- [x] Schema 定义结构 (类型、默认值、描述)
- [x] 模块注册机制 (ts_config_module_register)
- [x] SD卡优先加载逻辑 (ts_config_module_load)
- [x] NVS JSON blob 存储 (解决 15 字符键名限制)
- [x] 双写同步 (NVS + SD卡)
- [x] pending_sync 热插拔处理
- [x] 配置导入/导出 (ts_config_module_export_to_sdcard)
- [x] 配置重置 (ts_config_module_reset)

### ts_config_meta - 元配置管理
- [x] global_seq 全局序列号
- [x] sync_seq 同步序列号
- [x] pending_sync 位掩码
- [x] Schema 版本存储

### ts_config_schemas - Schema 定义
- [x] NET 模块 Schema (7 项: eth.enabled, eth.dhcp, eth.ip...)
- [x] DHCP 模块 Schema (6 项: enabled, start_ip, end_ip...)
- [x] WIFI 模块 Schema (9 项: mode, ap.ssid, sta.ssid...)
- [x] LED 模块 Schema (8 项: brightness, effect_speed, matrix.rotation...)
- [x] FAN 模块 Schema (11 项: mode, min_duty, curve.t1...)
- [x] DEVICE 模块 Schema (7 项: agx.auto_power_on, monitor.interval...)
- [x] SYSTEM 模块 Schema (8 项: timezone, log_level, console.baudrate...)

### CLI 命令增强
- [x] `config --allsave` - 保存所有模块
- [x] `net --save` - 保存网络配置
- [x] `dhcp --save` - 保存 DHCP 配置
- [x] `wifi --save` - 保存 WiFi 配置
- [x] `led --save` - 保存 LED 配置
- [x] `fan --save` - 保存风扇配置
- [x] `device --save` - 保存设备配置
- [x] `system --save` - 保存系统配置

### 文档
- [x] 统一配置系统设计文档 (CONFIG_SYSTEM_DESIGN.md)
- [x] GPIO 引脚映射文档 (GPIO_MAPPING.md)

---

## 📋 Phase 10: WebUI 增强 & SSH Shell ✅

### WebUI SPA 重构
- [x] SPA 路由器实现 (router.js)
- [x] Hash 路由 (#/path) 支持
- [x] 页面懒加载和状态管理
- [x] WebSocket 连接状态指示器

### Web 终端 (xterm.js)
- [x] xterm.js 终端集成
- [x] 本地 CLI 命令执行
- [x] 命令历史和光标编辑
- [x] ANSI 颜色支持
- [x] 心跳保活机制（15秒间隔）

### SSH Shell WebSocket
- [x] WebSocket SSH 消息协议 (ssh_connect/ssh_input/ssh_output/ssh_status)
- [x] libssh2 SSH 会话管理
- [x] PTY 终端分配
- [x] 双向数据流转发
- [x] ssh_poll 任务资源清理
- [x] 远程关闭检测和清理

### Core API 扩展
- [x] ts_api_wifi.c - WiFi 状态/扫描/连接/断开
- [x] ts_api_dhcp.c - DHCP 状态/客户端/启动/停止
- [x] ts_api_nat.c - NAT 状态/启用/禁用/保存
- [x] ts_api_ssh.c - SSH 执行/测试/密钥生成
- [x] ts_api_sftp.c - SFTP ls/get/put/rm/mkdir/stat
- [x] ts_api_key.c - 密钥列表/信息/生成/删除
- [x] ts_api_hosts.c - Known Hosts 管理
- [x] ts_api_agx.c - AGX 监控状态/数据/配置/启停

### 电源监控服务
- [x] Power 服务注册 (ts_services.c)
- [x] 电源监控初始化和启动
- [x] 电压保护策略启动
- [x] WebSocket 电压保护事件广播

### 文件管理系统
- [x] Storage API 扩展 (ts_api_storage.c)
  - storage.status - 存储状态查询
  - storage.list - 目录列表
  - storage.delete - 文件/目录删除
  - storage.mkdir - 创建目录
  - storage.rename - 重命名
  - storage.info - 文件信息
- [x] 文件传输端点 (ts_webui_api.c)
  - GET /api/v1/file/download?path=xxx - 文件下载
  - POST /api/v1/file/upload?path=xxx - 文件上传
  - URL 解码支持 (%XX 编码路径)
  - 安全检查 (上传仅限 /sdcard)
- [x] WebUI 文件管理页面
  - 分区切换 (SD卡 / SPIFFS)
  - 目录导航 (面包屑路径)
  - 文件列表 (名称/大小/类型)
  - 文件上传 (多文件支持)
  - 文件下载
  - 新建文件夹
  - 重命名
  - 删除

### 配置优化
- [x] CONFIG_LWIP_MAX_SOCKETS=16（解决 socket 用尽问题）
- [x] ssh_poll 任务栈 8192 字节（解决栈溢出问题）

---

## � Phase 11: OTA 固件升级 ✅

### ts_ota - OTA 升级组件
- [x] OTA 主框架 (ts_ota.c)
  - 状态管理 (IDLE/CHECKING/DOWNLOADING/VERIFYING/WRITING/PENDING_REBOOT/ERROR)
  - 进度跟踪 (百分比/已接收/总大小/状态消息)
  - 互斥保护 (线程安全)
  - 事件发布 (TS_EVENT_BASE_OTA)
- [x] HTTPS OTA (ts_ota_https.c)
  - esp_https_ota 集成
  - 证书验证 (CA bundle / 自定义证书 / 跳过验证)
  - 进度回调
  - 版本比较 (禁止降级选项)
- [x] SD 卡 OTA (ts_ota_sdcard.c)
  - 本地固件文件加载
  - ESP 镜像头验证
  - 分块写入 (4KB buffer)
  - 固件列表扫描
- [x] 回滚机制 (ts_ota_rollback.c)
  - 固件验证计时器 (默认60秒)
  - ts_ota_mark_valid() 确认固件
  - 自动回滚 (超时未确认)
  - 分区信息查询
  - NVS 升级记录 (时间戳/计数)
- [x] 版本管理 (ts_ota_version.c)
  - 语义化版本解析 (x.y.z[-prerelease])
  - 版本比较 (major/minor/patch/prerelease)
  - 运行固件版本查询
  - 版本格式化输出

### OTA Core API (ts_api_ota.c)
- [x] `ota.status` - 获取 OTA 状态 (运行分区/下一分区/待验证)
- [x] `ota.progress` - 获取升级进度 (百分比/已接收/总大小)
- [x] `ota.version` - 获取固件版本信息
- [x] `ota.start_url` - 从 URL 启动 OTA (auto_reboot/allow_downgrade/skip_verify)
- [x] `ota.start_file` - 从 SD 卡启动 OTA
- [x] `ota.abort` - 中止升级
- [x] `ota.validate` - 标记固件有效 (取消回滚)
- [x] `ota.rollback` - 回滚到上一版本
- [x] `ota.upload_begin` - 开始固件上传 (WebUI)
- [x] `ota.upload_end` - 结束固件上传
- [x] `ota.upload_abort` - 中止固件上传

### OTA CLI 命令 (ts_cmd_ota.c)
- [x] `ota --status` - 显示 OTA 状态
- [x] `ota --progress` - 显示升级进度 (含进度条)
- [x] `ota --version` - 显示固件版本
- [x] `ota --partitions` - 显示分区信息
- [x] `ota --url <url>` - 从 HTTPS URL 升级
- [x] `ota --file <path>` - 从 SD 卡文件升级
- [x] `ota --validate` - 标记固件有效
- [x] `ota --rollback` - 回滚到上一版本
- [x] `ota --abort` - 中止升级
- [x] `--no-reboot` 选项 - 升级后不自动重启
- [x] `--allow-downgrade` 选项 - 允许降级
- [x] `--skip-verify` 选项 - 跳过证书验证
- [x] `--json` 选项 - JSON 格式输出

### 分区表 (partitions_ota.csv)
- [x] 双 OTA 分区 (ota_0 + ota_1, 各 3MB)
- [x] OTA 数据分区 (otadata, 8KB)
- [x] www 分区扩展至 2MB (WebUI + 静态资源)
- [x] 16MB Flash 布局优化

### Kconfig 配置
- [x] CONFIG_TS_OTA_ENABLED - OTA 功能开关
- [x] CONFIG_TS_OTA_ROLLBACK_TIMEOUT - 回滚超时 (10-300秒)
- [x] CONFIG_TS_OTA_HTTPS_ENABLED - HTTPS OTA 开关
- [x] CONFIG_TS_OTA_SDCARD_ENABLED - SD 卡 OTA 开关
- [x] CONFIG_TS_OTA_WEBUI_UPLOAD_ENABLED - WebUI 上传开关
- [x] CONFIG_TS_OTA_BUFFER_SIZE - 缓冲区大小 (1-16KB)
- [x] CONFIG_TS_OTA_VERIFY_SIGNATURE - 固件签名验证 (可选)
- [x] CONFIG_TS_OTA_ANTI_ROLLBACK - 防回滚保护 (可选)
- [x] CONFIG_TS_OTA_VERSION_CHECK - 版本检查开关
- [x] CONFIG_TS_OTA_DEFAULT_URL - 默认 OTA 服务器 URL
- [x] CONFIG_TS_OTA_TASK_STACK_SIZE - 任务栈大小 (4-16KB)
- [x] CONFIG_TS_OTA_TASK_PRIORITY - 任务优先级 (1-24)

---
## 📋 Phase 12: OTA 增强 & Bug修复 ✅

### OTA 回滚机制修复
- [x] 修复 `ts_ota_is_pending_verify()` 返回值错误
  - 问题：新固件首次启动时误判为「无需验证」
  - 原因：`s_rollback_timer` 在计时器创建前为 NULL
  - 解决：检查 OTA 状态和分区状态而非计时器句柄
- [x] 修复 `ts_ota_mark_valid()` 直接调用 SDK 而非组件层
  - 修改为调用 `ts_ota_rollback_cancel()`

### WebUI OTA 回滚功能
- [x] 首页新固件验证横幅
  - 倒计时显示（60秒）
  - 确认固件 / 立即回滚按钮
  - 自动刷新倒计时
- [x] OTA 页面手动验证/回滚控制
  - 显示验证状态和剩余时间
  - 确认固件和回滚按钮
- [x] 修复 OTA 进度显示
  - 修复进度条不更新问题
  - 修复上传方式无进度显示
  - 添加 keepalive 机制防止 HTTP 超时

### 手动升级 www 分区支持
- [x] WebUI 两种手动升级方式添加 "包含 WebUI" 复选框
  - URL 升级：勾选后依次升级 app + www 分区
  - SD 卡升级：勾选后依次升级 app + www 分区
- [x] 新增 `ota.www.start_sdcard` API
  - 从 SD 卡文件升级 www (SPIFFS) 分区
  - 路径推断：`/sdcard/firmware.bin` → `/sdcard/www.bin`
- [x] `ts_ota_www.c` 新增 SD 卡 www OTA 实现
  - `ts_ota_www_start_sdcard()` 启动函数
  - `www_ota_sdcard_task()` 异步任务
  - 分区擦除、分块写入、进度回调

### Core API 扩展
- [x] `ota.www.start_sdcard` - 从 SD 卡升级 www 分区
- [x] `ota.https.start` - 支持 `www_url` 参数

---

## 📋 Phase 13: 日志系统增强 ✅

### ts_log - 日志系统核心增强
- [x] ESP_LOG 捕获钩子
  - 安装 `vprintf` 钩子拦截所有 ESP_LOG 输出
  - 解析 ESP_LOG 格式（时间戳、级别、TAG、消息）
  - 去除 ANSI 颜色代码，提取纯文本
  - 防递归保护（避免日志风暴）
- [x] 日志缓冲区扩展
  - 默认容量从 100 增加到 1000 条
  - Kconfig 可配置范围 50-5000
  - 每条约 300 字节，1000 条约 300KB
- [x] 日志统计 API
  - `ts_log_get_stats()` - 获取缓冲区容量、数量、丢弃数
  - `ts_log_buffer_search()` - 支持级别/TAG/关键字过滤搜索
  - `ts_log_enable_esp_capture()` - 动态启用/禁用捕获
- [x] Kconfig 配置项
  - `CONFIG_TS_LOG_BUFFER_SIZE` - 缓冲区大小（默认 1000）
  - `CONFIG_TS_LOG_CAPTURE_ESP_LOG` - ESP_LOG 捕获开关

### Log Core API (ts_api_log.c)
- [x] `log.list` - 获取日志列表
  - 支持分页（offset/limit）
  - 支持级别过滤（minLevel/maxLevel）
  - 支持 TAG 子字符串匹配
  - 支持关键字全文搜索
- [x] `log.stats` - 获取日志统计信息
  - 缓冲区容量/数量
  - 总捕获数/丢弃数
  - 当前日志级别
- [x] `log.clear` - 清空日志缓冲区
- [x] `log.setLevel` - 设置日志级别（全局或按 TAG）
- [x] `log.capture` - 控制 ESP_LOG 捕获开关

### WebSocket 日志流 (ts_webui_ws.c)
- [x] 新增 `WS_CLIENT_TYPE_LOG` 客户端类型
- [x] WebSocket 消息协议
  - `log_subscribe` - 订阅日志流（支持 minLevel 过滤）
  - `log_unsubscribe` - 取消订阅
  - `log_set_level` - 更新级别过滤
  - `log_get_history` - 获取历史日志（最多 500 条）
- [x] 实时日志推送
  - 日志回调注册到 `ts_log`
  - 按客户端级别过滤后推送
  - JSON 格式：timestamp/level/levelName/tag/message/task
- [x] 智能流控制
  - 无订阅客户端时自动禁用回调
  - 客户端断开时自动更新状态

### WebUI 日志页面 (app.js)
- [x] 全新日志查看界面
  - 深色终端风格（#1a1a2e 背景）
  - 等宽字体（SF Mono/Monaco/Consolas）
  - 日志级别颜色高亮（ERROR 红/WARN 橙/INFO 绿/DEBUG 蓝）
- [x] 工具栏功能
  - 级别过滤下拉框（全部/ERROR/WARN+/INFO+/DEBUG+）
  - TAG 过滤输入框
  - 关键字搜索（支持高亮）
  - WebSocket 状态指示器
  - 自动滚动开关
  - 加载历史/清空按钮
- [x] 实时日志流
  - WebSocket 订阅后自动推送
  - 前端最大保留 1000 条
  - 防抖动过滤渲染
- [x] 响应式布局
  - 移动端自适应
  - 工具栏换行布局

### WebUI 集成
- [x] 导航栏添加「日志」入口
- [x] SPA 路由注册 `/logs`
- [x] TianShanWS 添加 `readyState` getter

---
## 📝 开发日志

### 2026-01-23
- **日志系统增强**：
  - 实现 ESP_LOG 捕获钩子，自动解析 ESP-IDF 所有日志
  - 日志缓冲区扩展到 1000 条，支持 WebUI 长时间查看
  - 新增 WebSocket 日志流协议，支持实时推送和历史查询
  - WebUI 新增日志页面，支持实时查看、过滤、搜索、清空
  - Log Core API 5 个端点：list/stats/clear/setLevel/capture

- **OTA 回滚机制修复**：
  - 修复 `ts_ota_is_pending_verify()` 误判问题
    - 问题：新固件首次启动时，函数返回 false（无需验证）
    - 原因：依赖 `s_rollback_timer` 句柄判断，但计时器在函数调用后才创建
    - 修复：改用 OTA 状态 + 分区状态判断（`esp_ota_check_rollback_is_possible()`）
  - 修复 `ts_ota_mark_valid()` 调用链
    - 问题：直接调用 SDK `esp_ota_mark_app_valid_cancel_rollback()` 绕过组件层
    - 修复：改为调用 `ts_ota_rollback_cancel()` 统一管理

- **WebUI OTA 回滚功能**：
  - 首页新固件验证横幅
    - 醒目的黄色背景提示
    - 实时倒计时显示（剩余 XX 秒）
    - 「确认固件」和「立即回滚」按钮
  - OTA 页面验证控制
    - 当前验证状态显示
    - 剩余时间倒计时
    - 手动确认/回滚操作
  - 修复 OTA 进度显示问题
    - 问题：进度条卡在 0% 不更新
    - 原因：定时器 ID 未保存导致无法清除
    - 修复：正确管理 `pollInterval` 变量
  - 修复上传方式无进度显示
    - 添加 keepalive 定时器，每 5 秒调用 ota.progress

- **手动升级 www 分区支持**：
  - WebUI 手动升级增强
    - URL 升级方式添加「包含 WebUI」复选框（默认勾选）
    - SD 卡升级方式添加「包含 WebUI」复选框（默认勾选）
    - 勾选后执行两步 OTA：先 app 分区，后 www 分区
  - 新增 `ota.www.start_sdcard` API
    - 从 SD 卡文件升级 www (SPIFFS) 分区
    - 文件路径推断规则：`/sdcard/firmware.bin` → `/sdcard/www.bin`
  - 实现 `ts_ota_www_start_sdcard()` 函数
    - 文件存在性检查
    - 文件大小获取
    - 异步任务启动
  - 实现 `www_ota_sdcard_task()` 异步任务
    - 查找 www 分区（SPIFFS 类型）
    - 分区擦除
    - 分块读取文件并写入分区（4KB buffer）
    - 进度回调
    - 错误处理和状态更新

### 2026-01-22
- **统一配置系统修复**：
  - 修复 `MODULE_NAMES` 数组缺少 `NAT` 条目导致崩溃
    - 问题：`strlen(NULL)` 在 `ts_config_module_register()` 触发 LoadProhibited
    - 修复：在 `ts_config_module.c` 中添加 `[TS_CONFIG_MODULE_NAT] = "NAT"`
  - 为 NAT 模块添加统一配置支持
    - `ts_config_module.h` 添加 `TS_CONFIG_MODULE_NAT` 枚举
    - `ts_config_schemas.c` 添加 NAT schema 定义 (enabled, auto_start)
    - `ts_cmd_nat.c` 更新 `--save` 使用双写机制
    - `ts_api_config.c` 添加 NAT 模块解析
  - 修复 API 槽位不足问题
    - `CONFIG_TS_API_MAX_ENDPOINTS` 从 128 增加到 200

- **WebUI 网络配置页面重构**：
  - 新增状态概览区（顶部三接口卡片：以太网/WiFi STA/WiFi AP）
  - 双栏布局设计
    - 左栏：接口配置（以太网/WiFi 标签页切换）
    - 右栏：网络服务（主机名/DHCP/NAT）
  - WiFi 扫描结果改为卡片式布局，按信号强度排序
  - 设备列表（AP 接入设备/DHCP 客户端）改为网格卡片
  - 视觉优化
    - 状态点（绿/红/灰）替代文字状态
    - 服务徽章显示运行状态
    - 信号强度条显示 (████)
    - 响应式布局支持移动端
  - 新增 CSS 样式：500+ 行网络页面专用样式

- **WebUI LED 控制页面重构**：
  - **设备卡片全新布局**：
    - 卡片式设计替代旧版紧凑面板
    - 设备状态实时显示（已关闭/常亮/▶特效名称）
    - 边框颜色指示开关状态（绿色=开启）
  - **控制区域优化**：
    - 亮度滑块独立行，清晰可操作
    - 颜色选择器 + 8个快捷预设色圆点
    - 快捷特效按钮（前4个）+ "更多"按钮
    - 停止按钮独立显示
  - **底部操作栏**：
    - 电源按钮（开启/关闭状态切换）
    - Matrix 专属功能按钮（📷图像/📝文本/🎨滤镜）
    - 保存按钮固定右侧
  - **状态同步**：
    - 新增 `updateLedCardState()` 统一状态更新
    - 快捷特效实时高亮当前运行效果
    - 全部关闭功能 `allLedsOff()`
  - **响应式布局**：
    - 移动端单列显示
    - 按钮和间距自适应缩放
  - 新增 CSS 样式：300+ 行 LED 页面专用样式

### 2026-01-21
- **WebUI 文件管理功能**：
  - 实现 Storage API 扩展
    - storage.delete - 删除文件/目录
    - storage.mkdir - 创建目录
    - storage.rename - 重命名文件/目录
    - storage.info - 获取文件详细信息
  - 实现文件传输端点
    - GET /api/v1/file/download?path=xxx - 文件下载
    - POST /api/v1/file/upload?path=xxx - 文件上传
    - 添加 url_decode() 函数处理 URL 编码路径
    - 安全检查：上传仅限 /sdcard 目录
  - 实现 WebUI 文件管理页面
    - 分区切换（SD卡 / SPIFFS）
    - 目录导航（面包屑路径）
    - 文件列表（名称/大小/类型/图标）
    - 文件上传（多文件支持）
    - 文件下载
    - 新建文件夹
    - 重命名
    - 删除
  - 调试修复：
    - 修复 storageList API 使用 GET 无法发送 body 问题（改为 POST）
    - 修复 applyColor 未定义导致 JS 执行中断
    - 修复 URL 编码路径导致 403 Forbidden（添加 url_decode）

- **SSH Shell WebSocket 修复**：
  - 修复 "Too many open files in system" 错误
    - 原因：CONFIG_LWIP_MAX_SOCKETS=10 太小
    - 解决：增加到 16 (sdkconfig + sdkconfig.defaults)
  - 修复 ssh_poll 任务栈溢出
    - 原因：4096 字节栈对 libssh2 不足
    - 解决：增加到 8192 字节
  - 修复 ssh_poll_task 资源泄漏
    - 原因：远程关闭时未清理 shell/session
    - 解决：添加 need_cleanup 标记和完整清理逻辑

- **LED WebUI 增强**：
  - LED 页面完整实现
    - 设备列表（touch/board/matrix）
    - 亮度滑块控制
    - 颜色选择器（预设颜色 + 自定义）
    - 特效选择和速度调节
    - 图像上传和显示
  - 配置保存功能（led --save）
  - 状态实时展示

- **LED WebUI Matrix 功能增强**：
  - 实现 Matrix 专属功能区域（图像/QR码/文本/滤镜）
  - **文件选择器组件**
    - 通用 `openFilePickerFor(inputId, startPath)` 函数
    - 支持目录导航、文件预览、选择确认
    - 图像文件类型过滤 (.png/.jpg/.gif/.bmp)
  - **文本滚动效果修复**
    - 从 checkbox 改为下拉选择器
    - 支持方向选择：无/向左/向右/向上/向下
  - **字体选择器优化**
    - 移除无效的"默认字体"选项
    - 只显示 /sdcard/fonts 实际存在的字体文件
  - **QR Code 背景图支持**
    - Core API (`led.qrcode`) 添加 `bg_image` 参数
    - WebUI 添加背景图选择器（复用文件选择器组件）
    - NVS 保存/恢复支持（新增 `{device}.qrbg` 键）
    - 修复重启后背景图丢失问题
  - **后处理滤镜完整实现**
    - 滤镜分类展示：动态/渐变/静态效果
    - 速度参数支持（部分滤镜）
    - API: `led.filter.start`, `led.filter.stop`

- **性能优化**：
  - CPU 频率从 160 MHz 改为 240 MHz
  - 修改 sdkconfig.defaults 和 sdkconfig

- **HTTP Server 增强**：
  - 修复大 body 接收不完整问题
  - 添加循环接收直到获取完整 content_len
  - HTTP/HTTPS handler 统一修复

- **WebUI API 增强**：
  - 添加 query string 解析（`?path=xxx` 参数）
  - 改进错误处理和状态码返回
  - 添加 API 请求日志

### 2026-01-20
- **WebUI SPA 重构（Phase 10）**：
  - 实现 SPA 路由系统 (router.js)
    - Hash 路由 (#/path) 支持
    - 页面懒加载
    - 导航高亮同步
  - 实现 Web 终端 (terminal.js + xterm.js)
    - 本地 CLI 命令执行
    - SSH Shell 支持（通过 WebSocket）
    - 命令历史、光标编辑、ANSI 颜色
    - 心跳保活（15秒间隔）
    - Ctrl+\ 退出 SSH Shell
  - 实现 7 个完整页面
    - 仪表盘：系统/内存/网络/电源/设备/温度卡片
    - 系统管理：服务列表、重启操作
    - LED 控制：设备列表、亮度滑块、颜色选择、特效
    - 网络配置：以太网/WiFi/DHCP/NAT 状态和控制
    - 设备控制：AGX/LPMU 电源、风扇调速
    - 终端：Web CLI + SSH Shell
    - 安全：SSH 连接测试、密钥管理、已知主机
    - 配置：配置列表/筛选/编辑/删除
  - 扩展 api.js 支持所有 Core API 端点
  - 添加 WebSocket 连接状态指示器
  
- **Core API 扩展**：
  - ts_api_wifi.c - WiFi 管理 API
  - ts_api_dhcp.c - DHCP 服务器 API
  - ts_api_nat.c - NAT 网关 API
  - ts_api_ssh.c - SSH 执行/测试 API
  - ts_api_sftp.c - SFTP 文件传输 API
  - ts_api_key.c - 密钥管理 API
  - ts_api_hosts.c - Known Hosts API
  - ts_api_agx.c - AGX 监控 API

- **SSH Shell WebSocket 实现**：
  - WebSocket 消息协议：ssh_connect, ssh_input, ssh_output, ssh_status
  - libssh2 SSH 会话和 Shell 管理
  - PTY 终端分配
  - 双向数据流转发（poll 任务）
  - 电压保护事件 WebSocket 广播

- **服务系统扩展**：
  - 注册 Power 服务（电源监控 + 电压保护）
  - 注册 WebUI 服务（HTTP + WebSocket + 静态文件）
  - www SPIFFS 分区挂载

### 2026-01-19
- **统一配置系统 (Phase 9) 完成**：
  - 创建设计文档 (docs/CONFIG_SYSTEM_DESIGN.md)
    - SD卡优先 + NVS 备份双写同步机制
    - 无 RTC 设计（序列号替代时间戳）
    - pending_sync 热插拔处理
    - Schema 版本迁移框架
  - 实现 ts_config_module (ts_config_module.h/c)
    - 7 个配置模块: NET, DHCP, WIFI, LED, FAN, DEVICE, SYSTEM
    - 模块注册/加载/持久化/重置 API
    - SD卡/NVS 自动优先级选择
    - JSON blob 存储（解决 NVS 15字符键名限制）
  - 实现 ts_config_meta (ts_config_meta.h/c)
    - global_seq / sync_seq 序列号管理
    - pending_sync 位掩码管理
    - Schema 版本存储
  - 实现 ts_config_schemas (ts_config_schemas.c)
    - 7 个模块的 Schema 定义（50+ 配置项）
    - 自动注册和加载
  - CLI 命令增强：
    - `config --allsave` - 一键保存所有模块配置
    - 7 个模块 `--save` 命令: net, dhcp, wifi, led, fan, device, system
  - 修复 NVS 键名超长错误（JSON blob 存储方案）
  - 创建 GPIO 引脚映射文档 (docs/GPIO_MAPPING.md)
  - 更新架构设计文档引脚定义（与 robOS/PCB 一致）
  - 修正 SD 卡引脚、设备控制引脚、电源监控引脚

### 2026-01-18
- **SSH 高级功能**：
  - 实现端口转发模块 (ts_port_forward)
    - 本地端口转发：`-L localport:remotehost:remoteport`
    - 使用 `libssh2_channel_direct_tcpip()` 建立隧道
    - 支持多并发连接（最多 5 个）
    - 后台任务异步处理数据转发
  - 实现交互式 Shell 模块 (ts_ssh_shell)
    - PTY 分配：`libssh2_channel_request_pty_ex()`
    - 支持 xterm/vt100/vt220/ansi/dumb 终端类型
    - 双向 I/O 回调机制
    - 窗口大小调整（SIGWINCH）支持
    - 信号发送（Ctrl+C 等）
  - CLI 命令扩展：
    - `ssh --shell` - 交互式 Shell
    - `ssh --forward` - 端口转发配置
  - 调试修复：交互式 Shell 字符回显延迟问题
    - 问题：输入字符不立即显示，按回车后才可见
    - 原因：UART 读取 50ms 超时阻塞主循环
    - 修复：非阻塞 UART 读取 + fwrite/fflush 立即输出

### 2026-01-17
- **SSH 安全功能完善**：
  - 实现 Known Hosts 验证 (ts_ssh_known_hosts)
    - 主机密钥指纹验证
    - SHA256 指纹格式（OpenSSH 兼容）
    - 首次连接信任策略（TOFU）
    - NVS 持久化存储
  - CLI 命令：`ssh --known-hosts list/remove/clear`

### 2026-01-16
- **LED 文本显示系统**：
  - 实现 ttf2fnt.py 字体转换工具（TTF → .fnt 嵌入式格式）
  - 实现 ts_led_font 模块（字库动态加载, LRU缓存, 二分查找索引）
  - 实现 ts_led_text 模块（UTF-8解析, 对齐, 滚动, 覆盖层渲染）
  - 支持 BoutiqueBitmap9x9 像素字体（ASCII + GB2312）
  - 实现文本覆盖层系统（Layer 1独立渲染, 与动画/图像叠加）
  - 实现反色覆盖模式（--invert, 自动检测亮色背景）
  - 实现循环滚动（--loop, 文本滚出后重新进入）
  - 生成测试字库：boutique9x9.fnt (ASCII), cjk.fnt (GB2312)
  - CLI命令：--draw-text, --stop-text, --font, --scroll, --align, --invert, --loop
  - 更新 COMMANDS.md 文档

### 2026-01-18

- **安全加固（L1 软件层）**：
  - 创建生产安全实现进度文档 (docs/SECURITY_IMPLEMENTATION.md)
    - 定义 L1-L4 四级安全层次（开发→预生产→生产→高安全）
    - 攻击向量与防御策略分析
    - 实现清单与状态跟踪
  - **私钥保护**：
    - 移除 `ts_keystore_export_to_file()` 私钥导出能力
    - 新增 `ts_keystore_export_public_key_to_file()` 仅导出公钥
    - 旧 API 标记为 deprecated，调用时自动转为公钥导出
  - **内存安全**：
    - 新增 `secure_free_key()` 函数，使用 volatile 指针防止编译器优化
    - `ts_keystore_import_from_file()` 导入后立即清零内存
    - `ts_keystore_generate_key()` 生成后立即清零临时缓冲区
  - **分区表扩容**：
    - NVS 分区：24KB → 48KB（支持 8 个 RSA-4096 密钥）
    - 新增 nvs_keys 分区（4KB，NVS 加密支持）
    - factory 分区偏移调整：0x10000 → 0x20000（64KB 对齐）
  - **配置文件更新**：
    - sdkconfig.defaults 添加安全配置注释（开发/生产区分）
    - ts_keystore.h 更新头文件文档和安全原则说明

- **SSH 内存密钥认证修复**：
  - 修复 libssh2 mbedTLS 后端 `gen_publickey_from_rsa()` 中的 mpint padding bug
  - 问题：检查 N 的 MSB 时使用 1 字节 buffer 调用 `mbedtls_mpi_write_binary()` 导致未定义行为
  - 解决：使用 `mbedtls_mpi_bitlen()` 正确判断是否需要 padding
  - 文件：`components/ch405labs_esp_libssh2/libssh2/src/mbedtls.c`
- **ts_keystore 安全密钥存储完善**：
  - 实现 `--keyid` 参数支持从 NVS 加密分区加载私钥
  - 私钥永不写入临时文件，直接在内存中使用
  - 完整工作流：`key --generate` → `ssh --copyid --keyid` → `ssh --keyid --shell`
- 更新 TROUBLESHOOTING.md 文档（记录 SSH mpint padding bug）
- 更新 COMMANDS.md 文档（完善 SSH/key 命令示例）

### 2026-01-15
- 完成项目规划与设计阶段
- 创建架构设计文档
- 创建命令规范文档
- 创建项目目录结构
- 确定技术选型
- **Phase 1 完成**：
  - 实现 ts_config 模块（配置管理，支持 NVS/文件后端，变更通知）
  - 实现 ts_log 模块（多级日志，控制台/文件/缓冲区输出）
  - 实现 ts_event 模块（发布/订阅，事件优先级，事务支持）
- **Phase 2 完成**：
  - 实现 ts_hal 主模块（HAL初始化、平台检测、能力查询）
  - 实现 ts_pin_manager（引脚功能映射、JSON配置加载、NVS持久化）
  - 实现 ts_hal_gpio（GPIO抽象、方向/上下拉配置、ISR服务）
  - 实现 ts_hal_pwm（PWM抽象、LEDC驱动、定时器自动分配、渐变控制）
  - 实现 ts_hal_i2c（I2C抽象、新版i2c_master API、设备扫描）
  - 实现 ts_hal_spi（SPI抽象、总线/设备分离、DMA支持）
  - 实现 ts_hal_uart（UART抽象、事件队列、回调系统）
  - 实现 ts_hal_adc（ADC抽象、oneshot模式、曲线/线性校准）
  - 创建 ESP32S3 平台适配器（ts_platform_s3）
  - 创建 ESP32P4 平台适配器占位（ts_platform_p4）
  - 创建 rm01_esp32s3 板级配置（board.json、pins.json、services.json）
  - 实现 ts_service 模块（8阶段启动，依赖管理，健康检查）
  - 创建主应用程序入口（main.c, ts_core_init.c）
  - 创建项目构建配置（CMakeLists.txt, sdkconfig.defaults, partitions.csv）
- **Phase 3 完成**：
  - 实现 ts_console（esp_console集成, argtable3参数解析, 9个内置命令）
  - 实现 ts_api 框架（端点注册, JSON结果, 权限标记）
  - 实现 ts_api_system（system.info/memory/tasks/reboot/log.level）
  - 实现 ts_api_config（config.get/set/delete/list/save）
  - 实现 ts_storage（SPIFFS挂载/格式化, SD卡SPI/SDIO模式, 文件操作API）
- **Phase 4 完成**：
  - 实现 ts_led 核心（设备管理, 渲染任务, 亮度控制）
  - 实现 ts_led_driver（WS2812 RMT驱动, led_strip集成）
  - 实现 ts_led_layer（图层系统, 绘图操作: 像素/填充/矩形/线/圆/渐变）
  - 实现 ts_led_color（HSV/RGB转换, 调色盘, 颜色混合, 颜色解析）
  - 实现 ts_led_effects（6种内置特效: rainbow/breathing/chase/fire/sparkle/solid）
  - 实现 ts_led_image（BMP图像加载, 图像显示）
  - 实现 ts_led_preset（touch/board/matrix设备预设, 状态指示器）
- **Phase 5 完成**：
  - 实现 ts_drivers 主模块（初始化/反初始化）
  - 实现 ts_fan（PWM风扇控制, 温度曲线, Tach转速测量）
  - 实现 ts_power（ADC电压监测, 电压警报）
  - 实现 ts_device_ctrl（AGX电源控制, 关机请求处理）
  - 实现 ts_usb_mux（USB切换控制, Host/Device模式）
- **Phase 6 完成**：
  - 实现 ts_net 网络框架（状态查询, IP配置, MAC/主机名）
  - 实现 ts_eth（W5500 SPI以太网, 事件处理）
  - 实现 ts_wifi（STA/AP/APSTA模式, 扫描, 连接管理）
  - 实现 ts_dhcp_server（lwIP DHCP服务器封装）
  - 实现 ts_http_server（路由注册, 请求处理, 文件服务, CORS）
  - 实现 ts_security（会话管理, 权限检查, Token认证）
  - 实现 ts_crypto（SHA哈希, HMAC, AES-GCM, Base64/Hex）
  - 实现 ts_auth（登录/登出, 密码验证）
  - 实现 ts_ssh_client（SSH-2.0协议, 密码/公钥认证, 远程命令执行）
  - 实现 ts_ssh_known_hosts（主机密钥验证, SHA256指纹, TOFU策略, NVS存储）
  - 实现 ts_port_forward（本地端口转发, 多并发连接, libssh2隧道）
  - 实现 ts_ssh_shell（PTY分配, 多终端类型, 双向I/O, 信号处理）
  - 实现 SSH 密钥生成（RSA/ECDSA生成, PEM私钥导出, OpenSSH公钥格式）
- **Phase 7 完成**：
  - 实现 ts_webui 主模块（初始化/启动/停止）
  - 实现 ts_webui_api（REST API网关, 登录/登出, Token认证）
  - 实现 ts_webui_ws（WebSocket广播, 事件推送）
  - 创建前端界面（index.html仪表盘, CSS样式, API客户端, WS客户端）
- **Phase 8 完成**：
  - 创建快速入门指南 (docs/QUICK_START.md)
  - 创建 API 参考文档 (docs/API_REFERENCE.md)
  - 创建板级配置指南 (docs/BOARD_CONFIGURATION.md)
  - 更新 README.md
  - 更新开发进度文档

---

## 🔗 相关文档

- [架构设计文档](docs/ARCHITECTURE_DESIGN.md)
- [命令规范文档](docs/COMMAND_SPECIFICATION.md)
- [原项目描述](PROJECT_DESCRIPTION.md)
- [重构分析](REFACTORING_ANALYSIS.md)
