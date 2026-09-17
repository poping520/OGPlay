# 开发方式手册

本目录提供操作命令、产物说明和故障判读，按当前场景选择一篇即可。
工作流、验证强度及文档更新统一见 [AGENTS.md](../../AGENTS.md)；
需要讨论能力范围或实现路线时，按需参考 [开发设计参考](DEV-REFERENCE.md)。

| 场景 | 读这篇 |
| --- | --- |
| 让一款新游戏跑上 dexvm 路线 | [NEW-TITLE.md](NEW-TITLE.md) |
| 用 MCP 工具驱动会话：截图、触摸、步进、读状态 | [MCP-TESTING.md](MCP-TESTING.md) |
| 把一次试玩固化成可复跑的场景 | [SCENARIO-RUNNER.md](SCENARIO-RUNNER.md) |
| 遇到黑屏、死锁、guest fault 等症状 | [TROUBLESHOOTING.md](TROUBLESHOOTING.md) |

## 工具选择

`tools/run_scenario.py` 使用同一套 MCP 工具，将操作和断言固化为可复跑场景。

| 目的 | 工具 |
| --- | --- |
| 探索未知操作、定位故障或复现单点问题 | 直接使用 MCP，命令见对应手册 |
| 逐步编写场景 | runner 的 `--watch` 模式，保活并增量执行 checkpoint |
| 重放固定操作及断言 | runner 普通模式，使用 `.scenario.toml` 与匹配的 Title Profile |

探索过程无需为使用 runner 提前创建 Profile。缺口报告、证据目录及反编译产物放在
`.local/` 或临时目录，不入库；工具参数和产物格式见各操作手册。
