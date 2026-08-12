# 开发方式手册

“怎么在这个项目里干活”的操作手册：每篇对应一件反复要做的事，给出精确命令、
产物含义与判读方式。规划与设计意图不在这里，见 [roadmap/](../roadmap/) 与
[design/](../design/)；范围红线与提交前流程以顶层 `AGENTS.md` 为准。

| 场景 | 读这篇 |
| --- | --- |
| 让一款新游戏跑上 dexvm 路线 | [NEW-TITLE.md](NEW-TITLE.md) |
| 手工看画面、点一下试试 | [MCP-TESTING.md](MCP-TESTING.md) |
| 把一次手工试玩固化成可复跑的场景 | [SCENARIO-RUNNER.md](SCENARIO-RUNNER.md) |
| 遇到黑屏、死锁、guest fault 等症状 | [TROUBLESHOOTING.md](TROUBLESHOOTING.md) |

两条贯穿全部手册的纪律：

- 诊断工具（survey 模式、trace、手工截图）只回答“游戏碰了什么”，不产出兼容性
  结论。gate、golden 与验收结论只能来自不带诊断开关的运行。
- APK 派生产物（缺口报告、证据目录、反汇编）一律留在 `.local/` 或临时目录，
  不入库。
