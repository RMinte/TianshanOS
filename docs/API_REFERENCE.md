# TianShanOS API 参考文档

## 概述

TianShanOS 提供统一的 API 接口，支持通过控制台命令或 REST API 调用。

## 认证

### 登录
```
POST /api/v1/auth/login
Content-Type: application/json

{
  "username": "admin",
  "password": "tianshan"
}

Response:
{
  "token": "...",
  "session_id": 12345
}
```

### 登出
```
POST /api/v1/auth/logout
Authorization: Bearer <token>

Response:
{
  "success": true
}
```

## 系统 API

### system.info
获取系统信息。

**请求**: `GET /api/v1/system/info`

**响应**:
```json
{
  "code": 0,
  "message": "success",
  "data": {
    "chip": "ESP32S3",
    "version": "0.1.0",
    "uptime_ms": 123456,
    "free_heap": 200000,
    "mac": "AA:BB:CC:DD:EE:FF"
  }
}
```

### system.memory
获取内存信息。

**请求**: `GET /api/v1/system/memory`

**响应**:
```json
{
  "code": 0,
  "data": {
    "total_heap": 327680,
    "free_heap": 200000,
    "min_free_heap": 180000,
    "largest_block": 65536
  }
}
```

### system.tasks
获取任务列表。

**请求**: `GET /api/v1/system/tasks`

**响应**:
```json
{
  "code": 0,
  "data": {
    "tasks": [
      {"name": "main", "state": "Running", "priority": 5, "stack_free": 2048},
      {"name": "IDLE", "state": "Ready", "priority": 0, "stack_free": 1024}
    ]
  }
}
```

### system.reboot
重启系统。

**请求**: `POST /api/v1/system/reboot`

**响应**:
```json
{
  "code": 0,
  "message": "Rebooting..."
}
```

### system.log.level
设置日志级别。

**请求**:
```
POST /api/v1/system/log/level
{
  "level": "debug"
}
```

**响应**:
```json
{
  "code": 0,
  "message": "Log level set to debug"
}
```

## 配置 API

### config.get
获取配置值。

**请求**: `GET /api/v1/config/get?key=wifi.ssid`

**响应**:
```json
{
  "code": 0,
  "data": {
    "key": "wifi.ssid",
    "value": "MyNetwork"
  }
}
```

### config.set
设置配置值。

**请求**:
```
POST /api/v1/config/set
{
  "key": "wifi.ssid",
  "value": "NewNetwork"
}
```

**响应**:
```json
{
  "code": 0,
  "message": "Configuration saved"
}
```

### config.delete
删除配置项。

**请求**:
```
POST /api/v1/config/delete
{
  "key": "wifi.ssid"
}
```

### config.list
列出配置项。

**请求**: `GET /api/v1/config/list?prefix=wifi`

**响应**:
```json
{
  "code": 0,
  "data": {
    "items": [
      {"key": "wifi.ssid", "value": "MyNetwork"},
      {"key": "wifi.password", "value": "***"}
    ]
  }
}
```

### config.save
保存配置到存储。

**请求**: `POST /api/v1/config/save`

## 变量 API

变量系统用于存储和管理动态数据，主要应用场景：
- SSH 命令执行结果存储
- 自动化引擎数据源
- 命令参数动态替换

### var.get
获取变量值。

**请求**: `GET /api/v1/var/get?name=cpu_temp.extracted`

**响应**:
```json
{
  "code": 0,
  "data": {
    "name": "cpu_temp.extracted",
    "value": "45000",
    "type": "string",
    "source": "ssh",
    "timestamp": 1706355600000,
    "persistent": false
  }
}
```

**错误响应** (变量不存在):
```json
{
  "code": 404,
  "message": "Variable not found: cpu_temp.extracted"
}
```

### var.set
设置变量值。

**请求**:
```
POST /api/v1/var/set
{
  "name": "my_variable",
  "value": "hello world",
  "persistent": false
}
```

**参数**:
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 变量名（最大 64 字符） |
| `value` | string | 是 | 变量值（最大 256 字符） |
| `persistent` | bool | 否 | 是否持久化到 NVS（默认 false） |

**响应**:
```json
{
  "code": 0,
  "message": "Variable set successfully"
}
```

### var.delete
删除变量。

**请求**:
```
POST /api/v1/var/delete
{
  "name": "my_variable"
}
```

**响应**:
```json
{
  "code": 0,
  "message": "Variable deleted"
}
```

### var.list
列出所有变量或指定前缀的变量。

**请求**: `GET /api/v1/var/list` 或 `GET /api/v1/var/list?prefix=cpu_temp`

**响应**:
```json
{
  "code": 0,
  "data": {
    "count": 3,
    "variables": [
      {
        "name": "cpu_temp.extracted",
        "value": "45000",
        "type": "string",
        "source": "ssh",
        "timestamp": 1706355600000,
        "persistent": false
      },
      {
        "name": "cpu_temp.status",
        "value": "success",
        "type": "string",
        "source": "ssh",
        "timestamp": 1706355600000,
        "persistent": false
      },
      {
        "name": "cpu_temp.exit_code",
        "value": "0",
        "type": "number",
        "source": "ssh",
        "timestamp": 1706355600000,
        "persistent": false
      }
    ]
  }
}
```

### 变量类型

| 类型 | 说明 | 示例 |
|------|------|------|
| `string` | 字符串（默认） | `"hello"` |
| `number` | 数值（存储为字符串） | `"45000"` |
| `bool` | 布尔值 | `"true"`, `"false"` |

### 变量来源

| 来源 | 说明 |
|------|------|
| `user` | 用户通过 API 或 CLI 设置 |
| `ssh` | SSH 命令执行结果自动生成 |
| `system` | 系统内置变量（如 `sys.time`） |
| `alias` | 指向其他变量的别名 |

### SSH 结果自动变量

当 SSH 命令执行时指定 `var_name` 参数，系统自动创建以下变量：

| 变量名 | 说明 |
|--------|------|
| `${var_name}.extracted` | 从输出中提取的值 |
| `${var_name}.status` | 执行状态（success/error/timeout） |
| `${var_name}.exit_code` | 命令退出码 |
| `${var_name}.host` | 执行主机 |
| `${var_name}.timestamp` | 执行时间戳 |
| `${last}.extracted` | 最后一次执行的提取值 |
| `${last}.*` | 最后一次执行的所有属性 |

### 变量替换语法

在支持变量替换的场景中，使用 `${变量名}` 语法：

```
// 示例命令
echo "CPU temperature: ${cpu_temp.extracted}"

// 替换后
echo "CPU temperature: 45000"
```

## LED API

### led.devices
获取 LED 设备列表。

**请求**: `GET /api/v1/led/devices`

**响应**:
```json
{
  "code": 0,
  "data": {
    "devices": [
      {"name": "touch", "count": 16, "brightness": 80},
      {"name": "board", "count": 32, "brightness": 60}
    ]
  }
}
```

### led.brightness
设置亮度。

**请求**:
```
POST /api/v1/led/brightness
{
  "device": "touch",
  "brightness": 100
}
```

### led.color
设置颜色。

**请求**:
```
POST /api/v1/led/color
{
  "device": "touch",
  "color": "#FF0000"
}
```

### led.effect
设置效果。

**请求**:
```
POST /api/v1/led/effect
{
  "device": "touch",
  "effect": "rainbow"
}
```

可用效果: `rainbow`, `breathing`, `chase`, `fire`, `sparkle`, `solid`

## 网络 API

### network.status
获取网络状态。

**请求**: `GET /api/v1/network/status`

**响应**:
```json
{
  "code": 0,
  "data": {
    "eth_connected": true,
    "wifi_connected": false,
    "ip": "192.168.1.100",
    "gateway": "192.168.1.1",
    "dns": "8.8.8.8"
  }
}
```

### network.wifi.scan
扫描 WiFi 网络。

**请求**: `GET /api/v1/network/wifi/scan`

**响应**:
```json
{
  "code": 0,
  "data": {
    "networks": [
      {"ssid": "Network1", "rssi": -45, "channel": 6, "auth": "WPA2"},
      {"ssid": "Network2", "rssi": -70, "channel": 11, "auth": "OPEN"}
    ]
  }
}
```

### network.wifi.connect
连接 WiFi。

**请求**:
```
POST /api/v1/network/wifi/connect
{
  "ssid": "MyNetwork",
  "password": "password123"
}
```

## 设备 API

### device.status
获取设备状态。

**请求**: `GET /api/v1/device/status`

**响应**:
```json
{
  "code": 0,
  "data": {
    "agx_powered": true,
    "agx_uptime_ms": 3600000,
    "power_good": true
  }
}
```

### device.power
控制设备电源。

**请求**:
```
POST /api/v1/device/power
{
  "device": "agx",
  "on": true
}
```

### device.fan.status
获取风扇状态。

**请求**: `GET /api/v1/device/fan/status`

**响应**:
```json
{
  "code": 0,
  "data": {
    "fans": [
      {"id": 0, "running": true, "duty": 50, "rpm": 2500},
      {"id": 1, "running": true, "duty": 60, "rpm": 2800}
    ]
  }
}
```

### device.fan.speed
设置风扇速度。

**请求**:
```
POST /api/v1/device/fan/speed
{
  "fan": 0,
  "speed": 80
}
```

## 风扇控制 API

### fan.status
获取风扇状态。

**请求**: `POST /api/v1/fan.status`

**参数**:
```json
{}                    // 获取所有风扇
{"id": 0}            // 获取指定风扇
```

**响应**:
```json
{
  "code": 0,
  "data": {
    "fans": [
      {
        "id": 0,
        "mode": "curve",
        "duty": 30,
        "target_duty": 25,
        "rpm": 0,
        "temperature": 35.5,
        "enabled": true,
        "running": true,
        "fault": false
      }
    ],
    "temperature": 35.5,
    "temp_valid": true,
    "temp_source": "agx.cpu_temp"
  }
}
```

### fan.mode
设置风扇工作模式。

**请求**:
```json
POST /api/v1/fan.mode
{
  "id": 0,
  "mode": "curve"    // "off", "manual", "auto", "curve"
}
```

### fan.set
设置风扇速度（手动模式）。

**请求**:
```json
POST /api/v1/fan.set
{
  "id": 0,
  "duty": 75
}
```

### fan.curve
设置温度-转速曲线。

**请求**:
```json
POST /api/v1/fan.curve
{
  "id": 0,
  "curve": [
    {"temp": 30, "duty": 10},
    {"temp": 50, "duty": 40},
    {"temp": 70, "duty": 80}
  ],
  "hysteresis": 3.0,
  "min_interval": 2000
}
```

**参数说明**:
- `curve`: 温度-占空比曲线点（温度°C，占空比 0-100%）
- `hysteresis`: 温度迟滞，防止频繁调速（°C）
- `min_interval`: 最小调速间隔（毫秒）

### fan.limits
设置风扇占空比限制。

**请求**:
```json
POST /api/v1/fan.limits
{
  "id": 0,
  "min_duty": 10,
  "max_duty": 100
}
```

**响应**:
```json
{
  "code": 0,
  "data": {
    "id": 0,
    "min_duty": 10,
    "max_duty": 100
  }
}
```

### fan.config
获取风扇完整配置。

**请求**:
```json
POST /api/v1/fan.config
{
  "id": 0
}
```

**响应**:
```json
{
  "code": 0,
  "data": {
    "id": 0,
    "mode": "curve",
    "min_duty": 10,
    "max_duty": 100,
    "hysteresis": 3.0,
    "min_interval": 2000,
    "invert_pwm": false,
    "curve": [
      {"temp": 30, "duty": 10},
      {"temp": 50, "duty": 40},
      {"temp": 70, "duty": 80}
    ]
  }
}
```

### fan.save
保存风扇配置到 NVS。

**请求**:
```json
POST /api/v1/fan.save
{"id": 0}   // 保存指定风扇
{}          // 保存所有风扇
```

## 温度源 API

### temp.status
获取温度状态。

**请求**: `POST /api/v1/temp.status`

**响应**:
```json
{
  "code": 0,
  "data": {
    "current_temp": 35.5,
    "preferred_source": "variable",
    "active_source": "variable",
    "bound_variable": "agx.cpu_temp",
    "manual_mode": false
  }
}
```

### temp.bind
绑定温度变量。

**请求**:
```json
POST /api/v1/temp.bind
{
  "variable": "agx.cpu_temp"   // 绑定变量
}

// 解除绑定
POST /api/v1/temp.bind
{
  "variable": null
}
```

### temp.select
选择温度源。

**请求**:
```json
POST /api/v1/temp.select
{
  "source": "variable"   // "default", "sensor", "agx", "variable"
}
```

### temp.manual
设置测试温度（手动模式）。

**请求**:
```json
POST /api/v1/temp.manual
{
  "enable": true,
  "temperature": 45.0    // 测试温度 (°C)
}

// 禁用手动模式
POST /api/v1/temp.manual
{
  "enable": false
}
```

## 存储 API

### storage.status
获取存储状态。

**请求**: `GET /api/v1/storage/status`

**响应**:
```json
{
  "code": 0,
  "data": {
    "spiffs": {
      "mounted": true,
      "total": 1048576,
      "used": 524288,
      "free": 524288
    },
    "sd": {
      "mounted": true,
      "total": 32212254720,
      "used": 1073741824,
      "free": 31138512896
    }
  }
}
```

### storage.list
列出目录内容。

**请求**: `GET /api/v1/storage/list?path=/sdcard`

**响应**:
```json
{
  "code": 0,
  "data": {
    "path": "/sdcard",
    "entries": [
      {"name": "config", "type": "dir"},
      {"name": "test.txt", "type": "file", "size": 1024}
    ]
  }
}
```

**错误响应**（SD 卡未挂载）:
```json
{
  "code": 3,
  "message": "SD card not mounted"
}
```

### storage.mount
挂载 SD 卡。

**请求**: `POST /api/v1/storage/mount`

**响应**:
```json
{
  "code": 0,
  "message": "SD card mounted successfully"
}
```

### storage.unmount
卸载 SD 卡。

**请求**: `POST /api/v1/storage/unmount`

**响应**:
```json
{
  "code": 0,
  "message": "SD card unmounted successfully"
}
```

### storage.delete
删除文件或目录。

**请求**:
```
POST /api/v1/storage/delete
{
  "path": "/sdcard/test.txt"
}
```

### storage.mkdir
创建目录。

**请求**:
```
POST /api/v1/storage/mkdir
{
  "path": "/sdcard/newfolder"
}
```

### storage.rename
重命名文件或目录。

**请求**:
```
POST /api/v1/storage/rename
{
  "old_path": "/sdcard/old.txt",
  "new_path": "/sdcard/new.txt"
}
```

### storage.upload
上传文件（multipart/form-data）。

**请求**: `POST /api/v1/storage/upload?path=/sdcard`

### storage.download
下载文件。

**请求**: `GET /api/v1/storage/download?path=/sdcard/file.txt`

## WebSocket API

连接 `ws://<device-ip>/ws` 进行实时通信。

### 订阅消息
```json
{"type": "subscribe"}
```

### 心跳
```json
{"type": "ping"}
```

响应:
```json
{"type": "pong"}
```

### 事件推送
服务器主动推送:
```json
{
  "type": "event",
  "event": "device.power_on",
  "data": {"device": "agx"}
}
```

### 日志订阅
订阅实时日志流：
```json
{"type": "log_subscribe", "minLevel": 3}
```
- `minLevel`: 最小日志级别（1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE）

订阅确认响应：
```json
{"type": "log_subscribed", "minLevel": 3}
```

### 取消日志订阅
```json
{"type": "log_unsubscribe"}
```

### 更新日志级别过滤
```json
{"type": "log_set_level", "minLevel": 2}
```

### 获取历史日志
```json
{"type": "log_get_history", "limit": 500, "minLevel": 1, "maxLevel": 5}
```

历史日志响应：
```json
{
  "type": "log_history",
  "logs": [
    {
      "timestamp": 12345678,
      "level": 3,
      "levelName": "INFO",
      "tag": "main",
      "message": "System started",
      "task": "main"
    }
  ],
  "total": 150
}
```

### 实时日志推送
服务器主动推送日志：
```json
{
  "type": "log",
  "timestamp": 12345678,
  "level": 3,
  "levelName": "INFO",
  "tag": "ts_net",
  "message": "Ethernet connected",
  "task": "net_task"
}
```

## 日志 API

### log.list
获取日志列表（带过滤）。

**请求**: `GET /api/v1/log/list?limit=50&minLevel=1&maxLevel=5`

**参数**:
| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| offset | number | 0 | 起始偏移 |
| limit | number | 50 | 返回数量（最大200） |
| minLevel | number | 1 | 最小级别 |
| maxLevel | number | 5 | 最大级别 |
| tag | string | - | TAG 过滤（子字符串匹配） |
| keyword | string | - | 关键字搜索 |

**响应**:
```json
{
  "code": 0,
  "data": {
    "logs": [
      {
        "timestamp": 12345678,
        "level": 3,
        "levelName": "INFO",
        "tag": "main",
        "message": "System started",
        "task": "main"
      }
    ],
    "total": 150,
    "offset": 0,
    "returned": 50,
    "bufferCapacity": 1000,
    "bufferCount": 150
  }
}
```

### log.stats
获取日志统计信息。

**请求**: `GET /api/v1/log/stats`

**响应**:
```json
{
  "code": 0,
  "data": {
    "bufferCapacity": 1000,
    "bufferCount": 150,
    "totalCaptured": 1523,
    "dropped": 0,
    "espLogCaptureEnabled": true,
    "currentLevel": 3,
    "currentLevelName": "INFO"
  }
}
```

### log.clear
清空日志缓冲区。

**请求**: `POST /api/v1/log/clear`

**响应**:
```json
{
  "code": 0,
  "data": {
    "success": true,
    "message": "Log buffer cleared"
  }
}
```

### log.setLevel
设置日志级别。

**请求**:
```
POST /api/v1/log/setLevel
{
  "level": "debug",
  "tag": "ts_net"
}
```

**参数**:
- `level`: 日志级别（0-5 或字符串 "error"/"warn"/"info"/"debug"/"verbose"）
- `tag`: 可选，针对特定 TAG 设置级别

**响应**:
```json
{
  "code": 0,
  "data": {
    "success": true,
    "level": "DEBUG",
    "tag": "ts_net"
  }
}
```

### log.capture
控制 ESP_LOG 捕获。

**请求**:
```
POST /api/v1/log/capture
{
  "enable": true
}
```

**响应**:
```json
{
  "code": 0,
  "data": {
    "success": true,
    "captureEnabled": true
  }
}
```

## OTA API

### ota.status
获取 OTA 状态。

**请求**: `GET /api/v1/ota/status`

**响应**:
```json
{
  "code": 0,
  "data": {
    "state": "idle",
    "running_partition": "ota_0",
    "next_partition": "ota_1",
    "pending_verify": false,
    "rollback_possible": true
  }
}
```

### ota.progress
获取升级进度。

**请求**: `GET /api/v1/ota/progress`

**响应**:
```json
{
  "code": 0,
  "data": {
    "state": "downloading",
    "percent": 45,
    "received": 921600,
    "total": 2048000,
    "message": "Downloading firmware..."
  }
}
```

### ota.version
获取固件版本信息。

**请求**: `GET /api/v1/ota/version`

**响应**:
```json
{
  "code": 0,
  "data": {
    "version": "0.2.0",
    "idf_version": "v5.5.2",
    "compile_time": "Jan 23 2026 12:00:00"
  }
}
```

### ota.https.start
从 HTTPS URL 启动 OTA。

**请求**:
```
POST /api/v1/ota/https/start
{
  "url": "https://server/TianShanOS.bin",
  "auto_reboot": true,
  "allow_downgrade": false,
  "skip_verify": false,
  "www_url": "https://server/www.bin"
}
```

**响应**:
```json
{
  "code": 0,
  "message": "OTA started"
}
```

### ota.sdcard.start
从 SD 卡启动 OTA。

**请求**:
```
POST /api/v1/ota/sdcard/start
{
  "path": "/sdcard/TianShanOS.bin",
  "auto_reboot": true
}
```

### ota.www.start
从 HTTPS URL 升级 www 分区。

**请求**:
```
POST /api/v1/ota/www/start
{
  "url": "https://server/www.bin"
}
```

### ota.www.start_sdcard
从 SD 卡升级 www 分区。

**请求**:
```
POST /api/v1/ota/www/start_sdcard
{
  "path": "/sdcard/www.bin"
}
```

**响应**:
```json
{
  "code": 0,
  "message": "WWW OTA started from SD card"
}
```

### ota.validate
标记固件有效（取消回滚）。

**请求**: `POST /api/v1/ota/validate`

**响应**:
```json
{
  "code": 0,
  "message": "Firmware validated, rollback cancelled"
}
```

### ota.rollback
回滚到上一版本。

**请求**: `POST /api/v1/ota/rollback`

**响应**:
```json
{
  "code": 0,
  "message": "Rolling back to previous firmware..."
}
```

### ota.abort
中止升级。

**请求**: `POST /api/v1/ota/abort`

## UI 配置 API

### ui.widgets.get
获取 WebUI 数据监控组件配置。

**请求**: `POST /api/v1/call`
```json
{
  "method": "ui.widgets.get"
}
```

**响应**:
```json
{
  "code": 0,
  "data": {
    "widgets": [
      {"id": "w1", "type": "power", "title": "总功耗", "config": {...}},
      {"id": "w2", "type": "temp", "title": "CPU温度", "config": {...}}
    ],
    "refresh_interval": 5000,
    "source": "sdcard"
  }
}
```

**source 字段说明**:
- `"sdcard"`: 从 SD 卡加载
- `"nvs"`: 从 NVS 加载（并已同步到 SD 卡）
- `"default"`: 使用默认空配置

### ui.widgets.set
保存 WebUI 数据监控组件配置（双写 SD 卡 + NVS）。

**请求**: `POST /api/v1/call`
```json
{
  "method": "ui.widgets.set",
  "params": {
    "widgets": [
      {"id": "w1", "type": "power", "title": "总功耗", "config": {...}}
    ],
    "refresh_interval": 5000
  }
}
```

**响应**:
```json
{
  "code": 0,
  "data": {
    "sdcard_saved": true,
    "nvs_saved": true
  }
}
```

**存储位置**:
- SD 卡: `/sdcard/config/ui_widgets.json`
- NVS: namespace `ts_ui`, key `widgets`

## 错误码

| 代码 | 含义 |
|------|------|
| 0 | 成功 |
| 1 | 通用错误 |
| 2 | 无效参数 |
| 3 | 未找到 |
| 4 | 未授权 |
| 5 | 权限不足 |
| 6 | 超时 |
| 7 | 状态错误 |

## HTTP 状态码

| 状态码 | 含义 |
|--------|------|
| 200 | 成功 |
| 201 | 已创建 |
| 400 | 请求错误 |
| 401 | 未授权 |
| 403 | 禁止访问 |
| 404 | 未找到 |
| 500 | 服务器错误 |
