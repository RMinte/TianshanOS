# TianShanOS OTA Server

本地 OTA 服务器，用于开发调试时快速部署固件更新。

## 快速开始

```bash
# 启动服务器（默认端口 57807）
python OTA_Server/server.py

# 指定端口
python OTA_Server/server.py 8080
```

## API 端点

| 端点 | 说明 |
|------|------|
| `/` | 服务器状态页面（HTML） |
| `/version` | 获取固件版本信息（JSON） |
| `/firmware` | 下载固件文件 |
| `/info` | 详细固件信息（JSON） |

## 版本信息响应

```json
{
  "version": "0.1.0",
  "project": "TianShanOS",
  "size": 1981488,
  "sha256": "abc123...",
  "timestamp": 1737556638,
  "compile_date": "Jan 22 2026",
  "compile_time": "21:47:18",
  "download_url": "/firmware"
}
```

## WebUI 配置

1. 打开 TianShanOS WebUI
2. 导航到 OTA 页面
3. 在"服务器设置"中输入 OTA 服务器地址：`http://YOUR_IP:8070`
4. 点击"保存"
5. 点击"检查更新"

## 开发工作流

```bash
# 1. 修改代码
# 2. 编译项目
idf.py build

# 3. 启动 OTA 服务器（如果未启动）
python OTA_Server/server.py &

# 4. 在 WebUI 点击"检查更新"
# 5. 如果有新版本，点击"升级"
```

## 注意事项

- 服务器自动从 `build/TianShanOS.bin` 读取固件
- 版本信息从 `build/project_description.json` 获取
- 支持 CORS，可跨域访问
- 仅用于开发调试，**不要用于生产环境**
