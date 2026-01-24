# TianShanOS 内存优化方案

**日期**：2026年1月24日  
**问题**：DRAM 使用率 86%（257.7 KB / 301.3 KB），接近上限

---

## 📊 当前内存状态

### DRAM（内部 RAM）
- **已用**：257.7 KB
- **总量**：301.3 KB
- **使用率**：**86%** ⚠️ 告急
- **剩余**：43.6 KB

### PSRAM（外部 RAM）
- **已用**：2.4 MB
- **总量**：8.0 MB
- **使用率**：30% ✅ 充足
- **剩余**：5.6 MB

---

## 🔍 内存分析工具

### 1. 编译时静态分析

```bash
# 查看各段内存使用
idf.py size

# 详细的组件内存占用
idf.py size-components

# 生成内存使用报告
idf.py size-files
```

### 2. 运行时动态监控

#### 添加 CLI 命令：`system --memory-detail`

**实现位置**：`components/ts_console/commands/ts_cmd_system.c`

```c
// 显示详细的堆内存信息
static int cmd_system_memory_detail(void) {
    ts_console_printf("\n=== Heap Memory Summary ===\n");
    
    // DRAM (Internal)
    ts_console_printf("\n[DRAM - Internal RAM]\n");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    // PSRAM (External)
    ts_console_printf("\n[PSRAM - External RAM]\n");
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    // DMA capable
    ts_console_printf("\n[DMA Capable Memory]\n");
    heap_caps_print_heap_info(MALLOC_CAP_DMA);
    
    // 显示最大可分配块
    size_t dram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t dram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    
    ts_console_printf("\n=== Allocation Info ===\n");
    ts_console_printf("DRAM  Free: %u bytes, Largest block: %u bytes\n", 
                      dram_free, dram_largest);
    ts_console_printf("PSRAM Free: %u bytes, Largest block: %u bytes\n", 
                      psram_free, psram_largest);
    
    return 0;
}
```

#### 添加 WebUI API：`GET /api/v1/system/memory-detail`

返回 JSON 格式的详细内存信息，便于 WebUI 可视化。

---

## 🎯 内存优化策略

### 策略 1：将大数据结构迁移到 PSRAM

#### 候选对象（按优先级）

| 对象 | 估计大小 | 位置 | 优先级 |
|------|---------|------|--------|
| WebSocket 缓冲区 | ~32 KB | `ts_webui_ws.c` | 高 |
| HTTP 请求/响应缓冲区 | ~16 KB | `ts_http_server.c` | 高 |
| 日志环形缓冲区 | ~16 KB | `ts_log.c` | 高 |
| LED 帧缓冲区 | ~12 KB | `ts_led.c` | 中 |
| 文件上传缓冲区 | ~8 KB | `ts_webui_api.c` | 中 |
| xterm.js 终端缓冲区 | ~4 KB | `ts_webui_ws.c` | 低 |

**预计可释放 DRAM**：约 **80-100 KB**

---

### 策略 2：启用 ESP-IDF PSRAM 配置

#### Kconfig 选项（`sdkconfig`）

```kconfig
# 启用 PSRAM malloc 默认分配
CONFIG_SPIRAM_USE_MALLOC=y

# PSRAM 分配策略
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768  # 保留 32KB DRAM

# 将 WiFi/BT 数据放入 PSRAM
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y

# 将任务栈放入 PSRAM（需要谨慎）
CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=n  # 建议关闭，影响性能
```

---

### 策略 3：使用 `MALLOC_CAP_SPIRAM` 显式分配

#### 修改关键分配点

**示例 1：WebSocket 缓冲区**

```c
// 原代码（使用 DRAM）
static uint8_t ws_buffer[32768];

// 优化后（使用 PSRAM）
static uint8_t *ws_buffer = NULL;

void init_ws_buffer(void) {
    ws_buffer = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM);
    if (!ws_buffer) {
        ESP_LOGE(TAG, "Failed to allocate WS buffer in PSRAM");
        // Fallback to DRAM
        ws_buffer = malloc(32768);
    }
}
```

**示例 2：日志环形缓冲区**

```c
// ts_log.c
typedef struct {
    char *buffer;      // 指向 PSRAM
    size_t size;
    size_t write_pos;
    size_t read_pos;
} log_ring_buffer_t;

esp_err_t log_buffer_init(log_ring_buffer_t *rb, size_t size) {
    // 优先使用 PSRAM
    rb->buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!rb->buffer) {
        rb->buffer = malloc(size);  // Fallback
    }
    rb->size = size;
    return ESP_OK;
}
```

---

### 策略 4：减少静态全局数组

#### 问题代码模式

```c
// ❌ 不好：静态数组直接占用 DRAM
static char global_buffer[8192];
static ts_log_entry_t log_entries[1000];
```

#### 优化方案

```c
// ✅ 好：动态分配到 PSRAM
static char *global_buffer = NULL;
static ts_log_entry_t *log_entries = NULL;

void init_buffers(void) {
    global_buffer = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    log_entries = heap_caps_calloc(1000, sizeof(ts_log_entry_t), MALLOC_CAP_SPIRAM);
}
```

---

## 🛠️ 实施计划

### Phase 1：分析工具（1 天）
- [x] 添加 `system --memory-detail` 命令
- [ ] 添加 WebUI 内存监控页面
- [ ] 实现内存使用热点追踪

### Phase 2：高优先级优化（2-3 天）
- [ ] WebSocket 缓冲区迁移到 PSRAM
- [ ] HTTP 缓冲区迁移到 PSRAM
- [ ] 日志环形缓冲区迁移到 PSRAM
- [ ] 测试和验证

### Phase 3：中优先级优化（1-2 天）
- [ ] LED 帧缓冲区迁移
- [ ] 文件上传缓冲区迁移
- [ ] 配置 ESP-IDF PSRAM 策略

### Phase 4：验证和监控（1 天）
- [ ] 压力测试（并发连接、大文件上传）
- [ ] 性能测试（PSRAM 访问延迟）
- [ ] 内存泄漏检查

---

## ⚠️ 注意事项

### 1. PSRAM 性能影响
- **读取速度**：PSRAM (~40 MHz) 比 DRAM (~240 MHz) 慢约 6 倍
- **适合场景**：大缓冲区、非时间敏感的数据
- **不适合**：中断处理、DMA 操作、实时任务栈

### 2. DMA 兼容性
```c
// ❌ 错误：DMA 不能访问 PSRAM
uint8_t *dma_buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
spi_device_transmit(spi, dma_buffer);  // 会失败

// ✅ 正确：DMA 需要使用 DRAM
uint8_t *dma_buffer = heap_caps_malloc(size, MALLOC_CAP_DMA);
```

### 3. 任务栈
```c
// ⚠️ 谨慎：将任务栈放入 PSRAM 会影响性能
xTaskCreate(task_func, "task", 
            8192,              // 栈大小
            NULL, 
            5, 
            &task_handle);

// 如果必须使用 PSRAM 栈
xTaskCreateWithCaps(task_func, "task", 
                    8192, NULL, 5, &task_handle,
                    MALLOC_CAP_SPIRAM);
```

---

## 📊 预期效果

### 优化前
- DRAM 使用：257.7 KB / 301.3 KB (86%)
- PSRAM 使用：2.4 MB / 8.0 MB (30%)

### 优化后（预计）
- DRAM 使用：**160-180 KB** / 301.3 KB (**53-60%**) ✅
- PSRAM 使用：**2.5-2.6 MB** / 8.0 MB (31-33%)
- **释放 DRAM**：约 **80-100 KB**

---

## 🔧 工具脚本

### 脚本 1：内存使用热点分析

```python
#!/usr/bin/env python3
"""
分析编译产物中的内存使用热点
使用方法: python analyze_memory.py build/TianShanOS.map
"""

import sys
import re
from collections import defaultdict

def parse_map_file(map_file):
    """解析 .map 文件，统计各组件的内存使用"""
    component_mem = defaultdict(lambda: {'data': 0, 'bss': 0, 'text': 0})
    
    with open(map_file, 'r') as f:
        for line in f:
            # 匹配内存段
            match = re.search(r'(\S+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(.+)', line)
            if match:
                section = match.group(1)
                size = int(match.group(2), 16)
                path = match.group(3)
                
                # 提取组件名
                if '/components/' in path:
                    component = path.split('/components/')[1].split('/')[0]
                elif '/main/' in path:
                    component = 'main'
                else:
                    component = 'other'
                
                # 分类累加
                if '.data' in section:
                    component_mem[component]['data'] += size
                elif '.bss' in section:
                    component_mem[component]['bss'] += size
                elif '.text' in section:
                    component_mem[component]['text'] += size
    
    # 打印结果
    print("Component Memory Usage (DRAM = data + bss):\n")
    sorted_components = sorted(component_mem.items(), 
                               key=lambda x: x[1]['data'] + x[1]['bss'], 
                               reverse=True)
    
    for comp, mem in sorted_components:
        dram = mem['data'] + mem['bss']
        if dram > 1024:  # 只显示 > 1KB 的
            print(f"{comp:30s} DRAM: {dram:6d} bytes  "
                  f"(data: {mem['data']:5d}, bss: {mem['bss']:5d})")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: python analyze_memory.py build/TianShanOS.map")
        sys.exit(1)
    parse_map_file(sys.argv[1])
```

### 脚本 2：运行时内存监控

```bash
#!/bin/bash
# 通过串口实时监控内存使用
# 使用方法: ./monitor_memory.sh

while true; do
    echo "system --memory" | socat - /dev/ttyACM0,b115200,raw,echo=0
    sleep 5
done
```

---

## 📚 参考资源

- [ESP-IDF PSRAM 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/external-ram.html)
- [Heap Memory Debugging](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/heap_debug.html)
- [Memory Types in ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/memory-types.html)

---

## 下一步行动

1. **立即执行**：
   - 添加 `system --memory-detail` 命令
   - 运行 `idf.py size-components` 分析编译时内存分配

2. **短期优化**（本周）：
   - 将 WebSocket/HTTP 缓冲区迁移到 PSRAM
   - 测试并验证

3. **持续监控**：
   - 在 WebUI 添加内存监控图表
   - 设置内存使用告警阈值（> 80%）

请确认是否开始实施 Phase 1（添加内存分析工具）？
