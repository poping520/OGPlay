# 架构决策记录

ADR 按主题归并，编号全局唯一。历史决策正文只追加、不改写；推翻旧决定时，在对应主题
末尾追加新编号记录，并以 `Supersedes` 指向被替代条款。状态使用 Proposed、Accepted、
Deprecated、Superseded；日期使用 `YYYY-MM-DD`。当前能力与实施进度以
[CURRENT](../state/CURRENT.md)、[能力账本](../../capabilities.toml) 和模块契约为准。

## 主题索引

| 主题 | 条数 | 编号 |
| --- | --- | --- |
| [构建、依赖与开发流程](development.md) | 8 | [0005](development.md#adr-0005)、[0007](development.md#adr-0007)、[0008](development.md#adr-0008)、[0012](development.md#adr-0012)、[0014](development.md#adr-0014)、[0015](development.md#adr-0015)、[0072](development.md#adr-0072)、[0073](development.md#adr-0073) |
| [DexVM、BootDex 与 Java 平台边界](dexvm.md) | 42 | [0017](dexvm.md#adr-0017)、[0028](dexvm.md#adr-0028)、[0029](dexvm.md#adr-0029)、[0030](dexvm.md#adr-0030)、[0031](dexvm.md#adr-0031)、[0032](dexvm.md#adr-0032)、[0033](dexvm.md#adr-0033)、[0034](dexvm.md#adr-0034)、[0035](dexvm.md#adr-0035)、[0036](dexvm.md#adr-0036)、[0037](dexvm.md#adr-0037)、[0038](dexvm.md#adr-0038)、[0039](dexvm.md#adr-0039)、[0040](dexvm.md#adr-0040)、[0041](dexvm.md#adr-0041)、[0042](dexvm.md#adr-0042)、[0043](dexvm.md#adr-0043)、[0044](dexvm.md#adr-0044)、[0045](dexvm.md#adr-0045)、[0046](dexvm.md#adr-0046)、[0047](dexvm.md#adr-0047)、[0048](dexvm.md#adr-0048)、[0049](dexvm.md#adr-0049)、[0050](dexvm.md#adr-0050)、[0051](dexvm.md#adr-0051)、[0052](dexvm.md#adr-0052)、[0053](dexvm.md#adr-0053)、[0054](dexvm.md#adr-0054)、[0055](dexvm.md#adr-0055)、[0056](dexvm.md#adr-0056)、[0057](dexvm.md#adr-0057)、[0058](dexvm.md#adr-0058)、[0059](dexvm.md#adr-0059)、[0060](dexvm.md#adr-0060)、[0064](dexvm.md#adr-0064)、[0065](dexvm.md#adr-0065)、[0066](dexvm.md#adr-0066)、[0067](dexvm.md#adr-0067)、[0068](dexvm.md#adr-0068)、[0075](dexvm.md#adr-0075)、[0077](dexvm.md#adr-0077)、[0089](dexvm.md#adr-0089) |
| [可观测性、线程握手与退出](diagnostics.md) | 8 | [0006](diagnostics.md#adr-0006)、[0023](diagnostics.md#adr-0023)、[0024](diagnostics.md#adr-0024)、[0025](diagnostics.md#adr-0025)、[0026](diagnostics.md#adr-0026)、[0095](diagnostics.md#adr-0095)、[0112](diagnostics.md#adr-0112)、[0113](diagnostics.md#adr-0113) |
| [图形、音频与视频](media.md) | 18 | [0003](media.md#adr-0003)、[0019](media.md#adr-0019)、[0021](media.md#adr-0021)、[0027](media.md#adr-0027)、[0061](media.md#adr-0061)、[0062](media.md#adr-0062)、[0063](media.md#adr-0063)、[0069](media.md#adr-0069)、[0070](media.md#adr-0070)、[0090](media.md#adr-0090)、[0091](media.md#adr-0091)、[0094](media.md#adr-0094)、[0099](media.md#adr-0099)、[0100](media.md#adr-0100)、[0106](media.md#adr-0106)、[0116](media.md#adr-0116)、[0128](media.md#adr-0128)、[0130](media.md#adr-0130) |
| [运行时基础与模块边界](runtime.md) | 51 | [0001](runtime.md#adr-0001)、[0002](runtime.md#adr-0002)、[0004](runtime.md#adr-0004)、[0009](runtime.md#adr-0009)、[0010](runtime.md#adr-0010)、[0011](runtime.md#adr-0011)、[0013](runtime.md#adr-0013)、[0016](runtime.md#adr-0016)、[0018](runtime.md#adr-0018)、[0076](runtime.md#adr-0076)、[0078](runtime.md#adr-0078)、[0079](runtime.md#adr-0079)、[0080](runtime.md#adr-0080)、[0081](runtime.md#adr-0081)、[0082](runtime.md#adr-0082)、[0083](runtime.md#adr-0083)、[0084](runtime.md#adr-0084)、[0085](runtime.md#adr-0085)、[0087](runtime.md#adr-0087)、[0088](runtime.md#adr-0088)、[0092](runtime.md#adr-0092)、[0093](runtime.md#adr-0093)、[0096](runtime.md#adr-0096)、[0097](runtime.md#adr-0097)、[0098](runtime.md#adr-0098)、[0101](runtime.md#adr-0101)、[0102](runtime.md#adr-0102)、[0103](runtime.md#adr-0103)、[0104](runtime.md#adr-0104)、[0105](runtime.md#adr-0105)、[0107](runtime.md#adr-0107)、[0108](runtime.md#adr-0108)、[0109](runtime.md#adr-0109)、[0110](runtime.md#adr-0110)、[0111](runtime.md#adr-0111)、[0114](runtime.md#adr-0114)、[0115](runtime.md#adr-0115)、[0117](runtime.md#adr-0117)、[0118](runtime.md#adr-0118)、[0119](runtime.md#adr-0119)、[0120](runtime.md#adr-0120)、[0121](runtime.md#adr-0121)、[0122](runtime.md#adr-0122)、[0123](runtime.md#adr-0123)、[0124](runtime.md#adr-0124)、[0125](runtime.md#adr-0125)、[0126](runtime.md#adr-0126)、[0127](runtime.md#adr-0127)、[0129](runtime.md#adr-0129)、[0131](runtime.md#adr-0131)、[0132](runtime.md#adr-0132) |
| [会话、Profile 与持久沙盒](session.md) | 7 | [0020](session.md#adr-0020)、[0022](session.md#adr-0022)、[0071](session.md#adr-0071)、[0074](session.md#adr-0074)、[0086](session.md#adr-0086)、[0133](session.md#adr-0133)、[0134](session.md#adr-0134) |

当前最大编号 ADR-0134，新决策使用 ADR-0135。

## 维护方式

- 新决策使用上方给出的下一个编号，追加在所属主题文件末尾：`<a id="adr-NNNN"></a>`、空行、
  `## ADR-NNNN · 标题`；背景、决定、后果等使用三级标题。只有无法归入现有主题时才新增
  主题文件。
- 同一提交内更新本索引（条数、编号与下一个编号）和主题文件头部目录。
- 跨文档引用使用 `主题.md#adr-NNNN`；编号不随归并重排。已有决策的实施补充保留原日期；
  有实质决策变化时追加新编号与精确替代关系，不能覆盖旧文。
- 并行会话可能取到同一编号：提交前用 `rg "^## ADR-NNNN" docs/adr` 确认未被占用，发现重复时由后提交者改用新编号。

## 整合与更正记录

- 2026-09-06：按用户要求，将原 32 个独立 ADR 文件归并为以上主题文件，删除旧文件。原编号、
  标题、日期、状态、正文和替代关系保留，只调整标题层级和链接目标；仓库引用同步迁移到
  章节锚点。该调整改变文档组织方式，不新增或撤销架构决定。
- 2026-10-13：修正 4 组重复编号，按提交时间保留先者，后者改号：runtime.md 的 ADR-0086
  改为 ADR-0129（VFS 随机设备），media.md 的 ADR-0110 改为 ADR-0130（Metal packed 表面），
  runtime.md 的 ADR-0116 改为 ADR-0131（APK Asset 身份），runtime.md 的 ADR-0128 改为
  ADR-0132（同包 task 复用）；同时补齐缺失锚点。仓库引用已同步，决策正文未改。
