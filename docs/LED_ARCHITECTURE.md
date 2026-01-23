# TianShanOS LED 子系统架构设计

## 概述

本文档描述 LED 子系统的分层架构设计，明确 **图像 (Image)**、**动画 (Animation)**、**特效 (Effect)** 和 **图层 (Layer)** 的概念边界和交互方式。

---

## 架构分层

```
┌─────────────────────────────────────────────────────────────┐
│                      渲染输出 (Output)                       │
│                   最终合成 → LED 驱动                        │
├─────────────────────────────────────────────────────────────┤
│                      特效层 (Effect)                         │
│     后处理滤镜：作用于图层输出，不生成内容                    │
│     闪烁、脉冲、淡入淡出、色相偏移、扫描线...                 │
├─────────────────────────────────────────────────────────────┤
│                      图层系统 (Layer)                        │
│     Layer 0 (背景) → Layer 1 → Layer 2 → ... → 混合合成      │
│     每层独立的内容、透明度、混合模式                          │
├─────────────────────────────────────────────────────────────┤
│                      内容层 (Content)                        │
│  ┌───────────┬─────────────┬─────────────┬──────────────┐   │
│  │  Image    │  Animation  │   Drawing   │    Text      │   │
│  │ 静态图片  │  动态内容   │   绘图API   │  文本渲染    │   │
│  │ PNG/BMP   │  GIF/程序化 │ 点/线/矩形  │ UTF-8/字库   │   │
│  └───────────┴─────────────┴─────────────┴──────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## 概念定义

### 1. Image（图像）

**定义**：静态的像素数据，不随时间变化。

| 属性 | 说明 |
|------|------|
| 来源 | PNG、BMP 文件或内存数据 |
| 特点 | 单帧、静态、加载后不变 |
| 操作 | 缩放、裁剪、透明度、位置偏移 |

```c
// 加载并显示图像
ts_led_image_t img;
ts_led_image_load("/sdcard/logo.png", &img);
ts_led_layer_set_image(layer, img);
```

### 2. Animation（动画）

**定义**：随时间变化的内容序列，可以是多帧图像或程序生成。

| 类型 | 说明 |
|------|------|
| **帧动画** | GIF 等多帧图像文件 |
| **程序动画** | 代码生成的动态内容（原 effect） |

```c
// 帧动画（GIF）
ts_led_animation_t anim;
ts_led_animation_load("/sdcard/anim.gif", &anim);
ts_led_layer_play_animation(layer, anim);

// 程序动画（原 effect，如 rainbow、fire）
ts_led_layer_play_builtin(layer, "fire", &params);
```

**程序动画列表**（原 effect 重命名）：

| 动画名 | 适用设备 | 说明 |
|--------|----------|------|
| `rainbow` | 全部 | 彩虹渐变 |
| `breathing` | 全部 | 呼吸灯 |
| `sparkle` | 全部 | 闪烁 |
| `fire` | matrix | 火焰 |
| `rain` | matrix | 数字雨 |
| `plasma` | matrix | 等离子 |
| `chase` | board | 追逐灯 |
| `comet` | board | 流星 |

### 3. Effect（特效）

**定义**：后处理滤镜，作用于图层已有内容，不生成新内容。

| 特效 | 说明 | 主要参数 |
|------|------|----------|
| `blink` | 闪烁（周期性开关） | 频率、占空比 |
| `pulse` | 脉冲（亮度正弦波动） | 频率、幅度 |
| `fade_in` | 淡入 | 持续时间 |
| `fade_out` | 淡出 | 持续时间 |
| `color_shift` | 色相偏移 | 偏移角度、速度 |
| `brightness_wave` | 亮度波 | 角度 (0-360°)、波长、振幅、速度 |
| `scanline` | 扫描线 | 角度 (0-360°)、宽度、强度、速度 |
| `sparkle` | 闪耀 | 速度 (0.1-100)、密度 (0-255)、衰减 (0-255) |
| `glitch` | 故障风格 | 强度、频率 |

**参数详解**：
- **scanline/wave angle**：0°=水平向右，90°=垂直向上，支持0-360°任意角度
- **wave amplitude**：亮度变化幅度 (0-255)，推荐50-200
- **sparkle speed**：闪烁频率，推荐1-10（低值更慢）
- **sparkle density**：同时闪烁的像素数，推荐50-150
- **sparkle decay**：余晖衰减速度，推荐100-200（值越大衰减越快）

```c
// 特效作用于图层
ts_led_effect_params_t params = {
    .type = TS_EFFECT_PULSE,
    .frequency = 2.0,    // 2Hz
    .amplitude = 0.3,    // 30% 亮度变化
};
ts_led_layer_apply_effect(layer, &params);

// 移除特效
ts_led_layer_remove_effect(layer);
```

**关键区别**：
- Animation 生成内容 → 填充图层缓冲区
- Effect 处理内容 → 修改图层输出（不改变原数据）

### 4. Text（文本渲染）

**定义**：使用位图字体在 LED 矩阵上显示文本，支持滚动和覆盖层模式。

| 属性 | 说明 |
|------|------|
| 字体 | BoutiqueBitmap 9x9 像素字体 |
| 字符集 | ASCII (95字符) + CJK (GB2312 6763汉字) |
| 对齐 | left / center / right |
| 滚动 | left / right / up / down / none |
| 覆盖层 | 独立 Layer 1 渲染，不影响底层动画 |

**字体文件格式 (.fnt)**：

```
┌─────────────────────────────────────┐
│ Header (16 bytes)                   │
│   magic(4) + version(1) + w(1)      │
│   + h(1) + flags(1) + count(4)      │
│   + index_offset(4)                 │
├─────────────────────────────────────┤
│ Index Table (6 bytes × glyph_count) │
│   codepoint(2) + offset(4)          │
├─────────────────────────────────────┤
│ Bitmap Data                         │
│   packed bits, ceil(w*h/8) per char │
└─────────────────────────────────────┘
```

**覆盖层架构**：

```
┌─────────────────────────────────────┐
│     Text Overlay (Layer 1)          │  ← 文本渲染任务
├─────────────────────────────────────┤
│  Animation/Image (Layer 0)          │  ← 背景内容
├─────────────────────────────────────┤
│     Main Render Task (60Hz)         │  ← 图层合成
├─────────────────────────────────────┤
│     LED Driver (WS2812)             │
└─────────────────────────────────────┘
```

```c
// 文本显示 API
esp_err_t ts_led_draw_text(ts_led_device_t dev, const char *text, 
                           const ts_led_text_config_t *config);
esp_err_t ts_led_text_overlay_start(ts_led_device_t dev, const char *text,
                                    const ts_led_text_config_t *config);
esp_err_t ts_led_text_overlay_stop(ts_led_device_t dev);
```

### 5. Layer（图层）

**定义**：独立的渲染单元，包含内容和属性，多个图层可叠加合成。

| 属性 | 说明 |
|------|------|
| `content` | 图像、动画或绘图内容 |
| `opacity` | 图层整体透明度 0-255 |
| `blend_mode` | 混合模式 |
| `effect` | 应用的特效 |
| `visible` | 是否可见 |
| `z_order` | 堆叠顺序 |

**混合模式**：

```c
typedef enum {
    TS_BLEND_NORMAL,      // 正常（alpha 混合）
    TS_BLEND_ADD,         // 加法（发光效果）
    TS_BLEND_MULTIPLY,    // 乘法（暗化）
    TS_BLEND_SCREEN,      // 滤色（亮化）
    TS_BLEND_OVERLAY,     // 叠加
} ts_led_blend_mode_t;
```

---

## 渲染流程

```
┌──────────────────────────────────────────────────────────────┐
│                        渲染循环                               │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. 更新动画帧                                               │
│     for each layer:                                          │
│         if has_animation: advance_frame()                    │
│                                                              │
│  2. 渲染图层内容                                             │
│     for each layer:                                          │
│         render content → layer_buffer                        │
│                                                              │
│  3. 应用特效                                                 │
│     for each layer:                                          │
│         if has_effect: apply_effect(layer_buffer)            │
│                                                              │
│  4. 合成图层                                                 │
│     for z_order 0 to N:                                      │
│         blend layer → composite_buffer                       │
│                                                              │
│  5. 输出到设备                                               │
│     composite_buffer → LED driver → WS2812                   │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

---

## API 设计

### 图层管理

```c
// 创建/获取图层
ts_led_layer_t ts_led_layer_create(ts_led_device_t dev);
ts_led_layer_t ts_led_layer_get(ts_led_device_t dev, int index);

// 图层属性
esp_err_t ts_led_layer_set_opacity(ts_led_layer_t layer, uint8_t opacity);
esp_err_t ts_led_layer_set_blend_mode(ts_led_layer_t layer, ts_led_blend_mode_t mode);
esp_err_t ts_led_layer_set_visible(ts_led_layer_t layer, bool visible);
```

### 内容设置

```c
// 静态图像
esp_err_t ts_led_layer_set_image(ts_led_layer_t layer, ts_led_image_t image);

// 动画
esp_err_t ts_led_layer_play_animation(ts_led_layer_t layer, ts_led_animation_t anim);
esp_err_t ts_led_layer_play_builtin(ts_led_layer_t layer, const char *name, void *params);
esp_err_t ts_led_layer_stop_animation(ts_led_layer_t layer);

// 绘图
esp_err_t ts_led_layer_draw_pixel(ts_led_layer_t layer, int x, int y, ts_led_rgb_t color);
esp_err_t ts_led_layer_draw_rect(ts_led_layer_t layer, int x, int y, int w, int h, ts_led_rgb_t color);
esp_err_t ts_led_layer_draw_text(ts_led_layer_t layer, int x, int y, const char *text, ...);
```

### 特效控制

```c
// 应用特效
esp_err_t ts_led_layer_apply_effect(ts_led_layer_t layer, const ts_led_effect_params_t *params);
esp_err_t ts_led_layer_remove_effect(ts_led_layer_t layer);

// 特效参数
typedef struct {
    ts_led_effect_type_t type;
    float frequency;       // Hz
    float amplitude;       // 0.0-1.0
    float phase;           // 0.0-1.0
    uint32_t duration_ms;  // 0 = 永久
    // ... 其他参数
} ts_led_effect_params_t;
```

---

## 重构计划

### Phase 1: 重命名（保持兼容）

1. **内部重命名**：
   - `ts_led_effect_t` → `ts_led_animation_def_t`
   - `ts_led_effect_start()` → `ts_led_animation_start()`（内部）
   - 保留 `ts_led_effect_*` 作为别名

2. **CLI 命令保持不变**：
   - `led --effect` 继续工作
   - 内部调用重命名后的函数

### Phase 2: 新建特效系统

1. **创建 `ts_led_effect.h/c`**（真正的后处理特效）
2. **实现基础特效**：`blink`, `pulse`, `fade`
3. **图层集成**：`ts_led_layer_apply_effect()`

### Phase 3: 增强图层系统

1. **多图层支持**：动态创建/销毁图层
2. **混合模式**：实现 blend 算法
3. **图层特效**：每层独立特效

### Phase 4: CLI 扩展

```bash
# 新命令示例
led --layer 0 --image /sdcard/bg.png           # 图层0显示背景
led --layer 1 --animation fire                  # 图层1播放动画
led --layer 1 --effect pulse --frequency 2      # 图层1应用脉冲特效
led --layer 1 --opacity 128                     # 图层1半透明
led --layer 1 --blend add                       # 图层1加法混合
```

---

## 内存考量

| 组件 | 内存占用 | 说明 |
|------|----------|------|
| 图层缓冲区 | 32×32×3 = 3KB | 每图层 |
| 合成缓冲区 | 3KB | 全局一个 |
| 动画帧 | 3KB × N | GIF 帧数 |
| 特效状态 | ~100B | 每图层 |

**8MB PSRAM 足够支持**：
- 4 个图层（12KB）
- 32 帧 GIF（96KB）
- 大量余量

---

## 文件结构

```
components/ts_led/
├── include/
│   ├── ts_led.h              # 主头文件
│   ├── ts_led_layer.h        # 图层管理
│   ├── ts_led_image.h        # 图像加载
│   ├── ts_led_animation.h    # 动画系统（新）
│   ├── ts_led_effect.h       # 特效系统（新，后处理）
│   └── ts_led_draw.h         # 绘图API（新）
└── src/
    ├── ts_led.c
    ├── ts_led_layer.c
    ├── ts_led_image.c
    ├── ts_led_animation.c    # 原 effects 重构
    ├── ts_led_effect.c       # 新特效系统
    ├── ts_led_draw.c         # 绘图实现
    └── ts_led_driver.c
```

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.2 | 2026-01-16 | 添加文本渲染和覆盖层系统 |
| 0.1 | 2026-01-15 | 初始架构设计 |
