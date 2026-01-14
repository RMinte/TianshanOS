# TianShanOS

> 天山操作系统 - ESP32 板上机架操作系统
> 
> 天山控制新疆南北两大盆地——北向 AGX 提供 AI 算力，南向 LPMU 提供通用计算和存储服务

---

## 🚀 项目概述

TianShanOS 是一个面向配置而非面向代码的嵌入式操作系统框架，基于 ESP-IDF 开发，支持 ESP32S3 和 ESP32P4 平台。

### 核心特性

- **完全模块化** - 每个功能都是独立组件，支持单独开发和测试
- **面向配置** - 通过配置文件定义系统行为，而非硬编码
- **运行时灵活** - 引脚配置、服务管理等支持运行时加载
- **跨平台设计** - 同时支持 ESP32S3 和 ESP32P4
- **安全优先** - mTLS 认证、加密通信、权限管理
- **统一接口** - CLI 和 WebUI 共享 Core API

### 系统架构

```
┌─────────────────────────────────────────────┐
│           用户交互层 (CLI / WebUI)           │
├─────────────────────────────────────────────┤
│              Core API 层                     │
├─────────────────────────────────────────────┤
│              服务管理层                       │
├─────────────────────────────────────────────┤
│            事件/消息总线                      │
├─────────────────────────────────────────────┤
│              配置管理层                       │
├─────────────────────────────────────────────┤
│            硬件抽象层 (HAL)                   │
├─────────────────────────────────────────────┤
│          平台适配层 (S3/P4)                   │
└─────────────────────────────────────────────┘
```

---

## 📦 项目结构

```
TianShanOS/
├── components/              # ESP-IDF 组件
│   ├── ts_core/            # 核心框架
│   ├── ts_hal/             # 硬件抽象层
│   ├── ts_console/         # 控制台系统
│   ├── ts_api/             # Core API
│   ├── ts_led/             # LED 系统
│   ├── ts_net/             # 网络系统
│   ├── ts_security/        # 安全模块
│   ├── ts_webui/           # WebUI
│   ├── ts_storage/         # 存储管理
│   └── ts_drivers/         # 设备驱动
├── boards/                 # 板级配置
├── config/                 # 配置文件模板
├── main/                   # 主程序入口
├── docs/                   # 文档
├── tests/                  # 测试
├── tools/                  # 工具脚本
└── sdcard/                 # SD 卡内容模板
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

# 烧录
idf.py -p /dev/ttyUSB0 flash

# 监控
idf.py -p /dev/ttyUSB0 monitor
```

---

## 📚 文档

- [架构设计文档](docs/ARCHITECTURE_DESIGN.md)
- [命令规范文档](docs/COMMAND_SPECIFICATION.md)
- [开发进度](DEVELOPMENT_PROGRESS.md)
- [API 参考](docs/API_REFERENCE.md) *(待完善)*

---

## 🎯 当前状态

**版本**: 0.1.0-dev  
**阶段**: Phase 1 - 基础架构开发中

查看 [开发进度](DEVELOPMENT_PROGRESS.md) 了解详细状态。

---

## 📄 许可证

*待定*

---

## 👥 贡献者

- Thomas (项目负责人)
