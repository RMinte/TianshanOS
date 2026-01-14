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
