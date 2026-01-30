# TianShanOS

> 天山操作系统 - ESP32 机架管理操作系统
> 
> 天山控制南北两大盆地——北向 AGX 提供 AI 算力，南向 LPMU 提供通用计算和存储服务

```
╔══════════════════════════════════════════════════════════════════════╗
║                                                                      ║
║   ████████╗██╗ █████╗ ███╗   ██╗███████╗██╗  ██╗ █████╗ ███╗   ██╗   ║
║   ╚══██╔══╝██║██╔══██╗████╗  ██║██╔════╝██║  ██║██╔══██╗████╗  ██║   ║
║      ██║   ██║███████║██╔██╗ ██║███████╗███████║███████║██╔██╗ ██║   ║
║      ██║   ██║██╔══██║██║╚██╗██║╚════██║██╔══██║██╔══██║██║╚██╗██║   ║
║      ██║   ██║██║  ██║██║ ╚████║███████║██║  ██║██║  ██║██║ ╚████║   ║
║      ╚═╝   ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝   ║
║                                                                      ║
║                         TianShanOS v0.3.0                            ║
║                ESP32 Rack Management Operating System                ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
```

---

## 🚀 项目概述

TianShanOS 是一个**面向配置而非面向代码**的嵌入式操作系统框架，基于 ESP-IDF v5.5+ 开发，用于 NVIDIA Jetson AGX + DFRobot LattePanda Mu 载板的机架管理。

### 核心特性

- **完全模块化** - 220+ 个 C 源文件，110+ 个头文件，18 个独立组件
- **面向配置** - 通过 JSON 配置文件定义硬件引脚和系统行为
- **统一配置系统** - SD 卡优先 + NVS 备份双写，支持热插拔同步、Schema 版本迁移
- **自动化引擎** - 数据源采集、规则引擎、动作执行、变量系统
- **OTA 升级** - HTTPS/SD 卡双通道固件升级，自动回滚保护
- **跨平台设计** - 支持 ESP32-S3 和 ESP32-P4
- **安全优先** - HTTPS/mTLS、SSH 公钥认证、PKI 证书管理、分级权限
- **统一接口** - CLI 和 WebUI 共享 Core API，行为一致
- **多语言支持** - 中英日韩多语言界面

### 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│           用户交互层 (CLI / WebUI / HTTPS API)                   │
├─────────────────────────────────────────────────────────────────┤
│                      Core API 层 (ts_api)                        │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────────┐ │
│  │  服务管理层   │ │  自动化引擎   │ │      安全模块            │ │
│  │  (8阶段启动)  │ │ (数据源/规则) │ │ (SSH/PKI/mTLS)          │ │
│  └──────────────┘ └──────────────┘ └──────────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│                    事件/消息总线 (ts_event)                       │
├─────────────────────────────────────────────────────────────────┤
│         配置管理层 (NVS/SD卡/默认值，优先级：内存>SD>NVS>默认)     │
├─────────────────────────────────────────────────────────────────┤
│               硬件抽象层 (GPIO/PWM/I2C/SPI/UART/ADC)              │
├─────────────────────────────────────────────────────────────────┤
│                  平台适配层 (ESP32-S3 / ESP32-P4)                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📦 项目结构

```
TianShanOS/
├── components/              # ESP-IDF 组件 (18个)
│   ├── ts_core/            # 核心框架 (配置/事件/服务/日志)
│   ├── ts_hal/             # 硬件抽象层 (GPIO/PWM/I2C/SPI/UART/ADC)
│   ├── ts_console/         # 控制台 (命令/多语言/脚本引擎)
│   ├── ts_api/             # Core API (统一接口层)
│   ├── ts_automation/      # 自动化引擎 (数据源/规则/动作/变量)
│   ├── ts_led/             # LED 系统 (WS2812/图层/特效/滤镜)
│   ├── ts_net/             # 网络 (WiFi/以太网/DHCP/NAT/HTTP)
│   ├── ts_security/        # 安全 (SSH客户端/认证/加密/SFTP)
│   ├── ts_https/           # HTTPS 服务器 (mTLS/API网关)
│   ├── ts_cert/            # 证书管理 (生成/签发/验证)
│   ├── ts_pki_client/      # PKI 客户端 (证书申请/续期)
│   ├── ts_webui/           # WebUI (REST API/WebSocket/仪表盘)
│   ├── ts_storage/         # 存储 (SPIFFS/SD卡/文件操作)
│   ├── ts_ota/             # OTA升级 (HTTPS/SD卡/回滚)
│   ├── ts_drivers/         # 设备驱动 (风扇/电源/AGX/温度)
│   └── ts_jsonpath/        # JSONPath 解析器
├── boards/                 # 板级配置 (pins.json/services.json)
├── main/                   # 主程序入口
├── docs/                   # 文档
├── sdcard/                 # SD 卡内容模板
├── partitions.csv          # 分区表 (factory 3MB + storage)
└── sdkconfig.defaults      # 默认配置
```

---

## 🛠️ 开发环境

### 依赖
- ESP-IDF v5.5.2+
- Python 3.10+
- CMake 3.16+

### 快速开始

```bash
# 克隆项目
git clone https://github.com/thomas-hiddenpeak/TianshanOS.git
cd TianShanOS

# 设置 ESP-IDF 环境
. $HOME/esp/v5.5/esp-idf/export.sh

# 设置目标芯片
idf.py set-target esp32s3

# 配置项目 (TianShanOS 选项在顶层菜单)
idf.py menuconfig

# 编译
idf.py build

# 烧录并监控
idf.py -p /dev/ttyACM0 flash monitor
```

### VS Code 开发

推荐使用 ESP-IDF 扩展，支持一键构建/烧录/监控、Menuconfig 图形界面、代码补全和跳转。

详细说明请参阅 [快速入门指南](docs/QUICK_START.md)。

---

## 📚 文档

| 文档 | 描述 |
|-----|------|
| [快速入门](docs/QUICK_START.md) | 环境搭建与首次运行 |
| [架构设计](docs/ARCHITECTURE_DESIGN.md) | 系统架构与设计决策 |
| [配置系统设计](docs/CONFIG_SYSTEM_DESIGN.md) | 统一配置系统详细设计 |
| [自动化引擎](docs/AUTOMATION_ENGINE.md) | 触发器-条件-动作系统 |
| [GPIO 引脚映射](docs/GPIO_MAPPING.md) | 硬件引脚定义与 PCB 对应关系 |
| [API 设计](docs/API_DESIGN.md) | REST API 架构与设计理念 |
| [API 参考](docs/API_REFERENCE.md) | REST API 和 CLI 命令 |
| [命令规范](docs/COMMAND_SPECIFICATION.md) | CLI 命令格式规范 |
| [板级配置](docs/BOARD_CONFIGURATION.md) | 引脚和服务配置指南 |
| [LED 架构](docs/LED_ARCHITECTURE.md) | LED 系统多设备多图层架构 |
| [安全实现](docs/SECURITY_IMPLEMENTATION.md) | SSH、PKI、认证机制 |
| [开发进度](docs/DEVELOPMENT_PROGRESS.md) | 功能实现状态跟踪 |
| [测试计划](docs/TEST_PLAN.md) | 测试策略与用例 |
| [故障排除](docs/TROUBLESHOOTING.md) | 常见问题与解决方案 |

---

## 🎯 当前状态

**版本**: 0.3.0  
**阶段**: Phase 20 完成 - 自动化引擎、SSH 远程执行、变量系统

### 已完成功能

| 模块 | 功能 |
|-----|------|
| 核心框架 | 配置管理、事件总线、8 阶段服务管理、日志系统 |
| 配置系统 | SD 卡优先 + NVS 备份双写、热插拔同步、Schema 版本迁移 |
| 硬件抽象 | GPIO、PWM、I2C、SPI、UART、ADC |
| LED 系统 | WS2812 驱动、多设备多图层、特效引擎、BMP/PNG/JPG/GIF |
| 控制台 | 命令系统、多语言、脚本引擎、配置持久化 |
| 网络 | WiFi、以太网 W5500、HTTP/HTTPS 服务器 |
| 安全 | 会话管理、Token 认证、AES-GCM、RSA/EC、SSH 客户端、PKI 证书管理 |
| 驱动 | 风扇控制、电源监控 (ADC/INA3221/PZEM)、AGX/LPMU 电源控制、USB MUX |
| WebUI | REST API 网关、WebSocket 广播、前端仪表盘 |
| OTA | 双分区升级、版本检测、完整性校验、自动回滚 |
| 自动化引擎 | 触发器-条件-动作系统、SSH 远程执行、正则解析、变量系统 |

### 自动化引擎特性

- **数据源**：定时器、SSH 命令输出、系统指标、GPIO、变量、HTTP
- **触发器**：阈值、正则匹配、时间表达式、变化检测
- **动作**：变量设置、命令执行、事件发布、日志输出
- **SSH 集成**：支持连续监控命令（如 `ping`）、实时变量更新

### 统一配置系统特性

- **7 个配置模块**：网络、DHCP、WiFi、LED、风扇、设备、系统
- **双写同步机制**：NVS + SD 卡同时写入，确保一致性
- **热插拔处理**：SD 卡拔出期间标记 `pending_sync`，插入后自动同步
- **优先级管理**：内存缓存 > SD 卡 > NVS > Schema 默认值
- **序列号机制**：无需 RTC，通过单调递增序列号判断配置新旧
- **Schema 迁移**：支持配置格式升级与向后兼容
- **显式持久化**：CLI 修改默认临时，`--save` 显式持久化

查看 [开发进度](docs/DEVELOPMENT_PROGRESS.md) 了解详细状态。

---

## 👥 贡献者

- Thomas (项目负责人)
- massif-01
