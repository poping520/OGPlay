# Work Unit 索引

Work Unit 按所属里程碑存放，创建后不因完成状态移动。新任务文件名使用里程碑内编号
`WU-M<里程碑>-<三位序号>.md`，例如 `WU-M8-001.md`；M0..M8 早期以四位全局编号创建的
`WU-0001..0379` 保持原名，不重编号也不迁移。任务之间使用文件名声明依赖，具体内容通过
本表定位。

| 里程碑 | 编号范围 | 状态 | 目录 | 验收记录 |
| --- | --- | --- | --- | --- |
| M0 | WU-0001..0014 | 完成 | [`m0/`](m0/) | [`M0-ACCEPTANCE.md`](../state/M0-ACCEPTANCE.md) |
| M1 | WU-0015..0040 | 完成 | [`m1/`](m1/) | [`M1-ACCEPTANCE.md`](../state/M1-ACCEPTANCE.md) |
| M2 | WU-0041..0103 | 完成 | [`m2/`](m2/) | [`M2-ACCEPTANCE.md`](../state/M2-ACCEPTANCE.md) |
| M3 | WU-0104..0149 | 完成 | [`m3/`](m3/) | [`M3-ACCEPTANCE.md`](../state/M3-ACCEPTANCE.md) |
| M4 | WU-0150..0198 | 完成 | [`m4/`](m4/) | [`M4-ACCEPTANCE.md`](../state/M4-ACCEPTANCE.md) |
| M5 | WU-0199..0327 | 能力范围已封板，待里程碑验收 | [`m5/`](m5/) | 三批拆分见 [`m5/README.md`](m5/README.md) |
| M6 | 从 WU-0328 开始 | 下一阶段：AI 自动化测试 | [`m6/`](m6/) | 规划见 [`m6/README.md`](m6/README.md) |
| M8 | WU-0360..0379、WU-M8-001.. | 兼容性冲刺 | [`m8/`](m8/) | 批次索引见 [`m8/README.md`](m8/README.md) |
| M9 | WU-M9-001..019 | DexVM 有界解释执行；pilot gate 已过 | [`m9/`](m9/) | 索引见 [`m9/README.md`](m9/README.md) |

跨里程碑的专项方案按方案编号自成目录，同样创建后不移动、不重编号：

| 专项 | 编号 | 状态 | 目录 | 设计 |
| --- | --- | --- | --- | --- |
| 每游戏持久沙盒 | SBX-1..7 | 进行中 | [`sandbox/`](sandbox/) | [ADR-0020](../adr/0020-per-title-persistent-sandbox.md) |

## 规则

- 一个 Work Unit 在一次会话内形成一句话目标、显式依赖和机器可判定验收。
- 新任务直接创建在当前里程碑目录，例如 M8 的下一项任务使用 `docs/tasks/m8/WU-M8-008.md`。
- `CURRENT.md` 只保留正在进行和最近完成的少量 WU；完整历史保留在本目录、里程碑
  验收文档和 Git 中。
- 查找任务可使用 `rg --files docs/tasks | rg 'WU-0105.md'`，不要假定 WU 位于根目录。
