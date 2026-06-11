#!/bin/bash
# Build the temporary root password reset firmware.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="build-root-reset"
SDKCONFIG_PATH="$PROJECT_DIR/$BUILD_DIR/sdkconfig"
ROOT_RESET_VERSION="${ROOT_PASSWORD_RESET_VERSION:-}"

cd "$PROJECT_DIR"

if [ -z "$IDF_PATH" ]; then
    echo "正在激活 ESP-IDF 环境..."
    source ~/esp/v5.5.2/esp-idf/export.sh 2>/dev/null || {
        echo "错误: 无法找到 ESP-IDF，请先安装或手动 source export.sh"
        exit 1
    }
fi

GIT_SHORT_SHA="$(git rev-parse --short=6 HEAD 2>/dev/null || echo nogit)"
RUN_ID="$(date -u +%H%M%S).${GIT_SHORT_SHA}"

echo "构建 root 密码恢复固件..."
echo "Run ID: ${RUN_ID}"
if [ -n "$ROOT_RESET_VERSION" ]; then
    echo "Version override: ${ROOT_RESET_VERSION}"
fi

mkdir -p "$BUILD_DIR"
rm -f "$BUILD_DIR/CMakeCache.txt"

IDF_ARGS=(
    -B "$BUILD_DIR"
    -DSDKCONFIG="$SDKCONFIG_PATH"
    -DROOT_PASSWORD_RESET=ON
    -DROOT_PASSWORD_RESET_RUN_ID="$RUN_ID"
)
if [ -n "$ROOT_RESET_VERSION" ]; then
    IDF_ARGS+=("-DROOT_PASSWORD_RESET_VERSION=$ROOT_RESET_VERSION")
fi

IDF_COMPONENT_MANAGER=0 idf.py \
    "${IDF_ARGS[@]}" \
    build

python3 - "$BUILD_DIR" <<'PY'
import json
import pathlib
import sys

build_dir = pathlib.Path(sys.argv[1])

project = json.loads((build_dir / "project_description.json").read_text())
version = project.get("project_version", "")
if "-root-reset." not in version:
    raise SystemExit(f"错误: 构建产物版本不是 root-reset: {version}")

components = set(project.get("build_components", []))
forbidden_components = {
    "ts_api",
    "ts_automation",
    "ts_cert",
    "ts_config",
    "ts_config_pack",
    "ts_console",
    "ts_core",
    "ts_drivers",
    "ts_event",
    "ts_hal",
    "ts_https",
    "ts_jsonpath",
    "ts_led",
    "ts_log",
    "ts_mempool",
    "ts_net",
    "ts_ota",
    "ts_pki_client",
    "ts_security",
    "ts_service",
    "ts_storage",
    "ts_webui",
}
present_forbidden = sorted(components & forbidden_components)
if present_forbidden:
    raise SystemExit("错误: 恢复固件包含正常系统组件: " + ", ".join(present_forbidden))

managed = sorted(component for component in components if component.startswith("espressif__"))
if managed:
    raise SystemExit("错误: 恢复固件包含托管组件: " + ", ".join(managed))

compile_commands = json.loads((build_dir / "compile_commands.json").read_text())
main_sources = sorted({
    pathlib.Path(command["file"]).name
    for command in compile_commands
    if "/main/" in command["file"]
})
if main_sources != ["root_password_reset_main.c"]:
    raise SystemExit("错误: main 源文件不符合恢复构建预期: " + ", ".join(main_sources))

print(f"恢复固件自检通过: {version}")
PY

cp "$BUILD_DIR/TianShanOS.bin" "$BUILD_DIR/TianShanOS-RootReset.bin"

echo ""
echo "恢复固件已生成:"
echo "  $BUILD_DIR/TianShanOS.bin"
echo "  $BUILD_DIR/TianShanOS-RootReset.bin"
echo ""
echo "人工操作请使用 $BUILD_DIR/TianShanOS-RootReset.bin。"
