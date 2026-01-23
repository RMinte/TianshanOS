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

## 4. SSH 交互式 Shell 字符回显延迟问题

### 问题描述

**症状**：使用 `ssh --shell` 连接远程主机后，输入的字符在屏幕上看不到，只有按下回车后才能看到输入的命令和执行结果。Shell 提示符也显示不完整（例如只显示 `(base)` 而非完整的提示符）。

**用户反馈**：
> "打的字立即看不到，回车之后才能看见，这个对于大部分的操作人员来说都是很不舒服的"

### 错误诊断过程

**错误假设 1：网络延迟**
- 被否定："你的逻辑不成立啊，我是本地网络。不可能是网络延迟"

**错误修复 1：添加本地回显**
```c
// 错误方案：在本地回显输入字符
ts_console_printf("%c", ch);
```
- 结果：输入 `ls` 显示为 `llss`（双重回显）
- 原因：远程 SSH 服务器已经回显字符，本地再回显导致重复

### 根本原因分析

问题出在 `shell_input_callback()` 中的 UART 读取：

```c
// 问题代码：50ms 超时阻塞
int len = uart_read_bytes(UART_NUM_0, data, sizeof(data), pdMS_TO_TICKS(50));
```

**问题机制**：
1. UART 读取有 50ms 超时，每次循环都会阻塞 50ms
2. 在阻塞期间，远程服务器的回显数据无法被及时读取和显示
3. 即使远程数据已到达，也要等 UART 超时后才能处理输出
4. 导致用户感知到的延迟约为 50-100ms，累积起来非常明显

### 解决方案

**修复 1：非阻塞 UART 读取**

```c
// 正确方案：timeout = 0，立即返回
int len = uart_read_bytes(UART_NUM_0, data, sizeof(data), 0);
```

**修复 2：立即刷新输出缓冲区**

```c
// 原代码：逐字符 printf（可能有缓冲）
for (int i = 0; i < len; i++) {
    ts_console_printf("%c", data[i]);
}

// 正确方案：fwrite + fflush 立即输出
fwrite(data, 1, len, stdout);
fflush(stdout);
```

**修复 3：优化主循环顺序**

```c
// ts_ssh_shell_run() 主循环优化
while (running) {
    bool had_activity = false;
    
    // 1. 先处理本地输入（非阻塞读取）
    if (input_callback) {
        // ... 发送到远程
        had_activity = true;
    }
    
    // 2. 用 do-while 循环排空所有远程数据
    do {
        ssize_t nread = libssh2_channel_read(...);
        if (nread > 0) {
            output_callback(buf, nread);
            had_activity = true;
        } else {
            break;
        }
    } while (1);
    
    // 3. 只有没有活动时才等待
    if (!had_activity) {
        wait_socket(session, 10);  // 短超时
    }
}
```

### 技术要点

1. **ESP-IDF VFS stdout 缓冲**：默认可能有行缓冲或块缓冲，必须 `fflush(stdout)` 才能立即显示
2. **非阻塞 I/O 模式**：嵌入式实时系统中，阻塞操作会影响响应性
3. **远程回显机制**：SSH PTY 模式下，服务器负责回显字符，客户端不应重复回显
4. **libssh2 非阻塞模式**：需配合 `wait_socket()` 使用 `select()` 等待数据

### 相关文件

- [ts_cmd_ssh.c](../components/ts_console/commands/ts_cmd_ssh.c) - `shell_input_callback()`, `shell_output_callback()`
- [ts_ssh_shell.c](../components/ts_security/src/ts_ssh_shell.c) - `ts_ssh_shell_run()` 主循环

---

## 5. SSH 公钥认证失败问题 (libssh2 + mbedTLS)

### 问题描述

**症状**：使用 RSA 私钥进行公钥认证时，`libssh2_userauth_publickey_fromfile_ex()` 返回 `rc=-1`，错误信息为空或不相关。

**环境**：
- libssh2 1.10.1_DEV (ESP-IDF 移植版，mbedTLS 后端)
- ESP32-S3
- RSA-2048 密钥（传统 PEM 格式：`-----BEGIN RSA PRIVATE KEY-----`）

**错误日志**：
```
I (8848) ts_ssh: Authenticating with public key from file: /sdcard/id_rsa
I (8863) ts_ssh: Key file size: 1675 bytes, header: -----BEGIN RSA PRIVATE KEY-----
E (9004) ts_ssh: libssh2_userauth_publickey_fromfile_ex failed: rc=-1, err_code=0, err=
```

### 调试过程

1. **验证密钥格式**：确认是传统 RSA PEM 格式（mbedTLS 不支持 OpenSSH 新格式）
2. **验证公钥部署**：通过密码认证确认服务器 `~/.ssh/authorized_keys` 包含正确公钥
3. **验证服务器配置**：确认服务器支持 `publickey` 认证方法
4. **尝试内存加载**：`libssh2_userauth_publickey_frommemory()` 同样失败

### 根本原因

**libssh2 的 mbedTLS 后端在某些情况下需要显式提供公钥文件路径**。

当 `libssh2_userauth_publickey_fromfile_ex()` 的 `publickey` 参数为 `NULL` 时，libssh2 应该从私钥推导公钥，但 mbedTLS 后端可能无法正确处理这种情况。

### 解决方案

**显式提供公钥文件路径**：

```c
/* 构造公钥文件路径 (私钥路径 + ".pub") */
char pubkey_path[128];
snprintf(pubkey_path, sizeof(pubkey_path), "%s.pub", private_key_path);

/* 检查公钥文件是否存在 */
const char *pubkey_ptr = NULL;
FILE *pub_f = fopen(pubkey_path, "r");
if (pub_f) {
    fclose(pub_f);
    pubkey_ptr = pubkey_path;
    ESP_LOGI(TAG, "Using public key file: %s", pubkey_path);
}

/* 使用显式公钥路径调用认证 */
rc = libssh2_userauth_publickey_fromfile_ex(
    session,
    username, strlen(username),
    pubkey_ptr,  /* 必须提供公钥文件路径！ */
    private_key_path,
    passphrase);
```

### 其他注意事项

1. **ECDSA 不支持**：libssh2 的 mbedTLS 后端不支持 ECDSA 密钥认证（会报 "Key type not supported"）
2. **非阻塞模式**：`libssh2_userauth_list()` 也需要处理 `LIBSSH2_ERROR_EAGAIN`
3. **密钥格式**：必须使用传统 PEM 格式，不支持 OpenSSH 新格式（`-----BEGIN OPENSSH PRIVATE KEY-----`）

### 生成兼容密钥

```bash
# 生成 RSA-2048 传统 PEM 格式密钥
ssh-keygen -t rsa -b 2048 -m PEM -f /sdcard/id_rsa

# 或在 TianShanOS 设备上生成
ssh --keygen --type rsa --bits 2048 --output /sdcard/id_rsa
```

### 相关文件

- [ts_ssh_client.c](../components/ts_security/src/ts_ssh_client.c) - `ts_ssh_connect()` 认证逻辑
- [ts_cmd_ssh.c](../components/ts_console/commands/ts_cmd_ssh.c) - CLI 命令实现

---

## 6. SSH 内存密钥认证失败 (libssh2 mbedTLS mpint padding bug)

### 问题描述

**症状**：使用 `--keyid` 从安全存储加载私钥进行 SSH 公钥认证，服务器返回 `PUBLICKEY_UNVERIFIED`（rc=-18），错误信息："Username/PublicKey combination invalid"。

**环境**：
- libssh2 1.10.1_DEV (ch405labs_esp_libssh2，mbedTLS 后端)
- ESP32-S3
- RSA-2048 密钥

**关键观察**：
- 从私钥提取公钥成功（`keylen=278`）
- 公钥 blob 前 20 字节看起来正确
- 但 `n_pad` 值不稳定（有时 0，有时 1）
- 服务器拒绝公钥（与 `authorized_keys` 中的不匹配）

### 调试过程

**第一步：添加详细日志**

在 `gen_publickey_from_rsa()` 中添加日志：
```c
MBEDTLS_DEBUG("e_bytes=%d, n_bytes=%d, e_pad=%d, n_pad=%d, n_buf[0]=0x%02x",
              e_bytes, n_bytes, e_pad, n_pad, n_buf[0]);
```

**第二步：发现异常**

对于同一个密钥，不同次调用 `n_buf[0]` 的值不同：
- 首次：`n_buf[0]=0x80` → `n_pad=1`
- 再次：`n_buf[0]=0x30` → `n_pad=0`

### 根本原因

**Bug 位置**：`libssh2/src/mbedtls.c` 的 `gen_publickey_from_rsa()` 函数

```c
// 有 bug 的代码：
unsigned char n_buf[1];  // 只有 1 字节的 buffer！
mbedtls_mpi_write_binary(&rsa->N, n_buf, 1);  // N 有 256 字节，buffer 太小！
if(n_buf[0] & 0x80)  // n_buf[0] 是未初始化的随机值！
    n_pad = 1;
```

**问题机制**：
1. `mbedtls_mpi_write_binary()` 需要足够大的 buffer 存储整个 MPI
2. 传入 1 字节的 buffer 存储 256 字节的 N，函数返回 `MBEDTLS_ERR_MPI_BUFFER_TOO_SMALL`
3. 代码没有检查返回值，`n_buf[0]` 保持未初始化状态（栈上随机值）
4. 导致 `n_pad` 的计算结果随机
5. 生成的公钥 blob 可能与 `ts_crypto` 生成的不一致（部署到服务器的是 ts_crypto 版本）

### 解决方案

使用 `mbedtls_mpi_bitlen()` 正确判断 MSB：

```c
// 修复后的代码：
if(n_bytes > 0) {
    size_t n_bitlen = mbedtls_mpi_bitlen(&rsa->MBEDTLS_PRIVATE(N));
    // 如果 bitlen == bytes * 8，说明最高位 byte 的 MSB 为 1
    if(n_bitlen == (size_t)n_bytes * 8)
        n_pad = 1;
}
```

**原理**：
- `mbedtls_mpi_size()` 返回存储 MPI 所需的最小字节数
- `mbedtls_mpi_bitlen()` 返回 MPI 的实际位数
- 如果 `bitlen == bytes * 8`，说明最高字节的最高位是 1，需要 padding

### 修改的文件

- `components/ch405labs_esp_libssh2/libssh2/src/mbedtls.c`
  - `gen_publickey_from_rsa()` - 修复 mpint padding 计算

### SSH mpint 格式说明

SSH 协议中的 mpint（多精度整数）格式：
- 大端序存储
- 如果最高字节的 MSB 为 1，需要添加前导 0x00（表示正数）
- 长度前缀包含 padding 字节

```
// 示例：N = 0x80... (MSB=1)
// 编码为：
00 00 01 01    // 长度 = 257 (256 + 1 padding)
00             // padding byte
80 xx xx ...   // N 的 256 字节数据
```

### 验证修复

```bash
# 1. 生成新密钥
key --generate --id test_new --type rsa

# 2. 部署到服务器
ssh --copyid --host 10.10.99.100 --user thomas --password cdromdir --keyid test_new

# 3. 使用密钥连接（成功！）
ssh --host 10.10.99.100 --user thomas --keyid test_new --exec "uname -a"
```

### 经验教训

1. **始终检查 mbedTLS 函数返回值**
2. **使用正确的 API 获取 MPI 属性**：`mbedtls_mpi_bitlen()` 比手动读取字节更可靠
3. **栈上变量未初始化可能导致随机行为**
4. **详细日志对于调试密码学问题至关重要**

---

## 8. SD 卡卸载后重新挂载失败 (ESP_ERR_NO_MEM)

### 问题描述

**症状**：执行 `storage --unmount` 卸载 SD 卡后，再执行 `storage --mount` 重新挂载失败，错误码为 `ESP_ERR_NO_MEM (0x101)`。

**错误日志**：
```
E (xxxxx) vfs_fat_sdmmc: mount_to_vfs failed (0x101)
```

**误导性**：错误码 `ESP_ERR_NO_MEM` 暗示内存不足，但通过 `heap_caps_get_largest_free_block()` 检查发现堆内存充足（6MB+）。

### 排查过程

**1. 检查 FATFS pdrv 槽位**

在 mount 前添加诊断：
```c
uint8_t pdrv = 0xFF;
esp_err_t ret = ff_diskio_get_drive(&pdrv);
ESP_LOGI(TAG, "ff_diskio_get_drive: ret=%s, pdrv=%d", esp_err_to_name(ret), pdrv);
```

结果：`ESP_OK, pdrv=0`（正常）。排除 FATFS 槽位耗尽。

**2. 检查 FATFS s_fat_ctxs[] 状态**

使用 `esp_vfs_fat_info()` 检查：
```c
uint64_t total, free;
esp_err_t ret = esp_vfs_fat_info("/sdcard", &total, &free);
// 返回 ESP_ERR_INVALID_STATE 表示正确清理
```

结果：卸载后返回 `ESP_ERR_INVALID_STATE`（正常）。排除 FATFS 上下文泄漏。

**3. 检查 VFS 路径注册状态**

使用 `esp_vfs_dump_registered_paths()` 打印所有 VFS 条目：
```
0:/dev/uart -> 0x3c191994
1:/dev/secondary -> 0x3c191c70
2:/dev/null -> 0x3c191da8
3:/dev/console -> 0x3c191cdc
4:/spiffs -> 0x3c197740
5:/sdcard -> 0x3c197580  ← 卸载前
5:NULL -> 0x0            ← 卸载后（正确清理）
6: -> 0x3c1c4ef4         ← socket VFS
7:/www -> 0x3c197740
```

**关键发现**：系统有 8 个 VFS 条目，而 `CONFIG_VFS_MAX_COUNT=8`！

### 根本原因

**ESP-IDF VFS 层的 `s_vfs_count` 计数器在注销时不递减**。

查看 ESP-IDF 源码 `vfs.c`：
```c
// esp_vfs_register_fs_common() 中：
s_vfs_count++;  // 注册时递增

// esp_vfs_unregister() 中：
s_vfs[id] = NULL;  // 只清空槽位
// 没有 s_vfs_count--  ← 问题所在！
```

这意味着：
- 系统启动时注册 8 个 VFS（uart, secondary, null, console, spiffs, sdcard, socket, www）
- `s_vfs_count = 8`，达到 `CONFIG_VFS_MAX_COUNT=8` 上限
- 卸载 SD 卡后，`/sdcard` 槽位变为 NULL，但 `s_vfs_count` 仍为 8
- 重新挂载时，`esp_vfs_register_common()` 检查 `s_vfs_count >= VFS_MAX_COUNT`，直接返回 `ESP_ERR_NO_MEM`

### 解决方案

**增加 VFS_MAX_COUNT 配置**：

```kconfig
# sdkconfig.defaults
# ============================================================================
# VFS 配置
# ESP-IDF 的 s_vfs_count 在 unregister 时不递减，需要足够大的 MAX_COUNT
# ============================================================================
CONFIG_VFS_MAX_COUNT=16
CONFIG_FATFS_VOLUME_COUNT=4
```

### 诊断代码

在 `ts_storage_sd.c` 中添加的诊断日志（可保留用于未来排查）：

```c
// Mount 前检查
ESP_LOGI(TAG, "=== VFS paths BEFORE mount ===");
esp_vfs_dump_registered_paths();

// Unmount 后验证
uint64_t total, free;
esp_err_t info_ret = esp_vfs_fat_info("/sdcard", &total, &free);
if (info_ret == ESP_ERR_INVALID_STATE) {
    ESP_LOGI(TAG, "Good: /sdcard removed from FATFS s_fat_ctxs[]");
} else {
    ESP_LOGW(TAG, "Warning: /sdcard still in FATFS s_fat_ctxs[]");
}
```

### 经验教训

1. **ESP_ERR_NO_MEM 不一定是堆内存不足**：可能是其他资源（VFS 槽位、文件描述符等）耗尽
2. **ESP-IDF VFS 的 s_vfs_count 只增不减**：这是设计行为，需要配置足够大的 `VFS_MAX_COUNT`
3. **诊断日志的价值**：`esp_vfs_dump_registered_paths()` 是排查 VFS 问题的利器
4. **阅读框架源码**：当文档不足时，直接阅读 ESP-IDF 源码是最可靠的方法

---

## 9. 配置系统加载 JSON 时 Double-Free 崩溃

### 问题描述

**症状**：SD 卡重新挂载成功后，配置系统自动加载 JSON 文件时崩溃。

**错误日志**：
```
assert failed: tlsf_free tlsf.c:630 (!block_is_free(block) && "block already marked as free")

Backtrace:
... → free → free_value → config_set_value → ts_config_set_string → parse_json_value
```

**触发条件**：配置键已存在，重新设置相同类型（STRING）的值。

### 根本原因

`config_set_value()` 中的内存管理逻辑错误：

```c
// 问题代码
} else {
    // 保存旧值用于通知
    had_old_value = true;
    old_value = node->item.value;  // 浅拷贝！
    // ...
}

// 复制新值
copy_value(&node->item.value, value, type, size);  // 这里释放了 val_string

// ... 函数末尾 ...
if (had_old_value) {
    free_value(&old_value, type);  // 再次释放同一指针！
}
```

**问题链**：
1. `old_value = node->item.value` 是浅拷贝
2. `old_value.val_string` 和 `node->item.value.val_string` 指向同一块内存
3. `copy_value()` 内部释放 `dst->val_string`（第一次 free）
4. `free_value(&old_value)` 释放 `old_value.val_string`（第二次 free，同一指针！）

### 解决方案

在调用 `copy_value()` 前，清零 `node->item.value` 中的指针：

```c
} else {
    // 保存旧值用于通知
    had_old_value = true;
    old_value = node->item.value;
    
    if (node->item.type != type) {
        free_value(&node->item.value, node->item.type);
        memset(&node->item.value, 0, sizeof(ts_config_value_t));
        node->item.type = type;
        had_old_value = false;
    } else {
        // 类型相同，清零指针防止 copy_value 中 double-free
        // （旧值已保存在 old_value 中，会在最后统一释放）
        if (type == TS_CONFIG_TYPE_STRING) {
            node->item.value.val_string = NULL;
        } else if (type == TS_CONFIG_TYPE_BLOB) {
            node->item.value.val_blob.data = NULL;
            node->item.value.val_blob.size = 0;
        }
    }
}
```

### 经验教训

1. **浅拷贝结构体时注意指针成员**：指针成员会共享内存，需要明确所有权
2. **内存释放要有清晰的责任归属**：要么调用者释放，要么被调用者释放，不能两边都释放
3. **Double-free 的典型模式**：保存旧值 → 函数内部释放 → 外部再释放旧值
4. **断言信息很有价值**：`block already marked as free` 直接指明是 double-free

---

## 更新日志

- 2026-01-22: 添加 SD 卡重挂载失败（VFS_MAX_COUNT 限制）和配置 double-free 问题修复记录- 2026-01-23: 添加 OTA 回滚机制误判问题和 www 分区 OTA 实现记录

---

## 10. OTA 回滚机制 `ts_ota_is_pending_verify()` 误判问题

### 问题描述

**症状**：新固件首次启动后，WebUI 显示"无需验证固件"，但实际上应该显示验证倒计时横幅，等待用户确认固件。

**错误日志**：
```
I (xxxxx) OTA: ts_ota_is_pending_verify: timer=0x0, state=0 -> false
```

**预期行为**：新固件首次启动时，`ts_ota_is_pending_verify()` 应返回 `true`，触发 WebUI 显示验证横幅。

### 根本原因

`ts_ota_is_pending_verify()` 的实现依赖 `s_rollback_timer` 句柄：

```c
// 错误实现：
bool ts_ota_is_pending_verify(void)
{
    return (s_rollback_timer != NULL);  // timer 在后续才创建！
}
```

**问题链**：
1. `ts_ota_rollback_init()` 在系统启动时被调用
2. 该函数检查 OTA 状态并启动回滚计时器
3. 但 `ts_ota_is_pending_verify()` 可能在计时器创建前被调用
4. 此时 `s_rollback_timer == NULL`，错误返回 `false`

### 解决方案

改用 OTA 状态和分区状态判断：

```c
bool ts_ota_is_pending_verify(void)
{
    // 方法1：检查计时器（如果已启动）
    if (s_rollback_timer != NULL) {
        return true;
    }
    
    // 方法2：检查 OTA 状态机
    if (s_ota_state == TS_OTA_STATE_PENDING_VERIFY) {
        return true;
    }
    
    // 方法3：直接检查分区状态（最可靠）
    // esp_ota_check_rollback_is_possible() 检查：
    // - 当前分区是否标记为 ESP_OTA_IMG_PENDING_VERIFY
    // - 是否有可回滚的分区
    if (esp_ota_check_rollback_is_possible()) {
        return true;
    }
    
    return false;
}
```

### 相关修复

同时修复 `ts_ota_mark_valid()` 绕过组件层直接调用 SDK 的问题：

```c
// 错误实现：
esp_err_t ts_ota_mark_valid(void)
{
    return esp_ota_mark_app_valid_cancel_rollback();  // 绕过组件层！
}

// 正确实现：
esp_err_t ts_ota_mark_valid(void)
{
    return ts_ota_rollback_cancel();  // 通过组件层统一管理
}
```

### 调试技巧

在 `ts_ota_is_pending_verify()` 中添加详细日志：

```c
bool ts_ota_is_pending_verify(void)
{
    bool timer_valid = (s_rollback_timer != NULL);
    bool state_pending = (s_ota_state == TS_OTA_STATE_PENDING_VERIFY);
    bool rollback_possible = esp_ota_check_rollback_is_possible();
    
    ESP_LOGI(TAG, "is_pending_verify: timer=%d, state=%d, rollback=%d",
             timer_valid, state_pending, rollback_possible);
    
    return timer_valid || state_pending || rollback_possible;
}
```

### 经验教训

1. **不要依赖运行时状态判断静态属性**：分区是否待验证是分区表的静态属性，应该查询分区状态
2. **组件层应该是唯一的 API 入口**：避免绕过组件层直接调用底层 SDK
3. **时序问题需要考虑**：在系统启动早期，某些资源可能尚未初始化

---

## 11. WebUI OTA 进度条不更新问题

### 问题描述

**症状**：WebUI 触发 OTA 升级后，进度条卡在 0%，不随实际下载进度更新。状态文字显示"正在下载"但进度条始终为空。

**相关代码**：
```javascript
// startOta() 中启动定时轮询
pollInterval = setInterval(pollProgress, 500);

// pollProgress() 中清除定时器
function pollProgress() {
    if (otaStep === 'idle') {
        clearInterval(pollInterval);  // pollInterval 可能是 undefined！
    }
}
```

### 根本原因

JavaScript 变量作用域问题：

```javascript
let pollInterval;  // 在函数外声明

function startOta() {
    // 启动定时器
    pollInterval = setInterval(pollProgress, 500);
}

function pollProgress() {
    // ... 检查状态 ...
    if (state === 'idle' || state === 'error') {
        clearInterval(pollInterval);  // 这里 pollInterval 可能是旧的或 undefined
        pollInterval = null;
    }
}
```

**问题链**：
1. 第一次 OTA：`pollInterval` 被设置为定时器 ID
2. OTA 完成：`clearInterval(pollInterval)` 清除定时器
3. 第二次 OTA：`pollInterval = setInterval(...)` 设置新 ID
4. 但如果 `pollProgress` 使用了闭包中的旧引用，会导致逻辑错误

### 解决方案

确保 `pollInterval` 变量正确管理：

```javascript
let pollInterval = null;

function startOta() {
    // 先清除可能存在的旧定时器
    if (pollInterval) {
        clearInterval(pollInterval);
        pollInterval = null;
    }
    
    // 启动新定时器
    pollInterval = setInterval(pollProgress, 500);
}

function pollProgress() {
    if (otaStep === 'idle' || data.state === 'error') {
        if (pollInterval) {
            clearInterval(pollInterval);
            pollInterval = null;
        }
        return;
    }
    
    // 更新进度
    updateProgressUI(data.percent, data.message);
}
```

### 额外修复：上传方式的进度显示

对于 WebUI 上传方式，固件数据通过 HTTP POST 发送，后端写入 Flash 期间前端无法获取进度。

**解决方案**：添加 keepalive 机制

```javascript
let keepaliveInterval = null;

function uploadOta() {
    // 启动 keepalive 定时器，定期查询进度
    keepaliveInterval = setInterval(async () => {
        const progress = await api.call('ota.progress');
        updateProgressUI(progress.percent, progress.message);
    }, 5000);  // 每 5 秒查询一次
    
    // 执行上传
    const response = await fetch('/api/v1/ota/upload', {
        method: 'POST',
        body: formData
    });
    
    // 清除 keepalive
    clearInterval(keepaliveInterval);
}
```

### 经验教训

1. **定时器 ID 必须正确保存和清除**：避免内存泄漏和逻辑错误
2. **长时间操作需要 keepalive**：HTTP 连接可能超时，需要定期发送请求保活
3. **前端状态管理要清晰**：使用明确的变量跟踪 OTA 阶段（idle/app/www）

---

## 9. SSH 功能问题与调试

### 9.1 已知主机列表初始为空

**症状**：
- WebUI "安全管理"页面打开时，"已知主机"列表为空
- SSH 连接失败后刷新页面，列表突然显示出主机记录
- 日志显示 NVS 中确实存储了主机密钥

**根本原因**：

`ts_known_hosts` 模块采用**延迟初始化**策略：
- 只在第一次调用 `ts_known_hosts_verify()` 时初始化
- 安全服务启动时未主动初始化
- 导致 `ts_known_hosts_list()` 返回 `ESP_ERR_INVALID_STATE`

**解决方案**：

在安全服务初始化时主动调用 `ts_known_hosts_init()`：

**文件**：[main/ts_services.c](../main/ts_services.c)
```c
static esp_err_t security_service_init(ts_service_handle_t handle, void *user_data)
{
    // ... 其他初始化 ...
    
    /* 初始化已知主机管理 */
    ret = ts_known_hosts_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to init known hosts: %s", esp_err_to_name(ret));
    }
    
    return ESP_OK;
}
```

**调试日志**：
```c
// ts_known_hosts.c
ESP_LOGI(TAG, "Listing known hosts, max_hosts=%zu", max_hosts);
ESP_LOGI(TAG, "NVS iterator result: %s", esp_err_to_name(ret));
ESP_LOGI(TAG, "Found NVS entry: key='%s', type=%d", info.key, info.type);
ESP_LOGI(TAG, "Returning %zu known hosts", *count);

// ts_api_hosts.c
TS_LOGI(TAG, "API: hosts.list called");
TS_LOGI(TAG, "API: ts_known_hosts_list returned %s, count=%zu", esp_err_to_name(ret), count);
```

---

### 9.2 SSH 测试密钥不匹配时显示"未知错误"

**症状**：
- SSH 连接到主机密钥已变化的服务器
- 后端正确检测到 HOST KEY MISMATCH
- WebUI 显示"❌ 连接失败: 未知错误"，未弹出警告模态框

**根本原因**：

前端代码逻辑错误，未检查应用层错误码：
```javascript
// ❌ 错误的逻辑
try {
    const result = await api.sshTest(...);
    if (result.data?.success) {
        // 成功
    } else {
        // ❌ 进入这里，显示"未知错误"
        resultBox.textContent = '❌ 连接失败: ' + (result.data?.error || '未知错误');
    }
} catch (e) {
    // ❌ 期望在这里检查 e.code === 1001，但实际没有抛出异常
    if (e.code === 1001) {
        showHostMismatchModal(...);
    }
}
```

**问题分析**：
- 后端返回 HTTP 200（成功），`result.code = 1001`（应用层错误码）
- 前端没有检查 `result.code`，直接进入 `else` 分支
- `catch` 块只处理真正的网络异常，不会执行

**解决方案**：

修改前端逻辑，先检查 `result.code`：

**文件**：[components/ts_webui/web/js/app.js](../components/ts_webui/web/js/app.js)
```javascript
try {
    const result = await api.sshTest(host, user, auth, port);
    
    // ✅ 先检查应用层错误码
    if (result.code === 1001) {
        // 主机指纹不匹配
        showHostMismatchModal(result.data || { ... });
        resultBox.textContent = '⚠️ 主机指纹不匹配! 可能存在中间人攻击风险';
        resultBox.classList.add('error');
        return;
    }
    
    if (result.code === 1002) {
        // 新主机需要确认
        resultBox.textContent = '🆕 新主机: ' + (result.data?.fingerprint || '');
        resultBox.classList.add('warning');
        return;
    }
    
    // 检查连接结果
    if (result.data?.success) {
        // 成功
    } else {
        resultBox.textContent = '❌ 连接失败: ' + (result.data?.error || result.message || '未知错误');
    }
} catch (e) {
    // 只处理真正的网络错误
    console.error('SSH test error:', e);
    resultBox.textContent = '❌ 连接失败: ' + e.message;
}
```

**后端错误处理**：

确保密钥不匹配时返回 HTTP 200（而非 500）：

**文件**：[components/ts_api/src/ts_api_ssh.c](../components/ts_api/src/ts_api_ssh.c)
```c
ret = verify_host_fingerprint(session, params, result, &host_info);
if (ret != ESP_OK) {
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    cleanup_key_buffer();
    
    // 如果是 MISMATCH 或 NEW_HOST，返回 ESP_OK 让 HTTP 层返回 200
    // 实际错误信息已在 result 中设置
    if (result->code == TS_API_ERR_HOST_MISMATCH || 
        result->code == TS_API_ERR_HOST_NEW) {
        return ESP_OK;  // HTTP 200，但 result.code 指示实际问题
    }
    return ret;
}
```

---

### 9.3 主机指纹更新功能

当检测到 SSH 主机密钥不匹配时，允许用户通过 WebUI 一键更新主机密钥。

#### 后端实现

**文件**：[components/ts_api/src/ts_api_hosts.c](../components/ts_api/src/ts_api_hosts.c)
```c
/**
 * @brief hosts.update - Force update a host fingerprint
 * 
 * 强制更新已知主机的指纹。用于当服务器重装导致指纹变化时。
 * 
 * Params: { "host": "192.168.1.100", "port": 22 }
 */
static esp_err_t api_hosts_update(const cJSON *params, ts_api_result_t *result)
{
    // ... 参数检查 ...
    
    /* 先删除旧的 */
    esp_err_t ret = ts_known_hosts_remove(host->valuestring, port);
    if (ret != ESP_OK && ret != ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to remove old host key");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "updated", true);
    cJSON_AddStringToObject(data, "message", "Old host key removed. Reconnect to trust the new key.");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}
```

#### 前端实现

**API 客户端**：[components/ts_webui/web/js/api.js](../components/ts_webui/web/js/api.js)
```javascript
async hostsUpdate(host, port = 22) { 
    return this.call('hosts.update', { host, port }, 'POST'); 
}
```

**WebUI 交互流程**：
```javascript
async function removeAndRetry() {
    if (!currentMismatchInfo) return;
    
    try {
        // 使用新的 hosts.update API
        await api.hostsUpdate(currentMismatchInfo.host, currentMismatchInfo.port || 22);
        showToast('旧主机密钥已移除，请重新连接以信任新密钥', 'success');
        hideHostMismatchModal();
        await refreshSecurityPage();
    } catch (e) {
        showToast('更新失败: ' + e.message, 'error');
    }
}
```

**使用流程**：
1. SSH 测试连接到指纹已变化的服务器
2. 自动弹出警告模态框，显示新旧指纹对比
3. 点击"🔄 更新主机密钥"按钮
4. 系统删除旧记录
5. 重新连接，自动信任新密钥

---

### 9.4 SSH 调试技巧

#### 分层日志追踪
```
用户操作 → 前端 API 调用 → 后端 API 处理 → 核心服务 → NVS 存储
```
在每一层添加日志，明确数据流向。

#### 错误码设计原则
- **HTTP 状态码**：传输层错误（200/404/500）
- **`result.code`**：应用层错误码（0=成功，1001=MISMATCH，1002=NEW_HOST）
- **前端优先检查应用层错误码**

#### 初始化时机权衡
- **延迟初始化**：节省资源，但需要处理未初始化状态
- **主动初始化**：更可靠，适合系统服务
- **建议**：频繁使用的模块（如 known hosts）采用主动初始化

#### WebUI 调试工具
- 浏览器开发者工具 Console 查看 API 请求/响应
- ESP32 串口日志查看后端处理流程
- WebUI 日志页面实时查看系统日志

---

## 10. SNTP 时钟同步配置问题

### 10.1 SNTP 服务器数量超限错误

**症状**：
```
E (2826) esp_netif_sntp: sntp_init_api(48): Tried to configure more servers than enabled in lwip. Please update CONFIG_SNTP_MAX_SERVERS
E (2838) esp_netif_sntp: esp_netif_sntp_init(119): Failed initialize SNTP service
E (2845) ts_time_sync: Failed to init SNTP: ESP_ERR_INVALID_ARG
```

**根本原因**：

ESP-IDF 的 LWIP 配置默认只支持 1 个 SNTP 服务器（`CONFIG_LWIP_SNTP_MAX_SERVERS=1`），但代码尝试配置 2 个服务器。

**解决方案**：

1. **增加 SNTP 服务器配置数量**

**文件**：[sdkconfig.defaults](../sdkconfig.defaults)
```plaintext
# SNTP 配置
CONFIG_LWIP_SNTP_MAX_SERVERS=2
```

2. **正确配置双服务器**

**文件**：[components/ts_net/src/ts_time_sync.c](../components/ts_net/src/ts_time_sync.c)
```c
esp_err_t ts_time_sync_start_ntp(void)
{
    // ... 参数检查 ...
    
    ESP_LOGI(TAG, "Starting NTP sync with primary: %s, secondary: %s", 
             s_time_sync.ntp_server1, 
             s_time_sync.ntp_server2[0] ? s_time_sync.ntp_server2 : "none");
    
    /* 配置 SNTP */
    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_time_sync.ntp_server1);
    sntp_config.sync_cb = time_sync_notification_cb;
    
    /* 配置第二个服务器（如果有）*/
    if (s_time_sync.ntp_server2[0]) {
        sntp_config.num_of_servers = 2;
        sntp_config.servers[0] = s_time_sync.ntp_server1;
        sntp_config.servers[1] = s_time_sync.ntp_server2;
    }
    
    /* 配置重试策略：服务器可能在启动初期不可用 */
    sntp_config.start = true;
    sntp_config.sync_mode = SNTP_SYNC_MODE_SMOOTH;
    sntp_config.wait_for_sync = false;  // 不阻塞等待
    
    esp_err_t ret = esp_netif_sntp_init(&sntp_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SNTP: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_time_sync.ntp_started = true;
    s_time_sync.info.status = TS_TIME_SYNC_IN_PROGRESS;
    
    ESP_LOGI(TAG, "NTP synchronization started (retry enabled for unreliable servers)");
    return ESP_OK;
}
```

---

### 10.2 SNTP 服务器不可用处理

**场景**：
- 内网时钟服务器（10.10.99.99、10.10.99.98）可能在启动前 100 秒内不在线
- 服务器可能长期不可用

**处理策略**：

1. **不阻塞启动**：`wait_for_sync = false`，允许系统在时间未同步时继续运行
2. **平滑同步模式**：`SNTP_SYNC_MODE_SMOOTH`，时间调整更平滑
3. **自动重试**：SNTP 库会自动重试连接不可用的服务器
4. **双服务器冗余**：配置两个服务器，提高可用性

**用户操作**：
- 通过 WebUI "系统时间"功能手动设置时间
- 或使用"浏览器同步"功能从客户端获取时间
- 时钟服务器恢复后会自动同步

**日志示例**：
```
I (2820) ts_time_sync: Starting NTP sync with primary: 10.10.99.99, secondary: 10.10.99.98
I (2825) ts_time_sync: NTP synchronization started (retry enabled for unreliable servers)
W (5000) ts_time_sync: NTP sync timeout, will retry...
I (65000) ts_time_sync: Time synchronized from 10.10.99.99
```

---

### 10.3 时间同步最佳实践

| 场景 | 推荐方案 | 说明 |
|------|---------|------|
| 内网环境 | 双 NTP 服务器 | 10.10.99.99 + 10.10.99.98 |
| 互联网环境 | pool.ntp.org | 使用公共 NTP 池 |
| 离线环境 | 浏览器同步 | WebUI 手动同步时间 |
| 开发调试 | 禁用 NTP | 避免启动延迟 |

**配置示例**：

```bash
# CLI 配置 NTP 服务器
config --set time.ntp1 --value 10.10.99.99 --persist
config --set time.ntp2 --value 10.10.99.98 --persist

# 启动 NTP 同步
system --time --sync-ntp

# 手动设置时间（UTC）
system --time --set "2026-01-23T15:30:00Z"
```
---

## 11. WebSocket 连接管理与终端会话问题

### 11.1 WebSocket 连接槽位泄漏

**症状**:
- WebUI 运行一段时间后出现大量警告: `W webui_ws: No free WebSocket slots`
- 终端/日志功能无法使用
- 必须重启设备才能恢复

**根本原因**:

WebSocket 连接断开时，ESP32 HTTP 服务器可能未立即调用断开事件处理器，导致连接槽位未释放。主要原因:
1. **浏览器刷新页面**: 旧连接未正常关闭，新连接建立
2. **网络波动**: 连接实际断开但未被检测到
3. **异步发送无法检测断连**: `httpd_ws_send_frame_async()` 即使连接断开也可能返回 `ESP_OK`

**解决方案**:

#### 方案 1: 增加连接槽位数量

**文件**: [components/ts_webui/Kconfig](../components/ts_webui/Kconfig)
```kconfig
config TS_WEBUI_WS_MAX_CLIENTS
    int "Max WebSocket Clients"
    default 8  # 从 4 增加到 8
    depends on TS_WEBUI_WS_ENABLE
```

#### 方案 2: 主动检测陈旧连接

使用底层 socket 状态检测替代不可靠的 WebSocket ping:

**文件**: [components/ts_webui/src/ts_webui_ws.c](../components/ts_webui/src/ts_webui_ws.c)
```c
#include <sys/socket.h>
#include <errno.h>

static void add_client(httpd_handle_t hd, int fd, ws_client_type_t type)
{
    // 检测陈旧连接并清理
    char probe;
    int result = recv(s_clients[i].fd, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
    
    if (result == 0 || (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        // Socket 已断开，清理槽位
        cleanup_disconnected_client(old_fd);
    }
}
```

**关键技术**:
- `recv(fd, buf, 1, MSG_PEEK | MSG_DONTWAIT)` - 窥探 socket 而不消耗数据
- `result == 0` - 对端已关闭连接 (FIN 收到)
- `errno == EAGAIN/EWOULDBLOCK` - 无数据可读 (连接正常)

---

### 11.2 终端会话"Another terminal session is active"问题

**症状**:
- WebUI 终端页面刷新后无法重新连接
- 错误提示: `错误: Another terminal session is active`
- 串口日志: `W webui_ws: Terminal session already active (fd=51), rejecting new request from fd=52`

**解决方案: 终端会话接管策略**

采用"新优先"策略，允许新终端请求主动接管旧会话:

```c
static void start_terminal_session(httpd_req_t *req)
{
    if (s_terminal_client_fd >= 0 && s_terminal_client_fd != fd) {
        // 采用"新优先"策略: 主动关闭旧会话
        TS_LOGI(TAG, "Terminal takeover: closing old session (fd=%d) for new request (fd=%d)", 
                s_terminal_client_fd, fd);
        
        // 向旧会话发送关闭通知
        send_close_notification(old_hd, s_terminal_client_fd, "Another terminal session requested");
        
        // 清理旧会话
        cleanup_disconnected_client(s_terminal_client_fd);
        s_terminal_client_fd = -1;
    }
}
```

**策略优势**:
1. **用户友好**: 刷新页面立即恢复终端，无需手动关闭旧会话
2. **容错性强**: 不依赖不可靠的连接检测
3. **明确通知**: 旧会话收到 `session_closed` 消息

---

### 11.3 WebSocket 调试技巧

#### 串口日志关键信息
```
I webui_ws: WebSocket client connected (fd=51, type=event)
I webui_ws: Terminal takeover: closing old session (fd=51) for new request (fd=52)
I webui_ws: Cleaned up stale connection (fd=48, recv=0, errno=0)
```

#### ESP32 内存监控
```c
ESP_LOGI(TAG, "Active slots: %d/%d", active_count, MAX_WS_CLIENTS);
ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
```

---

### 11.4 WebSocket 最佳实践

| 场景 | 建议配置 | 说明 |
|------|---------|------|
| 开发调试 | `MAX_CLIENTS=16` | 允许频繁刷新 |
| 生产环境 | `MAX_CLIENTS=8` | 平衡资源和连接数 |
| 多用户 | 实现用户隔离 | 每用户限制连接数 |

---

### 11.5 相关文件索引

| 文件 | 功能 |
|------|------|
| [ts_webui_ws.c](../components/ts_webui/src/ts_webui_ws.c) | WebSocket 连接管理 |
| [ts_webui/Kconfig](../components/ts_webui/Kconfig) | WebSocket 配置选项 |
| [app.js](../components/ts_webui/web/js/app.js) | WebUI 终端前端 |

---

## 更新日志

- 2026-01-22: 添加 SD 卡重挂载失败 (VFS_MAX_COUNT 限制) 和配置 double-free 问题修复记录
- 2026-01-23: 添加 OTA 回滚机制误判问题、www 分区 OTA 实现、SSH 功能调试记录
- 2026-01-23: 添加 SNTP 双服务器配置和 WiFi 开放网络连接问题记录
- 2026-01-23: 添加 WebSocket 连接管理和终端会话问题完整调试过程
