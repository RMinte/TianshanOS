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
- [fs - 文件系统操作](#fs---文件系统操作)
- [net - 网络管理](#net---网络管理)
- [dhcp - DHCP 服务器管理](#dhcp---dhcp-服务器管理)
- [wifi - WiFi 管理](#wifi---wifi-管理)
- [device - 设备控制](#device---设备控制)
- [ssh - SSH 客户端](#ssh---ssh-客户端)

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
| `--draw-text` | | 绘制文本到 matrix（仅 matrix） |
| `--stop-text` | | 停止文本覆盖层 |
| `--font <name>` | | 字体名称（cjk/boutique9x9，默认 cjk） |
| `--text-file <path>` | | 从文件读取文本（UTF-8，支持中文） |
| `--align <mode>` | | 文本对齐：left/center/right |
| `--scroll <dir>` | | 文本滚动方向：left/right/up/down/none |
| `--x <pos>` | | 文本起始 X 位置（默认 0） |
| `--y <pos>` | | 文本起始 Y 位置（默认 0） |
| `--invert` | | 反色覆盖模式（与底层图像叠加时自动反色） |
| `--loop` | | 循环滚动（文本滚出后重新进入） |
| `--qrcode` | | 生成并显示 QR 码（仅 matrix） |
| `--text <string>` | | 文本内容（--draw-text/--qrcode 使用） |
| `--ecc <L\|M\|Q\|H>` | | QR 纠错等级 |
| `--bg <path>` | | QR 前景图片路径（前景像素使用图片颜色） |
| `--file <path>` | `-f` | 图像文件路径（PNG/GIF） |
| `--center <mode>` | | 居中模式：image（图像居中）或 content（内容居中） |
| `--parse-color` | | 解析颜色信息 |
| `--device <name>` | `-d` | 指定设备名称 |
| `--name <effect>` | `-n` | 程序动画名称 |
| `--value <0-255>` | `-v` | 亮度值 |
| `--color <color>` | | 颜色值 |
| `--speed <1-100>` | | 动画/效果/滚动速度（1=慢, 100=快） |
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

# QR 码生成（仅 matrix 设备，自动选择 v1-v4 版本）
led --qrcode --text "https://rminte.com/"           # 默认白色，ECC=M
led --qrcode --text "HELLO WORLD" --ecc H           # 高纠错等级
led --qrcode --text "192.168.1.100:8080" --color green  # 绿色 QR 码
led --qrcode --text "WIFI:T:WPA;S:MySSID;P:password;;" --ecc M  # WiFi 配置
led --qrcode --text "https://example.com" --bg /sdcard/me.png  # 图片前景
```

### QR Code 功能

Matrix 设备支持生成 QR Code 显示，自动选择最佳版本：

| 版本 | 尺寸 | 适配 32×32 | 容量（字母数字/M级） |
|------|------|-----------|-------------------|
| v1 | 21×21 | ✅ 居中 | 20 字符 |
| v2 | 25×25 | ✅ 居中 | 38 字符 |
| v3 | 29×29 | ✅ 居中 | 61 字符 |
| v4 | 33×33 | ⚠️ 裁剪1px | 90 字符 |

系统会根据文本长度自动选择最小版本，优先使用 v1-v3 以避免裁剪。

| 参数 | 说明 |
|------|------|
| `--text` | 要编码的文本内容（必需） |
| `--ecc` | 纠错等级：L/M/Q/H |
| `--color` | 前景色（模块颜色），默认白色 |
| `--bg` | 前景图片路径，前景像素使用图片对应位置的颜色 |

**纠错等级与容量（字母数字模式，v4 版本）：**

| ECC | 纠错能力 | 最大容量 |
|-----|---------|---------|
| L | ~7% | 114 字符 |
| M | ~15% | 90 字符 |
| Q | ~25% | 67 字符 |
| H | ~30% | 50 字符 |

> **注意**：内容较短时优先使用 v1-v3（完全适配 32×32），长内容自动升级到 v4（裁剪 1 像素边缘）。

### 文本显示功能

Matrix 设备支持在 Layer 1（覆盖层）上显示文本，**不影响底层动画/图像**。所有文本显示统一使用覆盖层机制，`--stop-text` 可随时清除文本恢复底层内容。

#### 基本用法

```bash
# 静态显示
led --draw-text --text "Hello" --color cyan

# 居中显示
led --draw-text --text "OK" --align center --color green

# 指定位置显示
led --draw-text --text "Hi" --x 5 --y 10
```

#### 滚动与反色

```bash
# 滚动文本
led --draw-text --text "Hello World" --scroll left
led --draw-text --text "Super Mario World" --scroll left

# 循环滚动
led --draw-text --text "Breaking News..." --scroll left --loop

# 反色覆盖（在亮色背景上自动反色）
led --draw-text --text "警告" --font cjk --invert

# 停止文本显示（恢复底层内容）
led --stop-text
```

#### 参数说明

| 参数 | 说明 |
|------|------|
| `--draw-text` | 启用文本绘制模式 |
| `--stop-text` | 停止文本覆盖层 |
| `--text <string>` | 要显示的文本（ASCII） |
| `--text-file <path>` | 从文件读取 UTF-8 文本（支持中文） |
| `--font <name>` | 字体名称：cjk（默认）或 boutique9x9 |
| `--align <mode>` | 对齐方式：left/center/right |
| `--color <color>` | 文本颜色 |
| `--x <pos>` | 起始 X 位置（默认 0） |
| `--y <pos>` | 起始 Y 位置（默认 0） |
| `--scroll <dir>` | 滚动方向：left/right/up/down/none |
| `--invert` | 反色覆盖（在亮色背景上自动反色显示） |
| `--loop` | 循环滚动（需配合 --scroll） |
| `--speed <1-100>` | 滚动速度（1=慢, 100=快） |

**可用字体**：

| 字体名 | 说明 | 字符集 |
|--------|------|--------|
| `cjk` | BoutiqueBitmap 9x9 CJK（默认） | GB2312（6763 汉字 + ASCII） |
| `boutique9x9` | BoutiqueBitmap 9x9 | ASCII（95 字符） |

**更多示例**：

```bash
# 显示中文（通过文件，推荐方式）
led --draw-text --text-file /sdcard/msg.txt --font cjk --color yellow

# 调整滚动速度（1=慢, 100=快）
led --draw-text --text "Fast!" --scroll left --font boutique9x9 --speed 80

# 向上滚动
led --draw-text --text "不错" --scroll up --font boutique9x9  --loop

# 覆盖层组合使用
led --image -f /sdcard/images/bg.png               # 先显示背景
led --draw-text --text "提示" --invert --scroll left --loop  # 叠加滚动文本
led --stop-text                                     # 恢复背景
```

> **⚠️ 中文输入限制**：由于 ESP-IDF 串口控制台的 UTF-8 解析限制，直接在命令行输入中文（如 `--text "你好"`）可能导致参数解析错误。**请使用 `--text-file` 从文件读取中文文本**。

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

管理以太网网络配置。

> **当前状态**：
> - ✅ 以太网（W5500）完全支持
> - ✅ WiFi 请使用 `wifi` 命令
> - ✅ 配置保存/加载支持 NVS 持久化

### 语法

```
net [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--status` | `-s` | 显示网络状态 |
| `--config` | | 显示接口配置详情 |
| `--set` | | 设置网络参数（需配合其他参数） |
| `--start` | | 启动网络接口 |
| `--stop` | | 停止网络接口 |
| `--restart` | | 重启网络接口 |
| `--save` | | 保存配置到 NVS |
| `--load` | | 从 NVS 加载配置 |
| `--reset` | | 重置为默认配置 |
| `--ip` | | 快速显示 IP 地址 |
| `--iface <if>` | | 指定接口：eth（默认） |
| `--ip <addr>` | | 静态 IP 地址 |
| `--netmask <mask>` | | 子网掩码 |
| `--gateway <gw>` | | 默认网关 |
| `--dns <addr>` | | DNS 服务器地址 |
| `--mode <mode>` | | IP 模式：dhcp, static |
| `--hostname <name>` | | 设置主机名 |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 示例

```bash
# 显示网络状态
net --status

# 快速查看 IP
net --ip

# 显示以太网配置详情
net --config --iface eth

# 设置静态 IP
net --set --mode static --ip 10.10.99.97 --netmask 255.255.255.0 --gateway 10.10.99.1

# 切换到 DHCP 客户端模式
net --set --mode dhcp

# 设置 DNS
net --set --dns 8.8.8.8

# 设置主机名
net --set --hostname TianShanOS

# 保存配置到 NVS（重启后生效）
net --save

# 重启接口应用更改
net --restart

# 从 NVS 加载配置
net --load

# 重置为默认配置
net --reset

# JSON 格式输出
net --status --json
```

### 配置持久化

- `net --set` 修改的配置是**临时的**，重启后丢失
- 使用 `net --save` 保存到 NVS，重启后自动加载
- 修改配置后需要 `net --restart` 才能生效

---

## dhcp - DHCP 服务器管理

管理 TianShanOS 内置的 DHCP 服务器，支持多接口（WiFi AP、Ethernet）。

### 语法

```
dhcp [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--status` | | 显示 DHCP 服务器状态 |
| `--list` | | 列出所有接口状态 |
| `--clients` | | 列出已连接的客户端 |
| `--start` | | 启动 DHCP 服务器 |
| `--stop` | | 停止 DHCP 服务器 |
| `--restart` | | 重启 DHCP 服务器 |
| `--pool` | | 配置地址池 |
| `--bind` | | 添加静态绑定 |
| `--unbind` | | 删除静态绑定 |
| `--bindings` | | 列出所有静态绑定 |
| `--save` | | 保存配置到 NVS |
| `--reset` | | 重置为默认配置 |
| `--iface <if>` | | 指定接口：`ap`、`eth`、`all`（默认） |
| `--start-ip <ip>` | | 地址池起始 IP |
| `--end-ip <ip>` | | 地址池结束 IP |
| `--gateway <ip>` | | 网关地址 |
| `--netmask <mask>` | | 子网掩码 |
| `--dns <ip>` | | DNS 服务器地址 |
| `--lease <min>` | | 租约时间（分钟） |
| `--mac <addr>` | | MAC 地址（用于绑定） |
| `--ip <addr>` | | IP 地址（用于绑定） |
| `--hostname <name>` | | 主机名（用于绑定） |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 接口说明

| 接口 | 别名 | 说明 |
|------|------|------|
| `ap` | `wifi`, `wifi_ap` | WiFi 接入点 |
| `eth` | `ethernet` | 以太网接口 |
| `all` | （默认） | 所有接口 |

### 示例

```bash
# 显示所有接口 DHCP 状态
dhcp --status

# 显示以太网接口详细状态
dhcp --status --iface eth

# 列出所有客户端
dhcp --clients

# 列出以太网接口的客户端
dhcp --clients --iface eth

# 启动以太网 DHCP 服务器
dhcp --start --iface eth

# 停止 WiFi AP DHCP 服务器
dhcp --stop --iface ap

# 重启所有接口的 DHCP 服务器
dhcp --restart

# 配置以太网地址池
dhcp --pool --iface eth --start-ip 10.10.99.100 --end-ip 10.10.99.200

# 配置完整地址池
dhcp --pool --iface eth \
    --start-ip 10.10.99.100 \
    --end-ip 10.10.99.200 \
    --gateway 10.10.99.97 \
    --netmask 255.255.255.0 \
    --dns 8.8.8.8 \
    --lease 120

# 添加静态绑定（MAC → IP）
dhcp --bind --iface eth --mac aa:bb:cc:dd:ee:ff --ip 10.10.99.50

# 添加带主机名的静态绑定
dhcp --bind --iface eth --mac aa:bb:cc:dd:ee:ff --ip 10.10.99.50 --hostname jetson-agx

# 删除静态绑定
dhcp --unbind --iface eth --mac aa:bb:cc:dd:ee:ff

# 列出所有静态绑定
dhcp --bindings --iface eth

# 保存配置
dhcp --save

# 重置为默认配置
dhcp --reset

# JSON 格式输出
dhcp --status --json
dhcp --clients --iface eth --json
```

### 配置存储

DHCP 配置存储在 NVS 中，重启后自动加载。使用 `--save` 命令保存当前配置，使用 `--reset` 恢复默认值。

### 默认配置

| 接口 | 地址池 | 网关 | DNS | 租约 |
|------|--------|------|-----|------|
| Ethernet | 10.10.99.100-103 | 10.10.99.100 | 8.8.8.8 | 60 分钟 |
| WiFi AP | 192.168.4.2-10 | 192.168.4.1 | 192.168.4.1 | 60 分钟 |

---

## wifi - WiFi 管理

管理 WiFi AP 模式（热点）和 STA 模式（连接外网）。

### 语法

```
wifi [options]
```

### 选项

| 选项 | 简写 | 说明 |
|------|------|------|
| `--status` | `-s` | 显示 WiFi 状态 |
| `--scan` | | 扫描附近 WiFi 网络 |
| `--ap` | | 配置/查看 AP 模式 |
| `--connect` | | 连接到 WiFi 网络（STA 模式） |
| `--disconnect` | | 断开 WiFi 连接（STA 模式） |
| `--start` | | 启动 WiFi 接口 |
| `--stop` | | 停止 WiFi 接口 |
| `--save` | | 保存配置到 NVS |
| `--ssid <name>` | | WiFi 网络名称 |
| `--pass <password>` | | WiFi 密码 |
| `--iface <if>` | | 接口：`ap`（默认）或 `sta` |
| `--channel <1-13>` | | WiFi 信道（仅 AP 模式） |
| `--json` | `-j` | JSON 格式输出 |
| `--help` | `-h` | 显示帮助 |

### 示例

```bash
# 显示 WiFi 状态
wifi --status

# 扫描附近 WiFi 网络
wifi --scan

# 配置并启动 AP 热点 （尚未测试）
wifi --ap --ssid TianShanOS --pass tianshan123
wifi --start --iface ap

# 连接到外网 WiFi（STA 模式）
wifi --connect --ssid HomeWiFi --pass secret123

# 断开 STA 连接
wifi --disconnect

# 停止 AP
wifi --stop --iface ap

# 保存配置
wifi --save

# JSON 输出
wifi --status --json
```

### AP 模式（热点）

AP 模式用于：
- 备用管理通道（手机直连 ESP32）
- 为以太网设备提供外网（配合 STA 模式 + NAT）

默认配置：
- SSID: `TianShanOS`
- 密码: `tianshan123`
- IP: `192.168.4.1`
- 信道: 1

### STA 模式（客户端）

STA 模式用于连接外部 WiFi 路由器，获取外网访问能力。

---

## fs - 文件系统操作

类 Unix 风格的文件系统操作命令。

### 可用命令

| 命令 | 说明 |
|------|------|
| `ls [path]` | 列出目录内容 |
| `cat <file>` | 显示文件内容 |
| `cd <path>` | 切换工作目录 |
| `pwd` | 显示当前工作目录 |
| `mkdir <path>` | 创建目录 |
| `rm <path>` | 删除文件或目录 |
| `cp <src> <dst>` | 复制文件 |
| `mv <src> <dst>` | 移动/重命名文件 |
| `touch <file>` | 创建空文件 |

### 路径说明

- 支持绝对路径（以 `/` 开头）和相对路径
- 默认工作目录：`/sdcard`
- 支持 `..`（上级目录）和 `.`（当前目录）

### 挂载点

| 路径 | 说明 |
|------|------|
| `/spiffs` | SPIFFS 分区（内置 Flash） |
| `/sdcard` | SD 卡挂载点 |

### 示例

```bash
# 显示当前目录
pwd

# 列出当前目录
ls

# 列出指定目录
ls /sdcard/images

# 详细列表（显示大小、时间）
ls -l
ls -l /sdcard

# 切换目录
cd /sdcard/config
cd ..            # 上级目录
cd images        # 相对路径

# 查看文件内容
cat config.json
cat /sdcard/scripts/startup.sh

# 创建目录
mkdir /sdcard/backup
mkdir logs

# 创建空文件
touch /sdcard/test.txt

# 复制文件
cp config.json config.json.bak
cp /sdcard/a.txt /sdcard/backup/a.txt

# 移动/重命名
mv old.txt new.txt
mv /sdcard/temp.txt /sdcard/archive/temp.txt

# 删除文件
rm /sdcard/temp.txt

# 删除空目录
rm /sdcard/empty_dir
```

### 注意事项

- `rm` 命令只能删除文件或空目录
- SD 卡需要插入才能访问 `/sdcard`
- SPIFFS 分区空间有限，不建议存放大文件

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

## ssh - SSH 客户端

远程 SSH 连接、命令执行、交互式 Shell、端口转发和密钥管理。

### 语法

```
ssh [options]
```

### 连接选项

| 选项 | 说明 |
|------|------|
| `--host <ip>` | 远程主机地址 |
| `--port <num>` | SSH 端口（默认 22） |
| `--user <name>` | 用户名 |
| `--password <pwd>` | 密码（密码认证） |
| `--key <path>` | 私钥文件路径（公钥认证） |
| `--keyid <id>` | 使用安全存储中的密钥（公钥认证） |
| `--exec <cmd>` | 执行远程命令 |
| `--shell` | 启动交互式 Shell |
| `--forward <spec>` | 端口转发（格式：`L<local>:<remote_host>:<remote_port>`） |
| `--test` | 测试 SSH 连接 |
| `--timeout <sec>` | 连接超时（秒） |
| `--verbose` | 详细输出 |

### 密钥文件生成选项

| 选项 | 说明 |
|------|------|
| `--keygen` | 生成 SSH 密钥对到文件 |
| `--type <type>` | 密钥类型：`rsa`, `rsa2048`, `rsa4096`, `ecdsa`, `ec256`, `ec384` |
| `--output <path>` | 私钥输出路径 |
| `--comment <text>` | 公钥注释（可选） |

### 安全密钥存储选项（推荐）

密钥存储在 ESP32 的 NVS 加密分区中，比文件存储更安全。

| 选项 | 说明 |
|------|------|
| `--keys` | 进入密钥管理模式 |
| `--keys --list` | 列出所有存储的密钥 |
| `--keys --import --id <name> --key <path>` | 从文件导入密钥到安全存储 |
| `--keys --import --id <name> --type <type>` | 生成密钥并存储到安全存储 |
| `--keys --delete --id <name>` | 删除存储的密钥 |
| `--keys --export --id <name> --output <path>` | 导出公钥到文件 |
| `--id <name>` | 密钥 ID（用于安全存储操作） |

### 密钥部署选项

| 选项 | 说明 |
|------|------|
| `--copy-id` | 部署公钥到远程服务器（类似 ssh-copy-id） |

使用 `--copyid` 时需要同时提供：
- `--host` - 目标服务器
- `--user` - 用户名
- `--password` - 密码（用于初始认证）
- `--key` - 私钥路径（公钥为 `<path>.pub`）

### 安全密钥存储示例（推荐）

```bash
# 查看存储的密钥
ssh --keys --list

# 生成 ECDSA 密钥并存储到安全存储（推荐）
ssh --keys --import --id agx --type ecdsa --comment "AGX production key"

# 从 SD 卡导入已有密钥到安全存储
ssh --keys --import --id backup --key /sdcard/id_rsa

# 使用安全存储的密钥连接
ssh --host 192.168.1.100 --user nvidia --keyid agx --shell
ssh --host 192.168.1.100 --user nvidia --keyid agx --exec "nvidia-smi"

# 导出公钥用于部署
ssh --keys --export --id agx --output /sdcard/agx.pub

# 删除不需要的密钥
ssh --keys --delete --id old_key
```

安全密钥存储的优势：
- **硬件保护**：密钥存储在 ESP32 的 NVS 加密分区
- **无需文件**：避免私钥文件泄露风险
- **使用方便**：通过 `--keyid` 直接引用密钥
- **支持旋转**：可随时删除旧密钥、添加新密钥

### 密钥文件生成示例

```bash
# 生成 RSA 2048 位密钥对（保存到文件）
ssh --keygen --type rsa2048 --output /sdcard/id_rsa

# 生成 ECDSA P-256 密钥对（推荐，更快更安全）
ssh --keygen --type ecdsa --output /sdcard/id_ecdsa --comment "TianShanOS AGX key"

# 生成 RSA 4096 位密钥对（最安全，但生成较慢）
ssh --keygen --type rsa4096 --output /sdcard/id_rsa_4096
```

生成的文件：
- 私钥：`<output>` (PEM 格式)
- 公钥：`<output>.pub` (OpenSSH 格式，可直接添加到 `authorized_keys`)

### 公钥部署示例（ssh-copy-id）

```bash
# 将公钥部署到远程服务器（使用密码进行初始认证）
ssh --copyid --host 192.168.1.100 --user nvidia --password secret --key /sdcard/id_ecdsa

# 部署完成后，可以使用公钥认证
ssh --host 192.168.1.100 --user nvidia --key /sdcard/id_ecdsa --exec "hostname"
```

`--copyid` 会执行以下操作：
1. 使用密码连接到远程服务器
2. 创建 `~/.ssh` 目录（如果不存在）
3. 将公钥追加到 `~/.ssh/authorized_keys`
4. 设置正确的目录/文件权限（700/600）
5. 自动验证公钥认证是否成功

### 连接示例

```bash
# 使用密码认证执行命令
ssh --host 192.168.1.100 --user root --password secret --exec "uptime"

# 使用公钥认证（使用生成的密钥）
ssh --host 192.168.1.100 --user nvidia --key /sdcard/id_ecdsa --exec "nvidia-smi"

# 测试连接
ssh --test --host 192.168.1.100 --user root --key /sdcard/id_rsa

# 启动交互式 Shell
ssh --host agx.local --user nvidia --key /sdcard/id_ecdsa --shell

# 本地端口转发（将本地 8080 转发到远程的 localhost:80）
ssh --host gateway.local --user admin --key /sdcard/id_ecdsa \
    --forward "L8080:localhost:80"
```

### 交互式 Shell

使用 `--shell` 选项启动交互式 Shell：
- 支持 PTY（伪终端），完整的终端体验
- 支持 Ctrl+C 发送中断信号
- 支持 Ctrl+D 退出 Shell
- 实时字符回显

### 端口转发

端口转发格式：`L<localport>:<remotehost>:<remoteport>`

```bash
# 将本地 8080 端口转发到远程的 internal.server:3000
ssh --host bastion --user admin --key /sdcard/id_ecdsa \
    --forward "L8080:internal.server:3000"
```

转发建立后，访问 ESP32 的 8080 端口等同于访问远程的 internal.server:3000。

### Known Hosts 验证

首次连接新主机时会显示指纹并询问是否信任：
```
Host key fingerprint: SHA256:AAAA...
Trust this host? (yes/no):
```

输入 `yes` 后密钥会保存到 NVS，后续连接自动验证。

### 典型工作流

#### 方式一：使用安全存储（推荐）

1. **生成密钥并存储到安全区域**：
   ```bash
   ssh --keys --import --id agx --type ecdsa --comment "TianShanOS AGX"
   ```

2. **导出公钥用于部署**：
   ```bash
   ssh --keys --export --id agx --output /sdcard/agx.pub
   ```

3. **部署公钥到目标服务器**：
   ```bash
   ssh --copyid --host 192.168.1.100 --user nvidia --password secret --key /sdcard/agx
   ```

4. **使用安全存储的密钥连接**：
   ```bash
   ssh --host 192.168.1.100 --user nvidia --keyid agx --shell
   ```

#### 方式二：使用文件存储

1. **在 TianShanOS 上生成密钥对**：
   ```bash
   ssh --keygen --type ecdsa --output /sdcard/id_ecdsa --comment "TianShanOS"
   ```

2. **查看生成的公钥**（输出中已显示，或使用 `fs --cat /sdcard/id_ecdsa.pub`）

3. **部署公钥到目标服务器**（使用 --copyid 自动部署）：
   ```bash
   ssh --copyid --host 192.168.1.100 --user nvidia --password secret --key /sdcard/id_ecdsa
   ```

4. **使用私钥连接**：
   ```bash
   ssh --host 192.168.1.100 --user nvidia --key /sdcard/id_ecdsa --exec "hostname"
   ```

#### 密钥迁移：文件 → 安全存储

已有密钥文件的用户可以迁移到安全存储：
```bash
# 导入已有密钥
ssh --keys --import --id mykey --key /sdcard/id_rsa

# 验证导入成功
ssh --keys --list

# 删除原文件（可选，确保安全）
fs --rm /sdcard/id_rsa
fs --rm /sdcard/id_rsa.pub
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
| `net` | ✅ 可用 | 以太网完全可用 |
| `dhcp` | ✅ 可用 | 完整 DHCP 服务器，支持静态绑定 |
| `wifi` | ✅ 可用 | AP/STA 模式，扫描、连接、热点 |
| `fs` | ✅ 可用 | 文件系统操作（ls/cat/rm/mkdir 等） |
| `device` | ⚠️ 模拟 | 未接入真实驱动 |
| `ssh` | ✅ 可用 | 命令执行、Shell、端口转发、密钥生成、公钥部署 |

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

- **文档版本**: 1.2.0
- **适用版本**: TianShanOS v0.1.0+
- **最后更新**: 2026-01-18
