# TianShanOS 板级配置指南

## 概述

TianShanOS 使用 JSON 配置文件定义板级硬件配置，实现"配置驱动"的设计理念。

## 配置文件结构

每个板级配置目录包含三个文件：

```
boards/<board_name>/
├── board.json      # 板级特性
├── pins.json       # 引脚映射
└── services.json   # 服务配置
```

## board.json

定义板级硬件特性和能力。

```json
{
  "name": "rm01_esp32s3",
  "description": "RoboMaster 01 Carrier Board with ESP32S3",
  "chip": "esp32s3",
  "features": {
    "ethernet": true,
    "wifi": true,
    "bluetooth": true,
    "usb_otg": true,
    "sd_card": true,
    "psram": true,
    "led_touch": true,
    "led_board": true,
    "led_matrix": false,
    "fan_count": 4,
    "usb_mux_count": 2,
    "power_monitor": true
  },
  "memory": {
    "flash_size": "16MB",
    "psram_size": "8MB"
  },
  "network": {
    "ethernet_chip": "W5500",
    "ethernet_spi_host": 2
  }
}
```

### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| name | string | 板级唯一标识符 |
| description | string | 人类可读描述 |
| chip | string | 目标芯片 (esp32s3, esp32p4) |
| features | object | 硬件特性开关 |
| memory | object | 内存配置 |
| network | object | 网络配置 |

## pins.json

定义所有引脚的功能映射。

```json
{
  "pins": {
    "LED_TOUCH": {
      "gpio": 45,
      "function": "led_data",
      "description": "Touch LED WS2812 data"
    },
    "LED_BOARD": {
      "gpio": 42,
      "function": "led_data",
      "description": "Board LED WS2812 data"
    },
    "FAN_1_PWM": {
      "gpio": 4,
      "function": "pwm_output",
      "description": "Fan 1 PWM control"
    },
    "FAN_1_TACH": {
      "gpio": 5,
      "function": "gpio_input",
      "description": "Fan 1 tachometer"
    },
    "ETH_MOSI": {
      "gpio": 11,
      "function": "spi_mosi",
      "spi_host": 2
    },
    "ETH_MISO": {
      "gpio": 13,
      "function": "spi_miso",
      "spi_host": 2
    },
    "ETH_SCLK": {
      "gpio": 12,
      "function": "spi_sclk",
      "spi_host": 2
    },
    "ETH_CS": {
      "gpio": 10,
      "function": "spi_cs"
    },
    "ETH_INT": {
      "gpio": 14,
      "function": "gpio_input",
      "interrupt": true
    },
    "ETH_RST": {
      "gpio": 9,
      "function": "gpio_output"
    },
    "AGX_POWER_EN": {
      "gpio": 1,
      "function": "gpio_output",
      "description": "AGX power enable"
    },
    "AGX_RESET": {
      "gpio": 2,
      "function": "gpio_output",
      "description": "AGX reset"
    },
    "AGX_POWER_GOOD": {
      "gpio": 3,
      "function": "gpio_input",
      "description": "AGX power good"
    },
    "USB_MUX_1_SEL": {
      "gpio": 39,
      "function": "gpio_output"
    },
    "USB_MUX_1_OE": {
      "gpio": 40,
      "function": "gpio_output"
    },
    "SD_CMD": {
      "gpio": 35,
      "function": "sdmmc_cmd"
    },
    "SD_CLK": {
      "gpio": 36,
      "function": "sdmmc_clk"
    },
    "SD_D0": {
      "gpio": 37,
      "function": "sdmmc_d0"
    },
    "I2C_SDA": {
      "gpio": 8,
      "function": "i2c_sda",
      "i2c_port": 0
    },
    "I2C_SCL": {
      "gpio": 18,
      "function": "i2c_scl",
      "i2c_port": 0
    }
  }
}
```

### 引脚功能类型

| 功能 | 说明 |
|------|------|
| gpio_input | GPIO 输入 |
| gpio_output | GPIO 输出 |
| led_data | WS2812 LED 数据线 |
| pwm_output | PWM 输出 |
| spi_mosi | SPI MOSI |
| spi_miso | SPI MISO |
| spi_sclk | SPI 时钟 |
| spi_cs | SPI 片选 |
| i2c_sda | I2C 数据 |
| i2c_scl | I2C 时钟 |
| uart_tx | UART 发送 |
| uart_rx | UART 接收 |
| adc_input | ADC 输入 |
| sdmmc_cmd | SD 命令 |
| sdmmc_clk | SD 时钟 |
| sdmmc_d0-d3 | SD 数据线 |

## services.json

定义系统服务及其配置。

```json
{
  "services": {
    "led_touch": {
      "enabled": true,
      "auto_start": true,
      "priority": 5,
      "depends": ["hal"],
      "config": {
        "led_count": 16,
        "brightness": 80,
        "default_effect": "rainbow"
      }
    },
    "led_board": {
      "enabled": true,
      "auto_start": true,
      "priority": 5,
      "depends": ["hal"],
      "config": {
        "led_count": 32,
        "brightness": 60
      }
    },
    "fan_control": {
      "enabled": true,
      "auto_start": true,
      "priority": 3,
      "depends": ["hal"],
      "config": {
        "update_interval_ms": 1000,
        "default_mode": "auto"
      }
    },
    "ethernet": {
      "enabled": true,
      "auto_start": true,
      "priority": 2,
      "depends": ["hal"],
      "config": {
        "dhcp": true,
        "hostname": "tianshanOS"
      }
    },
    "wifi_ap": {
      "enabled": true,
      "auto_start": false,
      "priority": 2,
      "depends": ["hal"],
      "config": {
        "ssid": "TianShanOS",
        "password": "tianshan123",
        "channel": 1,
        "max_connections": 4
      }
    },
    "webui": {
      "enabled": true,
      "auto_start": true,
      "priority": 6,
      "depends": ["ethernet", "storage"],
      "config": {
        "port": 80,
        "auth_required": true
      }
    },
    "console": {
      "enabled": true,
      "auto_start": true,
      "priority": 1,
      "config": {
        "uart_num": 0,
        "baud_rate": 115200
      }
    },
    "storage": {
      "enabled": true,
      "auto_start": true,
      "priority": 1,
      "config": {
        "spiffs_mount": "/data",
        "sd_mount": "/sd"
      }
    },
    "agx_control": {
      "enabled": true,
      "auto_start": true,
      "priority": 4,
      "depends": ["hal"],
      "config": {
        "power_on_delay_ms": 100,
        "shutdown_timeout_ms": 30000
      }
    }
  }
}
```

### 服务字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| enabled | bool | 是否启用此服务 |
| auto_start | bool | 是否自动启动 |
| priority | int | 启动优先级 (1-8) |
| depends | array | 依赖的其他服务 |
| config | object | 服务特定配置 |

## 添加新板级

1. 创建目录:
```bash
mkdir boards/my_board
```

2. 复制模板:
```bash
cp boards/rm01_esp32s3/*.json boards/my_board/
```

3. 修改配置以匹配硬件

4. 在 `sdkconfig.defaults` 中指定板级:
```
CONFIG_TS_BOARD_NAME="my_board"
```

## 运行时覆盖

引脚配置可以在运行时通过 NVS 覆盖:

```c
// 代码中设置覆盖
ts_pin_set_override("LED_TOUCH", 48);

// 通过命令行
config set pin.LED_TOUCH 48
```

覆盖优先级：NVS > pins.json > 编译时默认值

## 验证配置

使用以下命令验证配置:

```bash
# 检查 JSON 语法
python3 -m json.tool boards/my_board/board.json

# 运行配置验证脚本
python3 scripts/validate_board.py boards/my_board
```

## 最佳实践

1. **引脚冲突检测**: 确保同一 GPIO 不被多个功能使用
2. **保留引脚**: 避免使用 strapping pins (GPIO 0, 3, 45, 46)
3. **文档化**: 为每个引脚添加描述
4. **版本控制**: 将配置文件纳入版本控制
5. **测试**: 在实际硬件上验证配置
