#!/bin/bash
# TianShanOS OTA 服务器安装/管理脚本
#
# 用法:
#   ./ota_service.sh install  - 安装为系统服务
#   ./ota_service.sh start    - 启动服务
#   ./ota_service.sh stop     - 停止服务  
#   ./ota_service.sh restart  - 重启服务
#   ./ota_service.sh status   - 查看状态
#   ./ota_service.sh logs     - 查看日志
#   ./ota_service.sh uninstall - 卸载服务

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVICE_FILE="$SCRIPT_DIR/tianshan-ota.service"
SERVICE_NAME="tianshan-ota"

case "${1:-}" in
    install)
        echo "📦 安装 TianShanOS OTA 服务..."
        
        # 停止可能正在运行的手动进程
        pkill -f ota_server.py 2>/dev/null || true
        
        # 复制服务文件
        sudo cp "$SERVICE_FILE" /etc/systemd/system/
        sudo systemctl daemon-reload
        sudo systemctl enable "$SERVICE_NAME"
        sudo systemctl start "$SERVICE_NAME"
        
        echo "✅ 服务已安装并启动"
        echo ""
        echo "管理命令:"
        echo "  systemctl status $SERVICE_NAME   - 查看状态"
        echo "  systemctl restart $SERVICE_NAME  - 重启服务"
        echo "  journalctl -u $SERVICE_NAME -f   - 查看日志"
        ;;
    
    start)
        sudo systemctl start "$SERVICE_NAME"
        echo "✅ 服务已启动"
        ;;
    
    stop)
        sudo systemctl stop "$SERVICE_NAME"
        echo "✅ 服务已停止"
        ;;
    
    restart)
        sudo systemctl restart "$SERVICE_NAME"
        echo "✅ 服务已重启"
        ;;
    
    status)
        systemctl status "$SERVICE_NAME" --no-pager
        ;;
    
    logs)
        journalctl -u "$SERVICE_NAME" -f
        ;;
    
    uninstall)
        echo "🗑️ 卸载 TianShanOS OTA 服务..."
        sudo systemctl stop "$SERVICE_NAME" 2>/dev/null || true
        sudo systemctl disable "$SERVICE_NAME" 2>/dev/null || true
        sudo rm -f /etc/systemd/system/$SERVICE_NAME.service
        sudo systemctl daemon-reload
        echo "✅ 服务已卸载"
        ;;
    
    *)
        echo "TianShanOS OTA 服务管理"
        echo ""
        echo "用法: $0 <命令>"
        echo ""
        echo "命令:"
        echo "  install   - 安装为系统服务（开机自启）"
        echo "  start     - 启动服务"
        echo "  stop      - 停止服务"
        echo "  restart   - 重启服务（固件更新后使用）"
        echo "  status    - 查看服务状态"
        echo "  logs      - 实时查看日志"
        echo "  uninstall - 卸载服务"
        echo ""
        echo "服务端口: 57807"
        echo "访问地址: http://192.168.0.152:57807"
        ;;
esac
