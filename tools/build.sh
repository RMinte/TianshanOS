#!/bin/bash
# TianShanOS 构建脚本
# 
# 使用方法:
#   ./tools/build.sh          - 普通增量构建（版本号不变）
#   ./tools/build.sh --fresh  - 强制更新版本号后构建
#   ./tools/build.sh --clean  - 完整清理后构建（fullClean + build）
#
# 版本号只在 CMake reconfigure 时更新，增量编译不会改变版本号

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_DIR"

# 确保 ESP-IDF 环境已激活
if [ -z "$IDF_PATH" ]; then
    echo "正在激活 ESP-IDF 环境..."
    source ~/esp/v5.5/esp-idf/export.sh 2>/dev/null || {
        echo "错误: 无法找到 ESP-IDF，请先安装或手动 source export.sh"
        exit 1
    }
fi

case "${1:-}" in
    --fresh|-f)
        echo "🔄 强制更新版本号..."
        # 删除 CMake 缓存，强制 reconfigure
        rm -f build/CMakeCache.txt
        idf.py build
        ;;
    --clean|-c)
        echo "🧹 执行完整清理构建..."
        idf.py fullclean
        idf.py build
        ;;
    --help|-h)
        echo "TianShanOS 构建脚本"
        echo ""
        echo "用法: $0 [选项]"
        echo ""
        echo "选项:"
        echo "  (无)         普通增量构建（快速，版本号不变）"
        echo "  --fresh, -f  强制更新版本号后构建"
        echo "  --clean, -c  完整清理后构建（最慢但最干净）"
        echo "  --help, -h   显示此帮助"
        echo ""
        echo "版本号格式: MAJOR.MINOR.PATCH+HASH.TIMESTAMP"
        echo "例如: 0.3.1+7fdcca4d.01311234"
        ;;
    *)
        echo "📦 增量构建..."
        idf.py build
        ;;
esac

# 显示当前版本
if [ -f build/project_description.json ]; then
    VERSION=$(grep -o '"project_version":[^,]*' build/project_description.json | cut -d'"' -f4)
    echo ""
    echo "✅ 当前固件版本: $VERSION"
fi
