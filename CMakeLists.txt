# TianShanOS - Main Project CMakeLists.txt
# ESP32 机架管理操作系统
#
# 作者: TianShanOS Team
# 版本: 0.1.0

cmake_minimum_required(VERSION 3.16)

# 设置组件目录
set(EXTRA_COMPONENT_DIRS 
    "${CMAKE_CURRENT_SOURCE_DIR}/components"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core/ts_config"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core/ts_log"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core/ts_event"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core/ts_service"
)

# 包含 ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 项目定义
project(TianShanOS VERSION 0.1.0)

# 添加编译定义
add_compile_definitions(
    TIANSHAN_OS_VERSION_MAJOR=0
    TIANSHAN_OS_VERSION_MINOR=1
    TIANSHAN_OS_VERSION_PATCH=0
)

# 打印项目信息
message(STATUS "")
message(STATUS "╔═══════════════════════════════════════════════════════════╗")
message(STATUS "║                     TianShanOS v0.1.0                     ║")
message(STATUS "║           ESP32 Rack Management Operating System          ║")
message(STATUS "╚═══════════════════════════════════════════════════════════╝")
message(STATUS "")
message(STATUS "Target: ${IDF_TARGET}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
