# TianShanOS 开发进度跟踪

> **项目**：TianShanOS（天山操作系统）  
> **版本**：0.1.0-dev  
> **最后更新**：2026年1月21日  
> **代码统计**：95+ 个 C 源文件，70+ 个头文件

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
  - 仪表盘（系统/内存/网络/电源/设备/温度）
  - 系统管理（服务列表、重启）
  - LED 控制（设备/亮度/颜色/特效/图像）
  - 网络配置（以太网/WiFi/DHCP/NAT）
  - 设备控制（AGX/LPMU 电源、风扇调速）
  - 终端（Web CLI + SSH Shell）
  - 安全（SSH 测试、密钥管理、已知主机）
  - 配置（配置列表/编辑/删除）
  - 文件管理（SD卡/SPIFFS 浏览、上传/下载、新建/删除/重命名）

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

## 📝 开发日志

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
