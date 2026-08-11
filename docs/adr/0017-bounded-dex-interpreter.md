# ADR-0017 · 有界 DEX 解释执行与平台内建类边界

- 状态：Accepted（架构方向即日生效；实施启动时间由 roadmap 排期另行决定）
- 日期：2026-08-11
- 关联：[docs/design/dexvm/](../design/dexvm/README.md)（方案设计全文）、
  [roadmap 04 · §7](../roadmap/04-android-runtime.md)（DEX L0–L2 分级框架）
- Supersedes：roadmap 04 §7.4 中"先测量题库、再决定是否投入 L2"的 go/no-go 判定。
  是否投入已决定为"做"；题库测量继续执行，但只用于决定排期、批次顺序与
  平台内建类最小集，不再决定做与不做。

## 背景

profile 驱动的生命周期把每个游戏的 Java 胶水层人工翻译成 `native_call` 序列与
`[[java.class]]` implementation 映射。M8 的 Asphalt 6 推送初始化缺失分析证明该方式
存在系统性风险：Java `onCreate` 的任何一次 JNI 副作用漏抄，都会在很远的调用点以
"requires a valid class reference" 这类形式爆出，且只能靠反汇编逐个回溯；同时每款
游戏约 120–180 行人工逆向的 profile 让兼容成本随题量线性增长。

两个问题共同的根因是 HLE 边界画在"每个游戏自己的 Java 类"上，而这些类的行为
本来就静态存在于 APK 的 `classes.dex` 里，机器可读、机器可执行。

## 决定

- 实现有界 DEX 字节码解释器（模块名 `dexvm`），只解释应用自带 `classes.dex` 中的
  类；`android.*` / `java.*` / `javax.*` 一律作为宿主实现的内建类（intrinsic），
  不加载、不解释任何 framework/core 库字节码。
- HLE 边界从"游戏 Java 类"下移到"平台类 API 面"。游戏类的方法体由解释器真实
  执行，Title Profile 长期退化为 identity、数据布局、预算与 quirk。
- 有界性硬约束：无 JIT/AOT，无 odex/quickened 指令，无多 ClassLoader 层级与动态
  代码加载，无 JDWP/instrumentation；未实现的指令、内建类和内建方法必须记账并
  明确失败，禁止伪造成功。ADR-0001 对完整 ART/Dalvik 的禁令继续有效，本 ADR
  定义"有界解释器 + 平台内建类"位于该禁令之外。
- 对象模型统一：JNI 对象身份的"宿主对象或 VM 对象"双形态（roadmap 04 §7.4 的
  预留）落地为 session 级 JavaObjectModel，并吸收 `JniObjectArrayStore` 所有权
  统一的既有 backlog。
- 以固定 tag（`android-4.4.4_r2`，与 API 19 目标同代、Apache-2.0）vendor
  AOSP `platform/dalvik` 为参考基线：opcode 目录等数据与其机器比对，指令
  与运行时语义逐组件对照其实现；默认不编译不链接，不移植其对象模型/GC/
  线程/JNI 实现体——与 dynarmic/ANGLE/PowerVR 先例同一姿态（设计文档 07 章）。
- 与现有设计冲突时，以 [docs/design/dexvm/06-migration.md](../design/dexvm/06-migration.md)
  的冲突裁决表为准。

## 后果

- Asphalt 6 类"生命周期副作用漏抄"问题在 dexvm 生命周期下结构性消失；每款游戏
  的 Java 胶水翻译成本变为平台内建类的一次性摊销成本。
- 项目新增一个大型长期子系统（解释器、类链接、对象模型、GC、内建类库），必须
  按设计文档的阶段与机器可判定出口推进，禁止一次性大爆炸实现；语义实现
  必须记录 AOSP 出处，禁止凭记忆臆造 Dalvik 行为。
- 过渡期内 profile 驱动与 dexvm 驱动两种生命周期共存，按 title 逐个迁移；能力
  账本只增加新条目，既有条目状态不后退。
- libGDX / AndEngine / 纯 Java 休闲游戏等"Java 厚层"题目从结构上不可支持变为
  可支持，题库上限显著扩大。
