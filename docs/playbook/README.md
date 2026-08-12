# 开发方式手册

“怎么在这个项目里干活”的操作手册：每篇对应一件反复要做的事，给出精确命令、
产物含义与判读方式。规划与设计意图不在这里，见 [roadmap/](../roadmap/) 与
[design/](../design/)；范围红线与提交前流程以顶层 `AGENTS.md` 为准。

| 场景 | 读这篇 |
| --- | --- |
| 让一款新游戏跑上 dexvm 路线 | [NEW-TITLE.md](NEW-TITLE.md) |
| 用 MCP 工具驱动会话：截图、触摸、步进、读状态 | [MCP-TESTING.md](MCP-TESTING.md) |
| 把一次试玩固化成可复跑的场景 | [SCENARIO-RUNNER.md](SCENARIO-RUNNER.md) |
| 遇到黑屏、死锁、guest fault 等症状 | [TROUBLESHOOTING.md](TROUBLESHOOTING.md) |

## 两种测试方式怎么选

MCP 工具与 `tools/run_scenario.py` 不是二选一：runner 自己拉起
`run-apk --mcp-manual-step`，再调**完全相同**的那套 MCP 工具。区别在外层。

| | Agent 直接调 MCP | `run_scenario.py` |
| --- | --- | --- |
| 下一步做什么 | Agent 当场决定 | 冻结在 `.scenario.toml` |
| 跑哪款游戏 | 命令行随便指 | `[profile]` 必须精确命中一个 Title Profile |
| 超时判定 | 没有 | frame / guest tick / wall-time 三重预算 |
| 通过判定 | 看画面 | 闭集 assertion，产出 Result v1 |
| 留下什么 | 会话记录 | 证据目录，可换机复跑 |

按**要发现还是要结论**来选：

- **探索用 MCP**。新 title 还没有能过 gate 的 profile、不知道该点哪里、症状说不
  清——这时候写场景是浪费，因为断言写什么都还不知道。
- **结论必须用 scenario**。任何写进验收、gate、golden 或 `capabilities.toml` 的
  判断，都要是能三轮复跑的场景运行。手工点出来的“看着好了”不算结论。
- **中间用 `--watch`**：保活会话、追加 checkpoint 只跑增量，每步落一张坐标网格图
  供标定点击位置——用 MCP 的手感产出 scenario 的产物。定稿仍要用不带 `--watch`
  的完整运行取结论。

典型路径：MCP 摸出可行操作序列 → `--watch` 落成 checkpoint → 完整运行三轮 →
通过后把 profile 迁进 `data/profiles/`。

## 两条贯穿全部手册的纪律

- 诊断工具（survey 模式、trace、手工截图）只回答“游戏碰了什么”，不产出兼容性
  结论。gate、golden 与验收结论只能来自不带诊断开关的运行。
- APK 派生产物（缺口报告、证据目录、反汇编）一律留在 `.local/` 或临时目录，
  不入库。
