# OGPlay 文档索引

## 长期上下文

- 规划：[roadmap/README.md](roadmap/README.md)（当前规范的唯一来源）
- 架构决策：[adr/](adr/)
- 长期方案设计：[design/dexvm/README.md](design/dexvm/README.md)（有界 DEX 解释器，ADR-0017，未启动）、
  [design/sandbox/README.md](design/sandbox/README.md)（每游戏持久沙盒，ADR-0020，未启动）、
  [design/launcher/README.md](design/launcher/README.md)（GUI 主面板基础版，未启动）
- 模块契约：[modules/INDEX.md](modules/INDEX.md)
- 当前交接：[state/CURRENT.md](state/CURRENT.md)
- 长期已知问题：[state/KNOWN-ISSUES.md](state/KNOWN-ISSUES.md)
- 里程碑验收：[M0](state/M0-ACCEPTANCE.md)、[M1](state/M1-ACCEPTANCE.md)、
  [M2](state/M2-ACCEPTANCE.md)、[M3](state/M3-ACCEPTANCE.md)、[M4](state/M4-ACCEPTANCE.md)
- 工作单索引：[tasks/README.md](tasks/README.md)（任务按 [M0](tasks/m0/)、
  [M1](tasks/m1/)、[M2](tasks/m2/)、[M3](tasks/m3/)、[M4](tasks/m4/)、
  [M5](tasks/m5/)、[M6](tasks/m6/) 分区）

## 开发方式手册

反复要做的事的操作手册，总览见 [playbook/README.md](playbook/README.md)：

- AI 适配游戏：[playbook/NEW-TITLE.md](playbook/NEW-TITLE.md)（dexvm 路线，含缺口
  survey 流程与失败判读表）
- AI 测试 · MCP 工具：[playbook/MCP-TESTING.md](playbook/MCP-TESTING.md)（截图、触摸、
  帧步进与会话状态）
- AI 测试 · 场景 runner：[playbook/SCENARIO-RUNNER.md](playbook/SCENARIO-RUNNER.md)
  （`tools/run_scenario.py`，可复跑用例与证据判读）
- 排查手册：[playbook/TROUBLESHOOTING.md](playbook/TROUBLESHOOTING.md)（按症状索引）

`demo/` 是本地 DEMO 基线，只用于提取已经验证的知识；正式版不从中继承游戏特判、
协作式线程、手写 GLES 转译或静默桩。
