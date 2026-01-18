# TianShanOS Copilot 指南

## 项目概述

TianShanOS 是**面向配置而非面向代码**的嵌入式 OS，基于 ESP-IDF v5.5+，用于 NVIDIA Jetson AGX 载板机架管理。目标芯片：ESP32-S3 / ESP32-P4。

## 核心架构

```
CLI/WebUI → Core API (ts_api) → 服务管理(8阶段) → 事件总线 → HAL → 平台适配层
```

**关键约束**：
- 所有组件通过事件总线（`ts_event`）解耦，**禁止**直接调用其他组件内部函数
- GPIO 引脚**禁止硬编码**，必须通过 `boards/*/pins.json` 配置
- 命名规范：组件前缀 `ts_`，宏/枚举前缀 `TS_`

## 事件系统（组件通信唯一方式）

```c
// 发布事件（参考 ts_event.h）
ts_event_post(TS_EVENT_BASE_LED, TS_EVENT_LED_CHANGED, &data, sizeof(data), timeout_ms);

// 订阅事件
ts_event_handler_register(TS_EVENT_BASE_LED, TS_EVENT_ANY_ID, handler_fn, user_data);
```

## Core API 层（CLI/WebUI 统一接口）

CLI 和 WebUI **必须**通过 `ts_api` 调用功能，禁止直接调用组件函数（参考 `ts_api.h`）：

```c
// API 命名格式：<category>.<action>
// 示例：system.reboot, led.set_color, device.agx.power

// 注册 API
ts_api_endpoint_t ep = {
    .name = "led.set_brightness",
    .category = TS_API_CAT_LED,
    .handler = api_led_set_brightness,
    .requires_auth = false,
};
ts_api_register(&ep);

// 调用 API（返回 ts_api_result_t 包含 code/message/JSON data）
ts_api_call("led.set_brightness", params_json, &result);
```

## 服务系统（main/ts_services.c）

服务是系统基本单元，必须实现 4 个回调：

```c
static esp_err_t xxx_service_init(ts_service_handle_t h, void *d);   // 初始化资源
static esp_err_t xxx_service_start(ts_service_handle_t h, void *d);  // 启动服务
static esp_err_t xxx_service_stop(ts_service_handle_t h, void *d);   // 停止服务
static bool xxx_service_health(ts_service_handle_t h, void *d);      // 健康检查
```

**启动阶段**（按顺序）：`PLATFORM → CORE → HAL → DRIVER → NETWORK → SECURITY → SERVICE → UI`

新服务在 `main/ts_services.c` 注册，使用 `ts_service_config_t` 指定 `.phase` 和 `.depends_on`。

## 配置驱动开发

- `boards/rm01_esp32s3/pins.json` - 逻辑名 → GPIO 映射（如 `"FAN_PWM_0": {"gpio": 41}`）
- `boards/rm01_esp32s3/services.json` - 服务启用、依赖、运行时参数
- 配置优先级：CLI/API 实时修改 > NVS 持久化 > SD 卡文件 > 代码默认值

## CLI 命令实现（argtable3 风格）

命令使用**参数风格**（非子命令），示例：

```bash
fan --status --id 0 --json   # ✓ 正确
led --effect --device matrix --name fire --speed 50
```

**新增命令步骤**：
1. 创建 `components/ts_console/commands/ts_cmd_xxx.c`
2. 使用 `argtable3` 定义参数结构（参考 `ts_cmd_led.c` 的 `s_led_args`）
3. 在 `ts_cmd_register.c` 调用 `ts_cmd_xxx_register()`
4. 在 `include/ts_cmd_all.h` 添加函数声明

## 开发工作流

推荐使用 VS Code ESP-IDF 扩展的 GUI 操作，或命令行：

```bash
idf.py set-target esp32s3      # 设置目标芯片
idf.py menuconfig              # TianShanOS 选项在顶层菜单
idf.py build flash monitor     # 构建、烧录、串口监控
```

## 代码约定

```c
static const char *TAG = "模块名";  // 每个 .c 文件必须定义

// 错误处理 - 返回 esp_err_t，使用 esp_err_to_name() 打印
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
    return ret;
}
```

- 头文件：Doxygen 注释（`@brief`, `@param`, `@return`），**英文**
- 源文件：中文注释用于架构说明

## 组件结构模板

```
components/ts_xxx/
├── CMakeLists.txt           # REQUIRES/PRIV_REQUIRES 声明依赖
├── Kconfig                  # CONFIG_TS_XXX_* 配置选项
├── include/ts_xxx.h         # 公开 API
└── src/ts_xxx.c             # 实现
```

CMakeLists.txt 示例（参考 `ts_led/CMakeLists.txt`）：
```cmake
idf_component_register(
    SRCS "src/ts_xxx.c"
    INCLUDE_DIRS "include"
    REQUIRES ts_core ts_hal           # 公开依赖（头文件暴露）
    PRIV_REQUIRES ts_storage ts_event # 私有依赖（仅实现使用）
)
```

## 关键文件索引

| 用途 | 路径 |
|------|------|
| 服务注册入口 | `main/ts_services.c` |
| 事件系统 API | `components/ts_core/ts_event/include/ts_event.h` |
| 服务管理 API | `components/ts_core/ts_service/include/ts_service.h` |
| 统一 API 层 | `components/ts_api/include/ts_api.h` |
| CLI 命令实现 | `components/ts_console/commands/ts_cmd_*.c` |
| 命令注册汇总 | `components/ts_console/commands/ts_cmd_register.c` |
| 板级引脚配置 | `boards/rm01_esp32s3/pins.json` |
| 安全/SSH 模块 | `components/ts_security/include/ts_ssh_client.h` |
| 设备驱动框架 | `components/ts_drivers/` |
| 分区表 | `partitions.csv`（factory 3MB / storage SPIFFS / www WebUI）|

## 开发路线图（当前阶段）

| 优先级 | 任务 | 状态 | 关键文件 |
|--------|------|------|----------|
| 0 | SSH 客户端 & 安全功能 | 🚧 框架已有 | `ts_security/`, `ts_ssh_client.h` |
| 1 | device 完整功能（AGX/LPMU/监控） | 🚧 CLI 模拟 | `ts_cmd_device.c`, `ts_drivers/device/` |
| 2 | Core API 规范化 | 🚧 框架已有 | `ts_api/` |
| 3 | WebUI 开发 | ⏳ 待开始 | `ts_webui/` |
| 4 | 测试框架 | ⏳ 待开始 | `tests/` |

### Device 模块现状
- `ts_device_ctrl.c` - AGX 电源控制**已实现**（power_on/off/reset/force_off + GPIO 时序）
- `ts_cmd_device.c` - CLI 命令**模拟实现**，需接入 `ts_device_*` API
- LPMU 配置函数待补充（目前只有 `ts_device_configure_agx`）
- 设备监控待实现（电压/电流通过 ADC 或 UART PZEM 协议）

### Device 模块整合计划（源自 robOS）
需要将 robOS 中分散的功能整合到统一的 device 模块：

| robOS 组件 | 功能 | TianShanOS 目标 |
|-----------|------|----------------|
| `device_controller` | AGX/LPMU 电源控制 | `ts_device_ctrl` ✅ 已有框架 |
| `agx_monitor` | WebSocket 实时监控 | `ts_device_monitor` 待实现 |
| `power_monitor` | ADC/UART 电源监测 | `ts_power_monitor` 待实现 |
| `usb_mux_controller` | USB 切换 | `ts_usb_mux` 待实现 |

### AGX Monitor 规格（WebSocket）
- 协议：Socket.IO over WebSocket
- 服务器：`ws://<AGX_IP>:58090/socket.io/`
- 数据：CPU/内存/温度/功耗/GPU（JSON，1Hz）
- 启动延迟：45秒（等待 L4T/Ubuntu 启动）

### Core API 设计原则
- **唯一入口**：CLI / WebUI / HTTPS API 全部通过 `ts_api_call()` 调用
- **传输协议**：HTTPS RESTful + 加密 WebSocket
- **认证机制**：用于验证操作者是否有权限调用 Core API（Token/Session 待定）

## 命令参考

完整 CLI 命令文档见 `docs/COMMANDS.md`。常用命令：
- `system --info/--memory/--tasks/--reboot`
- `service --list/--status/--start/--stop --name <name>`
- `led --status/--effect/--image --device <touch|board|matrix>`
- `net --status/--set --mode static --ip x.x.x.x`
- `config --get/--set --key <key> --value <val> --persist`