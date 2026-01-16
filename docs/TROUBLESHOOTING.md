# TianShanOS 故障排除与最佳实践

本文档记录开发过程中遇到的问题、解决方案和最佳实践。

---

## 1. ESP-IDF 事件系统与 DHCP 服务器崩溃问题

### 问题描述

**症状**：ESP32 在 Ethernet link up 后启动 DHCP 服务器，当客户端获取 IP 地址时系统崩溃（Guru Meditation Error: LoadProhibited）。

**Backtrace**：
```
xTaskRemoveFromEventList → xQueueGenericSend → xQueueGiveMutexRecursive → esp_event_loop_run
```

**根本原因**：
1. DHCP 服务器分配 IP 时会触发 `IP_EVENT_AP_STAIPASSIGNED` 事件
2. 如果在事件处理器中使用 mutex（如 `xSemaphoreTake`），可能导致事件循环任务死锁或崩溃
3. ESP-IDF 默认事件循环任务栈空间有限，复杂操作可能导致栈溢出

### 解决方案

**方案1：事件处理器提前返回（临时方案）**

在 `ip_event_handler` 中，对不处理的事件类型提前返回：

```c
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    /* 只处理我们关心的事件，忽略其他 */
    if (event_id != IP_EVENT_ETH_GOT_IP && 
        event_id != IP_EVENT_ETH_LOST_IP && 
        event_id != IP_EVENT_STA_GOT_IP) {
        return;  // 忽略 IP_EVENT_AP_STAIPASSIGNED 等
    }
    
    /* 确保 mutex 有效 */
    if (!s_state.mutex) {
        return;
    }
    
    xSemaphoreTake(s_state.mutex, portMAX_DELAY);
    // ...
}
```

**方案2：独立任务处理（推荐方案）**

将 DHCP 服务器启动放在独立任务中，避免在事件处理器上下文中操作：

```c
static TaskHandle_t s_dhcp_task = NULL;

static void dhcp_start_task(void *arg)
{
    esp_netif_t *netif = (esp_netif_t *)arg;
    
    /* 等待网络层稳定 */
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if (netif && s_link_up) {
        esp_netif_dhcps_stop(netif);
        
        dhcps_lease_t lease;
        memset(&lease, 0, sizeof(lease));
        lease.enable = true;
        lease.start_ip.addr = ipaddr_addr(ETH_DHCP_POOL_START);
        lease.end_ip.addr = ipaddr_addr(ETH_DHCP_POOL_END);
        esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_REQUESTED_IP_ADDRESS,
                               &lease, sizeof(lease));
        
        esp_netif_dhcps_start(netif);
    }
    
    s_dhcp_task = NULL;
    vTaskDelete(NULL);
}

/* 在事件处理器中创建任务而不是直接操作 */
case ETHERNET_EVENT_CONNECTED:
    if (netif && s_dhcp_task == NULL) {
        xTaskCreate(dhcp_start_task, "dhcp_start", 4096, netif, 5, &s_dhcp_task);
    }
    break;
```

### 最佳实践

1. **事件处理器要快速返回**：避免在事件处理器中进行耗时操作或获取锁
2. **使用独立任务处理复杂逻辑**：需要获取锁或进行 I/O 操作时，创建独立任务
3. **注册事件时要精确**：使用具体事件 ID 而不是 `ESP_EVENT_ANY_ID`
4. **检查指针有效性**：在使用 mutex/semaphore 前检查是否已初始化

### 未来改进方向

1. 考虑使用 ESP-IDF 的 `esp_event_post_to()` 发送到自定义事件循环
2. 统一使用 TianShanOS 的 `ts_event` 系统替代 ESP-IDF 默认事件循环
3. 增加事件循环任务栈大小（通过 menuconfig）

---

## 2. lwIP DHCP 服务器 IP 池配置问题

### 问题描述

**症状**：DHCP 服务器启动后，客户端获取的 IP 地址不在预期的 IP 池范围内。

**根本原因**：lwIP DHCP 服务器的 IP 池配置只能在服务器停止状态下生效。

### 解决方案

必须按以下顺序操作：

```c
/* 1. 停止 DHCP 服务器 */
esp_netif_dhcps_stop(netif);

/* 2. 配置 IP 池（关键：enable = true） */
dhcps_lease_t lease;
memset(&lease, 0, sizeof(lease));
lease.enable = true;  // 必须设为 true！
lease.start_ip.addr = ipaddr_addr("10.10.99.100");
lease.end_ip.addr = ipaddr_addr("10.10.99.103");
esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET,
                       ESP_NETIF_REQUESTED_IP_ADDRESS,
                       &lease, sizeof(lease));

/* 3. 启动 DHCP 服务器 */
esp_netif_dhcps_start(netif);
```

### 关键点

- `dhcps_lease_t.enable` 必须设为 `true`，否则配置不生效
- 配置必须在 `dhcps_stop()` 和 `dhcps_start()` 之间进行
- 初始化时不要启动 DHCP，等 link up 事件再启动

---

## 3. TianShanOS 任务管理模式

### 推荐模式：事件触发 + 独立任务处理

```
事件源 → 事件总线 → 事件处理器(轻量) → 创建独立任务 → 执行实际操作
```

### 示例代码模式

```c
/* 事件处理器：只做分发，不做实际工作 */
static void event_handler(void *arg, esp_event_base_t base, 
                          int32_t id, void *data)
{
    switch (id) {
        case SOME_EVENT:
            /* 创建任务处理，传递必要参数 */
            xTaskCreate(handle_task, "handler", 4096, data, 5, NULL);
            break;
    }
}

/* 独立任务：执行实际操作 */
static void handle_task(void *arg)
{
    /* 这里可以安全地：
     * - 获取锁
     * - 进行 I/O 操作
     * - 等待其他资源
     */
    
    vTaskDelete(NULL);
}
```

### 优点

1. 事件处理器快速返回，不阻塞事件循环
2. 独立任务有自己的栈空间，可以进行复杂操作
3. 避免在事件循环任务中使用锁导致的问题

---

## 更新日志

- 2026-01-17: 初始版本，记录 DHCP 服务器崩溃问题及解决方案
