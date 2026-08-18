# 07 · 重入、失败语义与所有权

## 1. 最大实现风险：Java ↔ native 重入

合法调用链示例：

```text
DexVM: Application.<clinit>
  ↓
System.loadLibrary("game")
  ↓
NativeLibraryLoader
  ↓
ELF constructor / JNI_OnLoad
  ↓
JNI CallStaticVoidMethod(...)
  ↓
DexVM interprets Java callback
  ↓
System.loadLibrary("helper")
  ↓
NativeLibraryLoader (nested)
```

这不是异常边角，而是必须支持的正常初始化形态。

## 2. 锁纪律

实现前必须审计 DexVM execution lock、JNI invocation engine、guest call executor、
ELF namespace/loader 内部锁。硬规则：

1. **不得在调用 guest code 时持有禁止重入的全局 loader mutex。** constructors 与
   `JNI_OnLoad` 都是任意 guest code。
2. **不得在 JNI callback 进入 DexVM 时要求获取仍由外层 DexVM 执行持有的非重入锁。**
3. loader registry 可以用短临界区发布 `Loading` 状态，但执行 linker constructors /
   `JNI_OnLoad` 前应退出不必要的 host mutex。
4. 若 DexVM 现有 execution lock 明确支持同线程递归，必须有测试证明；若不支持，
   应改为显式 reentry token/执行上下文切换，而不是“换 recursive_mutex”掩盖设计问题。

## 3. 同线程递归加载

`libA` 正在 explicit load，`JNI_OnLoad(A)` 再请求 A：

- 不能等待自己；
- 不能第二次跑 constructors/JNI_OnLoad；
- 应按 AOSP/Dalvik 行为对照决定返回成功还是错误，并在 APS-4 夹具固定。

A → B → A 的 dependency/load cycle 同理，需要能输出完整 chain。

## 4. loader 事务边界

动态 ELF 映射可能很难完全 rollback。第一阶段允许采用：

```text
map/relocate published
  ↓
constructor/JNI_OnLoad failure
  ↓
mark explicit load Failed
  ↓
process is considered startup/runtime failed
```

不强制实现安全 `dlclose`/unmap rollback；但不得把失败 module 标记 Loaded 后继续运行。
重复请求应得到稳定失败，避免半初始化库被再次当新库执行。

## 5. Java 异常与 host error

边界原则：

- Java `System.load*` 调用应得到 Java 可观察的 link/load error；
- host C++ `Result` 保留结构化原因用于 CLI/log/test；
- guest fault/未捕获 Java exception 不能被转成“找不到 profile”；
- pending Java exception 规则继续遵守 DexVM/JNI 既有门禁。

## 6. Application/Activity 失败

任一阶段失败即停止后续入口：

```text
Application <clinit> fails → no Application instance / no Activity
Application onCreate fails → no launcher Activity
Activity <clinit>/<init>/onCreate fails → no onStart/onResume
```

不要为了“尽量进游戏”吞异常继续。这些失败正是平台 intrinsic 或 title quirk 的真实
缺口信号。

## 7. 生命周期对象所有权

- Application instance：process lifetime root；
- current Activity：lifecycle/session root，Stop 后释放；
- loaded native modules：native process lifetime；
- JavaVM/JNI bridge：必须覆盖所有 loaded module finalization；
- VFS/package context：覆盖 Application/Activity/native teardown。

析构顺序违反这些关系会制造退出期 use-after-free 或 JNI callback 失效，应有 clean
shutdown 测试覆盖。

## 8. 并发边界

本任务只要求当前 OGPlay 支持的线程模型下正确。若 guest-created thread 在运行时
调用 `System.load*`：

- 必须走同一 process loader；
- registry 状态必须线程安全；
- 不要求实现 Android linker namespace 的现代多 namespace 特性；
- 同一 library 并发首次 load 的结果必须唯一，不能双跑 JNI_OnLoad。

若当前 DexVM intrinsic 只能主 Java thread 调用，APS-5 要把限制显式记录为 capability，
不可默认为“永远不会发生”。
