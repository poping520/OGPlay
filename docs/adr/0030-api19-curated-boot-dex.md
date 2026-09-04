# ADR-0030 · API 19 curated Boot DEX 与方法覆盖

- 状态：Accepted
- 日期：2026-09-04
- 关联：[DVM-99](../tasks/dexvm/DVM-99.md)
- Supersedes：[ADR-0017](0017-bounded-dex-interpreter.md) 中“平台类只有
  intrinsic、不执行 core/framework 字节码”的绝对边界。

## 背景

应用 DEX 已真实执行，但纯 Java 平台 API 仍逐类翻译为 C++ intrinsic，导致每个 title
持续暴露新的类库缺口。`EnumSet` 是 API 19 普通 Java 逻辑，继续手写会复制 libcore
语义且不能让缺口收敛。

## 决定

- 允许随运行时发布一个固定 API 19 `bootdex.jar`。它是从 pinned AOSP
  `android-4.4.4_r2.0.1` 提取的受审最小闭包，不是完整 `core.jar/framework.jar`。
- jar 中 `classes.dex` 的每个 class_def 必须全部注册为 bootstrap class；选择发生在制品
  构建期，运行时不维护类白名单，也不静默跳过 jar 内类。
- linker 依次注册 intrinsic、Boot DEX、application DEX。每个方法保留所属 `DexUnitId`，
  常量池和解析缓存按 unit 隔离。Boot DEX 与 intrinsic 同类时，以 DEX 提供类/字段/层级
  事实；签名精确匹配的 C++ method handler 作为 overlay，其余 DEX 方法解释执行。
- Boot DEX 中未被精确 overlay 的 native 方法拒绝装载。未进入制品的平台类仍按既有
  intrinsic/survey/明确失败规则处理。
- `EnumSet` 试点只增加通用 VM 原语 `Enum.getSharedConstants(Class)`：初始化 enum，按
  `ACC_ENUM` 静态字段读取活对象，以 ordinal 校验并排序，返回强根缓存的 typed array。
  `EnumSet/MiniEnumSet/HugeEnumSet` 语义直接执行 API 19 字节码。

## 后果

纯 Java libcore 能按依赖闭包逐批迁入，而 C++ 收敛到 VM 原语、宿主资源和 Android
service façade。该决定不授权加载完整 framework、Binder/system_server、Zygote、动态
classpath、多应用 namespace 或完整 ART/Dalvik；这些边界继续由 ADR-0001 约束。
