# TianShanOS 文档中心

**欢迎来到 TianShanOS 文档中心！** 本目录包含完整的项目文档，涵盖用户指南、开发参考和 API 手册。

---

## 📚 文档导航

### 🚀 快速开始

| 文档 | 说明 | 适合人群 |
|------|------|---------|
| [快速入门指南](QUICK_START.md) | 环境配置、编译烧录、基本使用 | 所有用户 |
| [命令参考手册](COMMAND_REFERENCE.md) | CLI 命令完整参考 | 所有用户 |
| [配置指南](CONFIGURATION_GUIDE.md) | 板级配置、GPIO 映射、运行时配置 | 管理员、开发者 |

---

### 🏗️ 架构与设计

| 文档 | 说明 | 适合人群 |
|------|------|---------|
| [项目概述](PROJECT_DESCRIPTION.md) | 项目定位、特性、架构概览 | 所有人 |
| [架构设计](ARCHITECTURE_DESIGN.md) | 核心架构、事件系统、服务管理、API 设计 | 开发者 |
| [配置系统设计](CONFIG_SYSTEM_DESIGN.md) | 配置驱动架构、优先级、实现细节 | 开发者 |
| [LED 系统架构](LED_ARCHITECTURE.md) | LED 子系统设计、动画引擎、滤镜系统 | 开发者 |
| [安全实现](SECURITY_IMPLEMENTATION.md) | 认证机制、SSH 客户端、密钥管理 | 开发者 |

---

### 📖 API 参考

| 文档 | 说明 | 适合人群 |
|------|------|---------|
| [API 参考手册](API_REFERENCE.md) | Core API、WebSocket、HTTP API 完整文档 | 开发者、集成商 |

---

### 🔧 开发文档

| 文档 | 说明 | 适合人群 |
|------|------|---------|
| [开发进度跟踪](DEVELOPMENT_PROGRESS.md) | Phase 0-15 完整开发历史、专项方案 | 团队成员 |
| [故障排除](TROUBLESHOOTING.md) | 常见问题解决方案、Debug 技巧 | 开发者 |
| [测试计划](TEST_PLAN.md) | 单元测试、集成测试、测试策略 | 测试人员、开发者 |

---

### 📋 专项方案（进行中）

| 文档 | 说明 | 状态 |
|------|------|------|
| [安全增强方案](SECURITY_ENHANCEMENT_PLAN.md) | HTTPS、权限管理、双向认证 | 🚧 Phase 16 规划 |
| [WebUI 清理方案](WEBUI_CLEANUP_PROPOSAL.md) | 导航栏优化、日志整合 | ✅ Phase 15 已完成 |

---

## 📁 文档分类

### 用户文档（面向使用者）
- ✅ 快速入门指南
- ✅ 命令参考手册
- ✅ 配置指南

### 开发文档（面向贡献者）
- ✅ 架构设计
- ✅ API 参考手册
- ✅ 开发进度跟踪
- ✅ 故障排除

### 设计文档（面向架构师）
- ✅ 项目概述
- ✅ 配置系统设计
- ✅ LED 系统架构
- ✅ 安全实现

---

## 🗂️ 文档版本历史

### 2026-01-24 - 文档整合（v1.1）
- ✅ 合并 `COMMANDS.md` + `COMMAND_SPECIFICATION.md` → `COMMAND_REFERENCE.md`
- ✅ 合并 `BOARD_CONFIGURATION.md` + `GPIO_MAPPING.md` → `CONFIGURATION_GUIDE.md`
- ✅ 删除过时文档：`REFACTORING_ANALYSIS.md`, `WEBSOCKET_MIGRATION.md`, `PHASE15_WEBSOCKET_CPU.md`, `VERSION_MANAGEMENT.md`, `API_DESIGN.md`
- ✅ 文档数量：20 → 13（-35%）

### 2026-01-23 - Phase 15 完成
- 新增 `WEBUI_CLEANUP_PROPOSAL.md`
- 更新 `TROUBLESHOOTING.md`（WebSocket 日志流问题）
- 更新 `QUICK_START.md`（WebUI 导航说明）

### 2026-01-22 - Phase 14 完成
- 更新 `LED_ARCHITECTURE.md`（滤镜系统）
- 更新 `DEVELOPMENT_PROGRESS.md`（Phase 14）

---

## 📝 文档贡献指南

### 文档格式规范
- 使用 **Markdown** 格式
- 标题使用 `#` 层级（最多 4 层）
- 代码块使用 ` ```语言` 标记
- 表格对齐，保持可读性

### 文档结构
每个文档应包含：
1. **标题和元信息**（版本、日期）
2. **目录**（超过 500 行）
3. **正文内容**
4. **参考链接**（如有）

### 提交文档
1. 更新文档内容
2. 更新文档版本号和日期
3. 在本 `README.md` 中添加更新记录
4. 提交 Git commit

---

## 🔗 外部资源

### ESP-IDF 官方文档
- [ESP-IDF v5.5 编程指南](https://docs.espressif.com/projects/esp-idf/en/v5.5/)
- [ESP32-S3 技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)

### TianShanOS 相关
- [GitHub 仓库](https://github.com/thomas-hiddenpeak/TianshanOS)
- [开发计划看板](https://github.com/thomas-hiddenpeak/TianshanOS/projects)

---

## 📞 联系与支持

- **问题反馈**：[GitHub Issues](https://github.com/thomas-hiddenpeak/TianshanOS/issues)
- **功能建议**：[GitHub Discussions](https://github.com/thomas-hiddenpeak/TianshanOS/discussions)

---

**最后更新**：2026年1月24日  
**文档版本**：v1.1  
**项目版本**：0.2.1
