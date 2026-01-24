# 可迁移至 PSRAM 的业务组件清单

**分析日期**: 2024-01-24  
**基于代码扫描**: 已完成 WebSocket 终端缓冲 (4KB) 和日志缓冲 (10KB) 迁移  
**剩余可优化空间**: 约 15-20 KB

---

## 一、已完成的 PSRAM 迁移 ✅

| 组件 | 位置 | 大小 | 状态 |
|------|------|------|------|
| WebSocket 终端缓冲 | `ts_webui_ws.c` | 4 KB | ✅ 已迁移 (commit ec11871) |
| 日志环形缓冲 | `ts_log.c` | 10 KB | ✅ 已迁移 (commit ec11871) |

---

## 二、待优化的候选组件

### 🔴 高优先级（推荐立即迁移）

#### 1. API 端点注册表 (7-10 KB)

**位置**: `components/ts_api/src/ts_api.c`

**当前实现**:
```c
// 行 123
s_api.endpoints = calloc(CONFIG_TS_API_MAX_ENDPOINTS, sizeof(api_entry_t));
// CONFIG_TS_API_MAX_ENDPOINTS = 200
// sizeof(api_entry_t) ≈ 48 bytes
// 总计: 200 * 48 = 9600 bytes (~10 KB)
```

**问题**:
- 200 个端点配额过大（当前实际使用 < 50 个）
- 使用普通 `calloc` 分配在 DRAM
- 该数组只在初始化和查找时访问，性能不敏感

**优化方案 A（推荐）**：迁移到 PSRAM
```c
s_api.endpoints = heap_caps_calloc(CONFIG_TS_API_MAX_ENDPOINTS, 
                                    sizeof(api_entry_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!s_api.endpoints) {
    // Fallback
    s_api.endpoints = calloc(CONFIG_TS_API_MAX_ENDPOINTS, sizeof(api_entry_t));
}
```

**优化方案 B（更激进）**：减小端点数量
```c
// sdkconfig
CONFIG_TS_API_MAX_ENDPOINTS=100  // 200 → 100，释放 5 KB
```

**预期收益**: 
- 方案 A: 释放 **10 KB DRAM**
- 方案 B: 释放 **5 KB DRAM**（如果够用的话）

**风险**: 低（API 注册表查找频率低，延迟可接受）

---

#### 2. WebSocket 日志历史查询缓冲 (动态 ~10 KB)

**位置**: `components/ts_webui/src/ts_webui_ws.c:1023`

**当前实现**:
```c
// 每次查询时动态分配
ts_log_entry_t *entries = malloc(limit * sizeof(ts_log_entry_t));
// limit 最大 = CONFIG_TS_LOG_BUFFER_SIZE = 100
// sizeof(ts_log_entry_t) ≈ 104 bytes
// 最大分配: 100 * 104 = 10400 bytes (~10 KB)
```

**问题**:
- 每次 WebSocket 请求 `log_history` 都会分配 10 KB DRAM
- 使用后立即释放，造成碎片化
- 该操作非实时，可容忍 PSRAM 延迟

**优化方案**:
```c
// 行 1023 修改为
ts_log_entry_t *entries = heap_caps_malloc(limit * sizeof(ts_log_entry_t),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!entries) {
    entries = malloc(limit * sizeof(ts_log_entry_t));  // Fallback
}
if (entries) {
    // ... 处理逻辑
    free(entries);  // 使用后释放
}
```

**预期收益**: 减少 DRAM 峰值占用 **10 KB**，降低碎片化

**风险**: 低（日志查询不频繁，延迟可接受）

---

### 🟡 中优先级（可选优化）

#### 3. LED Sparkle 效果状态数组 (8 KB)

**位置**: `components/ts_led/src/ts_led_effect.c:655`

**当前实现**:
```c
static sparkle_state_t sparkle_states[1024] = {0};
// sizeof(sparkle_state_t) = 8 bytes
// 总计: 1024 * 8 = 8192 bytes (8 KB)
```

**问题**:
- 静态分配 8 KB 在 DRAM
- 仅当使用 `sparkle` 效果时才需要
- 支持最大 1024 个像素（实际 LED 数量远少于此）

**优化方案 A（推荐）**：动态分配到 PSRAM
```c
static sparkle_state_t *sparkle_states = NULL;
static size_t sparkle_capacity = 0;

// 首次使用时分配
if (!sparkle_states || count > sparkle_capacity) {
    if (sparkle_states) free(sparkle_states);
    
    sparkle_capacity = count;
    sparkle_states = heap_caps_calloc(sparkle_capacity, sizeof(sparkle_state_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sparkle_states) {
        sparkle_states = calloc(sparkle_capacity, sizeof(sparkle_state_t));
    }
}
```

**优化方案 B（更保守）**：减小数组大小
```c
static sparkle_state_t sparkle_states[256] = {0};  // 1024 → 256
// 释放: 6 KB
```

**预期收益**: 
- 方案 A: 释放 **8 KB DRAM**（实际按需分配）
- 方案 B: 释放 **6 KB DRAM**

**风险**: 中（LED 动画需要频繁访问，PSRAM 可能影响帧率）

---

#### 4. LED Fire 动画 Heat 数组 (1 KB)

**位置**: `components/ts_led/src/ts_led_animation.c:195`

**当前实现**:
```c
static uint8_t heat[1024];  // 32x32 max
```

**优化方案**: 动态分配到 PSRAM（同 Sparkle）

**预期收益**: 释放 **1 KB DRAM**

**风险**: 中（动画效果，频繁访问）

---

#### 5. LED Rain 动画状态数组 (~200 bytes)

**位置**: `components/ts_led/src/ts_led_animation.c:242-244`

**当前实现**:
```c
static uint8_t drop_y[32];
static uint8_t drop_life[32];
static bool drop_active[32];
```

**预期收益**: 释放 **~200 bytes**

**优先级**: 低（收益太小，不值得优化）

---

### 🟢 低优先级（不建议优化）

以下组件占用内存较小（< 1 KB）或访问频繁（需要 DRAM 性能），**不建议**迁移：

| 组件 | 大小 | 原因 |
|------|------|------|
| HAL 句柄结构 | < 100 bytes/个 | 访问频繁，需要低延迟 |
| 事件处理节点 | < 64 bytes/个 | 链表节点，频繁分配/释放 |
| 小型静态缓冲 | 128-512 bytes | 收益太小，不值得优化 |
| libssh2 内部分配 | 可变 | 第三方库，不可控 |

---

## 三、综合优化建议

### 推荐方案：渐进式优化

#### 阶段 1：立即执行（低风险，高收益）

1. **API 端点数组迁移 PSRAM** (+10 KB)
2. **WebSocket 日志查询缓冲迁移 PSRAM** (+10 KB 峰值)

**总收益**: 释放约 **15-20 KB DRAM**（静态 + 峰值）

**实施难度**: 低  
**测试工作量**: 中（需验证 API 调用和 WebSocket 查询）

---

#### 阶段 2：可选执行（中风险，中收益）

3. **LED Sparkle 效果动态化** (+8 KB)
4. **LED Fire 动画动态化** (+1 KB)

**总收益**: 释放约 **9 KB DRAM**

**实施难度**: 中  
**测试工作量**: 高（需验证 LED 动画流畅度）  
**注意事项**: 可能影响帧率，需在实际硬件测试

---

### 不推荐：减小配置参数

虽然可以通过以下方式释放 DRAM，但**不推荐**：

```
CONFIG_TS_API_MAX_ENDPOINTS=100         // -5 KB（可能不够用）
CONFIG_LWIP_MAX_SOCKETS=8               // -4 KB（影响并发连接）
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=2880    // -3 KB（降低吞吐量）
```

**原因**: 损害功能性和性能，得不偿失。

---

## 四、实施代码示例

### 示例 1：API 端点数组迁移

**文件**: `components/ts_api/src/ts_api.c`

**修改**:
```c
// 添加头文件
#include "esp_heap_caps.h"

// 修改 ts_api_init() 函数
esp_err_t ts_api_init(void)
{
    // ... 前面代码不变
    
    /* Allocate endpoint array in PSRAM */
    s_api.endpoints = heap_caps_calloc(CONFIG_TS_API_MAX_ENDPOINTS, 
                                        sizeof(api_entry_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_api.endpoints == NULL) {
        TS_LOGW(TAG, "PSRAM not available, using DRAM for API endpoints");
        s_api.endpoints = calloc(CONFIG_TS_API_MAX_ENDPOINTS, sizeof(api_entry_t));
        if (s_api.endpoints == NULL) {
            vSemaphoreDelete(s_api.mutex);
            return ESP_ERR_NO_MEM;
        }
    } else {
        TS_LOGI(TAG, "API endpoints allocated in PSRAM (%zu bytes)",
                CONFIG_TS_API_MAX_ENDPOINTS * sizeof(api_entry_t));
    }
    
    // ... 后续代码不变
}
```

---

### 示例 2：WebSocket 日志查询缓冲迁移

**文件**: `components/ts_webui/src/ts_webui_ws.c`

**修改**:
```c
// 行 1023 附近修改
ts_log_entry_t *entries = heap_caps_malloc(limit * sizeof(ts_log_entry_t),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!entries) {
    TS_LOGD(TAG, "PSRAM unavailable, using DRAM for log query");
    entries = malloc(limit * sizeof(ts_log_entry_t));
}

if (entries) {
    size_t count = ts_log_buffer_search(entries, limit, min_level, max_level, NULL, NULL);
    // ... 后续处理
    free(entries);  // 记得释放
} else {
    TS_LOGE(TAG, "Failed to allocate memory for log history");
    // ... 错误处理
}
```

---

### 示例 3：LED Sparkle 动态化（可选）

**文件**: `components/ts_led/src/ts_led_effect.c`

**修改**:
```c
// 行 654-656 修改
static sparkle_state_t *sparkle_states = NULL;
static size_t sparkle_capacity = 0;
static bool initialized = false;

// 在 process_sparkle 函数中
if (!initialized || count > sparkle_capacity) {
    if (sparkle_states) free(sparkle_states);
    
    sparkle_capacity = (count < 1024) ? count : 1024;
    sparkle_states = heap_caps_calloc(sparkle_capacity, sizeof(sparkle_state_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!sparkle_states) {
        sparkle_states = calloc(sparkle_capacity, sizeof(sparkle_state_t));
        if (!sparkle_states) {
            TS_LOGE(TAG, "Failed to allocate sparkle states");
            return;
        }
    } else {
        TS_LOGI(TAG, "Sparkle states allocated in PSRAM (%zu bytes)",
                sparkle_capacity * sizeof(sparkle_state_t));
    }
    initialized = true;
}

size_t max_idx = count < sparkle_capacity ? count : sparkle_capacity;
// ... 后续逻辑不变
```

---

## 五、测试与验证

### 验证步骤

1. **编译并烧录**:
   ```bash
   idf.py build flash monitor
   ```

2. **检查启动日志**:
   ```
   I (2345) ts_api: API endpoints allocated in PSRAM (9600 bytes)
   I (2456) ts_led_effect: Sparkle states allocated in PSRAM (2048 bytes)
   ```

3. **运行内存分析**:
   ```bash
   system --memory-detail
   ```
   
   **预期结果**:
   - DRAM Used: **78-80% → 70-75%**
   - DRAM Free: **59 KB → 74 KB**

4. **功能测试**:
   ```bash
   # 测试 API 调用
   curl http://esp32/api/system/info
   
   # 测试 WebSocket 日志查询
   # (通过 WebUI 或 ws-client)
   
   # 测试 LED 效果
   led --effect --device board --name sparkle --speed 50
   ```

5. **性能测试**（如优化了 LED）:
   ```bash
   # 检查帧率是否下降
   led --effect --device matrix --name sparkle --speed 100
   # 观察动画是否卡顿
   ```

---

## 六、风险评估与回退方案

### 风险矩阵

| 优化项 | 风险级别 | 失败影响 | 回退难度 |
|--------|---------|---------|---------|
| API 端点数组 | 🟢 低 | API 调用变慢 | 低（改回 `calloc`） |
| WS 日志缓冲 | 🟢 低 | 查询响应变慢 | 低（改回 `malloc`） |
| LED Sparkle | 🟡 中 | 动画卡顿 | 低（改回静态数组） |
| LED Fire | 🟡 中 | 动画卡顿 | 低（改回静态数组） |

### 回退方案

如果出现问题，只需将 `heap_caps_malloc` 改回 `malloc`/`calloc` 即可：

```bash
git revert <commit-hash>
idf.py build flash
```

---

## 七、总结

### 可迁移业务总览

| 优先级 | 组件 | 大小 | 收益 | 风险 | 推荐 |
|--------|------|------|------|------|------|
| 🔴 高 | API 端点数组 | 10 KB | 高 | 低 | ✅ 推荐 |
| 🔴 高 | WS 日志查询缓冲 | 10 KB | 高 | 低 | ✅ 推荐 |
| 🟡 中 | LED Sparkle 状态 | 8 KB | 中 | 中 | ⚠️ 可选 |
| 🟡 中 | LED Fire 热图 | 1 KB | 低 | 中 | ⚠️ 可选 |
| 🟢 低 | LED Rain 状态 | 0.2 KB | 极低 | 中 | ❌ 不推荐 |

### 最终建议

**立即执行（阶段 1）**：
1. ✅ 迁移 API 端点数组到 PSRAM
2. ✅ 迁移 WebSocket 日志查询缓冲到 PSRAM

**预期效果**：
- 释放 **15-20 KB DRAM**
- 加上已完成的 14 KB，**总计释放 30-35 KB**
- DRAM 使用率：86% → **70-72%**
- DRAM 剩余：45 KB → **75-80 KB**

**满足需求**：
- ✅ 60 KB 新应用可部署在 PSRAM（不占 DRAM）
- ✅ DRAM 剩余空间充足（>70 KB）
- ✅ 系统稳定性高，风险低

**不需要执行**：
- ❌ LED 动画优化（除非测试后发现无性能影响）
- ❌ 系统配置参数缩减（损害功能性）

---

**作者**: GitHub Copilot  
**审核**: TianShanOS Team  
**更新日期**: 2024-01-24
