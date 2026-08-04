# 当前状态

更新：2026-08-04 · M3 JNI 基线

## 当前阶段

- M0、M1、M2 均已完成；M3 已开始，当前尚无进行中的 Work Unit。
- `WU-0125` 已完成 DEX member ID 与 class_def 解析；下一个开发任务编号为 `WU-0126`。
- 本机开发只使用 Windows/MSVC 预设；Linux/macOS 使用持久目录增量验证，并在里程碑
  出口执行三平台总体验收。

## 已验收基线

| 里程碑 | 状态 | 验收记录 | Work Unit |
| --- | --- | --- | --- |
| M0 工程地基 | 完成 | [M0-ACCEPTANCE.md](M0-ACCEPTANCE.md) | `docs/tasks/m0/` |
| M1 内核与跨平台 | 完成 | [M1-ACCEPTANCE.md](M1-ACCEPTANCE.md) | `docs/tasks/m1/` |
| M2 Bionic 与 Syscall | 完成 | [M2-ACCEPTANCE.md](M2-ACCEPTANCE.md) | `docs/tasks/m2/` |

能力的机器可读现状以仓库根目录 `capabilities.toml` 为准；本文件不重复维护完整能力历史。

## 最近完成

- [WU-0099] M2 出口闭合：Windows/MSVC、Linux/GCC、macOS/AppleClang 均在
  warnings-as-errors 下构建成功并通过 CTest 159/159。
- [WU-0100..0102] 修复三平台严格编译发现的聚合初始化与迭代器类型问题。
- [ADR-0011/WU-0103] Android guest 页固定为 4 KiB；Apple Silicon 16 KiB 宿主页上的
  相邻 guest 页仍可独立映射、保护和释放。
- [ADR-0012/WU-0104] 文档状态改为滚动快照；103 个既有 WU 按里程碑完成一次性迁移，
  完整历史由验收文档、任务单与 Git 保存；文档布局门禁及 MSVC CTest 160/160 通过。
- [WU-0105] 以 Android NDK `jni.h` 冻结 233 槽 JNINativeInterface、精确 primitive
  宽度和 32 位强类型 handle；缺失函数统一记账并 trap，不再允许静默返回零。
- [WU-0106] 保留完整 JNI ABI 目录，但不要求一次性实现全部低频函数；M3 优先闭合
  引用、异常、查找/调用、字符串、数组、RegisterNatives 与 JavaVM 线程接口。
- [WU-0107] 完成 Local/Global/WeakGlobal 引用表、线程与 local frame 隔离、容量上限、
  weak 清除，以及可同时承载 host/未来 DEX VM 对象的身份契约。
- [WU-0108] 完成 guest 线程独立 pending exception、Throw/Occurred/Check/Clear 底座，
  pending 状态下只开放检查、清理、引用删除和资源 release 白名单。
- [WU-0109] 完成字段/方法描述符解析，覆盖 primitive、对象、数组、void 返回、255 维
  与 255 参数槽边界，为 GetMethodID 和三种调用变体提供统一布局。
- [WU-0110] 完成 JNI Modified UTF-8 严格编解码，覆盖嵌入 NUL、非 ASCII、代理项、
  过长编码、截断输入和标准 UTF-8 四字节序列拒绝。
- [WU-0111] 以 JniEnvironment 原子装配引用与异常线程状态，闭合 GetVersion、local
  frame、三类引用和异常检查/清理等首批常用操作。
- [WU-0112] 完成 UTF-16 字符串对象、Modified UTF-8 创建、长度/region 以及
  chars/UTF/critical 配对访问租约，并可发布到 JNIEnv 引用表。
- [WU-0113] 完成八种 primitive array 的零初始化、类型化 region、elements/critical
  租约与 copy-back/commit/abort 释放语义。
- [WU-0114] 完成 RegisterNatives 整批事务预检、重载精确查询、幂等重注册、冲突拒绝
  与按类 UnregisterNatives，host/DEX VM 类身份可共存。
- [WU-0115] 冻结 JavaVM 8 槽 ABI，完成 GetEnv、普通/daemon Attach 与 Detach，
  重复 attach 保持稳定 env，版本、未附着和失败回滚语义明确。
- [WU-0116] 完成声明式类注册、父类/可赋值查询、稳定 method/field ID，以及重载、
  静态、继承与构造器不继承的严格查找规则。
- [WU-0117] 将 `...`/`V`/`A` 统一为类型化参数，完成虚、指定类非虚与静态分派，
  参数/返回类型、receiver、方法类别和实现缺失均明确失败。
- [WU-0118] 将 169 个已有行为支撑的常用 JNIEnv 槽与 4 个 JavaVM 线程槽映射到
  稳定 thunk 并封口；字段访问、对象数组、反射、direct buffer 等低频槽继续 trap。
- [WU-0119] 声明式安装 Object、Bundle、Activity 类和生命周期方法，严格执行
  construct→create→start→resume→pause→stop→destroy 并记录确定性事件。
- [WU-0120] 完成 null 初始化对象数组、元素类冻结、继承可赋值校验及明确的长度、越界、
  未知对象和不兼容赋值失败。
- [WU-0121] 完成 descriptor 驱动的字段默认值、实例隔离与静态共享存储，并统一校验
  字段类别、继承兼容性和精确值类型。
- [WU-0122] 无图形累计契约闭合 HLE→guest native 与 native→Activity HLE，并在同一运行中
  验证 VM、字段、字符串、primitive/object 数组、引用和异常资源。
- [WU-0123] 建立 DEX 035..040 只读事实模型，交叉验证 header、固定表范围和唯一有序
  map_list；畸形标识、端序、对齐、范围与映射均明确失败。
- [WU-0124] 严格解码 DEX Modified UTF-8/ULEB128，并解析类型 descriptor、prototype
  type_list 与 shorty；UTF-16 长度、所有索引和签名一致性均受检。
- [WU-0125] 解析 field/method ID、class_def 与接口列表，严格校验声明类、成员索引、
  唯一类定义、父类/接口/源码索引及 data section 偏移。

## 下一步（按优先级）

1. 创建 `WU-0126`，解析 DEX class_data 和 code_item 元数据。
2. 随后生成引擎指纹与 Java 厚度报告，不引入字节码执行器。
3. M3 出口继续使用三平台 warnings-as-errors 构建与累计契约样本验收。

## 阻塞

- 无。

长期限制与非阻塞事项见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
