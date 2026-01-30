#!/usr/bin/env python3
"""
TianShanOS OTA Server

简单的本地 OTA 服务器，用于开发调试。
自动检测 build 目录中的固件，提供版本信息和下载服务。

用法:
    python server.py [port]
    
    默认端口: 8070
    
访问:
    http://localhost:8070/           - 服务器状态和固件信息
    http://localhost:8070/version    - 获取版本信息 (JSON)
    http://localhost:8070/firmware   - 下载固件文件
    http://localhost:8070/info       - 详细固件信息

配置 WebUI:
    在 OTA 页面设置服务器地址为: http://<your-ip>:8070
"""

import http.server
import socketserver
import json
import os
import sys
import re
import struct
import hashlib
from datetime import datetime
from pathlib import Path

# 配置
DEFAULT_PORT = 57807
BUILD_DIR = Path(__file__).parent.parent / "build"
FIRMWARE_NAME = "TianShanOS.bin"
WWW_NAME = "www.bin"

class OTAHandler(http.server.BaseHTTPRequestHandler):
    """OTA HTTP 请求处理器"""
    
    # 强制使用 HTTP/1.1 响应
    protocol_version = "HTTP/1.1"
    
    def log_message(self, format, *args):
        """自定义日志格式"""
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {args[0]}")
    
    def send_cors_headers(self):
        """发送 CORS 头"""
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        self.send_header('Connection', 'close')  # 明确关闭连接，避免 keep-alive 问题
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
    
    def do_OPTIONS(self):
        """处理 CORS 预检请求"""
        self.send_response(200)
        self.send_cors_headers()
        self.end_headers()
    
    def do_HEAD(self):
        """处理 HEAD 请求 - ESP-IDF OTA 客户端需要"""
        path = self.path.split('?')[0]
        
        if path == '/firmware' or path == '/firmware.bin' or path == '/TianShanOS.bin':
            firmware_path = BUILD_DIR / FIRMWARE_NAME
            if firmware_path.exists():
                stat = firmware_path.stat()
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(stat.st_size))
                self.send_header('Accept-Ranges', 'bytes')
                self.send_cors_headers()
                self.end_headers()
            else:
                self.send_error(404, "Firmware not found")
        elif path == '/www.bin':
            www_path = BUILD_DIR / WWW_NAME
            if www_path.exists():
                stat = www_path.stat()
                self.send_response(200)
                self.send_header('Content-Type', 'application/octet-stream')
                self.send_header('Content-Length', str(stat.st_size))
                self.send_header('Accept-Ranges', 'bytes')
                self.send_cors_headers()
                self.end_headers()
            else:
                self.send_error(404, "www.bin not found")
        else:
            self.send_error(404, "Not Found")
    
    def do_GET(self):
        """处理 GET 请求"""
        path = self.path.split('?')[0]  # 移除查询参数
        
        if path == '/' or path == '/status':
            self.handle_status()
        elif path == '/version':
            self.handle_version()
        elif path == '/firmware' or path == '/firmware.bin' or path == '/TianShanOS.bin':
            self.handle_firmware()
        elif path == '/www.bin':
            self.handle_www()
        elif path == '/info':
            self.handle_info()
        else:
            self.send_error(404, "Not Found")
    
    def get_firmware_info(self):
        """获取固件信息"""
        firmware_path = BUILD_DIR / FIRMWARE_NAME
        
        if not firmware_path.exists():
            return None
        
        stat = firmware_path.stat()
        
        # 尝试从 project_description.json 获取版本信息
        version = "unknown"
        project = "TianShanOS"
        idf_version = "unknown"
        
        desc_path = BUILD_DIR / "project_description.json"
        if desc_path.exists():
            try:
                with open(desc_path, 'r') as f:
                    desc = json.load(f)
                    project = desc.get('project_name', project)
                    version = desc.get('project_version', version)
                    idf_version = desc.get('idf_version', idf_version)
            except:
                pass
        
        # 计算 SHA256
        sha256 = hashlib.sha256()
        with open(firmware_path, 'rb') as f:
            for chunk in iter(lambda: f.read(8192), b''):
                sha256.update(chunk)
        
        # 获取编译时间（使用文件修改时间）
        compile_time = datetime.fromtimestamp(stat.st_mtime)
        
        return {
            'version': version,
            'project': project,
            'idf_version': idf_version,
            'size': stat.st_size,
            'sha256': sha256.hexdigest(),
            'compile_date': compile_time.strftime('%b %d %Y'),
            'compile_time': compile_time.strftime('%H:%M:%S'),
            'timestamp': int(stat.st_mtime),
            'filename': FIRMWARE_NAME,
        }
    
    def handle_status(self):
        """处理状态页面"""
        info = self.get_firmware_info()
        
        html = """<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>TianShanOS OTA Server</title>
    <style>
        body { font-family: sans-serif; max-width: 800px; margin: 50px auto; padding: 20px; }
        h1 { color: #333; }
        .info { background: #f5f5f5; padding: 20px; border-radius: 8px; }
        .info p { margin: 8px 0; }
        .label { font-weight: bold; display: inline-block; width: 120px; }
        a { color: #3498db; }
        .error { color: #e74c3c; }
        .success { color: #27ae60; }
        code { background: #eee; padding: 2px 6px; border-radius: 3px; }
    </style>
</head>
<body>
    <h1>⛰️ TianShanOS OTA Server</h1>
"""
        
        if info:
            html += f"""
    <div class="info">
        <p class="success">✅ 固件就绪</p>
        <p><span class="label">项目:</span> {info['project']}</p>
        <p><span class="label">版本:</span> {info['version']}</p>
        <p><span class="label">IDF 版本:</span> {info['idf_version']}</p>
        <p><span class="label">大小:</span> {info['size']:,} 字节 ({info['size']/1024/1024:.2f} MB)</p>
        <p><span class="label">编译时间:</span> {info['compile_date']} {info['compile_time']}</p>
        <p><span class="label">SHA256:</span> <code>{info['sha256'][:16]}...</code></p>
    </div>
    
    <h2>API 端点</h2>
    <ul>
        <li><a href="/version">/version</a> - 获取版本信息 (JSON)</li>
        <li><a href="/firmware">/firmware</a> - 下载固件文件 (TianShanOS.bin)</li>
        <li><a href="/www.bin">/www.bin</a> - 下载 WebUI 文件</li>
        <li><a href="/info">/info</a> - 详细固件信息 (JSON)</li>
    </ul>
    
    <h2>WebUI 配置</h2>
    <p>在 TianShanOS WebUI 的 OTA 页面，设置服务器地址为:</p>
    <p><code>http://YOUR_IP:{self.server.server_address[1]}</code></p>
"""
        else:
            html += f"""
    <div class="info">
        <p class="error">❌ 固件未找到</p>
        <p>请先编译项目:</p>
        <p><code>idf.py build</code></p>
        <p>期望路径: <code>{BUILD_DIR / FIRMWARE_NAME}</code></p>
    </div>
"""
        
        html += """
</body>
</html>
"""
        
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_cors_headers()
        self.end_headers()
        self.wfile.write(html.encode('utf-8'))
    
    def handle_version(self):
        """处理版本信息请求"""
        info = self.get_firmware_info()
        
        if not info:
            self.send_response(404)
            self.send_header('Content-Type', 'application/json')
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(json.dumps({
                'error': 'Firmware not found',
                'message': 'Please build the project first'
            }).encode('utf-8'))
            return
        
        # 简化的版本信息（用于快速检查）
        version_info = {
            'version': info['version'],
            'project': info['project'],
            'size': info['size'],
            'sha256': info['sha256'],
            'timestamp': info['timestamp'],
            'compile_date': info['compile_date'],
            'compile_time': info['compile_time'],
            'download_url': f'/firmware',
        }
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_cors_headers()
        self.end_headers()
        self.wfile.write(json.dumps(version_info, indent=2).encode('utf-8'))
    
    def handle_info(self):
        """处理详细信息请求"""
        info = self.get_firmware_info()
        
        if not info:
            self.send_response(404)
            self.send_header('Content-Type', 'application/json')
            self.send_cors_headers()
            self.end_headers()
            self.wfile.write(json.dumps({
                'error': 'Firmware not found'
            }).encode('utf-8'))
            return
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_cors_headers()
        self.end_headers()
        self.wfile.write(json.dumps(info, indent=2).encode('utf-8'))
    
    def handle_firmware(self):
        """处理固件下载"""
        firmware_path = BUILD_DIR / FIRMWARE_NAME
        
        if not firmware_path.exists():
            self.send_error(404, "Firmware not found")
            return
        
        stat = firmware_path.stat()
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(stat.st_size))
        self.send_header('Content-Disposition', f'attachment; filename="{FIRMWARE_NAME}"')
        self.send_cors_headers()
        self.end_headers()
        
        with open(firmware_path, 'rb') as f:
            while True:
                chunk = f.read(8192)
                if not chunk:
                    break
                self.wfile.write(chunk)
        
        print(f"  -> Sent firmware: {stat.st_size:,} bytes")

    def handle_www(self):
        """处理 WebUI (www.bin) 下载"""
        www_path = BUILD_DIR / WWW_NAME
        
        if not www_path.exists():
            self.send_error(404, "www.bin not found")
            return
        
        stat = www_path.stat()
        
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(stat.st_size))
        self.send_header('Content-Disposition', f'attachment; filename="{WWW_NAME}"')
        self.send_cors_headers()
        self.end_headers()
        
        with open(www_path, 'rb') as f:
            while True:
                chunk = f.read(8192)
                if not chunk:
                    break
                self.wfile.write(chunk)
        
        print(f"  -> Sent www.bin: {stat.st_size:,} bytes")


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_PORT
    
    # 检查固件
    firmware_path = BUILD_DIR / FIRMWARE_NAME
    www_path = BUILD_DIR / WWW_NAME
    
    if firmware_path.exists():
        stat = firmware_path.stat()
        print(f"✅ 固件就绪: {firmware_path}")
        print(f"   大小: {stat.st_size:,} bytes ({stat.st_size/1024/1024:.2f} MB)")
    else:
        print(f"⚠️  固件未找到: {firmware_path}")
        print(f"   请先运行: idf.py build")
    
    if www_path.exists():
        stat = www_path.stat()
        print(f"✅ WebUI 就绪: {www_path}")
        print(f"   大小: {stat.st_size:,} bytes ({stat.st_size/1024:.1f} KB)")
    else:
        print(f"⚠️  WebUI 未找到: {www_path}")
        print(f"   请先运行: idf.py build")
    
    # 启动服务器 - 允许端口重用
    class ReusableTCPServer(socketserver.TCPServer):
        allow_reuse_address = True
    
    with ReusableTCPServer(("", port), OTAHandler) as httpd:
        print(f"\n🚀 OTA 服务器启动")
        print(f"   地址: http://0.0.0.0:{port}")
        print(f"   版本信息: http://localhost:{port}/version")
        print(f"   固件下载: http://localhost:{port}/firmware")
        print(f"   WebUI 下载: http://localhost:{port}/www.bin")
        print(f"\n按 Ctrl+C 停止服务器\n")
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n👋 服务器已停止")


if __name__ == '__main__':
    main()
