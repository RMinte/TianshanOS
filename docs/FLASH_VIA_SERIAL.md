# 串口刷机指南

## 一、编译 www 和固件

### 1. 环境准备

- 已安装 **ESP-IDF v5.5+**，并执行过 `install.sh esp32s3`。
- 在终端中激活环境后再进行编译：

```bash
# 激活 ESP-IDF（路径按你本机安装位置调整，例如 v5.5、v5.5.2 等）
source ~/esp/v5.5.2/esp-idf/export.sh
```

若提示 **Python virtual environment not found**，需先在本机完成一次安装：

```bash
cd ~/esp/v5.5.2/esp-idf
./install.sh esp32s3
```

安装完成后再执行上面的 `source .../export.sh`。

### 2. www 与固件的关系

- **www** 即 `components/ts_webui/web/` 下的静态文件（HTML/JS/CSS 等）。
- 没有单独的「编译 www」步骤：执行 **一次** `idf.py build` 时会：
  - 把 `web/` 打成 SPIFFS 镜像；
  - 与固件一起参与链接，写入分区 `www`（约 2MB）。
- 因此：**改完 web 前端后，只需重新执行一次完整构建即可。**

### 3. 编译步骤

在项目根目录 `TianshanOS/` 下：

```bash
# 设置目标芯片（首次或换芯片时执行一次）
idf.py set-target esp32s3

# 方式 A：使用项目构建脚本（推荐）
./tools/build.sh              # 增量构建，版本号不变
./tools/build.sh --fresh      # 强制更新版本号后构建
./tools/build.sh --clean      # 完整清理后构建

# 方式 B：直接使用 idf.py
idf.py build
```

构建成功后，可在 `build/` 下看到：

- `TianshanOS.bin` — 应用固件（烧录到 ota_0）
- `bootloader.bin` — 引导程序
- `partition-table.bin` — 分区表
- `www.bin` — Web 资源 SPIFFS 镜像（烧录到 www 分区）

---

## 二、通过串口线刷机

### 1. 连接设备

- 用 **USB 串口线** 连接电脑与 ESP32-S3 板子。
- 确认板子进入下载模式（多数开发板会自动识别；若需手动，按说明进入 Boot 模式）。

### 2. 查看串口设备名

```bash
# Linux
ls /dev/ttyUSB* /dev/ttyACM*

# macOS
ls /dev/cu.usb* /dev/cu.usbserial*
```

常见示例：Linux 为 `/dev/ttyUSB0` 或 `/dev/ttyACM0`，macOS 为 `/dev/cu.usbserial-xxxx` 或 `/dev/cu.usbmodemxxxx`。

### 3. 一键烧录（推荐）

在项目根目录、且已 `source export.sh` 的前提下：

```bash
# 将 <PORT> 换成你的串口，例如 /dev/ttyUSB0 或 /dev/cu.usbserial-1234
idf.py -p <PORT> flash
```

例如：

```bash
idf.py -p /dev/ttyUSB0 flash
# 或
idf.py -p /dev/cu.usbmodem113301 flash
```

该命令会按当前分区表自动烧录：bootloader、分区表、应用固件、www 等所需分区。

### 4. 烧录后打开串口监视器

```bash
idf.py -p <PORT> monitor
```

或烧录与监视一起执行：

```bash
idf.py -p <PORT> flash monitor
```

退出监视器：`Ctrl + ]`。

### 5. 仅用 esptool 手动烧录（可选）

若无法使用 `idf.py flash`，可用 esptool 按分区表手动写：

当前 `partitions.csv` 中主要分区偏移为：

| 分区       | 偏移     | 文件 |
|------------|----------|------|
| bootloader | 0x0      | build/bootloader/bootloader.bin |
| 分区表     | 0x8000   | build/partition_table/partition-table.bin |
| 应用 ota_0 | 0x20000  | build/TianshanOS.bin |
| www        | 0x6A0000 | build/www.bin |

在 `build` 目录下执行（把 `<PORT>` 换成实际串口）：

```bash
cd build

esptool.py --chip esp32s3 -p <PORT> write_flash \
  0x0       bootloader/bootloader.bin \
  0x8000    partition_table/partition-table.bin \
  0x20000   TianshanOS.bin \
  0x6A0000  www.bin
```

**注意**：若分区表或分区名有改动，请以当前 `partitions.csv` 和构建产物为准，必要时用 `idf.py partition-table` 等确认。

---

## 三、常见问题

1. **提示找不到串口**  
   - 安装 CP210x/CH340 等 USB 转串口驱动。  
   - 用 `ls /dev/tty*` 或 `ls /dev/cu.*` 确认设备名。

2. **烧录超时 / 连接失败**  
   - 拔插 USB，或按住 Boot 键再上电/按 Reset，使芯片进入下载模式后再执行 `idf.py -p <PORT> flash`。

3. **只改了 web 前端**  
   - 执行一次 `idf.py build`（或 `./tools/build.sh`），再执行 `idf.py -p <PORT> flash` 即可，www 会随固件一起更新。

4. **权限不足**  
   - Linux 下可加 udev 规则，或临时：`sudo chmod 666 /dev/ttyUSB0`（不推荐长期使用）。
