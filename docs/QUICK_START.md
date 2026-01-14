# TianShanOS 快速入门指南

## 简介

TianShanOS 是一个面向 ESP32 的配置驱动嵌入式操作系统，专为 NVIDIA Jetson AGX 载板设计。

## 系统要求

- ESP-IDF v5.5.1 或更高版本
- ESP32S3（主要支持）或 ESP32P4
- Python 3.8+
- Git

## 快速开始

### 1. 环境准备

```bash
# 安装 ESP-IDF
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.5.1
./install.sh esp32s3
source export.sh
```

### 2. 克隆项目

```bash
git clone https://github.com/your-repo/TianshanOS.git
cd TianshanOS
```

### 3. 配置项目

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 打开配置菜单
idf.py menuconfig
```

关键配置项：
- `TianShanOS Configuration` → 各模块启用/配置
- `Serial flasher config` → 串口设置
- `Partition Table` → 使用自定义分区表

### 4. 编译固件

```bash
idf.py build
```

### 5. 烧录固件

```bash
idf.py -p /dev/ttyUSB0 flash
```

### 6. 监控输出

```bash
idf.py -p /dev/ttyUSB0 monitor
```

## 控制台命令

系统启动后，可通过串口控制台使用以下命令：

```
help              - 显示帮助
version           - 显示版本信息
sysinfo           - 显示系统信息
tasks             - 显示任务列表
free              - 显示内存使用
reboot            - 重启系统
log <level>       - 设置日志级别
```

## Web 界面

1. 连接到设备网络（以太网或 WiFi AP）
2. 打开浏览器访问设备 IP 地址
3. 默认登录凭据：
   - 用户名: `admin`
   - 密码: `tianshan`

## 配置文件

### 板级配置

配置文件位于 `boards/<board_name>/`:

- `board.json` - 板级特性定义
- `pins.json` - 引脚映射
- `services.json` - 服务配置

### 运行时配置

运行时配置存储在 NVS 中，可通过以下方式修改：

1. 控制台命令
2. Web 界面
3. API 调用

## API 使用

### REST API

```bash
# 获取系统信息
curl http://<device-ip>/api/v1/system/info

# 登录获取 Token
curl -X POST http://<device-ip>/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"tianshan"}'

# 使用 Token 调用 API
curl http://<device-ip>/api/v1/config/list \
  -H "Authorization: Bearer <token>"
```

## 常见问题

### Q: 烧录失败
A: 检查串口权限，确保按住 BOOT 按钮再按 RESET

### Q: 无法连接 WiFi
A: 检查 SSID 和密码是否正确，确保 WiFi 模块已启用

### Q: LED 不亮
A: 检查 pins.json 中的引脚配置，确保与硬件匹配

## 下一步

- 阅读 [架构设计文档](ARCHITECTURE_DESIGN.md)
- 查看 [命令规范文档](COMMAND_SPECIFICATION.md)
- 探索 [API 参考](API_REFERENCE.md)
