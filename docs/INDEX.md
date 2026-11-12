# OGPlay 文档入口

开发先读 [AGENTS.md](../AGENTS.md)、[当前状态](state/CURRENT.md)、
[已知问题](state/KNOWN-ISSUES.md) 和相关模块契约；有在办任务时再读任务单。
设计文档面向人类开发者，只在需要子系统背景时阅读。

## 当前事实与规则

- [当前运行路径与游戏进度](state/CURRENT.md)
- [尚未关闭的缺口与限制](state/KNOWN-ISSUES.md)
- [模块契约索引](modules/INDEX.md)
- [能力账本](../capabilities.toml)：通过 `python3 tools/capabilities.py show|find|list` 查询
- [现行架构决定](adr/README.md)
- [操作手册](playbook/README.md)
- [在办任务规则与模板](tasks/README.md)

## 模块设计

- [总体架构与端到端流程](design/README.md)
- [guest 执行环境](design/guest-execution.md)
- [native 边界与 JNI 互通](design/native-boundary.md)
- [DexVM 与 ROM Java](design/dexvm.md)
- [Android 平台层](design/android-framework.md)
- [图形、EGL 与帧生产](design/graphics.md)
- [音频与视频](design/audio-video.md)
- [存储、数据库与安全](design/storage-security.md)
- [输入映射](design/input.md)
- [前端、游戏库与会话](design/frontend.md)
- [可观测性与测试](design/diagnostics-testing.md)

## 用户与工具说明

- [项目说明](../README.zh-CN.md)、[兼容性边界](COMPATIBILITY.md)
- [第三方声明](../THIRD_PARTY_NOTICES.md)
- [工具入口](../tools/README.md)、[场景说明](../tests/scenarios/README.md)

本次清理前的完整文档见清理前提交 `0accb6022`。
