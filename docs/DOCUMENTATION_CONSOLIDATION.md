# 文档整合方案

**日期**：2026年1月24日  
**目标**：优化文档结构，减少冗余，提高可维护性

---

## 📊 当前文档清单（20 个文档）

### 核心设计文档（6 个）
1. `PROJECT_DESCRIPTION.md` - 项目概述
2. `ARCHITECTURE_DESIGN.md` - 架构设计
3. `CONFIG_SYSTEM_DESIGN.md` - 配置系统设计
4. `API_DESIGN.md` - API 设计
5. `LED_ARCHITECTURE.md` - LED 系统架构
6. `SECURITY_IMPLEMENTATION.md` - 安全实现

### 参考文档（5 个）
7. `API_REFERENCE.md` - API 参考手册
8. `COMMANDS.md` - CLI 命令参考
9. `BOARD_CONFIGURATION.md` - 板级配置说明
10. `GPIO_MAPPING.md` - GPIO 引脚映射
11. `COMMAND_SPECIFICATION.md` - 命令规范

### 开发文档（4 个）
12. `DEVELOPMENT_PROGRESS.md` - 开发进度跟踪
13. `TROUBLESHOOTING.md` - 故障排除
14. `TEST_PLAN.md` - 测试计划
15. `REFACTORING_ANALYSIS.md` - 重构分析

### 用户文档（1 个）
16. `QUICK_START.md` - 快速入门指南

### 专项方案文档（4 个）
17. `VERSION_MANAGEMENT.md` - 版本管理方案
18. `WEBSOCKET_MIGRATION.md` - WebSocket 迁移方案
19. `WEBUI_CLEANUP_PROPOSAL.md` - WebUI 清理方案
20. `PHASE15_WEBSOCKET_CPU.md` - Phase 15 WebSocket CPU 优化

---

## 🎯 整合方案

### 方案 A：合并重复/过时内容（推荐）

#### 1. 合并命令相关文档 → `COMMAND_REFERENCE.md`
**合并文件**：
- `COMMANDS.md` - CLI 命令列表
- `COMMAND_SPECIFICATION.md` - 命令规范

**理由**：内容高度重叠，命令规范应该包含命令列表

**新结构**：
```markdown
# TianShanOS 命令参考手册

## 命令设计原则
- argtable3 参数风格
- 统一错误处理
- JSON 输出支持

## 系统命令
- system --info/--memory/--tasks/--reboot
- service --list/--status/--start/--stop

## 网络命令
...

## 命令实现规范
- 文件结构
- 注册流程
- 最佳实践
```

#### 2. 合并配置相关文档 → `CONFIGURATION_GUIDE.md`
**合并文件**：
- `BOARD_CONFIGURATION.md` - 板级配置
- `GPIO_MAPPING.md` - GPIO 引脚映射
- `CONFIG_SYSTEM_DESIGN.md` 部分内容

**理由**：都是配置相关，便于用户查找

**新结构**：
```markdown
# TianShanOS 配置指南

## 板级配置
- boards/*/board.json
- boards/*/services.json

## GPIO 引脚配置
- pins.json 格式
- 引脚映射表

## 运行时配置
- NVS 键值对
- config 命令使用
```

#### 3. 合并专项方案到 DEVELOPMENT_PROGRESS.md
**合并文件**：
- `WEBSOCKET_MIGRATION.md` - WebSocket 迁移（已完成）
- `WEBUI_CLEANUP_PROPOSAL.md` - WebUI 清理（已完成）
- `PHASE15_WEBSOCKET_CPU.md` - CPU 优化（已完成）
- `VERSION_MANAGEMENT.md` - 版本管理（可作为附录）

**理由**：专项方案已实施完成，应该作为开发历史记录

**处理方式**：
- 将关键内容整合到 `DEVELOPMENT_PROGRESS.md` 的对应 Phase
- 删除独立文件，避免重复维护

#### 4. 精简架构文档
**保留**：
- `ARCHITECTURE_DESIGN.md` - 核心架构（主文档）
- `LED_ARCHITECTURE.md` - LED 子系统（特殊复杂度高）
- `SECURITY_IMPLEMENTATION.md` - 安全实现（独立性强）

**整合到主文档**：
- `API_DESIGN.md` → 作为 `ARCHITECTURE_DESIGN.md` 的一个章节

#### 5. 删除过时文档
**候选删除**：
- `REFACTORING_ANALYSIS.md` - 重构分析（一次性文档，已完成）

---

## 📁 整合后的文档结构（13 个文档）

### 📘 用户文档（3 个）
1. **QUICK_START.md** - 快速入门指南
2. **COMMAND_REFERENCE.md** - 命令参考手册（新）
3. **CONFIGURATION_GUIDE.md** - 配置指南（新）

### 📗 开发文档（4 个）
4. **ARCHITECTURE_DESIGN.md** - 架构设计（含 API 设计）
5. **DEVELOPMENT_PROGRESS.md** - 开发进度（含专项方案历史）
6. **TROUBLESHOOTING.md** - 故障排除
7. **TEST_PLAN.md** - 测试计划

### 📕 API 参考（2 个）
8. **API_REFERENCE.md** - API 参考手册
9. **LED_ARCHITECTURE.md** - LED 系统架构

### 📙 项目文档（4 个）
10. **PROJECT_DESCRIPTION.md** - 项目概述
11. **SECURITY_IMPLEMENTATION.md** - 安全实现
12. **BOARD_CONFIGURATION.md** - 板级配置（保留作为独立参考）
13. **GPIO_MAPPING.md** - GPIO 映射（保留作为快速参考）

---

## 🗑️ 待删除/归档文档

### 删除（一次性/已过时）
- ❌ `REFACTORING_ANALYSIS.md` - 重构分析（2026-01-15 已完成）
- ❌ `WEBSOCKET_MIGRATION.md` - WebSocket 迁移（Phase 10 已完成）
- ❌ `PHASE15_WEBSOCKET_CPU.md` - CPU 优化（Phase 15 已完成）

### 合并后删除
- ❌ `COMMANDS.md` → 合并到 `COMMAND_REFERENCE.md`
- ❌ `COMMAND_SPECIFICATION.md` → 合并到 `COMMAND_REFERENCE.md`
- ❌ `API_DESIGN.md` → 合并到 `ARCHITECTURE_DESIGN.md`
- ❌ `WEBUI_CLEANUP_PROPOSAL.md` → 合并到 `DEVELOPMENT_PROGRESS.md`
- ❌ `VERSION_MANAGEMENT.md` → 合并到 `DEVELOPMENT_PROGRESS.md` 附录

---

## 🔄 实施步骤

### 步骤 1：创建新文档
1. 创建 `COMMAND_REFERENCE.md`
   - 复制 `COMMAND_SPECIFICATION.md` 框架
   - 整合 `COMMANDS.md` 的命令列表
   - 添加实现规范

2. 创建 `CONFIGURATION_GUIDE.md`
   - 复制 `BOARD_CONFIGURATION.md` 内容
   - 整合 `GPIO_MAPPING.md` 内容
   - 添加运行时配置说明

### 步骤 2：更新现有文档
1. 更新 `ARCHITECTURE_DESIGN.md`
   - 添加 `API_DESIGN.md` 的 API 设计章节
   - 更新架构图

2. 更新 `DEVELOPMENT_PROGRESS.md`
   - 整合专项方案到对应 Phase
   - 添加版本管理附录

### 步骤 3：删除冗余文档
按照"待删除/归档文档"列表执行删除操作

### 步骤 4：更新文档索引
创建 `README.md`（docs 目录）列出所有文档及其用途

---

## ✅ 预期收益

| 指标 | 改进 |
|------|------|
| 文档数量 | 20 → 13（-35%）|
| 重复内容 | 显著减少 |
| 查找效率 | 提升（分类清晰）|
| 维护成本 | 降低 |

---

## 📋 待决策问题

1. **是否保留 `BOARD_CONFIGURATION.md` 和 `GPIO_MAPPING.md` 作为独立文档？**
   - 优点：快速参考，便于开发者查找
   - 缺点：与 `CONFIGURATION_GUIDE.md` 部分重复

2. **是否需要创建 docs/README.md 作为文档导航？**
   - 推荐：是，提供文档地图

3. **是否归档历史文档到 `docs/archive/` 目录？**
   - 推荐：是，保留历史但不污染主目录

---

## 下一步行动

请确认整合方案后，我将开始：
1. 创建新的整合文档
2. 更新现有文档
3. 删除/归档冗余文档
4. 创建 docs/README.md 导航
