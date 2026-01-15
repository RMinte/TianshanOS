# TianShanOS 命令参考手册

本文档详细描述了 TianShanOS 控制台的所有可用命令及其参数。

---

## 目录

- [system - 系统信息与控制](#system---系统信息与控制)
- [service - 服务管理](#service---服务管理)
- [config - 配置管理](#config---配置管理)
- [led - LED 控制](#led---led-控制)
- [fan - 风扇控制](#fan---风扇控制)
- [storage - 存储管理](#storage---存储管理)
- [net - 网络管理](#net---网络管理)
- [device - 设备控制](#device---设备控制)

---

## system - 系统信息与控制

查看系统信息、资源使用情况以及执行系统控制操作。

### 语法

```
system [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--info` | `-i` | 显示系统概览（默认） |
| `--version` | `-v` | 显示版本信息 |
| `--uptime` | `-u` | 显示系统运行时间 |
| `--memory` | `-m` | 显示内存使用情况 |
| `--tasks` | `-t` | 显示任务/线程列表 |
| `--reboot` | | 重启系统 |
| `--delay <ms>` | | 重启延时（毫秒） |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 示例

```bash
# 显示系统概览
system --info

# 显示版本
system --version

# 显示运行时间
system --uptime

# 显示内存使用（JSON 格式）
system --memory --json

# 显示所有任务
system --tasks

# 延时 1 秒后重启
system --reboot --delay 1000
```

---

## service - 服务管理

管理 TianShanOS 服务的启动、停止和状态查询。

### 语法

```
service [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--list` | `-l` | 列出所有服务 |
| `--status` | `-s` | 查看服务状态 |
| `--start` | | 启动服务 |
| `--stop` | | 停止服务 |
| `--restart` | | 重启服务 |
| `--deps` | | 显示服务依赖关系 |
| `--name <name>` | `-n` | 指定服务名称 |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 可用服务

| 服务名 | 说明 |
|--------|------|
| `hal` | 硬件抽象层 |
| `storage` | 存储服务 |
| `led` | LED 控制服务 |
| `drivers` | 驱动管理服务 |
| `console` | 控制台服务 |

### 示例

```bash
# 列出所有服务
service --list

# 查看特定服务状态
service --status --name led

# 启动服务
service --start --name led

# 停止服务
service --stop --name led

# 重启服务
service --restart --name led

# 显示服务依赖关系
service --deps --name led

# JSON 格式输出
service --list --json
```

---

## config - 配置管理

读取、修改和管理系统配置参数。

### 语法

```
config [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--get` | `-g` | 获取配置值 |
| `--set` | `-s` | 设置配置值 |
| `--list` | `-l` | 列出所有配置 |
| `--reset` | | 重置配置到默认值 |
| `--key <key>` | `-k` | 配置键名 |
| `--value <val>` | `-v` | 配置值 |
| `--persist` | `-p` | 持久化保存到 NVS |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 配置键格式

配置键使用点号分隔的层级结构：`category.subcategory.key`

常见配置键：
- `led.brightness` - LED 默认亮度
- `led.touch.gpio` - Touch LED GPIO 引脚
- `fan.speed` - 风扇速度
- `network.dhcp` - DHCP 开关

### 示例

```bash
# 列出所有配置
config --list

# 获取特定配置
config --get --key led.brightness

# 设置配置值（临时）
config --set --key led.brightness --value 128

# 设置并持久化保存
config --set --key led.brightness --value 128 --persist

# 重置所有配置
config --reset

# 重置特定配置
config --reset --key led.brightness

# JSON 格式输出
config --list --json
```

---

## led - LED 控制

控制 LED 设备的颜色、亮度和特效。

### 语法

```
led [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--status` | `-s` | 显示 LED 设备状态 |
| `--on` | | 点亮 LED |
| `--off` | | 关闭 LED |
| `--brightness` | `-b` | 获取/设置亮度 |
| `--clear` | `-c` | 清除 LED（熄灭） |
| `--effect` | `-e` | 启动程序动画（Animation） |
| `--stop-effect` | | 停止运行中的程序动画 |
| `--list-effects` | | 列出可用程序动画 |
| `--filter` | | 应用后处理效果（Filter） |
| `--stop-filter` | | 停止后处理效果 |
| `--list-filters` | | 列出可用后处理效果 |
| `--filter-name <name>` | | 后处理效果名称 |
| `--image` | | 显示图像/动画（仅 matrix） |
| `--qrcode` | | 生成并显示 QR 码（仅 matrix） |
| `--text <string>` | | QR 码内容（URL/文本） |
| `--ecc <L\|M\|Q\|H>` | | QR 纠错等级 |
| `--file <path>` | `-f` | 图像文件路径（PNG/GIF） |
| `--center <mode>` | | 居中模式：image（图像居中）或 content（内容居中） |
| `--parse-color` | | 解析颜色信息 |
| `--device <name>` | `-d` | 指定设备名称 |
| `--name <effect>` | `-n` | 程序动画名称 |
| `--value <0-255>` | `-v` | 亮度值 |
| `--color <color>` | | 颜色值 |
| `--speed <1-100>` | | 动画/效果速度（1=慢, 100=快） |
| `--save` | | 保存当前状态为开机配置（含动画和后处理效果） |
| `--show-boot` | | 显示已保存的开机配置 |
| `--clear-boot` | | 清除开机配置 |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### Animation vs Filter

TianShanOS LED 系统区分两类效果：

| 类型 | 说明 | 示例 |
|------|------|------|
| **Animation（程序动画）** | 生成帧内容的程序 | rainbow, fire, rain, plasma |
| **Filter（后处理效果）** | 对已有内容进行后处理 | pulse, blink, breathing, color-shift |

- Animation 是内容生成器，可独立运行
- Filter 叠加在 Animation 或 Image 之上，对输出进行调整
- 两者可以组合使用：Animation + Filter

### 可用设备

| 设备名 | 别名 | LED 数量 | 布局 | 说明 |
|--------|------|----------|------|------|
| `led_touch` | `touch` | 1 | 点光源 | 触摸板指示 LED（单颗 WS2812） |
| `led_board` | `board` | 28 | 环形 | PCB 边缘环板灯（28 颗 WS2812 环形排列） |
| `led_matrix` | `matrix` | 1024 | 矩阵 | LED 矩阵屏（32x32 WS2812 网格排列） |

### 可用特效

不同形态的 LED 设备有各自适合的特效：

#### 通用特效（所有设备）

| 特效名 | 说明 |
|--------|------|
| `rainbow` | 彩虹渐变效果 |
| `breathing` | 呼吸灯效果 |
| `solid` | 纯色填充 |
| `sparkle` | 闪烁效果 |

#### Touch 专属（点光源）

| 特效名 | 说明 |
|--------|------|
| `pulse` | 脉冲闪烁（快速闪亮后渐灭） |
| `heartbeat` | 心跳效果（双跳节奏） |
| `color_cycle` | 颜色循环（平滑色相变化） |

#### Board 专属（环形灯带）

| 特效名 | 说明 |
|--------|------|
| `chase` | 追逐/跑马灯（带拖尾） |
| `comet` | 流星效果（单向快速移动） |
| `spin` | 旋转彩虹（半亮半暗旋转） |
| `breathe_wave` | 呼吸波（波浪式亮度变化） |

#### Matrix 专属（矩阵屏）

| 特效名 | 说明 |
|--------|------|
| `fire` | 火焰效果（向上燃烧） |
| `rain` | 数字雨/雨滴（下落动画） |
| `plasma` | 等离子效果（2D 波纹） |
| `ripple` | 水波纹（中心扩散） |

### 可用后处理效果（Filter）

Filter 是叠加在内容之上的后处理效果，可与 Animation 或 Image 组合使用：

| 效果名 | 说明 |
|--------|------|
| `pulse` | 脉冲亮度（正弦波调节） |
| `blink` | 开关闪烁 |
| `breathing` | 平滑呼吸效果 |
| `fade-in` | 淡入（一次性） |
| `fade-out` | 淡出（一次性） |
| `color-shift` | 色相旋转 |
| `saturation` | 饱和度调整 |
| `invert` | 反色 |
| `grayscale` | 灰度转换 |
| `scanline` | 扫描线效果 |
| `wave` | 亮度波浪 |
| `glitch` | 随机故障效果 |

### 颜色格式

支持以下颜色格式：

- **十六进制**：`#RRGGBB`（如 `#FF0000`）
- **颜色名称**：`red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `white`, `black`, `orange`, `purple`, `pink`

### 示例

```bash
# 显示所有 LED 设备状态
led --status

# 显示特定设备状态
led --status --device touch
led --status --device board
led --status --device matrix

# 点亮 LED（默认白色）
led --on --device touch

# 点亮指定颜色
led --on --device touch --color red
led --on --device board --color #00FF00
led --on --device matrix --color blue

# 关闭 LED
led --off --device touch
led --off --device board

# 清除 LED
led --clear --device matrix

# 设置亮度
led --brightness --device touch --value 128
led --brightness --device board --value 200
led --brightness --device matrix --value 64

# 获取亮度
led --brightness --device touch

# 启动特效（使用各设备专属特效）
led --effect --device touch --name heartbeat
led --effect --device board --name spin
led --effect --device matrix --name fire

# 启动特效（带速度控制）
led --effect --device touch --name pulse --speed 50
led --effect --device board --name comet --speed 80
led --effect --device matrix --name rain --speed 30

# 启动特效（带颜色参数）
led --effect --device matrix --name rain --color red
led --effect --device matrix --name rain --color #FF5500 --speed 40

# 停止特效
led --stop-effect --device touch
led --stop-effect --device board

# 列出所有可用特效（按设备分类）
led --list-effects

# 列出特定设备的可用特效
led --list-effects --device touch
led --list-effects --device board
led --list-effects --device matrix

# 解析颜色
led --parse-color --color "#FF5500"

# 显示图像（静态 PNG）
led --image --device matrix --file /sdcard/logo.png

# 播放 GIF 动画
led --image --device matrix --file /sdcard/animation.gif

# 显示图像（内容居中模式，自动裁剪透明边缘）
led --image --device matrix --file /sdcard/icon.png --center content

# JSON 格式输出
led --status --json
led --list-effects --json

# 保存当前 LED 状态为开机配置（支持图像/动画/后处理效果）
led --save --device matrix         # 保存 matrix（包括当前图像和 filter）
led --save --device touch          # 保存 touch 设备
led --save                          # 保存所有设备

# 查看已保存的开机配置（显示 ANIMATION 和 FILTER 列）
led --show-boot
led --show-boot --device touch

# 清除开机配置
led --clear-boot --device touch    # 清除 touch 配置
led --clear-boot                    # 清除所有配置

# 后处理效果（Filter）命令
led --filter --device matrix --filter-name pulse           # 应用脉冲效果
led --filter --device matrix --filter-name pulse --speed 80  # 带速度参数
led --filter --device matrix --filter-name breathing       # 呼吸效果
led --filter --device matrix --filter-name color-shift     # 色相旋转

# 组合使用：先启动动画，再叠加后处理效果
led --effect --device matrix --name fire
led --filter --device matrix --filter-name pulse --speed 50
led --save --device matrix                                  # 保存组合效果

# 停止后处理效果
led --stop-filter --device matrix

# 列出可用后处理效果
led --list-filters
led --list-filters --json

# QR 码生成（仅 matrix 设备，使用 v4 版本 33x33 模块）
led --qrcode --text "https://rminte.com/"           # 默认白色，ECC=M
led --qrcode --text "HELLO WORLD" --ecc H           # 高纠错等级
led --qrcode --text "192.168.1.100:8080" --color green  # 绿色 QR 码
led --qrcode --text "WIFI:T:WPA;S:MySSID;P:password;;" --ecc M  # WiFi 配置
```

### QR Code 功能

Matrix 设备支持生成 QR Code v4（33x33 模块）显示：

| 参数 | 说明 |
|------|------|
| `--text` | 要编码的文本内容（必需） |
| `--ecc` | 纠错等级：L/M/Q/H |
| `--color` | 前景色（模块颜色），默认白色 |

**纠错等级与容量（字母数字模式）：**

| ECC | 纠错能力 | 最大容量 |
|-----|---------|---------|
| L | ~7% | 114 字符 |
| M | ~15% | 90 字符 |
| Q | ~25% | 67 字符 |
| H | ~30% | 50 字符 |

> **注意**：QR v4 尺寸为 33x33，显示在 32x32 matrix 上时会裁剪边缘 1 像素（静默区）。

---

## fan - 风扇控制

控制系统风扇的速度和运行模式。

> **硬件配置**：Fan 0 使用 GPIO 41。系统仅配置了一个风扇。

### 语法

```
fan [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--status` | `-s` | 显示风扇状态 |
| `--set` | | 设置风扇速度 |
| `--mode` | `-m` | 设置运行模式 |
| `--enable` | | 启用风扇 |
| `--disable` | | 禁用风扇 |
| `--save` | | 保存配置到 NVS |
| `--id <n>` | `-i` | 风扇 ID（0-3） |
| `--speed <0-100>` | `-S` | 速度百分比 |
| `--value <mode>` | | 模式值 |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 运行模式

| 模式 | 说明 |
|------|------|
| `auto` | 自动模式（根据温度调节） |
| `manual` | 手动模式（固定速度） |
| `curve` | 曲线模式（自定义温度曲线） |

### 示例

```bash
# 显示所有风扇状态
fan --status

# 显示特定风扇状态
fan --status --id 0

# 设置风扇速度（需指定 ID）
fan --set --id 0 --speed 75

# 设置运行模式
fan --mode --id 0 --value auto
fan --mode --id 0 --value manual

# 启用/禁用风扇
fan --enable --id 0
fan --disable --id 0

# 保存配置
fan --save

# JSON 格式输出
fan --status --json
```

---

## storage - 存储管理

管理 SD 卡和 SPIFFS 存储。

### 语法

```
storage [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--status` | `-s` | 显示存储状态 |
| `--mount` | | 挂载 SD 卡 |
| `--unmount` | | 卸载 SD 卡 |
| `--list` | `-l` | 列出文件 |
| `--read` | `-r` | 读取文件内容 |
| `--space` | | 显示磁盘空间 |
| `--format` | | 格式化存储 |
| `--path <path>` | `-p` | 目录路径 |
| `--file <file>` | `-f` | 文件路径 |
| `--recursive` | | 递归列出 |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 挂载点

| 路径 | 说明 |
|------|------|
| `/spiffs` | SPIFFS 分区（内置 Flash） |
| `/sdcard` | SD 卡挂载点 |

### 示例

```bash
# 显示存储状态
storage --status

# 挂载 SD 卡
storage --mount

# 卸载 SD 卡
storage --unmount

# 列出根目录文件
storage --list --path /sdcard

# 递归列出目录
storage --list --path /sdcard --recursive

# 读取文件内容
storage --read --file /sdcard/config.json

# 显示磁盘空间
storage --space

# JSON 格式输出
storage --status --json
```

---

## net - 网络管理

管理 WiFi 和以太网网络配置。

> **注意**：当前部分功能为模拟实现。

### 语法

```
net [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--status` | `-s` | 显示网络状态 |
| `--ip` | `-i` | 显示 IP 配置 |
| `--set` | | 设置静态 IP |
| `--reset` | | 重置网络配置 |
| `--enable` | | 启用（与 --dhcp 配合） |
| `--disable` | | 禁用（与 --dhcp 配合） |
| `--ip <addr>` | | IP 地址 |
| `--netmask <mask>` | | 子网掩码 |
| `--gateway <gw>` | | 默认网关 |
| `--dhcp` | | DHCP 控制 |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 示例

```bash
# 显示网络状态
net --status

# 显示 IP 配置
net --ip

# 设置静态 IP
net --set --ip 192.168.1.100 --netmask 255.255.255.0 --gateway 192.168.1.1

# 启用 DHCP
net --dhcp --enable

# 禁用 DHCP
net --dhcp --disable

# 重置网络配置
net --reset

# JSON 格式输出
net --status --json
```

---

## device - 设备控制

控制外部设备（AGX、LPMU、USB MUX）。

> **注意**：当前为模拟实现，未接入真实硬件驱动。

### 语法

```
device [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--agx` | | AGX 设备控制 |
| `--lpmu` | | LPMU 设备控制 |
| `--usb-mux` | | USB MUX 控制 |
| `--power <op>` | | 电源操作（on/off/restart） |
| `--target <target>` | | USB MUX 目标 |
| `--status` | `-s` | 显示设备状态 |
| `--reset` | | 重置设备 |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 设备说明

| 设备 | 说明 |
|------|------|
| AGX | NVIDIA Jetson AGX 模块 |
| LPMU | 低功耗管理单元 |
| USB MUX | USB 多路选择器 |

### USB MUX 目标

| 目标 | 说明 |
|------|------|
| `esp32` | USB 连接到 ESP32 |
| `agx` | USB 连接到 AGX |
| `lpmu` | USB 连接到 LPMU |

### 示例

```bash
# 显示所有设备状态
device

# 显示 AGX 状态
device --agx --status

# 开启 AGX 电源
device --agx --power on

# 关闭 AGX 电源
device --agx --power off

# 重启 AGX
device --agx --power restart

# 重置 AGX
device --agx --reset

# 显示 LPMU 状态
device --lpmu --status

# 开启 LPMU
device --lpmu --power on

# 显示 USB MUX 状态
device --usb-mux --status

# 切换 USB MUX 目标
device --usb-mux --target agx
device --usb-mux --target esp32

# JSON 格式输出
device --agx --status --json
```

---

## 命令状态总览

| 命令 | 状态 | 说明 |
|------|------|------|
| `system` | ✅ 可用 | 完全实现 |
| `service` | ✅ 可用 | 完全实现 |
| `config` | ✅ 可用 | 完全实现 |
| `led` | ✅ 可用 | touch/board/matrix 全部可用 |
| `fan` | ✅ 可用 | 支持 Fan 0（GPIO 41） |
| `storage` | ✅ 可用 | SPIFFS 可用，SD 卡需插入 |
| `net` | ⚠️ 部分 | 状态查询可用 |
| `device` | ⚠️ 模拟 | 未接入真实驱动 |

---

## 通用说明

### JSON 输出

所有命令都支持 `--json` 或 `-j` 选项，输出机器可读的 JSON 格式，便于脚本解析或 API 集成。

### 帮助信息

所有命令都支持 `--help` 或 `-h` 选项，显示命令的详细使用说明。

### 命令风格

TianShanOS CLI 使用**参数风格**（非 POSIX 子命令风格）：

```bash
# 正确 ✓
service --start --name fan_controller
fan --speed 75 --id 0

# 错误 ✗
service start fan_controller
fan set speed 75
```

---

## 版本信息

- **文档版本**: 1.0.0
- **适用版本**: TianShanOS v1.0.0+
- **最后更新**: 2026-01-15
