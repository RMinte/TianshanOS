# TianShanOS 命令规范文档

> **版本**：1.0  
> **日期**：2026年1月15日

---

## 📋 命令格式规范

### 基本格式

TianShanOS 采用**参数风格**命令格式，所有命令遵循统一规范：

```
<command> [options] [arguments]
```

### 选项格式

| 格式 | 说明 | 示例 |
|-----|------|-----|
| `--option value` | 长选项带值 | `--speed 75` |
| `--option=value` | 长选项带值（等号） | `--speed=75` |
| `-o value` | 短选项带值 | `-s 75` |
| `-o=value` | 短选项带值（等号） | `-s=75` |
| `--flag` | 布尔标志（开启） | `--verbose` |
| `--no-flag` | 布尔标志（关闭） | `--no-verbose` |
| `-f` | 短布尔标志 | `-v` |

### 通用选项

所有命令都支持以下通用选项：

| 选项 | 短选项 | 说明 |
|-----|-------|------|
| `--help` | `-h` | 显示命令帮助 |
| `--json` | `-j` | 以 JSON 格式输出 |
| `--quiet` | `-q` | 静默模式，只输出结果 |
| `--verbose` | `-v` | 详细模式，输出调试信息 |

---

## 📦 命令分类

### 系统命令

```bash
# 显示系统信息
system --info
system -i

# 重启系统
system --reboot
system --reboot --delay 5        # 延迟5秒重启

# 关机（待实现）
system --shutdown

# 显示版本
system --version
system -V

# 显示运行时间
system --uptime

# 显示内存使用
system --memory

# 显示任务列表
system --tasks
```

### 服务管理命令

```bash
# 列出所有服务
service --list
service -l

# 显示服务详情
service --status --name fan_controller
service -s -n fan_controller

# 启动服务
service --start --name fan_controller
service --start -n fan_controller

# 停止服务
service --stop --name fan_controller

# 重启服务
service --restart --name fan_controller

# 启用/禁用服务自启动
service --enable --name fan_controller
service --disable --name fan_controller

# 显示服务依赖
service --deps --name fan_controller
```

### 配置管理命令

```bash
# 获取配置值
config --get --key system.language
config -g -k system.language

# 设置配置值（临时）
config --set --key fan.default_speed --value 50

# 设置配置值（持久化）
config --set --key fan.default_speed --value 50 --persist
config --set -k fan.default_speed -v 50 -p

# 列出所有配置
config --list
config --list --namespace fan      # 指定命名空间

# 导出配置到文件
config --export --file /sdcard/config_backup.json

# 从文件导入配置
config --import --file /sdcard/config_backup.json

# 重置配置为默认值
config --reset --key fan.default_speed
config --reset --namespace fan     # 重置整个命名空间
config --reset --all               # 重置所有配置

# 显示配置变更历史
config --history
```

### 引脚管理命令

```bash
# 显示引脚状态
pin --list
pin --list --used                  # 只显示已使用的引脚
pin --list --available             # 只显示可用引脚

# 显示引脚映射
pin --mapping

# 获取引脚信息
pin --info --function LED_MATRIX
pin --info --gpio 9

# 测试 GPIO（调试用）
pin --test --gpio 2 --mode output --level high
pin --test --gpio 2 --mode input
```

### 风扇控制命令

```bash
# 显示风扇状态
fan --status
fan --status --id 0                # 指定风扇

# 设置风扇速度
fan --set --id 0 --speed 75
fan -s --id 0 -S 75

# 设置风扇模式
fan --mode --id 0 --value manual
fan --mode --id 0 --value auto
fan --mode --id 0 --value curve

# 启用/禁用风扇
fan --enable --id 0
fan --disable --id 0

# 配置温度曲线
fan --curve --id 0 --points "30:20,50:40,70:60,80:100"

# 保存/加载配置
fan --save
fan --save --id 0
fan --load
```

### 温度管理命令

```bash
# 显示温度状态
temp --status

# 获取当前温度
temp --get

# 设置测试温度（调试用）
temp --set --value 45

# 切换温度模式
temp --mode --value auto
temp --mode --value manual
```

### LED 控制命令

```bash
# === 通用 LED 命令 ===

# 列出所有 LED 设备
led --list

# 显示设备状态
led --status --device matrix
led -s -d matrix

# 设置亮度
led --brightness --device matrix --value 80
led -b -d matrix -v 80

# 清除显示
led --clear --device matrix

# 填充颜色
led --fill --device matrix --color "#FF0000"
led --fill -d matrix -c red          # 支持颜色名称

# 刷新显示
led --show --device matrix

# === 单像素操作 ===

# 设置单个像素
led --pixel --device matrix --index 0 --color "#00FF00"
led --pixel -d matrix -i 0 -c green

# 矩阵坐标方式
led --pixel --device matrix --x 16 --y 16 --color blue

# === 动画控制 ===

# 播放动画
led --animation --device matrix --name rainbow
led -a -d matrix -n rainbow

# 播放动画（带参数）
led --animation -d matrix -n breathe --speed 50 --color red

# 停止动画
led --animation --device matrix --stop

# 列出可用动画
led --animation --list

# 加载动画文件
led --animation --load --file /sdcard/animations/custom.json --name my_anim

# 导出当前动画
led --animation --export --file /sdcard/animations/export.json

# === 特效控制 ===

# 应用特效
led --effect --device board --name fire
led --effect -d board -n wave --speed 80

# 列出可用特效
led --effect --list

# 停止特效
led --effect --device board --stop

# === 图层操作（矩阵专用）===

# 创建图层
led --layer --device matrix --create --name overlay --z-order 1

# 设置图层透明度
led --layer --device matrix --name overlay --opacity 128

# 删除图层
led --layer --device matrix --name overlay --delete

# 列出图层
led --layer --device matrix --list

# === 图像操作（矩阵专用）===

# 显示图像
led --image --device matrix --file /sdcard/images/logo.png

# 显示图像（带缩放）
led --image -d matrix -f /sdcard/images/logo.png --scale fit

# 导出当前画面
led --image --device matrix --export --file /sdcard/export.png

# 支持的图像格式：BMP, PNG, JPG, GIF
# GIF 会自动播放动画

# === 状态绑定 ===

# 绑定状态指示
led --bind --device touch --source agx.status --config /sdcard/status_map.json

# 解除绑定
led --unbind --device touch

# === 配置保存 ===

# 保存配置
led --config --save
led --config --save --device matrix

# 加载配置
led --config --load

# 重置配置
led --config --reset --device matrix
```

### 网络管理命令

```bash
# 显示网络状态
net --status

# 显示 IP 配置
net --ip

# 设置静态 IP
net --set --ip 10.10.99.97 --netmask 255.255.255.0 --gateway 10.10.99.1

# 启用/禁用网络接口
net --enable
net --disable

# 重置网络
net --reset

# DHCP 服务器管理
net --dhcp --status
net --dhcp --enable
net --dhcp --disable
net --dhcp --clients              # 显示 DHCP 客户端

# 显示网络日志
net --log
net --log --count 20              # 显示20条日志

# 保存网络配置
net --save
```

### 设备控制命令

```bash
# AGX 控制
device --agx --power on
device --agx --power off
device --agx --power restart
device --agx --status

# LPMU 控制
device --lpmu --power on
device --lpmu --power off
device --lpmu --reset
device --lpmu --status

# USB MUX 控制
device --usb-mux --target esp32
device --usb-mux --target agx
device --usb-mux --target lpmu
device --usb-mux --status
```

### 存储管理命令

```bash
# 显示存储状态
storage --status

# 挂载/卸载 SD 卡
storage --mount
storage --unmount

# 格式化 SD 卡
storage --format --confirm

# 列出文件
storage --list --path /sdcard/
storage --list -p /sdcard/ --recursive

# 读取文件
storage --read --file /sdcard/config.json

# 写入文件
storage --write --file /sdcard/test.txt --content "Hello"

# 删除文件
storage --delete --file /sdcard/test.txt

# 创建目录
storage --mkdir --path /sdcard/logs/

# 显示磁盘空间
storage --space
```

### 安全管理命令

```bash
# 密钥管理
security --key --generate --type ed25519 --name device_key
security --key --list
security --key --export-public --name device_key --file /sdcard/device_key.pub
security --key --delete --name old_key

# SSH 客户端
security --ssh --connect --host 10.10.99.98 --user admin --key device_key
security --ssh --exec --host 10.10.99.98 --command "ls -la"
security --ssh --allowed-commands           # 显示允许的命令列表

# mTLS 管理
security --mtls --status
security --mtls --generate-cert             # 生成自签名证书
security --mtls --import-ca --file /sdcard/ca.pem

# 会话管理
security --session --list
security --session --terminate --id 12345
```

### 脚本命令

```bash
# 执行脚本文件
script --run --file /sdcard/scripts/startup.ts

# 执行单行命令
script --exec "fan --set --id 0 --speed 75"

# 列出可用脚本
script --list

# 创建定时任务
script --schedule --name check_temp --file /sdcard/scripts/temp_check.ts --interval 60

# 取消定时任务
script --schedule --cancel --name check_temp
```

### 帮助命令

```bash
# 显示总帮助
help

# 显示命令帮助
help fan
help led --animation

# 搜索命令
help --search "temperature"

# 显示命令示例
help fan --examples
```

---

## 🌍 多语言支持

### 语言切换

```bash
# 切换到中文
config --set --key system.language --value zh-CN --persist

# 切换到英文
config --set --key system.language --value en --persist

# 查看当前语言
config --get --key system.language
```

### 支持的语言

| 代码 | 语言 |
|-----|------|
| `en` | English |
| `zh-CN` | 简体中文 |

---

## 📜 脚本语法

### 基本语法

```bash
# 这是注释
# TianShanOS Script v1.0

# 变量定义
$speed = 75
$device = "matrix"

# 执行命令
fan --set --id 0 --speed $speed
led --brightness --device $device --value 80

# 等待（毫秒）
wait 1000

# 条件判断
if ${temp.current} > 60
    fan --set --id 0 --speed 100
    led --fill --device touch --color red
elif ${temp.current} > 40
    fan --set --id 0 --speed 75
    led --fill --device touch --color yellow
else
    fan --set --id 0 --speed 50
    led --fill --device touch --color green
endif

# 循环
loop 5
    led --animation --device board --name rainbow
    wait 5000
    led --animation --device board --name breathe
    wait 5000
endloop

# 无限循环
loop forever
    # ...
    break              # 跳出循环
endloop

# 调用其他脚本
call /sdcard/scripts/common.ts

# 定义函数
function set_status($color)
    led --fill --device touch --color $color
    led --show --device touch
endfunction

# 调用函数
set_status(red)
set_status(green)
```

### 内置变量

| 变量 | 说明 |
|-----|------|
| `${system.uptime}` | 系统运行时间（秒） |
| `${system.memory.free}` | 可用内存（字节） |
| `${temp.current}` | 当前温度 |
| `${temp.source}` | 温度来源 |
| `${fan.0.speed}` | 风扇0速度 |
| `${net.ip}` | IP 地址 |
| `${net.connected}` | 网络连接状态 |
| `${agx.status}` | AGX 状态 |
| `${lpmu.status}` | LPMU 状态 |

---

## 📊 输出格式

### 标准输出

```
[OK] 操作成功完成
[ERROR] 操作失败: 详细错误信息
[WARN] 警告信息
[INFO] 提示信息
```

### JSON 输出

使用 `--json` 选项时，输出 JSON 格式：

```json
{
  "success": true,
  "code": 0,
  "message": "Operation completed",
  "data": {
    "fan_id": 0,
    "speed": 75,
    "mode": "manual"
  }
}
```

```json
{
  "success": false,
  "code": -1,
  "message": "Invalid parameter: speed must be 0-100",
  "data": null
}
```

---

## 🔐 权限要求

| 权限级别 | 说明 | 可执行命令示例 |
|---------|------|--------------|
| `NONE` | 无权限 | - |
| `READ` | 只读 | `--status`, `--list`, `--info` |
| `OPERATE` | 操作 | `--set`, `--enable`, `--start` |
| `ADMIN` | 管理员 | `--reset`, `--format`, `--key` |

CLI（串口）默认具有 ADMIN 权限。
WebUI 需要 mTLS 认证，权限由证书确定。

---

## 📝 命令开发规范

### 命令注册示例

```c
#include "ts_console.h"
#include "argtable3.h"

// 定义参数
static struct {
    struct arg_str *device;
    struct arg_int *brightness;
    struct arg_lit *help;
    struct arg_end *end;
} led_brightness_args;

// 命令处理函数
static int cmd_led_brightness(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&led_brightness_args);
    
    if (led_brightness_args.help->count > 0) {
        printf("Usage: led --brightness --device <device> --value <0-100>\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, led_brightness_args.end, argv[0]);
        return 1;
    }
    
    const char *device = led_brightness_args.device->sval[0];
    int brightness = led_brightness_args.brightness->ival[0];
    
    // 调用 Core API
    ts_api_result_t result = ts_api_led_set_brightness(device, brightness);
    
    return result.code;
}

// 注册命令
void register_led_commands(void) {
    led_brightness_args.device = arg_str1("d", "device", "<device>", "LED device name");
    led_brightness_args.brightness = arg_int1("v", "value", "<0-100>", "Brightness value");
    led_brightness_args.help = arg_lit0("h", "help", "Show help");
    led_brightness_args.end = arg_end(5);
    
    const esp_console_cmd_t cmd = {
        .command = "led",
        .help = "LED control commands",
        .hint = NULL,
        .func = &cmd_led_brightness,
        .argtable = &led_brightness_args
    };
    
    esp_console_cmd_register(&cmd);
}
```

---

## 📚 参考

- [ESP-IDF Console 组件](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/console.html)
- [argtable3 文档](http://www.argtable.org/)

---

**文档版本**：1.0  
**状态**：已确定
