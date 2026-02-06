# TianshanOS Logo 资源

本目录包含 TianshanOS 的官方 logo 图片资源，以不同分辨率提供，用于不同场景。

## 文件说明

| 文件名 | 尺寸 | 用途 | 使用位置 |
|--------|------|------|----------|
| `tslogo-original.png` | 1024x1024 | 原始高清版本 | 设计源文件 |
| `tslogo-512.png` | 512x512 | 高分辨率版本 | 启动画面、关于页面 |
| `tslogo-256.png` | 256x256 | 中高分辨率 | 应用图标、大型展示 |
| `tslogo-128.png` | 128x128 | 标准分辨率 | 应用列表图标 |
| `tslogo-64.png` | 64x64 | 中等图标 | 工具栏、通知 |
| `tslogo-48.png` | 48x48 | Header 导航栏 | WebUI 顶部 Logo |
| `tslogo-32.png` | 32x32 | 小图标 | 按钮、标签页图标 |
| `favicon.ico` | 多尺寸 | 浏览器标签图标 | 浏览器 favicon |
| `favicon.icns` | 多尺寸 | macOS 图标格式 | 中间产物 |

## 设计说明

Logo 采用渐变背景设计：
- **颜色渐变**：从左下角的青色 (#00BCD4) 渐变到右上角的紫色 (#9C27B0)
- **主体图形**：黑色山形轮廓，象征"天山"主题
- **圆角设计**：柔和的圆角矩形背景，现代化风格
- **高对比度**：黑色图形在渐变背景上清晰可辨

## 使用示例

### HTML
```html
<!-- Header 导航栏 -->
<img src="/images/tslogo-48.png" alt="TianshanOS Logo" class="logo-icon">

<!-- Favicon -->
<link rel="icon" href="/favicon.ico" type="image/x-icon">
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
