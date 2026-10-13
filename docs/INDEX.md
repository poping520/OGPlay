# OGPlay 文档索引

新会话先读 [AGENTS.md](../AGENTS.md) 与 [当前状态](state/CURRENT.md)。本页只列入口，不记录状态；
状态以 CURRENT、[能力账本](../capabilities.toml)（用 `tools/capabilities.py` 查询）和任务单为准。

## 状态与能力

- 当前状态：[state/CURRENT.md](state/CURRENT.md)
- 长期已知问题：[state/KNOWN-ISSUES.md](state/KNOWN-ISSUES.md)
- 里程碑验收：[M0](state/M0-ACCEPTANCE.md)、[M1](state/M1-ACCEPTANCE.md)、
  [M2](state/M2-ACCEPTANCE.md)、[M3](state/M3-ACCEPTANCE.md)、[M4](state/M4-ACCEPTANCE.md)
- 工作单：[tasks/README.md](tasks/README.md)

## 架构与契约

- 规划：[roadmap/README.md](roadmap/README.md)
- 架构决策：[adr/README.md](adr/README.md)
- 模块契约：[modules/INDEX.md](modules/INDEX.md)

## 专项设计

- [APK Startup · Manifest 驱动的应用进程启动](design/apk-startup/README.md)
- [DexVM · 有界 DEX 解释器方案设计](design/dexvm/README.md)
- [Diagnostics · 通用停滞诊断能力设计](design/diagnostics/README.md)
- [Input · 通用 PC 操控映射方案设计](design/input/README.md)
- [方案：Profile 驱动的启动作用域裁剪](design/entry-scope/README.md)
- [OGPlay 主界面 + 设置页 设计（GUI v2）](design/gui/README.md)
- [Launcher · 主面板（游戏启动器）方案设计](design/launcher/README.md)
- [Layout UI · 有界 Android XML UI 方案设计](design/layout-ui/README.md)
- [Sandbox · 每游戏持久沙盒方案设计](design/sandbox/README.md)
- [VFS · 资源读取、寿命与安装实例沙盒规划](design/vfs/README.md)

## 操作手册

适配新游戏、MCP 测试、场景 runner、Dashboard 与排查流程见
[playbook/README.md](playbook/README.md)。

`demo/` 是本地 DEMO 基线，只用于提取已经验证的知识；正式版不从中继承游戏特判、
协作式线程、手写 GLES 转译或静默桩。
