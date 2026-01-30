#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TianShanOS OTA 服务器

一个健壮的固件更新服务器，为 TianShanOS 设备提供 OTA 更新服务。

功能特性：
- 版本信息管理 (/version)
- 固件文件下载 (/firmware, /www)
- 文件完整性校验 (SHA256)
- 断点续传支持 (Range 请求)
- CORS 跨域支持
- 自动版本检测
- 详细日志记录
- 优雅的错误处理

使用方法：
    python ota_server.py [--port PORT] [--firmware PATH] [--www PATH]
    
    # 使用构建输出目录
    python ota_server.py --build-dir /path/to/TianshanOS/build
    
    # 指定具体文件
    python ota_server.py --firmware TianShanOS.bin --www www.bin

作者：TianShanOS Team
版本：1.0.0
"""

import os
import sys
import json
import hashlib
import argparse
import logging
import struct
import mimetypes
from pathlib import Path
from datetime import datetime
from functools import wraps
from typing import Optional, Dict, Any, Tuple

# 尝试导入 Flask
try:
    from flask import Flask, jsonify, request, send_file, Response, abort
    from flask_cors import CORS
except ImportError:
    print("错误: 需要安装 Flask 和 flask-cors")
    print("运行: pip install flask flask-cors")
    sys.exit(1)

# ============================================================================
#                           配置常量
# ============================================================================

DEFAULT_PORT = 57807
DEFAULT_HOST = "0.0.0.0"

# ESP-IDF 应用头结构 (参考 esp_app_format.h)
ESP_APP_DESC_MAGIC = 0xABCD5432
ESP_APP_DESC_OFFSET = 0x20  # 在 bin 文件中的偏移

# ============================================================================
#                           日志配置
# ============================================================================

class ColoredFormatter(logging.Formatter):
    """带颜色的日志格式化器"""
    
    COLORS = {
        'DEBUG': '\033[36m',     # 青色
        'INFO': '\033[32m',      # 绿色
        'WARNING': '\033[33m',   # 黄色
        'ERROR': '\033[31m',     # 红色
        'CRITICAL': '\033[35m',  # 紫色
    }
    RESET = '\033[0m'
    
    def format(self, record):
        color = self.COLORS.get(record.levelname, self.RESET)
        record.levelname = f"{color}{record.levelname}{self.RESET}"
        return super().format(record)

def setup_logging(debug: bool = False):
    """配置日志系统"""
    level = logging.DEBUG if debug else logging.INFO
    handler = logging.StreamHandler()
    handler.setFormatter(ColoredFormatter(
        '%(asctime)s [%(levelname)s] %(name)s: %(message)s',
        datefmt='%H:%M:%S'
    ))
    logging.basicConfig(level=level, handlers=[handler])

logger = logging.getLogger('ota-server')

# ============================================================================
#                           固件解析器
# ============================================================================

class FirmwareInfo:
    """固件信息类"""
    
    def __init__(self):
        self.version: str = "0.0.0"
        self.project_name: str = "TianShanOS"
        self.compile_date: str = ""
        self.compile_time: str = ""
        self.idf_version: str = ""
        self.secure_version: int = 0
        self.size: int = 0
        self.sha256: str = ""
        self.file_path: str = ""
        self.valid: bool = False
        self.error: str = ""

def parse_firmware(filepath: str) -> FirmwareInfo:
    """
    解析固件文件，提取版本信息
    
    ESP-IDF 固件结构：
    - 0x00-0x1F: Image header
    - 0x20-0x117: esp_app_desc_t 结构
    
    esp_app_desc_t 布局：
    - 0x00: magic (4 bytes, 0xABCD5432)
    - 0x04: secure_version (4 bytes)
    - 0x08: reserv1[2] (8 bytes)
    - 0x10: version (32 bytes, null-terminated string)
    - 0x30: project_name (32 bytes, null-terminated string)
    - 0x50: time (16 bytes, compile time)
    - 0x60: date (16 bytes, compile date)
    - 0x70: idf_ver (32 bytes, IDF version)
    - 0x90: app_elf_sha256 (32 bytes)
    - 0xB0: reserv2 (20 bytes)
    """
    info = FirmwareInfo()
    info.file_path = filepath
    
    if not os.path.exists(filepath):
        info.error = f"文件不存在: {filepath}"
        return info
    
    try:
        info.size = os.path.getsize(filepath)
        
        with open(filepath, 'rb') as f:
            # 计算整个文件的 SHA256
            sha256 = hashlib.sha256()
            while chunk := f.read(8192):
                sha256.update(chunk)
            info.sha256 = sha256.hexdigest()
            
            # 读取 app_desc
            f.seek(ESP_APP_DESC_OFFSET)
            app_desc = f.read(0xC4)  # esp_app_desc_t 大小
            
            if len(app_desc) < 0xC4:
                info.error = "文件太小，无法读取 app_desc"
                return info
            
            # 检查魔数
            magic = struct.unpack('<I', app_desc[0:4])[0]
            if magic != ESP_APP_DESC_MAGIC:
                info.error = f"无效的魔数: 0x{magic:08X} (期望 0x{ESP_APP_DESC_MAGIC:08X})"
                # 继续尝试解析，可能是旧版本格式
            
            # 解析字段
            info.secure_version = struct.unpack('<I', app_desc[4:8])[0]
            info.version = app_desc[0x10:0x30].split(b'\x00')[0].decode('utf-8', errors='replace')
            info.project_name = app_desc[0x30:0x50].split(b'\x00')[0].decode('utf-8', errors='replace')
            info.compile_time = app_desc[0x50:0x60].split(b'\x00')[0].decode('utf-8', errors='replace')
            info.compile_date = app_desc[0x60:0x70].split(b'\x00')[0].decode('utf-8', errors='replace')
            info.idf_version = app_desc[0x70:0x90].split(b'\x00')[0].decode('utf-8', errors='replace')
            
            info.valid = True
            
    except Exception as e:
        info.error = str(e)
    
    return info

# ============================================================================
#                           OTA 服务器
# ============================================================================

class OTAServer:
    """OTA 更新服务器"""
    
    def __init__(self, firmware_path: str, www_path: Optional[str] = None):
        """
        初始化 OTA 服务器
        
        Args:
            firmware_path: 固件文件路径
            www_path: WebUI 文件路径（可选）
        """
        self.firmware_path = os.path.abspath(firmware_path) if firmware_path else None
        self.www_path = os.path.abspath(www_path) if www_path else None
        
        self.firmware_info: Optional[FirmwareInfo] = None
        self.www_info: Optional[FirmwareInfo] = None
        
        # 文件修改时间缓存（用于自动检测更新）
        self._firmware_mtime: float = 0
        self._www_mtime: float = 0
        
        self.app = Flask(__name__)
        CORS(self.app, resources={
            r"/*": {
                "origins": "*",
                "methods": ["GET", "HEAD", "OPTIONS"],
                "allow_headers": ["Content-Type", "Range"],
                "expose_headers": ["Content-Length", "Content-Range", "Accept-Ranges"]
            }
        })
        
        self._setup_routes()
        self._load_firmware_info()
    
    def _check_file_changed(self) -> bool:
        """检查固件文件是否有变化，如有变化则自动重新加载"""
        changed = False
        
        # 检查固件文件
        if self.firmware_path and os.path.exists(self.firmware_path):
            mtime = os.path.getmtime(self.firmware_path)
            if mtime != self._firmware_mtime:
                changed = True
        
        # 检查 WebUI 文件
        if self.www_path and os.path.exists(self.www_path):
            mtime = os.path.getmtime(self.www_path)
            if mtime != self._www_mtime:
                changed = True
        
        if changed:
            logger.info("🔄 检测到固件文件更新，自动重新加载...")
            self._load_firmware_info()
        
        return changed
    
    def _load_firmware_info(self):
        """加载固件信息"""
        if self.firmware_path and os.path.exists(self.firmware_path):
            self._firmware_mtime = os.path.getmtime(self.firmware_path)
            self.firmware_info = parse_firmware(self.firmware_path)
            if self.firmware_info.valid:
                logger.info(f"📦 固件: {self.firmware_info.project_name} v{self.firmware_info.version}")
                logger.info(f"   编译时间: {self.firmware_info.compile_date} {self.firmware_info.compile_time}")
                logger.info(f"   文件大小: {self.firmware_info.size:,} bytes")
                logger.info(f"   SHA256: {self.firmware_info.sha256[:16]}...")
            else:
                logger.warning(f"⚠️ 固件解析失败: {self.firmware_info.error}")
        else:
            logger.warning(f"⚠️ 固件文件不存在: {self.firmware_path}")
        
        if self.www_path and os.path.exists(self.www_path):
            self._www_mtime = os.path.getmtime(self.www_path)
            self.www_info = FirmwareInfo()
            self.www_info.file_path = self.www_path
            self.www_info.size = os.path.getsize(self.www_path)
            with open(self.www_path, 'rb') as f:
                self.www_info.sha256 = hashlib.sha256(f.read()).hexdigest()
            self.www_info.valid = True
            logger.info(f"🌐 WebUI: {self.www_info.size:,} bytes")
        elif self.www_path:
            logger.warning(f"⚠️ WebUI 文件不存在: {self.www_path}")
    
    def _setup_routes(self):
        """设置路由"""
        
        @self.app.before_request
        def check_updates():
            """每次请求前检查固件是否有更新"""
            self._check_file_changed()
        
        @self.app.before_request
        def log_request():
            """记录请求日志"""
            client_ip = request.headers.get('X-Forwarded-For', request.remote_addr)
            logger.info(f"📥 {request.method} {request.path} from {client_ip}")
        
        @self.app.after_request
        def log_response(response):
            """记录响应日志"""
            logger.debug(f"📤 {response.status_code} {response.content_length or 0} bytes")
            return response
        
        @self.app.route('/')
        def index():
            """服务器首页"""
            return jsonify({
                "server": "TianShanOS OTA Server",
                "version": "1.0.0",
                "endpoints": {
                    "/version": "获取固件版本信息 (GET)",
                    "/firmware": "下载固件文件 (GET)",
                    "/www": "下载 WebUI 文件 (GET)",
                    "/health": "健康检查 (GET)"
                },
                "firmware_available": self.firmware_info.valid if self.firmware_info else False,
                "www_available": self.www_info.valid if self.www_info else False
            })
        
        @self.app.route('/health')
        def health():
            """健康检查端点"""
            status = {
                "status": "ok",
                "timestamp": datetime.now().astimezone().isoformat(),
                "firmware": {
                    "available": self.firmware_info.valid if self.firmware_info else False,
                    "path": self.firmware_path
                },
                "www": {
                    "available": self.www_info.valid if self.www_info else False,
                    "path": self.www_path
                }
            }
            return jsonify(status)
        
        @self.app.route('/version')
        def version():
            """返回固件版本信息"""
            if not self.firmware_info or not self.firmware_info.valid:
                return jsonify({
                    "error": "固件不可用",
                    "message": self.firmware_info.error if self.firmware_info else "未配置固件路径"
                }), 503
            
            return jsonify({
                "version": self.firmware_info.version,
                "project_name": self.firmware_info.project_name,
                "compile_date": self.firmware_info.compile_date,
                "compile_time": self.firmware_info.compile_time,
                "idf_version": self.firmware_info.idf_version,
                "secure_version": self.firmware_info.secure_version,
                "size": self.firmware_info.size,
                "sha256": self.firmware_info.sha256,
                "www_available": self.www_info.valid if self.www_info else False,
                "www_size": self.www_info.size if self.www_info and self.www_info.valid else 0,
                "www_sha256": self.www_info.sha256 if self.www_info and self.www_info.valid else ""
            })
        
        @self.app.route('/firmware')
        def firmware():
            """下载固件文件（支持断点续传）"""
            return self._serve_file(
                self.firmware_info,
                'application/octet-stream',
                'TianShanOS.bin'
            )
        
        @self.app.route('/www')
        def www():
            """下载 WebUI 文件（支持断点续传）"""
            if not self.www_info or not self.www_info.valid:
                return jsonify({
                    "error": "WebUI 不可用",
                    "message": "未配置 WebUI 文件"
                }), 404
            
            return self._serve_file(
                self.www_info,
                'application/octet-stream',
                'www.bin'
            )
        
        @self.app.route('/reload', methods=['POST'])
        def reload():
            """重新加载固件信息"""
            self._load_firmware_info()
            return jsonify({
                "status": "ok",
                "message": "固件信息已重新加载",
                "firmware_valid": self.firmware_info.valid if self.firmware_info else False,
                "www_valid": self.www_info.valid if self.www_info else False
            })
        
        @self.app.errorhandler(404)
        def not_found(e):
            return jsonify({"error": "Not Found", "message": str(e)}), 404
        
        @self.app.errorhandler(500)
        def internal_error(e):
            logger.error(f"Internal error: {e}")
            return jsonify({"error": "Internal Server Error", "message": str(e)}), 500
    
    def _serve_file(self, file_info: Optional[FirmwareInfo], 
                    content_type: str, download_name: str) -> Response:
        """
        提供文件下载，支持断点续传（Range 请求）
        """
        if not file_info or not file_info.valid:
            return jsonify({
                "error": "文件不可用",
                "message": file_info.error if file_info else "未配置"
            }), 404
        
        filepath = file_info.file_path
        filesize = file_info.size
        
        # 检查 Range 请求
        range_header = request.headers.get('Range')
        
        if range_header:
            # 解析 Range: bytes=start-end
            try:
                range_match = range_header.replace('bytes=', '').split('-')
                start = int(range_match[0]) if range_match[0] else 0
                end = int(range_match[1]) if range_match[1] else filesize - 1
                
                if start >= filesize or end >= filesize or start > end:
                    return Response(
                        "Range Not Satisfiable",
                        status=416,
                        headers={'Content-Range': f'bytes */{filesize}'}
                    )
                
                length = end - start + 1
                
                with open(filepath, 'rb') as f:
                    f.seek(start)
                    data = f.read(length)
                
                response = Response(
                    data,
                    status=206,
                    mimetype=content_type,
                    direct_passthrough=True
                )
                response.headers['Content-Range'] = f'bytes {start}-{end}/{filesize}'
                response.headers['Content-Length'] = length
                response.headers['Accept-Ranges'] = 'bytes'
                response.headers['Content-Disposition'] = f'attachment; filename="{download_name}"'
                
                logger.info(f"   Range: {start}-{end}/{filesize} ({length} bytes)")
                return response
                
            except (ValueError, IndexError) as e:
                logger.warning(f"Invalid Range header: {range_header}")
        
        # 完整文件下载
        response = send_file(
            filepath,
            mimetype=content_type,
            as_attachment=True,
            download_name=download_name
        )
        response.headers['Accept-Ranges'] = 'bytes'
        response.headers['Content-Length'] = filesize
        response.headers['X-SHA256'] = file_info.sha256
        
        return response
    
    def run(self, host: str = DEFAULT_HOST, port: int = DEFAULT_PORT, 
            debug: bool = False):
        """启动服务器"""
        logger.info("=" * 60)
        logger.info("🚀 TianShanOS OTA 服务器启动")
        logger.info("=" * 60)
        logger.info(f"🌐 监听地址: http://{host}:{port}")
        logger.info(f"📁 固件路径: {self.firmware_path}")
        logger.info(f"📁 WebUI 路径: {self.www_path or '(未配置)'}")
        logger.info("-" * 60)
        logger.info("API 端点:")
        logger.info(f"  GET /version  - 版本信息")
        logger.info(f"  GET /firmware - 下载固件 ({self.firmware_info.size:,} bytes)" if self.firmware_info and self.firmware_info.valid else "  GET /firmware - (不可用)")
        logger.info(f"  GET /www      - 下载 WebUI ({self.www_info.size:,} bytes)" if self.www_info and self.www_info.valid else "  GET /www      - (不可用)")
        logger.info(f"  GET /health   - 健康检查")
        logger.info("=" * 60)
        
        self.app.run(host=host, port=port, debug=debug, threaded=True)

# ============================================================================
#                           命令行接口
# ============================================================================

def find_build_files(build_dir: str) -> Tuple[Optional[str], Optional[str]]:
    """
    在构建目录中查找固件和 WebUI 文件
    """
    firmware_path = None
    www_path = None
    
    build_path = Path(build_dir)
    
    # 查找固件文件
    candidates = [
        build_path / "TianShanOS.bin",
        build_path / "tianshanos.bin",
        build_path / "firmware.bin",
    ]
    
    # 也搜索子目录
    for bin_file in build_path.glob("*.bin"):
        if bin_file.name not in ['www.bin', 'bootloader.bin', 'partition-table.bin', 
                                   'ota_data_initial.bin', 'storage.bin']:
            candidates.insert(0, bin_file)
    
    for candidate in candidates:
        if candidate.exists():
            # 验证是否为有效固件
            info = parse_firmware(str(candidate))
            if info.valid:
                firmware_path = str(candidate)
                break
    
    # 查找 WebUI 文件
    www_candidates = [
        build_path / "www.bin",
        build_path / "spiffs_www.bin",
    ]
    
    for candidate in www_candidates:
        if candidate.exists():
            www_path = str(candidate)
            break
    
    return firmware_path, www_path

def main():
    parser = argparse.ArgumentParser(
        description='TianShanOS OTA 服务器',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 使用构建目录（自动检测文件）
  %(prog)s --build-dir ./build
  
  # 指定具体文件
  %(prog)s --firmware TianShanOS.bin --www www.bin
  
  # 自定义端口
  %(prog)s --build-dir ./build --port 8080
  
  # 调试模式
  %(prog)s --build-dir ./build --debug
"""
    )
    
    parser.add_argument('-p', '--port', type=int, default=DEFAULT_PORT,
                        help=f'监听端口 (默认: {DEFAULT_PORT})')
    parser.add_argument('-H', '--host', default=DEFAULT_HOST,
                        help=f'监听地址 (默认: {DEFAULT_HOST})')
    parser.add_argument('-b', '--build-dir', 
                        help='ESP-IDF 构建目录 (自动检测固件文件)')
    parser.add_argument('-f', '--firmware',
                        help='固件文件路径')
    parser.add_argument('-w', '--www',
                        help='WebUI 文件路径')
    parser.add_argument('-d', '--debug', action='store_true',
                        help='启用调试模式')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='详细输出')
    
    args = parser.parse_args()
    
    # 设置日志
    setup_logging(debug=args.debug or args.verbose)
    
    # 确定文件路径
    firmware_path = args.firmware
    www_path = args.www
    
    if args.build_dir:
        if not os.path.isdir(args.build_dir):
            logger.error(f"构建目录不存在: {args.build_dir}")
            sys.exit(1)
        
        auto_firmware, auto_www = find_build_files(args.build_dir)
        
        if not firmware_path and auto_firmware:
            firmware_path = auto_firmware
            logger.info(f"自动检测到固件: {firmware_path}")
        
        if not www_path and auto_www:
            www_path = auto_www
            logger.info(f"自动检测到 WebUI: {www_path}")
    
    # 验证必要文件
    if not firmware_path:
        logger.error("未指定固件文件，请使用 --firmware 或 --build-dir 参数")
        sys.exit(1)
    
    if not os.path.exists(firmware_path):
        logger.error(f"固件文件不存在: {firmware_path}")
        sys.exit(1)
    
    # 创建并启动服务器
    server = OTAServer(firmware_path, www_path)
    
    try:
        server.run(host=args.host, port=args.port, debug=args.debug)
    except KeyboardInterrupt:
        logger.info("\n服务器已停止")
    except Exception as e:
        logger.error(f"服务器错误: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
