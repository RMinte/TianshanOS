# TianShanOS 架构设计文档

> **版本**：1.0  
> **状态**：已确定  
> **日期**：2026年1月15日

---

## 📋 设计决策总结

### 核心设计原则

基于与 Tom 的讨论，TianShanOS 确立以下核心设计原则：

1. **面向配置而非面向代码** - 构建一个几乎完全通过配置定义的嵌入式系统
2. **运行时灵活性** - 引脚配置、服务管理等均支持运行时加载和调整
3. **跨平台可移植** - 支持 ESP32S3 和 ESP32P4，架构设计确保移植无需重写
4. **安全优先** - mTLS 认证、加密通信、分级权限管理
5. **统一接口** - WebUI 和 CLI 共享 Core API，行为一致
6. **低耦合架构** - 组件间通过事件/消息机制通信，完全解耦

---

## 🏗️ 系统架构总览

### 分层架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           用户交互层 (UI Layer)                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │   CLI (esp_console) │  │  WebUI (mTLS认证)   │  │   SSH Client    │  │
│  └──────────┬──────────┘  └──────────┬──────────┘  └────────┬────────┘  │
│             │                        │                       │           │
│             └────────────────────────┼───────────────────────┘           │
│                                      ▼                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                           Core API 层                                    │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  统一的命令处理接口 / 权限验证 / 多语言支持 / 脚本引擎          │    │
│  └─────────────────────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────────────┤
│                           服务管理层 (Service Layer)                     │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ 服务注册器  │  │ 依赖管理器  │  │ 生命周期    │  │ 健康检查    │    │
│  │ (Registry)  │  │ (Deps)      │  │ (Lifecycle) │  │ (Health)    │    │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │
├─────────────────────────────────────────────────────────────────────────┤
│                           事件/消息总线 (Event Bus)                      │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  发布/订阅 │ 事件过滤 │ 优先级队列 │ 异步处理 │ 事务支持       │    │
│  └─────────────────────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────────────┤
│                           配置管理层 (Config Layer)                      │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ NVS 存储    │  │ 文件配置    │  │ 默认值      │  │ 变更通知    │    │
│  │ (持久化)    │  │ (SD/SPIFFS) │  │ (代码内)    │  │ (观察者)    │    │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │
├─────────────────────────────────────────────────────────────────────────┤
│                           硬件抽象层 (HAL)                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ GPIO 抽象   │  │ PWM 抽象    │  │ SPI/I2C     │  │ UART 抽象   │    │
│  │             │  │             │  │ 抽象        │  │             │    │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘    │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │              引脚映射层 (逻辑功能 → 物理引脚)                    │    │
│  │              冲突检测 │ 复用管理 │ 运行时配置                    │    │
│  └─────────────────────────────────────────────────────────────────┘    │
├─────────────────────────────────────────────────────────────────────────┤
│                           平台适配层 (Platform)                          │
│  ┌──────────────────────────────┐  ┌──────────────────────────────┐    │
│  │        ESP32S3 适配          │  │        ESP32P4 适配          │    │
│  │  (RMT/LEDC/外设特性)         │  │  (RMT/LEDC/外设特性)         │    │
│  └──────────────────────────────┘  └──────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 📦 组件架构设计

### 目录结构

```
TianShanOS/
├── components/                     # ESP-IDF 组件目录
│   │
│   ├── ts_core/                    # ═══ 核心框架 ═══
│   │   ├── include/
│   │   │   └── ts_core.h           # 核心头文件（统一入口）
│   │   ├── ts_config/              # 配置管理子模块
│   │   │   ├── include/
│   │   │   │   └── ts_config.h
│   │   │   ├── ts_config.c
│   │   │   ├── ts_config_nvs.c     # NVS 后端
│   │   │   ├── ts_config_file.c    # 文件后端
│   │   │   └── CMakeLists.txt
│   │   ├── ts_event/               # 事件系统子模块
│   │   │   ├── include/
│   │   │   │   └── ts_event.h
│   │   │   ├── ts_event.c
│   │   │   ├── ts_event_bus.c      # 事件总线
│   │   │   └── CMakeLists.txt
│   │   ├── ts_service/             # 服务管理子模块
│   │   │   ├── include/
│   │   │   │   └── ts_service.h
│   │   │   ├── ts_service.c
│   │   │   ├── ts_service_registry.c
│   │   │   ├── ts_service_lifecycle.c
│   │   │   └── CMakeLists.txt
│   │   ├── ts_log/                 # 日志系统子模块
│   │   │   ├── include/
│   │   │   │   └── ts_log.h
│   │   │   ├── ts_log.c
│   │   │   └── CMakeLists.txt
│   │   └── CMakeLists.txt
│   │
│   ├── ts_hal/                     # ═══ 硬件抽象层 ═══
│   │   ├── include/
│   │   │   ├── ts_hal.h            # HAL 统一入口
│   │   │   ├── ts_hal_gpio.h
│   │   │   ├── ts_hal_pwm.h
│   │   │   ├── ts_hal_i2c.h
│   │   │   ├── ts_hal_spi.h
│   │   │   ├── ts_hal_uart.h
│   │   │   └── ts_hal_adc.h
│   │   ├── ts_hal_gpio.c
│   │   ├── ts_hal_pwm.c
│   │   ├── ts_hal_i2c.c
│   │   ├── ts_hal_spi.c
│   │   ├── ts_hal_uart.c
│   │   ├── ts_hal_adc.c
│   │   ├── ts_pin_manager.c        # 引脚管理器（冲突检测）
│   │   ├── platform/               # 平台特定实现
│   │   │   ├── esp32s3/
│   │   │   │   ├── ts_platform_s3.c
│   │   │   │   └── ts_platform_s3.h
│   │   │   └── esp32p4/
│   │   │       ├── ts_platform_p4.c
│   │   │       └── ts_platform_p4.h
│   │   └── CMakeLists.txt
│   │
│   ├── ts_console/                 # ═══ 控制台系统 ═══
│   │   ├── include/
│   │   │   ├── ts_console.h
│   │   │   ├── ts_cmd_registry.h   # 命令注册
│   │   │   └── ts_script.h         # 脚本引擎
│   │   ├── ts_console.c            # 基于 esp_console
│   │   ├── ts_cmd_registry.c
│   │   ├── ts_cmd_parser.c         # 参数风格解析
│   │   ├── ts_script.c             # 简单脚本支持
│   │   ├── ts_i18n.c               # 多语言支持
│   │   ├── commands/               # 内置命令
│   │   │   ├── cmd_system.c
│   │   │   ├── cmd_config.c
│   │   │   ├── cmd_service.c
│   │   │   └── cmd_help.c
│   │   └── CMakeLists.txt
│   │
│   ├── ts_api/                     # ═══ Core API 层 ═══
│   │   ├── include/
│   │   │   ├── ts_api.h            # API 统一入口
│   │   │   ├── ts_api_system.h
│   │   │   ├── ts_api_device.h
│   │   │   ├── ts_api_led.h
│   │   │   ├── ts_api_network.h
│   │   │   └── ts_api_security.h
│   │   ├── ts_api_system.c
│   │   ├── ts_api_device.c
│   │   ├── ts_api_led.c
│   │   ├── ts_api_network.c
│   │   ├── ts_api_security.c
│   │   ├── ts_api_permission.c     # 权限检查
│   │   └── CMakeLists.txt
│   │
│   ├── ts_led/                     # ═══ LED 系统 ═══
│   │   ├── include/
│   │   │   ├── ts_led.h            # LED 统一入口
│   │   │   ├── ts_led_driver.h     # 驱动抽象
│   │   │   ├── ts_led_renderer.h   # 渲染器
│   │   │   ├── ts_led_layer.h      # 图层管理
│   │   │   ├── ts_led_animation.h  # 动画引擎
│   │   │   ├── ts_led_effect.h     # 特效库
│   │   │   └── ts_led_image.h      # 图像处理
│   │   ├── driver/
│   │   │   ├── ts_led_ws2812.c     # WS2812 驱动
│   │   │   └── ts_led_apa102.c     # APA102 驱动（预留）
│   │   ├── ts_led_renderer.c
│   │   ├── ts_led_layer.c
│   │   ├── ts_led_animation.c
│   │   ├── ts_led_effect.c
│   │   ├── effects/                # 内置特效
│   │   │   ├── effect_breathe.c
│   │   │   ├── effect_rainbow.c
│   │   │   ├── effect_fire.c
│   │   │   ├── effect_wave.c
│   │   │   └── effect_sparkle.c
│   │   ├── image/
│   │   │   ├── ts_image_bmp.c      # BMP 解码
│   │   │   ├── ts_image_png.c      # PNG 解码
│   │   │   ├── ts_image_gif.c      # GIF 解码（含动画）
│   │   │   └── ts_image_jpg.c      # JPG 解码
│   │   ├── devices/                # LED 设备实例
│   │   │   ├── led_touch.c         # 蓝宝石触摸灯
│   │   │   ├── led_board.c         # 主板环板灯
│   │   │   └── led_matrix.c        # 矩阵灯
│   │   └── CMakeLists.txt
│   │
│   ├── ts_net/                     # ═══ 网络系统 ═══
│   │   ├── include/
│   │   │   ├── ts_net.h
│   │   │   ├── ts_ethernet.h
│   │   │   ├── ts_wifi.h
│   │   │   ├── ts_http_server.h
│   │   │   └── ts_websocket.h
│   │   ├── ts_ethernet.c           # W5500 以太网
│   │   ├── ts_wifi.c               # WiFi 管理
│   │   ├── ts_http_server.c        # HTTP 服务器
│   │   ├── ts_websocket.c          # WebSocket
│   │   ├── ts_dhcp_server.c        # DHCP 服务器
│   │   └── CMakeLists.txt
│   │
│   ├── ts_security/                # ═══ 安全模块 ═══
│   │   ├── include/
│   │   │   ├── ts_security.h
│   │   │   ├── ts_crypto.h         # 加密功能
│   │   │   ├── ts_key_manager.h    # 密钥管理
│   │   │   ├── ts_ssh_client.h     # SSH 客户端
│   │   │   ├── ts_mtls.h           # mTLS 认证
│   │   │   └── ts_permission.h     # 权限管理
│   │   ├── ts_crypto.c
│   │   ├── ts_key_manager.c
│   │   ├── ts_ssh_client.c         # SSH 客户端（wolfSSH）
│   │   ├── ts_mtls.c
│   │   ├── ts_permission.c
│   │   └── CMakeLists.txt
│   │
│   ├── ts_webui/                   # ═══ WebUI ═══
│   │   ├── include/
│   │   │   └── ts_webui.h
│   │   ├── ts_webui.c
│   │   ├── ts_webui_api.c          # REST API 端点
│   │   ├── ts_webui_auth.c         # mTLS 认证集成
│   │   ├── www/                    # 静态资源（压缩）
│   │   │   ├── index.html.gz
│   │   │   ├── app.js.gz
│   │   │   └── style.css.gz
│   │   └── CMakeLists.txt
│   │
│   ├── ts_storage/                 # ═══ 存储管理 ═══
│   │   ├── include/
│   │   │   ├── ts_storage.h
│   │   │   └── ts_fs.h
│   │   ├── ts_storage.c
│   │   ├── ts_sdcard.c             # SD 卡（FAT32）
│   │   ├── ts_spiffs.c             # SPIFFS
│   │   └── CMakeLists.txt
│   │
│   └── ts_drivers/                 # ═══ 设备驱动 ═══
│       ├── fan/
│       │   ├── include/
│       │   │   └── ts_fan.h
│       │   ├── ts_fan.c
│       │   └── CMakeLists.txt
│       ├── power/
│       │   ├── include/
│       │   │   └── ts_power.h
│       │   ├── ts_power.c
│       │   └── CMakeLists.txt
│       ├── device/                 # AGX/LPMU 设备控制
│       │   ├── include/
│       │   │   ├── ts_agx.h
│       │   │   └── ts_lpmu.h
│       │   ├── ts_agx.c
│       │   ├── ts_lpmu.c
│       │   └── CMakeLists.txt
│       └── usb_mux/
│           ├── include/
│           │   └── ts_usb_mux.h
│           ├── ts_usb_mux.c
│           └── CMakeLists.txt
│
├── boards/                         # ═══ 板级配置 ═══
│   ├── rm01_esp32s3/
│   │   ├── board.json              # 板级配置文件
│   │   ├── pins.json               # 引脚映射
│   │   ├── services.json           # 默认服务配置
│   │   └── Kconfig.board           # 板级 Kconfig
│   └── rm01_esp32p4/
│       ├── board.json
│       ├── pins.json
│       ├── services.json
│       └── Kconfig.board
│
├── config/                         # ═══ 配置文件模板 ═══
│   ├── default_pins.json           # 默认引脚配置
│   ├── default_services.json       # 默认服务配置
│   └── default_led.json            # 默认 LED 配置
│
├── main/
│   ├── main.c                      # 入口点
│   └── CMakeLists.txt
│
├── docs/                           # 文档
│
├── tests/                          # 测试
│   ├── unit/                       # 单元测试
│   └── integration/                # 集成测试
│
├── tools/                          # 工具脚本
│   ├── gen_keys.py                 # 密钥生成工具
│   └── config_validator.py         # 配置验证工具
│
├── sdcard/                         # SD 卡内容模板
│   ├── config/
│   ├── images/
│   ├── animations/
│   └── scripts/
│
├── CMakeLists.txt
├── sdkconfig.defaults
└── partitions.csv
```

---

## 🔧 核心模块详细设计

### 1. 配置管理系统 (ts_config)

#### 设计目标
- 支持多配置源（NVS、文件、默认值）
- 运行时动态加载和修改
- 配置变更通知机制
- 配置验证和版本控制

#### 配置优先级（高→低）
1. 命令行/API 临时修改（内存中）
2. NVS 持久化配置
3. SD 卡配置文件
4. 代码内默认值

#### 配置类型
- **临时配置**：仅存在于内存，重启丢失
- **持久化配置**：存储于 NVS
- **外部配置**：从 SD 卡加载，可选择是否导入 NVS

#### 核心 API
```c
// 配置系统初始化
esp_err_t ts_config_init(void);

// 获取配置值
esp_err_t ts_config_get_int(const char *key, int *value);
esp_err_t ts_config_get_string(const char *key, char *buf, size_t len);
esp_err_t ts_config_get_blob(const char *key, void *buf, size_t *len);

// 设置配置值
esp_err_t ts_config_set_int(const char *key, int value, ts_config_scope_t scope);
esp_err_t ts_config_set_string(const char *key, const char *value, ts_config_scope_t scope);

// 配置监听
esp_err_t ts_config_subscribe(const char *key_pattern, ts_config_callback_t callback);

// 配置导入/导出
esp_err_t ts_config_import_file(const char *path);
esp_err_t ts_config_export_file(const char *path);

// 配置验证
esp_err_t ts_config_validate(const char *schema_path);
```

#### 配置作用域枚举
```c
typedef enum {
    TS_CONFIG_SCOPE_TEMP,       // 临时（内存）
    TS_CONFIG_SCOPE_PERSIST,    // 持久化（NVS）
    TS_CONFIG_SCOPE_EXTERNAL    // 外部文件
} ts_config_scope_t;
```

---

### 2. 服务管理系统 (ts_service)

#### 设计目标
- 服务注册和发现
- 依赖管理和启动顺序
- 生命周期管理
- 健康检查和故障恢复

#### 启动策略：阶段化 + 依赖图

```
启动阶段：
  Phase 0: Platform    [平台初始化、时钟、内存]
      ↓
  Phase 1: Core        [日志、配置、事件系统]
      ↓
  Phase 2: HAL         [硬件抽象层、引脚管理]
      ↓
  Phase 3: Drivers     [设备驱动：风扇、电源、LED驱动]
      ↓
  Phase 4: Network     [网络协议栈、以太网、WiFi]
      ↓
  Phase 5: Security    [密钥管理、mTLS]
      ↓
  Phase 6: Services    [业务服务：AGX监控、设备控制]
      ↓
  Phase 7: UI          [控制台、WebUI]

每个阶段内部使用依赖图确定顺序，无依赖的服务可并行启动
```

#### 服务定义结构
```c
typedef struct {
    const char *name;               // 服务名称
    const char *description;        // 服务描述
    ts_service_phase_t phase;       // 启动阶段
    const char **depends_on;        // 强依赖列表
    size_t depends_count;
    const char **optional_depends;  // 可选依赖列表
    size_t optional_depends_count;
    
    // 生命周期回调
    esp_err_t (*init)(void);        // 初始化
    esp_err_t (*start)(void);       // 启动
    esp_err_t (*stop)(void);        // 停止
    esp_err_t (*deinit)(void);      // 反初始化
    esp_err_t (*health_check)(void);// 健康检查
    
    // 配置
    bool auto_start;                // 自动启动
    ts_restart_policy_t restart_policy;  // 重启策略
    uint32_t restart_delay_ms;      // 重启延迟
    uint32_t health_check_interval; // 健康检查间隔
} ts_service_def_t;

typedef enum {
    TS_RESTART_NEVER,       // 不自动重启
    TS_RESTART_ON_FAILURE,  // 失败时重启
    TS_RESTART_ALWAYS       // 总是重启
} ts_restart_policy_t;
```

#### 服务配置文件示例 (services.json)
```json
{
  "services": [
    {
      "name": "fan_controller",
      "enabled": true,
      "phase": "drivers",
      "depends_on": ["ts_hal", "ts_config"],
      "optional_depends": ["agx_monitor"],
      "auto_start": true,
      "restart_policy": "on_failure",
      "config": {
        "default_mode": "auto",
        "check_interval_ms": 1000
      }
    },
    {
      "name": "led_matrix",
      "enabled": true,
      "phase": "drivers",
      "depends_on": ["ts_hal", "ts_config"],
      "auto_start": true,
      "config": {
        "brightness": 80,
        "default_animation": "idle"
      }
    }
  ]
}
```

#### 核心 API
```c
// 服务注册
esp_err_t ts_service_register(const ts_service_def_t *def);

// 服务控制
esp_err_t ts_service_start(const char *name);
esp_err_t ts_service_stop(const char *name);
esp_err_t ts_service_restart(const char *name);

// 服务查询
ts_service_state_t ts_service_get_state(const char *name);
esp_err_t ts_service_list(ts_service_info_t *list, size_t *count);

// 系统启动
esp_err_t ts_service_boot(void);  // 执行完整启动流程
```

---

### 3. 硬件抽象层 (ts_hal)

#### 设计目标
- 逻辑功能与物理引脚分离
- 运行时引脚配置加载
- 引脚冲突检测和管理
- 跨平台兼容（S3/P4）

#### 引脚管理器设计

```c
// 逻辑功能定义
typedef enum {
    // LED 系统
    TS_PIN_FUNC_LED_TOUCH,      // 触摸灯数据线
    TS_PIN_FUNC_LED_BOARD,      // 板载灯数据线
    TS_PIN_FUNC_LED_MATRIX,     // 矩阵灯数据线
    
    // 风扇系统
    TS_PIN_FUNC_FAN_PWM_0,      // 风扇0 PWM
    TS_PIN_FUNC_FAN_PWM_1,      // 风扇1 PWM
    TS_PIN_FUNC_FAN_TACH_0,     // 风扇0 转速（预留）
    
    // 以太网
    TS_PIN_FUNC_ETH_MISO,
    TS_PIN_FUNC_ETH_MOSI,
    TS_PIN_FUNC_ETH_SCLK,
    TS_PIN_FUNC_ETH_CS,
    TS_PIN_FUNC_ETH_INT,
    TS_PIN_FUNC_ETH_RST,
    
    // USB MUX
    TS_PIN_FUNC_USB_MUX_1,
    TS_PIN_FUNC_USB_MUX_2,
    
    // 设备控制
    TS_PIN_FUNC_AGX_POWER,
    TS_PIN_FUNC_LPMU_POWER,
    TS_PIN_FUNC_LPMU_RESET,
    
    // 电源监控
    TS_PIN_FUNC_POWER_ADC,
    TS_PIN_FUNC_POWER_UART_RX,
    
    // SD 卡
    TS_PIN_FUNC_SD_CMD,
    TS_PIN_FUNC_SD_CLK,
    TS_PIN_FUNC_SD_D0,
    TS_PIN_FUNC_SD_D1,
    TS_PIN_FUNC_SD_D2,
    TS_PIN_FUNC_SD_D3,
    
    TS_PIN_FUNC_MAX
} ts_pin_function_t;

// 引脚配置结构
typedef struct {
    ts_pin_function_t function;
    int gpio_num;
    const char *description;
} ts_pin_mapping_t;
```

#### 引脚配置文件示例 (pins.json)
```json
{
  "board": "rm01_esp32s3",
  "version": "1.0",
  "pins": {
    "LED_TOUCH": { "gpio": 45, "description": "蓝宝石触摸灯" },
    "LED_BOARD": { "gpio": 42, "description": "主板环板灯" },
    "LED_MATRIX": { "gpio": 9, "description": "矩阵灯" },
    "FAN_PWM_0": { "gpio": 41, "description": "风扇0 PWM" },
    "ETH_MISO": { "gpio": 13 },
    "ETH_MOSI": { "gpio": 11 },
    "ETH_SCLK": { "gpio": 12 },
    "ETH_CS": { "gpio": 10 },
    "ETH_INT": { "gpio": 38 },
    "ETH_RST": { "gpio": 39 },
    "USB_MUX_1": { "gpio": 8 },
    "USB_MUX_2": { "gpio": 48 },
    "AGX_POWER": { "gpio": 1 },
    "LPMU_POWER": { "gpio": 2 },
    "LPMU_RESET": { "gpio": 3 },
    "SD_CMD": { "gpio": 4 },
    "SD_CLK": { "gpio": 5 },
    "SD_D0": { "gpio": 6 },
    "SD_D1": { "gpio": 7 },
    "SD_D2": { "gpio": 15 },
    "SD_D3": { "gpio": 16 }
  }
}
```

#### 引脚管理器 API
```c
// 初始化引脚管理器
esp_err_t ts_pin_manager_init(void);

// 加载引脚配置
esp_err_t ts_pin_manager_load_config(const char *path);

// 获取引脚映射
int ts_pin_get_gpio(ts_pin_function_t function);

// 申请引脚使用权（冲突检测）
esp_err_t ts_pin_acquire(ts_pin_function_t function, const char *owner);

// 释放引脚
esp_err_t ts_pin_release(ts_pin_function_t function);

// 检查引脚是否可用
bool ts_pin_is_available(int gpio_num);

// 获取引脚状态
esp_err_t ts_pin_get_status(ts_pin_status_t *status, size_t *count);
```

#### GPIO 抽象 API
```c
// 创建 GPIO 实例
ts_gpio_handle_t ts_gpio_create(ts_pin_function_t function);

// 配置 GPIO
esp_err_t ts_gpio_configure(ts_gpio_handle_t handle, const ts_gpio_config_t *config);

// GPIO 操作
esp_err_t ts_gpio_set_level(ts_gpio_handle_t handle, int level);
int ts_gpio_get_level(ts_gpio_handle_t handle);

// 销毁 GPIO 实例
esp_err_t ts_gpio_destroy(ts_gpio_handle_t handle);
```

#### PWM 抽象 API
```c
// 创建 PWM 实例
ts_pwm_handle_t ts_pwm_create(ts_pin_function_t function);

// 配置 PWM
typedef struct {
    uint32_t frequency;
    uint8_t resolution_bits;
    bool invert;
} ts_pwm_config_t;

esp_err_t ts_pwm_configure(ts_pwm_handle_t handle, const ts_pwm_config_t *config);

// PWM 控制
esp_err_t ts_pwm_set_duty(ts_pwm_handle_t handle, float duty_percent);
esp_err_t ts_pwm_set_duty_raw(ts_pwm_handle_t handle, uint32_t duty);

// 销毁 PWM 实例
esp_err_t ts_pwm_destroy(ts_pwm_handle_t handle);
```

---

### 4. 事件系统 (ts_event)

#### 设计目标
- 低耦合的组件间通信
- 支持同步和异步事件
- 事件过滤和优先级
- 事务支持（可选回滚）

#### 核心 API
```c
// 事件定义
typedef struct {
    const char *category;       // 事件分类
    const char *name;           // 事件名称
    void *data;                 // 事件数据
    size_t data_len;            // 数据长度
    uint8_t priority;           // 优先级
    bool persistent;            // 是否持久化
} ts_event_t;

// 发布事件
esp_err_t ts_event_publish(const ts_event_t *event);
esp_err_t ts_event_publish_sync(const ts_event_t *event);  // 同步等待处理完成

// 订阅事件
typedef void (*ts_event_handler_t)(const ts_event_t *event, void *user_data);
esp_err_t ts_event_subscribe(const char *category, const char *name_pattern, 
                              ts_event_handler_t handler, void *user_data);

// 取消订阅
esp_err_t ts_event_unsubscribe(ts_event_handler_t handler);

// 事务支持
ts_transaction_t ts_event_begin_transaction(void);
esp_err_t ts_event_commit(ts_transaction_t txn);
esp_err_t ts_event_rollback(ts_transaction_t txn);
```

#### 预定义事件类别
```c
// 系统事件
#define TS_EVENT_SYSTEM_BOOT        "system.boot"
#define TS_EVENT_SYSTEM_SHUTDOWN    "system.shutdown"
#define TS_EVENT_SYSTEM_ERROR       "system.error"

// 服务事件
#define TS_EVENT_SERVICE_STARTED    "service.started"
#define TS_EVENT_SERVICE_STOPPED    "service.stopped"
#define TS_EVENT_SERVICE_FAILED     "service.failed"

// 配置事件
#define TS_EVENT_CONFIG_CHANGED     "config.changed"

// 设备事件
#define TS_EVENT_DEVICE_CONNECTED   "device.connected"
#define TS_EVENT_DEVICE_DISCONNECTED "device.disconnected"

// 网络事件
#define TS_EVENT_NET_CONNECTED      "net.connected"
#define TS_EVENT_NET_DISCONNECTED   "net.disconnected"
```

---

### 5. Core API 层 (ts_api)

#### 设计目标
- 统一的业务接口层
- CLI 和 WebUI 共享
- 权限验证集成
- 返回统一的结果格式

#### API 结果格式
```c
typedef struct {
    int code;                   // 状态码
    char message[128];          // 消息
    cJSON *data;                // 数据（JSON 格式）
} ts_api_result_t;

// 状态码定义
#define TS_API_OK               0
#define TS_API_ERR_INVALID_PARAM    -1
#define TS_API_ERR_NOT_FOUND        -2
#define TS_API_ERR_PERMISSION       -3
#define TS_API_ERR_INTERNAL         -4
#define TS_API_ERR_BUSY             -5
```

#### 权限级别
```c
typedef enum {
    TS_PERM_NONE = 0,       // 无权限
    TS_PERM_READ = 1,       // 只读（查看状态）
    TS_PERM_OPERATE = 2,    // 操作（控制设备）
    TS_PERM_ADMIN = 3       // 管理员（所有权限）
} ts_permission_level_t;
```

#### API 注册机制
```c
typedef struct {
    const char *name;           // API 名称
    const char *description;    // 描述
    ts_permission_level_t min_permission;  // 最低权限
    ts_api_handler_t handler;   // 处理函数
} ts_api_def_t;

typedef ts_api_result_t (*ts_api_handler_t)(const cJSON *params, ts_session_t *session);

// 注册 API
esp_err_t ts_api_register(const ts_api_def_t *def);

// 调用 API
ts_api_result_t ts_api_call(const char *name, const cJSON *params, ts_session_t *session);
```

---

### 6. 控制台系统 (ts_console)

#### 设计目标
- 基于 esp_console 实现
- 参数风格命令格式
- 多语言支持
- 脚本/批处理能力

#### 命令格式规范
```
<command> [options] [arguments]

选项格式：
  --option value      长选项
  -o value            短选项
  --flag              布尔标志

示例：
  fan --id 0 --speed 75
  led --device matrix --brightness 80
  config --get system.language
  config --set system.language --value zh-CN
  service --start fan_controller
  service --list
```

#### 多语言支持
```c
// 语言定义
typedef enum {
    TS_LANG_EN,     // English
    TS_LANG_ZH_CN,  // 简体中文
} ts_language_t;

// 设置语言
esp_err_t ts_i18n_set_language(ts_language_t lang);

// 获取翻译
const char *ts_i18n_get(const char *key);

// 字符串定义示例
// strings_en.json: { "cmd.fan.help": "Control fan speed and mode" }
// strings_zh.json: { "cmd.fan.help": "控制风扇转速和模式" }
```

#### 脚本支持
```c
// 执行脚本文件
esp_err_t ts_script_run_file(const char *path);

// 执行脚本字符串
esp_err_t ts_script_run(const char *script);

// 脚本语法示例
/*
# 注释
set fan --id 0 --speed 50
wait 1000
set fan --id 0 --speed 75
if ${temp} > 60
  set fan --id 0 --speed 100
endif
*/
```

---

### 7. LED 系统 (ts_led)

#### 分层架构

```
┌─────────────────────────────────────────────────────┐
│                   应用层 API                        │
│  ts_led_set_status()  ts_led_show_image()          │
│  ts_led_play_animation()  ts_led_set_effect()      │
├─────────────────────────────────────────────────────┤
│                   设备抽象层                        │
│  led_touch / led_board / led_matrix                │
│  (每个设备可独立配置和控制)                          │
├─────────────────────────────────────────────────────┤
│                   图层管理器                        │
│  Layer 0 (Base) ──────────────────┐                │
│  Layer 1 (Effect) ────────────────┼──▶ 混合输出    │
│  Layer 2 (Status) ────────────────┘                │
├─────────────────────────────────────────────────────┤
│                   动画引擎                          │
│  关键帧动画 / 缓动函数 / 动画队列 / 循环控制        │
├─────────────────────────────────────────────────────┤
│                   特效库                            │
│  breathe / rainbow / fire / wave / sparkle / ...   │
├─────────────────────────────────────────────────────┤
│                   渲染器                            │
│  像素缓冲区 / 颜色空间转换 / 伽马校正              │
├─────────────────────────────────────────────────────┤
│                   驱动抽象层                        │
│  WS2812 / APA102 / 通用 LED 接口                   │
└─────────────────────────────────────────────────────┘
```

#### 设备配置示例
```json
{
  "devices": [
    {
      "id": "touch",
      "name": "蓝宝石触摸灯",
      "driver": "ws2812",
      "pin": "LED_TOUCH",
      "count": 1,
      "layout": "single"
    },
    {
      "id": "board",
      "name": "主板环板灯",
      "driver": "ws2812",
      "pin": "LED_BOARD",
      "count": 28,
      "layout": "strip"
    },
    {
      "id": "matrix",
      "name": "矩阵灯",
      "driver": "ws2812",
      "pin": "LED_MATRIX",
      "count": 1024,
      "layout": "matrix",
      "width": 32,
      "height": 32
    }
  ]
}
```

#### 核心 API
```c
// 设备操作
ts_led_device_t ts_led_get_device(const char *device_id);
esp_err_t ts_led_set_pixel(ts_led_device_t dev, int index, ts_color_t color);
esp_err_t ts_led_fill(ts_led_device_t dev, ts_color_t color);
esp_err_t ts_led_show(ts_led_device_t dev);

// 图层操作
ts_led_layer_t ts_led_create_layer(ts_led_device_t dev, int z_order);
esp_err_t ts_led_layer_set_opacity(ts_led_layer_t layer, uint8_t opacity);
esp_err_t ts_led_layer_set_blend_mode(ts_led_layer_t layer, ts_blend_mode_t mode);

// 动画操作
esp_err_t ts_led_play_animation(ts_led_device_t dev, const char *anim_name);
esp_err_t ts_led_stop_animation(ts_led_device_t dev);
esp_err_t ts_led_load_animation(const char *path, const char *name);

// 特效操作
esp_err_t ts_led_set_effect(ts_led_device_t dev, ts_led_layer_t layer, 
                            const char *effect_name, const cJSON *params);

// 图像操作
esp_err_t ts_led_show_image(ts_led_device_t dev, const char *path);
esp_err_t ts_led_show_image_data(ts_led_device_t dev, const uint8_t *data, 
                                  size_t len, ts_image_format_t format);

// 状态指示
esp_err_t ts_led_bind_status(ts_led_device_t dev, const char *status_source,
                              const ts_led_status_mapping_t *mapping);
```

#### 状态指示映射
```c
typedef struct {
    const char *condition;      // 条件表达式
    ts_color_t color;           // 颜色
    const char *effect;         // 特效（可选）
} ts_led_status_rule_t;

typedef struct {
    ts_led_status_rule_t *rules;
    size_t rule_count;
} ts_led_status_mapping_t;

// 配置示例（JSON）
/*
{
  "status_binding": {
    "source": "agx.temperature",
    "rules": [
      { "condition": "value < 40", "color": "#00FF00" },
      { "condition": "value < 60", "color": "#FFFF00" },
      { "condition": "value < 80", "color": "#FFA500", "effect": "breathe" },
      { "condition": "value >= 80", "color": "#FF0000", "effect": "flash" }
    ]
  }
}
*/
```

---

### 8. 安全系统 (ts_security)

#### 设计目标
- SSH 客户端功能（登录远程主机执行命令）
- 密钥生成和安全存储
- mTLS 双向认证（WebUI）
- 加密通信（除状态信息外）

#### SSH 客户端实现
使用 **wolfSSH** 库实现 SSH2 客户端：

```c
// SSH 会话管理
typedef struct {
    const char *host;
    uint16_t port;
    const char *username;
    ts_ssh_auth_method_t auth_method;
} ts_ssh_config_t;

typedef enum {
    TS_SSH_AUTH_PRIVATEKEY,     // 私钥认证
    TS_SSH_AUTH_PASSWORD        // 密码认证（不推荐）
} ts_ssh_auth_method_t;

// 创建 SSH 会话
ts_ssh_session_t ts_ssh_connect(const ts_ssh_config_t *config);

// 执行远程命令
esp_err_t ts_ssh_exec(ts_ssh_session_t session, const char *command, 
                       char *output, size_t output_len);

// 从文件读取可执行命令列表
esp_err_t ts_ssh_load_allowed_commands(const char *path);

// 关闭会话
void ts_ssh_disconnect(ts_ssh_session_t session);
```

#### 密钥管理
```c
// 生成密钥对
esp_err_t ts_key_generate(ts_key_type_t type, ts_key_handle_t *handle);

typedef enum {
    TS_KEY_RSA_2048,
    TS_KEY_RSA_4096,
    TS_KEY_ECDSA_P256,
    TS_KEY_ED25519
} ts_key_type_t;

// 存储密钥（私钥存储在加密 NVS）
esp_err_t ts_key_store(ts_key_handle_t handle, const char *name);

// 导出公钥（OpenSSH 格式，可写入 SD 卡）
esp_err_t ts_key_export_public(ts_key_handle_t handle, char *buf, size_t len);

// 加载密钥
esp_err_t ts_key_load(const char *name, ts_key_handle_t *handle);
```

#### mTLS 认证
```c
// mTLS 配置
typedef struct {
    const char *server_cert;    // 服务器证书
    const char *server_key;     // 服务器私钥
    const char *ca_cert;        // CA 证书（验证客户端）
    bool require_client_cert;   // 是否要求客户端证书
} ts_mtls_config_t;

// 初始化 mTLS
esp_err_t ts_mtls_init(const ts_mtls_config_t *config);

// 验证客户端证书
esp_err_t ts_mtls_verify_client(const mbedtls_x509_crt *cert, 
                                 ts_permission_level_t *out_permission);
```

#### 权限管理
```c
// 会话管理
typedef struct {
    uint32_t session_id;
    ts_permission_level_t permission;
    ts_auth_source_t source;    // CLI / WebUI / SSH
    time_t created_at;
    time_t last_active;
} ts_session_t;

// CLI 默认管理员权限（本地串口）
// WebUI 需要 mTLS 认证，权限由证书确定
// 未认证的 WebUI 只能查看公开状态信息
```

---

## 🔄 技术选型最终确定

| 领域 | 选择 | 理由 |
|-----|------|------|
| **控制台框架** | esp_console | ESP-IDF 官方，稳定可靠 |
| **命令解析** | argtable3 | 与 esp_console 配套，功能完整 |
| **事件系统** | 自定义增强 | 基于 esp_event 增强，支持事务 |
| **HTTP服务器** | esp_http_server | ESP-IDF 官方，支持 HTTPS |
| **JSON解析** | cJSON | 已使用，轻量级 |
| **SSH客户端** | wolfSSH | 轻量级，专为嵌入式设计 |
| **TLS** | mbedTLS | ESP-IDF 内置 |
| **图像解码** | stb_image + gifdec | stb_image 支持 BMP/PNG/JPG，gifdec 支持 GIF |
| **外部文件系统** | FAT32 | SD 卡通用格式 |
| **内部文件系统** | SPIFFS | 适合存储配置和小文件 |

---

## 📅 开发计划

### Phase 0: 规划与设计 ✅ （当前阶段）
- [x] 问题分析
- [x] 需求确认
- [x] 架构设计
- [ ] API 详细设计
- [ ] 命令规范文档

### Phase 1: 基础架构（预计 2 周）
- [ ] 项目结构搭建
- [ ] ts_core/ts_config 配置管理
- [ ] ts_core/ts_log 日志系统
- [ ] ts_core/ts_event 事件系统
- [ ] ts_core/ts_service 服务管理

### Phase 2: 硬件抽象层（预计 2 周）
- [ ] ts_hal 引脚管理器
- [ ] ts_hal GPIO/PWM/SPI/I2C/UART/ADC 抽象
- [ ] 平台适配层（ESP32S3）
- [ ] 板级配置文件

### Phase 3: 核心服务（预计 2 周）
- [ ] ts_console 控制台系统
- [ ] ts_api Core API 层
- [ ] ts_storage 存储管理
- [ ] 基础命令实现

### Phase 4: LED 系统（预计 2 周）
- [ ] ts_led 驱动层
- [ ] ts_led 渲染器和图层管理
- [ ] ts_led 动画引擎和特效库
- [ ] ts_led 图像解码（BMP/PNG/GIF/JPG）
- [ ] LED 设备实例和命令

### Phase 5: 设备驱动（预计 1 周）
- [ ] 风扇控制器
- [ ] 电源监控
- [ ] AGX/LPMU 设备控制
- [ ] USB MUX

### Phase 6: 网络与安全（预计 2 周）
- [ ] ts_net 以太网/WiFi
- [ ] ts_security SSH 客户端
- [ ] ts_security 密钥管理
- [ ] ts_security mTLS

### Phase 7: WebUI（预计 1 周）
- [ ] ts_webui HTTP/WebSocket 服务器
- [ ] ts_webui API 端点
- [ ] 前端界面

### Phase 8: 集成测试与优化（预计 1 周）
- [ ] 集成测试
- [ ] 性能优化
- [ ] 文档完善

**预计总周期：13 周**

---

## 📝 下一步行动

1. **完善 API 详细设计文档**
2. **制定命令规范文档**
3. **搭建项目基础结构**
4. **开始 Phase 1 开发**

---

**文档版本**：1.0  
**状态**：已确定  
**作者**：GitHub Copilot  
**审核**：Tom
