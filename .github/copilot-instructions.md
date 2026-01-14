# TianShanOS Copilot 指南

## 项目概述

TianShanOS 是一个**面向配置而非面向代码**的嵌入式操作系统框架，基于 ESP-IDF v5.5+ 开发，支持 ESP32S3/ESP32P4。用于 NVIDIA Jetson AGX 载板的机架管理。

## 架构分层

系统采用严格分层架构，**所有组件通过事件总线解耦**：

```
用户交互层 (CLI/WebUI/SSH) → Core API 层 → 服务管理层 → 事件总线 → 配置管理层 → HAL → 平台适配层
```

- 核心模块在 `components/ts_core/` 下：`ts_config`、`ts_event`、`ts_service`、`ts_log`
- 所有组件前缀 `ts_`，使用 `TS_` 前缀定义宏和枚举

## 服务系统（核心概念）

服务是系统的基本单元，通过 8 个启动阶段顺序初始化：

```c
PLATFORM → CORE → HAL → DRIVER → NETWORK → SECURITY → SERVICE → UI
```

服务定义模式（参考 [ts_service.h](components/ts_core/ts_service/include/ts_service.h#L67-L85)）：
- 实现 `init`、`start`、`stop`、`health_check` 回调
- 使用 `depends_on` 声明依赖关系
- 服务配置在 `boards/<board>/services.json` 中声明

## 配置驱动开发

**硬件引脚和服务均通过 JSON 配置，不硬编码**：

- `boards/<board>/pins.json` - 逻辑名称 → GPIO 映射（如 `"LED_TOUCH": {"gpio": 45}`）
- `boards/<board>/services.json` - 服务启用/配置
- 配置优先级：命令行/API > NVS 持久化 > SD 卡文件 > 代码默认值

## 事件系统模式

组件通信必须使用事件总线（[ts_event.h](components/ts_core/ts_event/include/ts_event.h)）：

```c
// 发布事件
ts_event_post(TS_EVENT_BASE_LED, TS_EVENT_LED_CHANGED, &data, sizeof(data));

// 订阅事件
ts_event_handler_register(TS_EVENT_BASE_LED, TS_EVENT_ANY_ID, handler_fn, user_data);
```

## Core API 层

CLI 和 WebUI 共享统一 API（[ts_api.h](components/ts_api/include/ts_api.h)）：
- API 命名格式：`<category>.<action>`（如 `system.reboot`、`led.set_color`）
- 所有 API 返回 `ts_api_result_t` 结构，包含 code/message/JSON data
- 需要权限的 API 设置 `requires_auth` 和 `permission` 字段

## 命令风格

CLI 命令使用参数风格（非 POSIX 子命令）：
```bash
service --start --name fan_controller   # 正确
fan --speed 75 --id 0                   # 正确
```

## ESP-IDF 开发工作流

```bash
# 使用 VS Code ESP-IDF 扩展或命令行
idf.py set-target esp32s3
idf.py menuconfig                       # TianShanOS 配置在顶层菜单
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

分区表定义在 [partitions.csv](partitions.csv)：`factory`(3MB app)、`storage`(SPIFFS)、`www`(WebUI)、`fatfs`

## 代码约定

- 日志使用 `ESP_LOGI/E/W/D(TAG, ...)`，每个文件定义 `static const char *TAG = "模块名"`
- 错误处理返回 `esp_err_t`，使用 `ESP_OK`/`ESP_ERR_*`
- 头文件使用 doxygen 风格注释，包含 `@brief`、`@param`、`@return`
- 中文注释用于架构说明，英文用于 API 文档

## 组件结构模板

新组件遵循此结构：
```
components/ts_xxx/
├── CMakeLists.txt
├── Kconfig              # 组件配置选项
├── include/
│   └── ts_xxx.h         # 公开 API
└── src/
    └── ts_xxx.c         # 实现
```

## 关键目录

- `components/ts_core/` - 核心框架（配置/事件/服务/日志）
- `components/ts_hal/` - 硬件抽象层，含平台特定代码在 `platform/esp32s3|p4/`
- `components/ts_api/` - 统一 API 层
- `boards/rm01_esp32s3/` - 板级 JSON 配置
- `sdcard/` - SD 卡内容模板（动画、脚本、图像）
