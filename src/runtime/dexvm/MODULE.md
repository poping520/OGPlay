# 模块：runtime/dexvm

## 职责

有界 Dalvik 字节码解释器（ADR-0017、`docs/design/dexvm/`）：类链接、统一
Java 对象模型、解释器内核（帧/分派/三路 invoke/异常展开/`<clinit>`）与
java.* 核心 intrinsic。只解释游戏自带 DEX 的应用类；平台类永远是 intrinsic。

## 公共 API

- `DexClassLinker`：`RegisterIntrinsics`（代码定义目录，平台命名空间只能来自
  这里）→ `RegisterDex`（单一 classes.dex，dex 中平台前缀类被忽略）→ `Link`
  （层级解析/字段布局/vtable/iftable，循环继承、final 覆盖、接口当 super、
  不可覆盖 intrinsic 方法均装配失败）。常量池解析带缓存
  （`ResolveTypeIndex/ResolveMethodIndex/ResolveFieldIndex`），数组类按需合成，
  `IsAssignable` 覆盖类层级、接口与数组协变。`PrecheckMethod` 懒执行结构
  预检（未定义 opcode、寄存器越界、分支/payload 目标、move-result 位置），
  规则子集对照 AOSP `CodeVerify.cpp`，不做全量数据流。
- `CoreIntrinsicCatalog()`：Object/String/Class/Throwable + 隐式异常层级 +
  Runnable/CharSequence 等接口的内建声明。
- `JavaObjectModel`：session 级统一对象身份（VmObjectRef 句柄空间，0=null）。
  VM 实例与对象数组自有存储；字符串与基元数组委托注入的
  `JniStringStore`/`JniPrimitiveArrayStore`——native 与解释器看到同一对象。
  GC-A 预算 arena：只分配不回收，默认 64 MiB，耗尽抛
  `heap_budget_exhausted`；`SetEmergencyReserve` 仅供解释器物化 OOM throwable。
- `Interpreter`：`Call(method, args)` 在当前宿主线程执行至完成，返回
  `VmCallOutcome`（值或未捕获 Java 异常 + 消息 + 栈回溯）。tagged 寄存器
  （uninit/cat1/wide 对/ref + 零值放宽）、每指令 1 tick 预算、帧深度上限
  （默认 512 → 真实 StackOverflowError）。invoke 三路由：解释方法压帧、
  intrinsic 查 `IntrinsicRegistry`（缺失 handler 记账 + UnsatisfiedLinkError）、
  native 走 `NativeMethodBridge`（未注入则记账 + 明确失败）。
  `EnsureClassInitialized` 实现 `<clinit>` 状态机（同线程重入放行、失败粘滞
  NoClassDefFoundError、静态初始值先于 `<clinit>` 物化）。
- `RegisterCoreBuiltinHandlers`：core.object/string/class/throwable handler
  （String/StringBuilder 面在 `intrinsics_string.cpp`）。

- Gap survey（诊断，默认关闭）：`EnableGapSurvey()` 后，未声明的**平台**类/
  方法被合成为中性桩（0/null/void）并逐次记账，一次运行即可收割新 title 的
  整条缺口队列；`GapSurveyHits()` + `RenderGapSurveyJson()`（`gap_survey.cpp`）
  输出按命中次数排序的机读工作单。survey 运行不是兼容性结论，调用方必须显式
  标注；关闭时行为不变（未声明即明确失败）。流程见
  `docs/playbook/NEW-TITLE.md`。

## 文件分工

`class_linker_internal.h` 持有 `DexClassLinker::Impl` 与共享 helper：注册/
布局/vtable 在 `class_linker.cpp`，常量池解析与可赋值性在
`class_linker_resolve.cpp`。

## 不变量

- 依赖只指向 core/loader/runtime‑jni；不依赖 runtime/framework（intrinsic 经
  `PlatformClassProvider` 形态由装配方注入 registry/目录）。
- guest 不可信：全部索引/偏移/tag 受检；未实现 opcode/intrinsic/native 记账
  且明确失败，绝不静默返回默认值。
- 语义出处：逐 opcode 对照 AOSP `vm/mterp/c/OP_*.cpp`（一致性夹具注释记录），
  分歧按 07 §5 仲裁。无 JIT、不改写指令流（quickening 红线）。
- 对象非移动，句柄生命周期内稳定。
- 时间与线程不进入本模块：单指令流在调用方宿主线程执行。

## 尚未实现（记账可查）

- monitor 指令目前是单线程重入计数（多线程 wait/notify 属阶段 4，
  `runtime.jni_guest_monitors` 接线随集成 WU）。
- `Method.invoke` 等反射面、finalizer、GC-B。

## 测试

`tests/dexvm/interpreter_tests.cpp`（dexasm 夹具一致性：算术边界、控制流、
数组、字段、三种 dispatch、clinit、跨帧异常、栈溢出、tick/heap 预算）；
`tests/dexvm/dex_code_tests.cpp`、`tests/dexvm/dexasm_readback_tests.cpp`、
`tests/dexvm/gap_survey_tests.cpp`（survey 开/关对照：关闭即失败、桩答中性值、
命中计数、工作单排序）。
