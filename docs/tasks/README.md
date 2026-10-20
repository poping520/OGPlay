# Work Unit 索引

Work Unit 按所属里程碑或语义化专项存放，创建后不因完成状态移动。里程碑任务文件名使用里程碑内编号
`WU-M<里程碑>-<三位序号>.md`，例如 `WU-M8-001.md`；M0..M8 早期以四位全局编号创建的
`WU-0001..0379` 保持原名，不重编号也不迁移。任务之间使用文件名声明依赖，具体内容通过
本表定位。跨里程碑或具有独立语义的专项使用自身缩写编号，例如 DexVM 的 `DVM-N`
和持久沙盒的 `SBX-N`。

早期里程碑 M0..M4 已完成，验收记录见 [docs/state](../state/)（`M<N>-ACCEPTANCE.md`）。
各目录的进度与在办项见其 README 或 [CURRENT](../state/CURRENT.md)。新增目录或编号前缀时
同步更新下表的编号范围。

| 目录 | 编号 | 数量 | 说明 |
| --- | --- | --- | --- |
| [`apk-startup/`](apk-startup/) | APS-1..15 | 15 | [APK Startup WU 任务书归档](apk-startup/README.md) |
| [`boundary/`](boundary/) | BND-1..54 | 54 | — |
| [`dexvm/`](dexvm/) | DVM-1..251 | 245 | [DexVM · 有界 DEX 解释执行](dexvm/README.md) |
| [`diagnostics/`](diagnostics/) | DIAG-1..3 | 3 | — |
| [`gui/`](gui/) | DASH-01..05、GUI-1..13 | 18 | [GUI · WebView 启动器](gui/README.md) |
| [`input/`](input/) | INP-1..8 | 8 | [Input · 通用 PC 操控映射](input/README.md) |
| [`launcher/`](launcher/) | GUI-1..18 | 18 | [Launcher · 主面板（游戏启动器）](launcher/README.md) |
| [`layoutui/`](layoutui/) | LUI-1..17 | 17 | [Layout UI · 有界 Android XML UI](layoutui/README.md) |
| [`m0/`](m0/) | WU-0001..0014 | 14 | — |
| [`m1/`](m1/) | WU-0015..0040 | 26 | — |
| [`m2/`](m2/) | WU-0041..0103 | 63 | — |
| [`m3/`](m3/) | WU-0104..0149 | 46 | [M3 Work Unit](m3/README.md) |
| [`m4/`](m4/) | WU-0150..0198 | 49 | — |
| [`m5/`](m5/) | WU-0199..0327 | 129 | [M5 Work Unit 拆分索引](m5/README.md) |
| [`m6/`](m6/) | WU-0328..0359 | 32 | [M6 · AI 自动化测试](m6/README.md) |
| [`m8/`](m8/) | WU-0360..0379、WU-M8-001..011 | 31 | [M8 · 兼容性冲刺](m8/README.md) |
| [`maintenance/`](maintenance/) | UTIL-1..3 | 3 | [基础工具维护工作单](maintenance/README.md) |
| [`optimization/`](optimization/) | WU-GLCTX-01..05、WU-HLE-01..04、WU-PERF-01..15 | 24 | — |
| [`sandbox/`](sandbox/) | SBX-1..15 | 15 | [沙盒 · 每游戏持久沙盒（ADR-0020）](sandbox/README.md) |
| [`vfs/`](vfs/) | VFS-04..10 | 7 | — |
| [`video/`](video/) | VID-1 | 1 | — |

## 规则

- 一个 Work Unit 在一次会话内形成一句话目标、显式依赖和机器可判定验收。
- 新任务创建在所属目录，按上表中该目录前缀的最大编号递增，例如 DexVM 使用
  `docs/tasks/dexvm/DVM-N.md`、里程碑使用 `docs/tasks/m8/WU-M8-NNN.md`。
- `CURRENT.md` 只保留正在进行和最近完成的少量 WU；完整历史保留在本目录、里程碑
  验收文档和 Git 中。
- 查找任务可使用 `rg --files docs/tasks | rg 'WU-0105.md'`，不要假定 WU 位于根目录。
