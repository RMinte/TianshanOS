# TianShanOS

> 天山操作系统 - ESP32 机架管理操作系统
> 
> 天山控制南北两大盆地——北向 AGX 提供 AI 算力，南向 LPMU 提供通用计算和存储服务

---

## 🚀 项目概述

TianShanOS 是一个**面向配置而非面向代码**的嵌入式操作系统框架，基于 ESP-IDF v5.5+ 开发，用于 NVIDIA Jetson AGX 载板的机架管理。

### 核心特性

- **完全模块化** - 57 个 C 源文件，41 个头文件，每个功能都是独立组件
- **面向配置** - 通过 JSON 配置文件定义硬件引脚和系统行为
- **运行时灵活** - 引脚配置、服务管理等支持运行时加载
- **跨平台设计** - 支持 ESP32S3 和 ESP32P4
- **安全优先** - HTTPS/mTLS、SSH 公钥认证、AES-GCM 加密、分级权限
- **统一接口** - CLI 和 WebUI 共享 Core API，行为一致
- **多语言支持** - 中英日韩多语言界面

### 系统架构

```
┌─────────────────────────────────────────────┐
│      用户交互层 (CLI / WebUI / SSH)          │
├─────────────────────────────────────────────┤
│              Core API 层                     │
├─────────────────────────────────────────────┤
│         服务管理层 (8阶段启动)                │
├─────────────────────────────────────────────┤
│            事件/消息总线                      │
├─────────────────────────────────────────────┤
│         配置管理层 (NVS/文件/默认值)          │
├─────────────────────────────────────────────┤
│            硬件抽象层 (HAL)                   │
├─────────────────────────────────────────────┤
│          平台适配层 (ESP32S3/P4)              │
└─────────────────────────────────────────────┘
```

---

## 📦 项目结构

```
TianShanOS/
├── components/              # ESP-IDF 组件
│   ├── ts_core/            # 核心框架 (配置/事件/服务/日志)
│   ├── ts_hal/             # 硬件抽象层 (GPIO/PWM/I2C/SPI/UART/ADC)
│   ├── ts_console/         # 控制台 (命令/多语言/脚本引擎)
│   ├── ts_api/             # Core API (统一接口层)
│   ├── ts_led/             # LED 系统 (WS2812/图层/特效/图像)
│   ├── ts_net/             # 网络 (WiFi/以太网/HTTP/HTTPS)
│   ├── ts_security/        # 安全 (认证/加密/SSH客户端)
│   ├── ts_webui/           # WebUI (REST API/WebSocket)
│   ├── ts_storage/         # 存储 (SPIFFS/SD卡/文件操作)
│   └── ts_drivers/         # 设备驱动 (风扇/电源/AGX控制)
├── boards/                 # 板级配置 (pins.json/services.json)
├── main/                   # 主程序入口
├── docs/                   # 文档
├── sdcard/                 # SD 卡内容模板 (动画/脚本/图像)
├── partitions.csv          # 分区表
└── sdkconfig.defaults      # 默认配置
```

---

## 🛠️ 开发环境

### 依赖
- ESP-IDF v5.5+
- Python 3.9+
- CMake 3.16+

### 快速开始

```bash
# 克隆项目
git clone https://github.com/thomas-hiddenpeak/TianShanOS.git
cd TianShanOS

# 设置目标芯片
idf.py set-target esp32s3

# 配置项目
idf.py menuconfig

# 编译
idf.py build

# 烧录并监控
idf.py -p /dev/ttyUSB0 flash monitor
```

详细说明请参阅 [快速入门指南](docs/QUICK_START.md)。

---

## 📚 文档

| 文档 | 描述 |
|-----|------|
| [快速入门](docs/QUICK_START.md) | 环境搭建与首次运行 |
| [架构设计](docs/ARCHITECTURE_DESIGN.md) | 系统架构与设计决策 |
| [API 参考](docs/API_REFERENCE.md) | REST API 和 CLI 命令 |
| [命令规范](docs/COMMAND_SPECIFICATION.md) | CLI 命令格式规范 |
| [板级配置](docs/BOARD_CONFIGURATION.md) | 引脚和服务配置指南 |
| [开发进度](docs/DEVELOPMENT_PROGRESS.md) | 功能实现状态跟踪 |
| [测试计划](docs/TEST_PLAN.md) | 测试策略与用例 |

---

## 🎯 当前状态

**版本**: 0.1.0-dev  
**阶段**: Phase 8 完成 - 所有核心功能已实现

### 已完成功能

| 模块 | 功能 |
|-----|------|
| 核心框架 | 配置管理、事件总线、服务管理、日志系统 |
| 硬件抽象 | GPIO、PWM、I2C、SPI、UART、ADC |
| LED 系统 | WS2812 驱动、图层、特效、BMP/PNG/JPG/GIF |
| 控制台 | 命令系统、多语言、脚本引擎 |
| 网络 | WiFi、以太网 W5500、HTTP/HTTPS 服务器 |
| 安全 | 会话管理、Token 认证、AES-GCM、RSA/EC、SSH 客户端 |
| 驱动 | 风扇控制、电源监控 (ADC/INA3221/PZEM)、AGX 电源控制 |
| WebUI | REST API 网关、WebSocket 广播、前端仪表盘 |

查看 [开发进度](docs/DEVELOPMENT_PROGRESS.md) 了解详细状态。

---

## 📄 许可证

*待定*

---

## 👥 贡献者

- Thomas (项目负责人)
