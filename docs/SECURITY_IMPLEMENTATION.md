# TianShanOS 安全实现进度表

本文档跟踪 TianShanOS 安全特性的实现状态，包括开发阶段和生产阶段的安全措施。

---

## 安全等级定义

| 等级 | 说明 | 适用场景 |
|------|------|---------|
| **L1** | 软件防护 | 开发阶段、内部测试 |
| **L2** | NVS 加密 | 预生产、受控环境 |
| **L3** | Flash 加密 + Secure Boot | 生产环境 |
| **L4** | 完整硬件安全 | 高安全性生产环境 |

---

## 一、密钥存储安全

### 1.1 私钥保护

| 特性 | 状态 | 等级 | 说明 |
|------|------|------|------|
| NVS 存储私钥 | ✅ 已实现 | L1 | 私钥存储在 NVS 命名空间 `ts_keystore` |
| 禁止私钥导出 API | ✅ 已实现 | L1 | `ts_keystore_export_to_file()` 已改为仅导出公钥 |
| 可导出密钥标记 | ✅ 已实现 | L1 | `--exportable` 标记，仅明确允许的密钥可导出私钥 |
| 隐藏密钥 ID | ✅ 已实现 | L1 | `hidden` 标记 + 别名系统，防止低权限用户知道敏感密钥 ID |
| CLI 仅导出公钥 | ✅ 已实现 | L1 | `key --export` 只输出公钥 |
| 私钥内存清零 | ✅ 已实现 | L1 | `secure_free_key()` 带边界检查的安全清零 |
| NVS 加密 | ⏳ 待配置 | L2 | 需要 `CONFIG_NVS_ENCRYPTION=y` |
| Flash 加密 | ⏳ 待配置 | L3 | 需要 `CONFIG_FLASH_ENCRYPTION_ENABLED=y` |

### 1.2 存储容量

| 配置项 | 当前值 | 目标值 | 说明 |
|--------|--------|--------|------|
| NVS 分区大小 | 0x6000 (24KB) | 0xC000 (48KB) | 支持 8 个 RSA-4096 密钥 |
| 最大密钥数 | 8 | 8 | `TS_KEYSTORE_MAX_KEYS` |
| 单个私钥最大 | 4096 字节 | 4096 字节 | RSA-4096 PEM ~3.3KB |

### 1.3 密钥容量估算

| 密钥类型 | 私钥大小 | 公钥大小 | 元数据 | 单个合计 | 8 个合计 |
|---------|---------|---------|--------|---------|---------|
| RSA-2048 | ~1.7KB | ~400B | ~150B | ~2.3KB | ~18KB |
| RSA-4096 | ~3.3KB | ~750B | ~150B | ~4.2KB | **~34KB** |
| ECDSA P-256 | ~230B | ~200B | ~150B | ~580B | ~4.6KB |
| ECDSA P-384 | ~300B | ~250B | ~150B | ~700B | ~5.6KB |

**结论**：48KB NVS 可支持 8 个 RSA-4096 + 系统配置。

---

## 二、SSH 连接安全

### 2.1 认证安全

| 特性 | 状态 | 等级 | 说明 |
|------|------|------|------|
| 密码认证 | ✅ 已实现 | L1 | `--password` 参数 |
| 公钥认证（文件） | ✅ 已实现 | L1 | `--key` 参数 |
| 公钥认证（安全存储） | ✅ 已实现 | L1 | `--keyid` 参数，私钥不离开安全存储 |
| 密钥部署 | ✅ 已实现 | L1 | `--copyid` 追加公钥到 authorized_keys |
| 公钥作废 | ✅ 已实现 | L1 | `--revoke` 从远程删除公钥 |

### 2.2 主机验证

| 特性 | 状态 | 等级 | 说明 |
|------|------|------|------|
| Known hosts 存储 | ✅ 已实现 | L1 | `ts_known_hosts.c` 存储在 NVS |
| 主机指纹验证 | ✅ 已实现 | L1 | 所有 SSH 连接自动验证 |
| 首次连接确认 | ✅ 已实现 | L1 | TOFU 策略，提示用户确认指纹 |
| 指纹变化警告 | ✅ 已实现 | L1 | 醒目警告框，检测 MITM 攻击 |
| `hosts` 命令 | ✅ 已实现 | L1 | 管理 known hosts（list/info/remove/clear） |

---

## 三、硬件安全（生产环境）

### 3.1 ESP32-S3 安全特性

| 特性 | 状态 | 配置项 | 说明 |
|------|------|--------|------|
| Secure Boot v2 | ⏳ 待启用 | `CONFIG_SECURE_BOOT=y` | 防止恶意固件 |
| Flash 加密 | ⏳ 待启用 | `CONFIG_FLASH_ENCRYPTION_ENABLED=y` | 整盘加密 |
| JTAG 禁用 | ⏳ 待启用 | `CONFIG_SECURE_BOOT_DISABLE_JTAG=y` | 防止调试攻击 |
| eFuse 保护 | ⏳ 待配置 | 烧录后不可更改 | 永久性安全配置 |

### 3.2 NVS 加密方案

| 方案 | 安全性 | 复杂度 | 适用等级 | 说明 |
|------|--------|--------|----------|------|
| **HMAC 密钥派生** ⭐ | 高 | 低 | **L2** | eFuse HMAC 派生密钥，无需 Flash 加密 |
| nvs_keys 分区 | 中 | 低 | L3 | 需要 Flash 加密保护 nvs_keys 分区 |
| Flash 加密 + NVS | 最高 | 高 | L3/L4 | 完整 Flash 加密支持 |

**L2 推荐方案：HMAC 密钥派生**

```
sdkconfig.defaults 配置：
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECTION_SCHEME=1
CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y
CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=4      # 使用 eFuse BLOCK4
```

工作原理：
1. `nvs_flash_init()` 检查 eFuse BLOCK4 是否有 HMAC 密钥
2. 如果没有，自动生成 256-bit 随机密钥并烧录到 eFuse
3. 使用 HMAC 密钥派生 XTS-AES 加密密钥
4. 所有 NVS 读写自动加密/解密

---

## 四、攻击向量与防御

### 4.1 物理攻击

| 攻击方式 | L1 防护 | L2 防护 | L3 防护 | L4 防护 |
|---------|---------|---------|---------|---------|
| JTAG 读取 | ❌ | ❌ | ✅ 禁用 | ✅ eFuse |
| Flash 读取 | ❌ | ⚠️ NVS 加密 | ✅ 全盘加密 | ✅ |
| UART 泄露 | ⚠️ 软件限制 | ⚠️ | ✅ | ✅ |

### 4.2 软件攻击

| 攻击方式 | 防护措施 | 状态 |
|---------|---------|------|
| API 私钥泄露 | 移除导出 API | ✅ 已实现 |
| 内存 dump | 使用后清零 | ✅ 已实现 |
| 恶意固件 | Secure Boot | ⏳ 待启用 |

---

## 五、分区表配置

### 5.1 当前配置

```csv
# Name,   Type, SubType, Offset,  Size,   Flags
nvs,      data, nvs,     0x9000,  0xC000,        # 48KB - 已扩大
nvs_keys, data, nvs_keys,0x15000, 0x1000,        # 4KB - 已添加
phy_init, data, phy,     0x16000, 0x1000,
factory,  app,  factory, 0x20000, 0x2F0000,      # 3008KB
storage,  data, spiffs,  0x310000,0xA0000,
www,      data, spiffs,  0x3B0000,0x40000,
fatfs,    data, fat,     0x3F0000,0x10000,
```

### 5.2 分区说明

| 分区 | 大小 | 用途 |
|------|------|------|
| nvs | 48KB | 系统配置 + 密钥存储（8个RSA-4096） |
| nvs_keys | 4KB | NVS加密密钥（启用加密时使用） |
| factory | 3008KB | 应用程序 |
| storage | 640KB | SPIFFS 用户数据 |
| www | 256KB | WebUI 静态文件 |
| fatfs | 64KB | FAT 文件系统 |

---

## 六、实施计划

### 阶段一：软件加固 (L1) - ✅ 已完成

| 任务 | 状态 | 日期 |
|------|------|------|
| 移除 `ts_keystore_export_to_file()` 私钥导出 | ✅ | 2026-01-18 |
| 添加私钥内存清零 | ✅ | 2026-01-18 |
| 限制私钥 API 调用范围 | ✅ | 2026-01-18 |
| 创建安全进度文档 | ✅ | 2026-01-18 |
| 添加 `exportable` 密钥标记 | ✅ | 2026-01-19 |
| 增强 `secure_free_key()` 边界检查 | ✅ | 2026-01-19 |
| SSH 主机密钥验证 | ✅ | 2026-01-19 |
| 首次连接 TOFU 确认 | ✅ | 2026-01-19 |
| 指纹变化警告 | ✅ | 2026-01-19 |
| 添加 `hosts` 命令 | ✅ | 2026-01-19 |
| 添加 `ssh --revoke` | ✅ | 2026-01-19 |
| WebUI 主机指纹更新功能 | ✅ | 2026-01-23 |
| 隐藏密钥 ID + 别名系统 | ✅ | 2026-01-23 |

### 阶段二：NVS 加密 (L2) - ⏸️ 待功能开发完成

| 任务 | 状态 | 优先级 |
|------|------|--------|
| 扩大 NVS 分区到 48KB | ✅ | P0 |
| 添加 nvs_keys 分区 | ✅ | P0 |
| 配置 HMAC 方案选项 | ✅ | P0 |
| 验证 `nvs_flash_init()` 兼容性 | ✅ | P0 |
| 启用 `CONFIG_NVS_ENCRYPTION=y` | ⏸️ 待功能完成 | P1 |
| 测试密钥存储功能 | ⏸️ 待功能完成 | P1 |

> **注意**：L2 配置已就绪，待所有功能性开发完成后再启用测试。
> 
> **原因**：
> - 开发阶段保持 L1 模式便于调试
> - 避免 eFuse 写入对开发板的永久影响
> - 统一在预生产阶段进行安全测试

#### L2 启用步骤

1. **备份重要数据**（启用加密后 NVS 数据会丢失）

2. **修改 sdkconfig.defaults**：
   ```
   # 取消注释以下行
   CONFIG_NVS_ENCRYPTION=y
   CONFIG_NVS_SEC_KEY_PROTECTION_SCHEME=1
   CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y
   CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=4
   
   # 注释掉这行
   #CONFIG_NVS_ENCRYPTION=n
   ```

3. **清理并重新配置**：
   ```bash
   rm sdkconfig
   idf.py reconfigure
   ```

4. **擦除 Flash 并烧录**：
   ```bash
   idf.py erase-flash flash monitor
   ```

5. **首次启动**：
   - ESP32 会自动在 eFuse BLOCK4 生成 HMAC 密钥
   - 使用 HMAC 派生 NVS 加密密钥
   - 后续所有 NVS 数据自动加密

⚠️ **注意**：HMAC 密钥烧录到 eFuse 后**不可更改**！

### 阶段三：SSH 安全增强 (L1) - ✅ 已完成

| 任务 | 状态 | 完成日期 |
|------|------|----------|
| 启用主机指纹验证 | ✅ | 2026-01-19 |
| 添加 `hosts` 命令 | ✅ | 2026-01-19 |
| 添加 `ssh --revoke` | ✅ | 2026-01-19 |

### 阶段四：生产安全 (L3/L4) - 📋 计划中

| 任务 | 状态 | 优先级 |
|------|------|--------|
| 启用 Secure Boot v2 | 📋 | P1 |
| 启用 Flash 加密 | 📋 | P1 |
| 禁用 JTAG | 📋 | P1 |
| eFuse 配置文档 | 📋 | P2 |

---

## 七、配置检查清单

### 开发阶段

- [x] `CONFIG_NVS_ENCRYPTION=n`（便于调试）
- [x] JTAG 可用
- [x] 串口控制台可用
- [ ] 测试密钥管理功能

### 预生产阶段

- [ ] `CONFIG_NVS_ENCRYPTION=y`
- [ ] 测试 NVS 加密功能
- [ ] 验证密钥存储容量

### 生产阶段

- [ ] `CONFIG_SECURE_BOOT=y`
- [ ] `CONFIG_SECURE_BOOT_V2_ENABLED=y`
- [ ] `CONFIG_FLASH_ENCRYPTION_ENABLED=y`
- [ ] `CONFIG_SECURE_BOOT_DISABLE_JTAG=y`
- [ ] eFuse 永久烧录
- [ ] 禁用 UART 调试输出

---

## 八、相关文件

| 文件 | 说明 |
|------|------|
| `components/ts_security/src/ts_keystore.c` | 密钥存储实现 |
| `components/ts_security/include/ts_keystore.h` | 密钥存储 API |
| `components/ts_security/src/ts_known_hosts.c` | Known hosts 实现 |
| `components/ts_console/commands/ts_cmd_key.c` | key 命令实现 |
| `components/ts_console/commands/ts_cmd_ssh.c` | ssh 命令实现 |
| `components/ts_console/commands/ts_cmd_hosts.c` | hosts 命令实现 |
| `partitions.csv` | 分区表配置 |
| `sdkconfig.defaults` | SDK 默认配置 |

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2026-01-18 | 初始版本，完成 L1 软件加固基础 |
| 1.1 | 2026-01-19 | 添加 exportable 密钥标记、增强内存安全 |
| 1.2 | 2026-01-19 | 完成 SSH 安全增强：主机验证、hosts 命令、revoke 功能 |
| 1.3 | 2026-01-19 | L2 准备：配置 HMAC 方案 NVS 加密选项，添加启用指南 |
| 1.4 | 2026-01-19 | L2 测试推迟至功能开发完成后进行 |
