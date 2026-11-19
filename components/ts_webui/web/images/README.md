# TianshanOS Logo 资源

本目录包含 TianshanOS 的官方 logo 图片资源，以不同分辨率提供，用于不同场景。

## 文件说明

| 文件名 | 尺寸 | 用途 | 使用位置 |
|--------|------|------|----------|
| `tslogo-original.png` | 256x256 | 黑色山形原图 | 生成其他尺寸的源文件 |
| `tslogo-512.png` | 512x512 | 高分辨率版本 | 启动画面、关于页面 |
| `tslogo-256.png` | 256x256 | 中高分辨率 | 应用图标、大型展示 |
| `tslogo-128.png` | 128x128 | 标准分辨率 | 应用列表图标 |
| `tslogo-64.png` | 64x64 | 中等图标 | 工具栏、通知 |
| `tslogo-48.png` | 48x48 | 小尺寸 Logo | 保留资源 |
| `tslogo-32.png` | 32x32 | 小尺寸 Logo | 保留资源 |
| `favicon.svg` | 矢量 | 当前页头山形，透明底黑色线条 | 标签图标生成源 |
| `../favicon.ico` | 16/32/48/64/128/256 | 透明底黑色线条 | 浏览器标签图标 |
| `favicon.icns` | 256x256 | macOS 图标 | 保留资源 |

## 设计说明

PNG 与 macOS 图标使用 `TianshanOS-favicon.png` 的透明底黑色山形。`tslogo-512.png` 由 256×256 原图放大，保留现有尺寸。

浏览器标签使用 `favicon.ico`，由同目录下的 `favicon.svg` 生成。

## 使用示例

### HTML
```html
<!-- Favicon -->
<link rel="icon" href="/favicon.ico?v=mono-1" type="image/x-icon" sizes="16x16 32x32 48x48 64x64 128x128 256x256">
```

### CSS
```css
.logo-icon {
    width: 32px;
    height: 32px;
    object-fit: contain;
    display: block;
}
```

## 更新日期

- 初始创建：2026-01-30
- 替换原有 ⛰️ emoji 图标

## 版权信息

TianshanOS Logo © 2026 TianshanOS Project
