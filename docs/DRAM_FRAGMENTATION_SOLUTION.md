# DRAM 碎片优化方案

## 问题分析

**当前状态**：
- 碎片率：70.4%
- 最大连续块：33 KB
- 分配块数：917
- 历史最低：70.8 KB

**碎片产生原因**：
1. **频繁小分配**：大量 `malloc()` 调用分配不同大小的块
2. **分配/释放交错**：不同生命周期的对象交错分配
3. **DRAM 过度使用**：大分配应放入 PSRAM

## 解决策略

### 策略 1：内存池（Memory Pool）

为固定大小的对象使用预分配内存池，避免频繁 malloc/free：

```c
// 示例：JSON 缓冲区池
#define JSON_BUF_SIZE     2048
#define JSON_BUF_COUNT    8

static char s_json_pool[JSON_BUF_COUNT][JSON_BUF_SIZE] EXT_RAM_BSS_ATTR;
static uint8_t s_json_pool_used[JSON_BUF_COUNT];

char *json_pool_alloc(void) {
    for (int i = 0; i < JSON_BUF_COUNT; i++) {
        if (!s_json_pool_used[i]) {
            s_json_pool_used[i] = 1;
            return s_json_pool[i];
        }
    }
    return heap_caps_malloc(JSON_BUF_SIZE, MALLOC_CAP_SPIRAM);
}
```

### 策略 2：PSRAM 优先分配

**规则**：≥128 字节的分配优先使用 PSRAM

```c
// 替换 malloc(size) 为：
void *ptr = (size >= 128) 
    ? heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
    : malloc(size);
```

### 策略 3：启动时预分配

在系统启动早期分配大块内存，减少后续碎片：

```c
// main.c 或 ts_core_init.c 中
void preallocate_buffers(void) {
    // 预分配常用大缓冲区
    g_ssh_buffer = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    g_json_buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    g_http_buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
}
```

### 策略 4：对象复用

避免频繁创建/销毁对象：

```c
// ❌ 每次请求都分配
void handle_request(void) {
    char *buf = malloc(2048);
    // 处理...
    free(buf);
}

// ✅ 复用静态缓冲区（放 PSRAM）
static char *s_request_buf = NULL;
void handle_request(void) {
    if (!s_request_buf) {
        s_request_buf = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM);
    }
    // 处理...（不释放）
}
```

## 优先修复列表

按碎片影响排序：

| 优先级 | 文件 | 问题 | 修复方案 |
|--------|------|------|----------|
| 1 | ts_source_manager.c | 频繁分配 `msg_buf` (4KB) | 使用静态池 |
| 1 | ts_action_manager.c | 反复分配 `expanded_cmd` | 静态缓冲区复用 |
| 2 | ts_rule_engine.c | 规则数组动态扩展 | 启动时预分配 |
| 2 | ts_api_automation.c | `poll_response` 重复分配 | 静态复用 |
| 3 | ts_api_cert.c | 证书缓冲区 4KB | PSRAM 分配 |
| 3 | ts_api_storage.c | 文件内容读取 | PSRAM 分配 |

## 碎片监控命令

```bash
# 查看详细内存状态
system --memory-detail

# 周期性监控
system --memory --json | jq '.dram.fragmentation'
```

## 预期效果

实施后预期：
- 碎片率：< 30%
- 最大连续块：> 80 KB
- 分配块数：< 500

## 实施步骤

1. **Phase 1**：创建通用内存池模块 `ts_mempool` ✅ 已完成
2. **Phase 2**：迁移高频分配到 PSRAM ✅ 已完成（阈值调整）
3. **Phase 3**：复用静态缓冲区 ⏳ 待实施
4. **Phase 4**：启动时预分配关键缓冲区 ⏳ 待实施

---

## 已实施修改记录

### 2026-02-04: Phase 1 & 2 完成

#### 1. 创建 `ts_mempool` 组件

**位置**：`components/ts_mempool/`

**功能**：在 PSRAM 中预分配内存池，减少 DRAM 碎片

**池配置**：
| 池类型 | 块大小 | 数量 | 总计 |
|--------|--------|------|------|
| SMALL | 256 B | 16 | 4 KB |
| MEDIUM | 1 KB | 12 | 12 KB |
| LARGE | 4 KB | 8 | 32 KB |
| XLARGE | 8 KB | 4 | 32 KB |
| **总计** | - | 40 | **~80 KB PSRAM** |

**API**：
```c
esp_err_t ts_mempool_init(void);      // 启动时初始化
void *ts_mempool_alloc(size_t size);  // 自动选池分配
void ts_mempool_free(void *ptr);      // 归还到池/堆
void ts_mempool_print_stats(void);    // 打印统计
```

**集成点**：
- `main/ts_core_init.c`: 在 cJSON hooks 之前调用 `ts_mempool_init()`
- `main/CMakeLists.txt`: 添加 `ts_mempool` 依赖
- `components/ts_core/CMakeLists.txt`: 添加 `ts_mempool` 依赖

#### 2. PSRAM 阈值调整

**文件**：`sdkconfig.defaults`

| 配置项 | 原值 | 新值 | 说明 |
|--------|------|------|------|
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | 128 | **64** | 更多分配使用 PSRAM |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | 8192 | **16384** | 保留更多 DRAM 给 DMA |

**效果**：只有 <64 字节的分配强制使用 DRAM，其余自动使用 PSRAM

#### 3. mDNS 条件编译

**文件**：`components/ts_net/Kconfig`, `components/ts_net/src/ts_net.c`

添加 `CONFIG_TS_NET_MDNS_ENABLE` 选项，允许禁用 mDNS 节省 ~4KB DRAM

### 相关文件变更

```
components/ts_mempool/              # 新组件
├── CMakeLists.txt
├── include/ts_mempool.h
└── src/ts_mempool.c

main/ts_core_init.c                 # 添加内存池初始化
main/CMakeLists.txt                 # 添加依赖
components/ts_core/CMakeLists.txt   # 添加依赖
components/ts_net/Kconfig           # mDNS 开关
components/ts_net/src/ts_net.c      # mDNS 条件编译
sdkconfig.defaults                  # PSRAM 阈值调整
```

### 验证方法

启动后运行：
```bash
system --memory-detail
```

预期改善：
- 碎片率：70.4% → <40%
- 最大连续块：33KB → >60KB
