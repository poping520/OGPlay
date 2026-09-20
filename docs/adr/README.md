# 架构决策记录

ADR 按主题归并，编号全局唯一。历史决策正文只追加、不改写；推翻旧决定时，在对应主题
末尾追加新编号记录，并以 `Supersedes` 指向被替代条款。状态使用 Proposed、Accepted、
Deprecated、Superseded；日期使用 `YYYY-MM-DD`。当前能力与实施进度以
[CURRENT](../state/CURRENT.md)、[能力账本](../../capabilities.toml) 和模块契约为准。

## 主题索引

| 主题 | 内容 | ADR 编号（精确定位） |
| --- | --- | --- |
| [运行时基础与模块边界](runtime.md) | 9 条决策 | [0001](runtime.md#adr-0001)、[0002](runtime.md#adr-0002)、[0004](runtime.md#adr-0004)、[0009](runtime.md#adr-0009)、[0010](runtime.md#adr-0010)、[0011](runtime.md#adr-0011)、[0013](runtime.md#adr-0013)、[0016](runtime.md#adr-0016)、[0018](runtime.md#adr-0018) |
| [构建、依赖与开发流程](development.md) | 6 条决策 | [0005](development.md#adr-0005)、[0007](development.md#adr-0007)、[0008](development.md#adr-0008)、[0012](development.md#adr-0012)、[0014](development.md#adr-0014)、[0015](development.md#adr-0015) |
| [图形、音频与视频](media.md) | 9 条决策 | [0003](media.md#adr-0003)、[0019](media.md#adr-0019)、[0021](media.md#adr-0021)、[0027](media.md#adr-0027)、[0061](media.md#adr-0061)、[0062](media.md#adr-0062)、[0063](media.md#adr-0063)、[0069](media.md#adr-0069)、[0070](media.md#adr-0070) |
| [会话、Profile 与持久沙盒](session.md) | 2 条决策 | [0020](session.md#adr-0020)、[0022](session.md#adr-0022) |
| [可观测性、线程握手与退出](diagnostics.md) | 5 条决策 | [0006](diagnostics.md#adr-0006)、[0023](diagnostics.md#adr-0023)、[0024](diagnostics.md#adr-0024)、[0025](diagnostics.md#adr-0025)、[0026](diagnostics.md#adr-0026) |
| [DexVM、BootDex 与 Java 平台边界](dexvm.md) | 39 条决策 | [0017](dexvm.md#adr-0017)、[0028](dexvm.md#adr-0028)、[0029](dexvm.md#adr-0029)、[0030](dexvm.md#adr-0030)、[0031](dexvm.md#adr-0031)、[0032](dexvm.md#adr-0032)、[0033](dexvm.md#adr-0033)、[0034](dexvm.md#adr-0034)、[0035](dexvm.md#adr-0035)、[0036](dexvm.md#adr-0036)、[0037](dexvm.md#adr-0037)、[0038](dexvm.md#adr-0038)、[0039](dexvm.md#adr-0039)、[0040](dexvm.md#adr-0040)、[0041](dexvm.md#adr-0041)、[0042](dexvm.md#adr-0042)、[0043](dexvm.md#adr-0043)、[0044](dexvm.md#adr-0044)、[0045](dexvm.md#adr-0045)、[0046](dexvm.md#adr-0046)、[0047](dexvm.md#adr-0047)、[0048](dexvm.md#adr-0048)、[0049](dexvm.md#adr-0049)、[0050](dexvm.md#adr-0050)、[0051](dexvm.md#adr-0051)、[0052](dexvm.md#adr-0052)、[0053](dexvm.md#adr-0053)、[0054](dexvm.md#adr-0054)、[0055](dexvm.md#adr-0055)、[0056](dexvm.md#adr-0056)、[0057](dexvm.md#adr-0057)、[0058](dexvm.md#adr-0058)、[0059](dexvm.md#adr-0059)、[0060](dexvm.md#adr-0060)、[0064](dexvm.md#adr-0064)、[0065](dexvm.md#adr-0065)、[0066](dexvm.md#adr-0066)、[0067](dexvm.md#adr-0067)、[0068](dexvm.md#adr-0068) |

## 维护方式

最新补充：[ADR-0070 · 有界音乐增量解码与音源分流增益](media.md#adr-0070)（Accepted）。

- 查找历史编号使用上表，跨文档引用使用 `主题.md#adr-NNNN`；编号不随归并重排。
- 新决策使用下一个全局编号（当前最大为 ADR-0070），在既有主题末尾追加；只有出现无法
  归入现有主题的独立领域时才新增主题文件。每条记录前保留 `<a id="adr-NNNN"></a>`。
- 条目标题使用二级标题，背景、决定、后果等使用三级标题；同步更新本索引和主题内目录。
- 已有决策的实施补充保留原日期；有实质决策变化时追加新编号与精确替代关系，不能覆盖旧文。

## 整合记录（2026-09-06）

按用户要求，将原 32 个独立 ADR 文件归并为以上 6 个主题文件，删除旧文件。原编号、标题、
日期、状态、正文和替代关系保留，只调整标题层级和链接目标；仓库引用同步迁移到章节锚点。
该调整改变文档组织方式，不新增或撤销架构决定。
