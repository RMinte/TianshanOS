# i18n 收尾检查报告

> 根据 `/Users/massif/Desktop/plan.md` 对项目逐项核查，所有剩余项已于 2025-02-12 完成 i18n 接入。

## 已完成的修改概览

| 类别 | 修改内容 |
|------|----------|
| 语言包 | 新增 common.loadFailedMsg、refreshOnce、uploadFailedMsg；toast.terminate*；terminal.errorLabel、unknownError；sshPage.getVarFailed、failMatchedTrue/False |
| app.js | terminateProcess msgMap、数据组件工具栏/状态、错误文案、变量分组、图标预览、取消/密钥选择、配置包验证、匹配结果等 |
| terminal.js | 错误输出文案 |
| index.html | 首屏 Loading 增加 data-i18n |

## 参考

- 计划文档：`/Users/massif/Desktop/plan.md`
- 语言包：`components/ts_webui/web/js/lang/zh-CN.js`、`en-US.js`
