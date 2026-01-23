# WebSocket 迁移计划

## 📊 HTTP 轮询审计结果

### 发现的轮询端点

| 页面 | 轮询函数 | 间隔 | API 端点 | 数据类型 |
|------|---------|------|----------|----------|
| System | `refreshSystemPage` | 3s | `system.info`, `time.info` | 系统信息、内存、时间同步 |
| Device | `refreshDevicePage` | 2s | `device.status` | AGX/LPMU 状态 |
| OTA | `refreshOtaProgress` | 1s | `ota.progress` | OTA 进度百分比 |
| Reboot Check | `rebootCheckInterval` | 3s | `system.info` | 重启后连接检测 |

### 优先级评估

| 优先级 | 端点 | 原因 |
|--------|------|------|
| 🔴 HIGH | Device Monitor | 高频更新（2s），实时性要求高 |
| 🟡 MEDIUM | OTA Progress | 仅升级时使用，1s 间隔合理 |
| 🟢 LOW | System Info | 低频更新（3s），实时性要求低 |

## 🏗️ 架构设计

### 后端 - 事件订阅机制

```c
// components/ts_core/ts_event/include/ts_event.h
// 已有事件类型（无需修改）
ESP_EVENT_DECLARE_BASE(TS_EVENT_BASE_SYSTEM);
ESP_EVENT_DECLARE_BASE(TS_EVENT_BASE_DEVICE);
ESP_EVENT_DECLARE_BASE(TS_EVENT_BASE_OTA);

// 事件 ID 定义
typedef enum {
    TS_EVENT_SYSTEM_INFO_CHANGED,   // 系统信息变更
    TS_EVENT_SYSTEM_TIME_SYNC,      // 时间同步完成
    TS_EVENT_SYSTEM_MEMORY_LOW,     // 内存低警告
} ts_event_system_t;

typedef enum {
    TS_EVENT_DEVICE_STATUS_CHANGED, // 设备状态变更（AGX/LPMU）
} ts_event_device_t;

typedef enum {
    TS_EVENT_OTA_PROGRESS_UPDATE,   // OTA 进度更新
    TS_EVENT_OTA_COMPLETED,         // OTA 完成
    TS_EVENT_OTA_FAILED,            // OTA 失败
} ts_event_ota_t;
```

### WebSocket 消息格式

#### 订阅请求 (Client → Server)

```json
{
  "type": "subscribe",
  "topic": "device.status",  // 支持: system.info, device.status, ota.progress
  "params": {
    "interval": 2000  // 可选：最小推送间隔（ms）
  }
}
```

#### 取消订阅 (Client → Server)

```json
{
  "type": "unsubscribe",
  "topic": "device.status"
}
```

#### 数据推送 (Server → Client)

```json
{
  "type": "data",
  "topic": "device.status",
  "data": {
    "devices": [
      {"name": "agx", "power": true, "ready": false},
      {"name": "lpmu", "power": false}
    ]
  },
  "timestamp": 1735920000
}
```

## 📝 实施步骤

### Phase 1: 后端 - 订阅管理器 (HIGH PRIORITY)

**文件**: `components/ts_webui/src/ts_ws_subscriptions.c`

```c
typedef struct {
    const char *topic;
    esp_event_base_t event_base;
    int32_t event_id;
    uint32_t min_interval_ms;  // 去抖动
} ts_ws_topic_mapping_t;

// 订阅映射表
static const ts_ws_topic_mapping_t s_topic_map[] = {
    {"system.info",    TS_EVENT_BASE_SYSTEM, TS_EVENT_SYSTEM_INFO_CHANGED, 3000},
    {"device.status",  TS_EVENT_BASE_DEVICE, TS_EVENT_DEVICE_STATUS_CHANGED, 2000},
    {"ota.progress",   TS_EVENT_BASE_OTA,    TS_EVENT_OTA_PROGRESS_UPDATE, 1000},
};

// 订阅管理
esp_err_t ts_ws_subscribe(httpd_handle_t hd, int fd, const char *topic);
esp_err_t ts_ws_unsubscribe(httpd_handle_t hd, int fd, const char *topic);

// 事件广播（当事件触发时调用）
void ts_ws_broadcast(const char *topic, cJSON *data);
```

### Phase 2: 后端 - 事件触发点 (CRITICAL)

**需要修改的组件**:

1. **Device Controller** (`ts_drivers/device/ts_device_ctrl.c`)
   ```c
   // AGX/LPMU 状态变化时
   ts_event_post(TS_EVENT_BASE_DEVICE, TS_EVENT_DEVICE_STATUS_CHANGED, 
                 &status, sizeof(status), portMAX_DELAY);
   ```

2. **OTA Service** (`ts_ota/src/ts_ota.c`)
   ```c
   // OTA 进度更新时（每 1% 触发）
   ts_ota_progress_t progress = {.percent = 45};
   ts_event_post(TS_EVENT_BASE_OTA, TS_EVENT_OTA_PROGRESS_UPDATE, 
                 &progress, sizeof(progress), 0);
   ```

3. **System Service** (`main/ts_services.c`)
   ```c
   // 系统信息变化时（重启、内存变化）
   ts_event_post(TS_EVENT_BASE_SYSTEM, TS_EVENT_SYSTEM_INFO_CHANGED, 
                 NULL, 0, 0);
   ```

### Phase 3: 前端 - WebSocket 客户端改造

**文件**: `components/ts_webui/web/js/app.js`

#### 1. 订阅管理器

```javascript
class SubscriptionManager {
    constructor(ws) {
        this.ws = ws;
        this.subscriptions = new Map(); // topic → callback[]
    }
    
    subscribe(topic, callback) {
        if (!this.subscriptions.has(topic)) {
            this.subscriptions.set(topic, []);
            this.ws.send({type: 'subscribe', topic});
        }
        this.subscriptions.get(topic).push(callback);
    }
    
    unsubscribe(topic, callback) {
        const callbacks = this.subscriptions.get(topic);
        if (!callbacks) return;
        
        const index = callbacks.indexOf(callback);
        if (index !== -1) callbacks.splice(index, 1);
        
        if (callbacks.length === 0) {
            this.subscriptions.delete(topic);
            this.ws.send({type: 'unsubscribe', topic});
        }
    }
    
    handleMessage(msg) {
        if (msg.type === 'data') {
            const callbacks = this.subscriptions.get(msg.topic);
            callbacks?.forEach(cb => cb(msg.data));
        }
    }
}
```

#### 2. 页面改造示例（Device 页面）

**修改前** (HTTP 轮询):
```javascript
async function loadDevicePage() {
    await refreshDevicePage();
    refreshInterval = setInterval(refreshDevicePage, 2000);
}

async function refreshDevicePage() {
    const status = await api.deviceStatus();
    updateDeviceUI(status.data);
}
```

**修改后** (WebSocket 订阅):
```javascript
let deviceStatusCallback = null;

async function loadDevicePage() {
    // 首次加载
    const status = await api.deviceStatus();
    updateDeviceUI(status.data);
    
    // 订阅实时更新
    deviceStatusCallback = (data) => updateDeviceUI(data);
    subscriptionMgr.subscribe('device.status', deviceStatusCallback);
}

function unloadDevicePage() {
    if (deviceStatusCallback) {
        subscriptionMgr.unsubscribe('device.status', deviceStatusCallback);
        deviceStatusCallback = null;
    }
}
```

## ⚠️ 注意事项

### 1. 向后兼容
- **保留 HTTP API**: WebSocket 仅作为**可选**的实时推送方式
- **降级机制**: WebSocket 连接失败时自动回退到 HTTP 轮询

### 2. 连接管理
- **单一订阅**: 同一 topic 只需订阅一次，多个组件共享数据
- **自动重连**: WebSocket 断开时清除所有订阅，重连后重新订阅
- **页面切换**: 离开页面时取消订阅，避免无效推送

### 3. 性能优化
- **去抖动**: 后端使用 `min_interval_ms` 限制推送频率
- **数据合并**: 短时间内多次变化时仅推送最新状态
- **条件推送**: 仅在数据真正变化时推送（diff 检测）

## 📈 预期收益

| 指标 | 轮询模式 | WebSocket 模式 | 改进 |
|------|---------|----------------|------|
| 网络请求数 | 120/min | ~2/min（仅变化时） | **98% ↓** |
| 平均延迟 | 1-3s | <100ms | **90% ↓** |
| CPU 占用 | 中 | 低（事件驱动） | **50% ↓** |
| 电量消耗 | 高（高频唤醒） | 低（被动接收） | **60% ↓** |

## 🚀 实施优先级

### Milestone 1: Device Monitor (本周)
- ✅ 后端订阅管理器框架
- ✅ Device 状态事件触发
- ✅ 前端 Device 页面迁移
- ✅ 测试与验证

### Milestone 2: OTA Progress (下周)
- ⏳ OTA 进度事件触发
- ⏳ 前端 OTA 页面迁移

### Milestone 3: System Info (低优先级)
- ⏳ System 信息事件触发
- ⏳ 前端 System 页面迁移

## 📚 相关文档

- [TianShanOS 事件系统](../components/ts_core/ts_event/README.md)
- [WebUI WebSocket 实现](../components/ts_webui/README.md)
- [API 参考文档](./API_REFERENCE.md)
