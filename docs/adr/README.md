# 架构决策记录（ADR）

只保留现行有效的范围边界、跨模块架构或全局不变量、外部依赖或交付形态、准入政策。
单类/单项功能实现、已替代决定与未实施提案不在此维护；有效模块约束归对应 MODULE.md。

精简前的全部 ADR（0001–0162）见提交 e5a210e1 的 docs/adr/。

## 主题索引

| 主题 | 条数 | 编号 |
| --- | ---: | --- |
| [构建、依赖与开发流程](development.md) | 4 | [0005](development.md#adr-0005)、[0007](development.md#adr-0007)、[0072](development.md#adr-0072)、[0138](development.md#adr-0138) |
| [DexVM、BootDex 与 Java 平台边界](dexvm.md) | 9 | [0017](dexvm.md#adr-0017)、[0029](dexvm.md#adr-0029)、[0066](dexvm.md#adr-0066)、[0068](dexvm.md#adr-0068)、[0147](dexvm.md#adr-0147)、[0148](dexvm.md#adr-0148)、[0152](dexvm.md#adr-0152)、[0157](dexvm.md#adr-0157)、[0158](dexvm.md#adr-0158) |
| [可观测性、线程握手与退出](diagnostics.md) | 3 | [0006](diagnostics.md#adr-0006)、[0025](diagnostics.md#adr-0025)、[0026](diagnostics.md#adr-0026) |
| [图形、音频与视频](media.md) | 5 | [0003](media.md#adr-0003)、[0063](media.md#adr-0063)、[0070](media.md#adr-0070)、[0136](media.md#adr-0136)、[0140](media.md#adr-0140) |
| [运行时基础与模块边界](runtime.md) | 7 | [0001](runtime.md#adr-0001)、[0002](runtime.md#adr-0002)、[0004](runtime.md#adr-0004)、[0010](runtime.md#adr-0010)、[0011](runtime.md#adr-0011)、[0013](runtime.md#adr-0013)、[0079](runtime.md#adr-0079) |
| [会话、Profile 与持久沙盒](session.md) | 6 | [0020](session.md#adr-0020)、[0133](session.md#adr-0133)、[0142](session.md#adr-0142)、[0163](session.md#adr-0163)、[0164](session.md#adr-0164)、[0165](session.md#adr-0165) |

合计 34 条。编号不复用，下一个编号为 ADR-0166。

## 维护方式

- 只收范围边界、跨模块架构或全局不变量、外部依赖或交付形态、准入政策四类决定。
- 每条不超过 80 行，包含原编号、原日期、背景、决定、后果；不写实施补充或阶段记录。
- 决定变化时原地改写为现行状态，旧文靠 git 保存，不写 Supersedes 链。
- 新编号全局递增且不复用；下一个编号为 ADR-0166。新条目归入现有主题，必要时才新增主题。
- 每条使用 `<a id="adr-NNNN"></a>` 与 `## ADR-NNNN · 标题`；跨文档链接使用 `主题.md#adr-NNNN`。
- 同次改动同步 README 索引条数/编号及主题文件头部目录，使用编号前检查冲突。
