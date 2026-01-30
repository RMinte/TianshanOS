#!/bin/bash
# TianShanOS OTA Server 启动脚本
#
# 用法：
#   ./start.sh                 # 自动检测 build 目录
#   ./start.sh /path/to/build  # 指定 build 目录
#   ./start.sh --port 8080     # 自定义端口

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="${PROJECT_ROOT}/build"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${BLUE}           TianShanOS OTA Server                           ${NC}"
echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"

# 检查 Python
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}错误: 未找到 python3${NC}"
    exit 1
fi

# 检查依赖
if ! python3 -c "import flask" 2>/dev/null; then
    echo -e "${YELLOW}安装依赖...${NC}"
    pip3 install -r "${SCRIPT_DIR}/requirements.txt"
fi

# 解析参数
BUILD_ARG=""
EXTRA_ARGS=""

for arg in "$@"; do
    if [[ -d "$arg" ]]; then
        BUILD_DIR="$arg"
    else
        EXTRA_ARGS="$EXTRA_ARGS $arg"
    fi
done

# 检查 build 目录
if [[ ! -d "$BUILD_DIR" ]]; then
    echo -e "${RED}错误: 构建目录不存在: ${BUILD_DIR}${NC}"
    echo -e "${YELLOW}请先运行 'idf.py build' 构建项目${NC}"
    exit 1
fi

# 检查固件文件
FIRMWARE="${BUILD_DIR}/TianShanOS.bin"
WWW="${BUILD_DIR}/www.bin"

if [[ ! -f "$FIRMWARE" ]]; then
    echo -e "${RED}错误: 固件文件不存在: ${FIRMWARE}${NC}"
    exit 1
fi

echo -e "${GREEN}✓ 固件: ${FIRMWARE}${NC}"
if [[ -f "$WWW" ]]; then
    echo -e "${GREEN}✓ WebUI: ${WWW}${NC}"
else
    echo -e "${YELLOW}⚠ WebUI 不可用${NC}"
fi

echo ""

# 启动服务器
exec python3 "${SCRIPT_DIR}/ota_server.py" \
    --build-dir "$BUILD_DIR" \
    $EXTRA_ARGS
