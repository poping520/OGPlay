# ADR-0028 · DexVM declaration 只保存 own members

- 状态：Accepted
- 日期：2026-08-29
- 关联：[DVM-94](../tasks/dexvm/DVM-94.md)

## 背景

早期 intrinsic builder 允许子类再次声明父类方法。这样既让 `Application` 等类型需要复制
`Context` API，又无法区分新增虚方法与真实 override；常量池缓存还只区分
direct-or-static，错误 opcode 可能复用不相容的解析结果。

## 决定

class declaration 只包含本类新增成员、构造器、静态成员、类生命周期方法和真实 override。
继承成员只由 linker 复制 vtable/field layout 产生。普通 `VirtualMethod` 命中父类同签名即
链接失败；真实覆盖必须使用 `OverrideMethod`（final 覆盖使用显式 final 版本），并校验父
签名、final/static/private 与可见性。

可见性比较使用 Java 的 public > protected > package > private 顺序。Intrinsic builder 的
普通虚方法默认 public，但平台 callback 必须按 pinned API 元数据显式写入 access flags；
不得为绕过链接错误而放宽 override 规则。Android 4.4.4 中 Activity 生命周期、
View.onSizeChanged、AsyncTask 回调和 HandlerThread.onLooperPrepared 均属于 protected。

调用解析以 `InvokeKind` 为键，返回含 declared owner、symbolic method 与可选 vtable slot 的
`ResolvedCallSite`。constructor/private 只查声明类，static 可沿父链但不多态，super 从当前
执行类的直接父类分派，`<clinit>` 不继承。descriptor 只在注册/链接时生成 `MethodShape`。

## 后果

`Application/Activity/Service` 自动继承 ContextWrapper；只有 ContextWrapper 的 `mBase`
委托是显式覆盖。反射 declared/public 查询和 declaring class 不再受复制声明污染。lazy 数组
和 Survey 追加依赖地址稳定 linker 存储，不得写回 DEX 或引入 quickening/JIT。
