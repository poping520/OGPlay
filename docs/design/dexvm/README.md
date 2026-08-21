# DexVM · 有界 DEX 解释器方案设计

本目录是"有界 Dalvik 字节码解释器 + HLE 边界下移到平台类"方案的完整设计，
写给两类读者：**决策者**（判断投入与时机）和 **实施阶段的 AI**（作为该子系统
开发的长期上下文根节点）。架构方向已由 [ADR-0017](../../adr/0017-bounded-dex-interpreter.md)
冻结；本目录负责把该决定展开为可分片执行的工程方案。

## 效力声明

- 本方案与项目现有设计冲突时，**以本方案为准**；全部已识别冲突及裁决见
  [06 · 迁移与冲突裁决](06-migration.md)。未在裁决表中的新冲突，实施时按
  "本方案优先"补记入裁决表。
- 本方案**不改变**项目的元规范：AGENTS.md 的工作流约束（WU 有界、机器可判定
  测试、能力记账、明确失败、结构化日志、800 行上限）对 dexvm 全部适用。
- 实施启动前，本目录是唯一权威；实施启动后，代码与 `MODULE.md` 契约逐步接管，
  本目录转为设计溯源。

## 一句话架构

> 解释执行游戏自带 `classes.dex` 里的类；`android.*` / `java.*` 由宿主 C++ 以
> 内建类（intrinsic）实现；游戏类与平台类共享同一个 session 级 Java 对象模型；
> 没有 JIT，没有 framework 字节码，没有动态类加载。

由此，游戏 Java 胶水层的行为不再需要人工逆向成 profile 声明——它被真实执行；
Title Profile 退化为 identity、数据布局、预算与 quirk。

实现不从零臆造：以 `.local/aosp/dalvik` 中固定 tag 的 AOSP Dalvik（KitKat，与
API 19 目标同代）为本地参考基线——目录类数据机器比对、指令语义逐 opcode 对照、结构校验
规则对标，但不移植其对象模型/GC/线程实现体（见 [07](07-aosp-reference.md)）。
这与项目既有姿态一致：CPU 用 dynarmic、GLES 用 ANGLE、PVRTC 用 PowerVR SDK，
从不凭记忆重写已有权威实现的语义。

## 阅读顺序

| 篇 | 内容 |
| --- | --- |
| [01 · 目标、非目标与有界性](01-scope.md) | 解决什么、明确不做什么、规模量级 |
| [02 · 核心架构](02-architecture.md) | 模块划分、类链接、对象模型、解释器内核、异常 |
| [03 · 平台内建类](03-platform-intrinsics.md) | intrinsic 边界、最小集、记账与失败语义 |
| [04 · 运行时集成](04-integration.md) | JNI 双向、生命周期反转、线程/monitor、GC、预算 |
| [05 · 验证体系](05-verification.md) | dexasm 夹具、一致性测试、exact-title gate、能力条目 |
| [06 · 迁移与冲突裁决](06-migration.md) | 阶段划分、WU 分解、冲突裁决表、风险 |
| [07 · AOSP 参考策略](07-aosp-reference.md) | 本地参考资料、机器校验/语义参考/测试素材三模式、逐组件对照表、红线 |
| [08 · 解释执行的 EGL/GL façade](08-egl-facade.md) | 自带 GLSurfaceView 的通用形态：javax EGL 面驱动宿主 ANGLE surface |
| [09 · GC-B 精确标记清除](09-gc.md) | 04 §5 B 期的实施方案：现状盘点、执行锁即停世界、根集/标记/清扫、WU 分批 |
| [10 · 解释器 v2 threaded 内核](10-interpreter-threaded.md) | 预解码 + 线程化分派的第二内核：与旧 switch 内核并存可切换，bridge 渐进迁移，tick 逐位等价 |

## 启动条件与当前状态

- 状态：设计定稿并经一次实证复盘（2026-08-11，WU-M8-011），**未启动实施**。
  M8（Asphalt 6 兼容冲刺）继续按现行 profile 路线推进，两者不互相阻塞。
- 实证依据：Dungeon Hunter 点一次"继续"命中 1 个缺失 Java 胶水 handler；对账
  显示该 title 的 profile 还引用 13 个无 handler 的 impl id，其中 license/billing/
  online 组的返回值改变游戏行为，人工实现必须逐个反编译取证。每款 title 的
  胶水成本重复发生且不摊销——这正是本方案要消除的结构性缺陷。
- **最早实战出口是方法级接管**（[04 §1](04-integration.md)）：第三路由在 v1
  profile 生命周期下即可逐方法替换 `[[java.class]]` 胶水映射，不必等完整
  生命周期反转。阶段 2 出口即产生真实 title 价值（[06 §1](06-migration.md)）。
- 建议启动窗口：阶段 0（题库测量工具、opcode 目录、dexasm 汇编器）不触碰
  运行时代码，可在 M8 间隙立即执行；阶段 1 不晚于 Asphalt 6 主界面 gate 闭合
  后启动。启动时按 ADR-0012 建立专属里程碑目录与 `WU-M<编号>-NNN` 任务单。
- 过渡期纪律：v1 `[[java.class]]` 胶水目录冻结增长——只补当前 gate 实际阻塞
  且语义无歧义的调用族；行为敏感组保持明确失败并登记为方法级接管候选
  （[06 §2](06-migration.md) 裁决 14）。

## 术语

| 术语 | 含义                                                                               |
| --- |----------------------------------------------------------------------------------|
| dexvm | 本方案的运行时子模块名（`src/runtime/dexvm/`），有界 DEX 解释器                                     |
| intrinsic / 内建类 | 由宿主 C++ 实现的 `android.*`/`java.*` 平台类，注册进统一类目录                                    |
| VM 对象 | 由解释器按 DEX 类布局分配、字段可直接读写的 Java 对象                                                 |
| 宿主背衬对象 | intrinsic 类实例，字段语义由宿主状态承载（如 AudioTrack）                                          |
| JavaObjectModel | session 级统一对象模型，VM 对象与宿主背衬对象共用引用身份                                               |
| 生命周期反转 | 由解释执行的真实 `onCreate`/`onDrawFrame` 驱动游戏，取代 profile `native_call` 序列               |
| L1 / L2 | roadmap 04 §7 的 DEX 分级：L1 只读解析（已完成），L2 解释执行（本方案）                                 |
| AOSP 基线 | `.local/aosp/dalvik` 中对齐 `android-4.4.4_r2` 的 `platform/dalvik` 源码树，仅作参考，不编译进运行时 |
