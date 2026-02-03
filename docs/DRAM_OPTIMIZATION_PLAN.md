# DRAM 优化策略 - 2026-02-04

## 一、当前状态分析

### 内存概览

| 指标 | 当前值 | 目标值 | 说明 |
|------|--------|--------|------|
| DRAM 总计 | 299.1 KB | - | 固定 |
| DRAM 已用 | 265.6 KB (88%) | < 220 KB (75%) | 🔴 严重 |
| DRAM 空闲 | 33.5 KB | > 75 KB | 需释放 ~45 KB |
| DRAM 最大块 | 20 KB | > 30 KB | 碎片化严重 |
| DRAM 碎片率 | 40.2% | < 25% | 需优化分配策略 |
| 历史最低 | 7 KB | > 20 KB | 🔴 极度危险 |
| PSRAM 空闲 | 5.22 MB | - | 🟢 充足可用 |

### 任务栈分析（总计 80.4 KB）

**可迁移到 PSRAM 的任务**（不涉及 Flash/NVS 操作）：

| 任务名 | 栈大小 | 使用率 | 操作类型 | 迁移优先级 |
|--------|--------|--------|----------|-----------|
| ssh_cmd_load | 8 KB | 短期 | SD 卡 I/O | 🔴 高 |
| ssh_cmd_sync | 8 KB | 短期 | SD 卡 I/O | 🔴 高 |
| ssh_host_load | 8 KB | 短期 | SD 卡 I/O | 🔴 高 |
| ssh_host_sync | 8 KB | 短期 | SD 卡 I/O | 🔴 高 |
| pki_enroll | 8 KB | 短期 | 网络 I/O | 🔴 高 |
| ssh_exec | 8 KB | 短期 | 网络 I/O | 🔴 高 |
| ssh_forward | 4 KB | 长期 | 网络 I/O | 🟡 中 |
| text_overlay | 4 KB | 长期 | LED 渲染 | 🟡 中 |
| dhcp_start | 4 KB | 短期 | 网络初始化 | 🟡 中 |
| lpmu_detect | 4 KB | 短期 | 网络检测 | 🟡 中 |

**必须保留在 DRAM 的任务**（涉及 Flash/NVS 操作）：

| 任务名 | 栈大小 | 原因 |
|--------|--------|------|
| action_exec | 13.2 KB | NVS 操作（禁用 cache） |
| action_load | 8 KB | NVS 操作 |
| rule_load | 8 KB | NVS 操作 |
| source_load | 8 KB | NVS 操作 |
| esp_timer | 3.5 KB | 系统任务 |
| wifi | 4.6 KB | 系统驱动 |
| mdns | 3.7 KB | 系统服务 |

### 静态内存占用

| 组件 | 大小 | 类型 | 状态 |
|------|------|------|--------|
| .data + .bss | 61 KB | 编译时固定 | ❌ 无法优化 |
| sparkle_states | 4 KB | 动态分配 | ✅ 已迁移 PSRAM |
| anim_state_t | ~1.5 KB | 动态分配 | ✅ 已迁移 PSRAM |
| 小型静态缓冲 | ~2 KB | 静态数组 | ❌ 不值得 |

---

## 二、优化方案

### 阶段 1：任务栈迁移 PSRAM（预计释放 40-50 KB）

#### 1.1 SSH 配置任务（释放 32 KB）

**文件**: `ts_ssh_commands_config.c`, `ts_ssh_hosts_config.c`

将以下任务的栈分配到 PSRAM：
- `ssh_cmd_load` (8 KB)
- `ssh_cmd_sync` (8 KB)  
- `ssh_host_load` (8 KB)
- `ssh_host_sync` (8 KB)

**原因**：这些任务只进行 SD 卡 JSON 文件读写，不涉及 NVS/Flash 操作。

#### 1.2 PKI/SSH 网络任务（释放 12-16 KB）

**文件**: `ts_pki_client.c`, `ts_webui_ws.c`, `ts_port_forward.c`

- `pki_enroll` (8 KB) - 证书申请，纯网络操作
- `ssh_exec` (8 KB) - SSH 命令执行，纯网络操作
- `ssh_forward` (4 KB) - SSH 端口转发

#### 1.3 其他可迁移任务（释放 8-12 KB）

- `text_overlay` (4 KB) - LED 文字叠加，纯渲染
- `dhcp_start` (4 KB) - DHCP 客户端启动
- `lpmu_detect` (4 KB) - LPMU 启动检测

### 阶段 2：静态数组动态化（预计释放 9 KB）

#### 2.1 LED Sparkle 状态数组

**文件**: `ts_led_effect.c`

```c
// 原来
static sparkle_state_t sparkle_states[1024];  // 8 KB DRAM

// 改为
static sparkle_state_t *sparkle_states = NULL;
// 按需在 PSRAM 中分配
```

#### 2.2 LED Fire 热图数组

**文件**: `ts_led_animation.c`

```c
// 原来
static uint8_t heat[1024];  // 1 KB DRAM

// 改为
static uint8_t *heat = NULL;
// 按需在 PSRAM 中分配
```

---

## 三、实施顺序

### 第一批（已实施）

1. ✅ text_overlay (4 KB) - LED 渲染，纯图形操作
2. ✅ dhcp_start (4 KB) - DHCP 启动，纯网络操作
3. ✅ lpmu_detect (4 KB) - LPMU 检测，纯网络 ping
4. ✅ ssh_forward (4 KB) - SSH 端口转发，纯 libssh2 网络操作

**已节省**：16 KB DRAM

### ⚠️ 无法迁移的任务（NVS 访问）

- ssh_cmd_load/sync (8 KB × 2) - 调用 `ts_ssh_commands_config_count()` 访问 NVS
- ssh_host_load/sync (8 KB × 2) - 调用 `ts_ssh_hosts_config_count()` 访问 NVS
- pki_enroll (8 KB) - PKI 操作访问 NVS 密钥存储
- ssh_exec (8 KB × 2) - SSH 执行访问 keystore (NVS)
- action_exec (16 KB) - 执行动作时访问 NVS

### 第二批（已完成）

5. ✅ sparkle_states (4 KB) - 动态分配到 PSRAM
6. ✅ anim_state_t (~1.5 KB) - fire_heat, rain, coderain 状态动态分配到 PSRAM

---

## 四、代码修改模板

### 4.1 任务栈迁移到 PSRAM

```c
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"

// 原来
xTaskCreate(my_task, "task_name", 8192, NULL, 5, NULL);

// 改为（SD 卡 / 网络 I/O 任务）
xTaskCreateWithCaps(my_task, "task_name", 8192, NULL, 5, NULL,
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
```

### 4.2 检测迁移是否成功

```c
// 在任务函数开头添加日志
static void my_task(void *arg)
{
    // 检查栈是否在 PSRAM 中
    void *stack_ptr = NULL;
    asm("mov %0, sp" : "=r"(stack_ptr));
    bool in_psram = esp_ptr_external_ram(stack_ptr);
    ESP_LOGI(TAG, "Task running, stack in %s", in_psram ? "PSRAM" : "DRAM");
    
    // ... 任务逻辑
}
```

---

## 五、风险评估

### 低风险（推荐立即实施）

| 任务 | 风险说明 | 回退方案 |
|------|---------|---------|
| ssh_cmd_* | 纯 JSON 解析，与 Flash 无关 | 改回 xTaskCreate |
| ssh_host_* | 纯 JSON 解析，与 Flash 无关 | 改回 xTaskCreate |
| pki_enroll | 纯网络 HTTPS 请求 | 改回 xTaskCreate |
| ssh_exec | 纯 libssh2 网络操作 | 改回 xTaskCreate |

### 中风险（需测试后实施）

| 任务 | 风险说明 | 验证方法 |
|------|---------|---------|
| text_overlay | LED 渲染可能受 PSRAM 延迟影响 | 测试文字滚动是否流畅 |
| sparkle_states | 频繁访问可能影响帧率 | 测试 sparkle 动画效果 |

### 高风险（不建议修改）

| 任务 | 风险说明 |
|------|---------|
| action_exec | 执行 NVS 操作，必须在 DRAM |
| action_load | 加载模板涉及 NVS |
| rule_load | 规则引擎涉及 NVS |
| source_load | 数据源涉及 NVS |

---

## 六、验证检查清单

### 编译后检查

```bash
# 查看静态内存使用
idf.py size-components

# 关注 .data 和 .bss 段大小
```

### 运行时检查

```bash
# 在 CLI 中执行
system --memory-detail

# 预期结果
# DRAM Used: < 220 KB (75%)
# DRAM Free: > 75 KB
# DRAM 最大块: > 30 KB
```

### 功能验证

1. SSH 命令配置保存/加载正常
2. SSH 主机配置保存/加载正常
3. PKI 证书申请流程正常
4. WebUI SSH 命令执行正常
5. LED 效果动画流畅（如优化了静态数组）

---

## 七、目标达成标准

| 指标 | 当前 | 目标 | 状态 |
|------|------|------|------|
| DRAM 使用率 | 88% | < 75% | 🔴 |
| DRAM 空闲 | 33.5 KB | > 75 KB | 🔴 |
| 历史最低空闲 | 7 KB | > 20 KB | 🔴 |
| DRAM 碎片率 | 40.2% | < 30% | 🟡 |

**优化完成后预期**：
- DRAM 使用率：~70%
- DRAM 空闲：~90 KB
- 为未来任务预留充足空间

---

**作者**: GitHub Copilot  
**创建日期**: 2026-02-04  
**状态**: 待实施
