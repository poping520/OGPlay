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
- `CoreIntrinsicCatalog()`：聚合 `intrinsics/` 下按 Java 类同址定义的声明与
  handler；覆盖 Object/String/Class/Throwable、隐式异常层级与核心集合接口。
- `IntrinsicClassBuilder`：以类为单位声明 static/virtual/overridable 方法、字段、
  常量与 `<clinit>`，`Build()` 在装配期校验类/方法/字段 descriptor、重复成员和
  interface 实例字段；声明在构建时直接持有实现，未实现方法通过
  `Unimplemented()` 显式进入 miss/记账路径。
- `JavaObjectModel`：session 级统一对象身份（VmObjectRef 句柄空间，0=null）。
  VM 实例与对象数组自有存储；字符串与基元数组委托注入的
  `JniStringStore`/`JniPrimitiveArrayStore`——native 与解释器看到同一对象。
  GC-A 预算 arena：只分配不回收，默认 64 MiB，耗尽抛
  `heap_budget_exhausted`；`SetEmergencyReserve` 仅供解释器物化 OOM throwable。
- `Interpreter`：`Call(method, args)` 在当前宿主线程执行至完成，返回
  `VmCallOutcome`（值或未捕获 Java 异常 + 消息 + 栈回溯）。tagged 寄存器
  （uninit/cat1/wide 对/ref + 零值放宽）、每指令 1 tick 预算、帧深度上限
  （默认 512 → 真实 StackOverflowError）。invoke 三路由：解释方法压帧、
  intrinsic 只直调声明内嵌的拥有型实现（缺失实现按 owner+方法签名逐次记账并
  抛 UnsatisfiedLinkError），
  native 走 `NativeMethodBridge`（未注入则记账 + 明确失败）。
  `CreateExecutionContext` 建立显式执行 context；`Call(context, ...)` 隔离其
  帧栈、pending exception、tick、返回值与 monitor recursion，同时共享 linker、
  object model 与 intrinsic 目录。默认 `Call` 保持原单线程 context。
  `RequestStop(context)` 是 teardown 握手：解释器每条指令检查一次，命中即以
  `thread_stopped` 离开执行；宿主线程 join 后
  `UnwindStoppedExecutionContext` 清理由 native/A32 重入边界留下的不可达外层帧，
  未请求停止或仍有 native frame 均明确拒绝，普通 `DiscardExecutionContext`
  继续禁止丢弃活动栈。
- `VmExecutionLock`（`Interpreter::ExecutionLock()`）：全 VM 执行锁。所有
  `Call`/`EnsureClassInitialized` 入口获取，同一宿主线程可重入；阻塞原语用
  `ReleaseForBlocking`/`ReacquireAfterBlocking` 整体释放再按原深度恢复；可注入
  一个宿主线程 id + blocked 状态 observer，回调在锁外执行，供上层观察通用 guest
  阻塞作用域，dexvm 不感知观察者的 EGL/session 用途。
  **同一时刻只有一个线程解释字节码**——这是显式记账的限制而非并发，换来的是
  linker 解析缓存、object model arena 与 intrinsic 侧表只有单写者，因此全部
  intrinsic handler 都在锁内运行。
- `VmMonitorTable`（`vm_monitors.h`，`Interpreter::Monitors()`）：session 级
  对象 monitor，owner 是 execution context token。`Enter`/`Exit` 提供真实
  跨线程互斥（争用时释放执行锁停泊）；`Wait` 逐步对照 AOSP `vm/Sync.cpp`
  `waitMonitor`——校验 owner → 入 wait-set → 保存并清零 recursion → 全量释放
  monitor → 停泊 → 唤醒后**先重新竞争 monitor 并恢复原 recursion**，然后才
  返回或抛 `InterruptedException`。无所有权一律
  `IllegalMonitorStateException`。唤醒源：`notify`（取 wait-set 头）、
  `notifyAll`、Clock 超时、`Interrupt`、`Shutdown`。超时只用注入的统一 Clock
  （`SetTimeSource`），没有时间源的 timed wait 记账并明确失败，绝不读宿主
  wall clock；条件变量的轮询间隔只是调度唤醒，不参与截止判定。
  线程结束时 `ReleaseAll` 释放其仍持有的 monitor 与 wait-set 成员资格。
- `VmThreadRuntime`（`vm_threads.h`）：一个 guest Java 线程 = 一个
  `hal::StartHostThread` 宿主线程 + 一个独立 execution context，linker/对象
  模型/JNI 身份共享，子线程与 root 看到同一个对象世界。`Start` 在调用方线程
  解析 `run()`（无 `run()` 或二次 start 明确抛
  `IllegalThreadStateException`）、`Join` 释放执行锁后停泊、`Interrupt` 置位
  并唤醒、`Shutdown` 先 RequestStop、join 全部宿主线程，再显式展开 stopped
  context（幂等，记录保留供事后查询）。未捕获异常与 VM 错误记入
  `TakeFailure()`，由生命周期驱动在帧
  边界上报，对齐设备上的进程级默认 handler，而不是丢给 `join()` 的调用方。
  线程持有 guest native 帧时拒绝停泊（`blocking_in_native` +
  `dexvm.threads.block_in_native` 记账）：A32 执行器只有一条 root guest 栈，
  让另一个线程复用它就是静默破坏。
  `EnsureClassInitialized` 实现 `<clinit>` 状态机（同线程重入放行、失败粘滞
  NoClassDefFoundError、静态初始值先于 `<clinit>` 物化）。
  `SetStaticFieldBits` 仅接受已完成初始化的真实 guest 静态字段，供 ADR-0022
  的结论级 Profile preset 写入精确槽位；类、字段、静态性、类型或槽位不匹配
  均明确失败，禁止绕过 `<clinit>`。
- Class/Method 反射只开放真实 declared-method 枚举和零参数、int-like
  返回的调用；其余明确抛 `UnsupportedOperationException`。
- `System.getProperty(String)` 与 `setProperty(String,String)` 共享每 VM 属性表；
  separator 默认值来自固定 API 19 guest 事实，未知属性返回 null，禁止泄露宿主属性。

- Gap survey（诊断，默认关闭）：`EnableGapSurvey()` 后，未声明的**平台**类/
  方法被合成为中性桩（0/null/void）并逐次记账，一次运行即可收割新 title 的
  整条缺口队列；`GapSurveyHits()` + `RenderGapSurveyJson()`（`gap_survey.cpp`）
  输出按命中次数排序的机读工作单。survey 运行不是兼容性结论，调用方必须显式
  标注；关闭时行为不变（未声明即明确失败）。survey 模式下中性桩返回的
  null/0 若流进 host 访问器触发 `object_model_failure`，会被转成 guest
  `NullPointerException` 而非终止进程（保证一次运行收割到底；非 survey 仍硬
  失败）。流程见 `docs/playbook/NEW-TITLE.md`。

## 文件分工

`class_linker_internal.h` 持有 `DexClassLinker::Impl` 与共享 helper：注册/
布局/vtable 在 `class_linker.cpp`，常量池解析与可赋值性在
`class_linker_resolve.cpp`。解释器主循环在 `interpreter.cpp`，显式执行 context
的选择、校验、thread-local 活跃路由与 `VmExecutionLock` 在
`interpreter_context.cpp`，宿主线程生命周期在 `vm_threads.cpp`。
`intrinsics/catalog.cpp` 显式聚合目录；每个 Java 类仍由唯一
`Declare_*()` 同址声明形状与 handler，默认一类一个同名 `.cpp`。唯一文件组织
例外是 Android 4.4.4 `java.lang` Throwable hierarchy，全部位于
`intrinsics/java_lang_throwables.cpp`；该 family 的类级 `Declare_*()` 为
TU-private，由单一 `AppendJavaLangThrowables()` 入口加入 core catalog。
`shared.h` 只放跨类内部 helper。原集中式 core catalog 与三个 handler 文件已
删除。

## 不变量

- 依赖只指向 core/loader/runtime‑jni；不依赖 runtime/framework（intrinsic 经
  `PlatformClassProvider` 形态由装配方注入 registry/目录）。
- guest 不可信：全部索引/偏移/tag 受检；未实现 opcode/intrinsic/native 记账
  且明确失败，绝不静默返回默认值。
- 语义出处：逐 opcode 对照 AOSP `vm/mterp/c/OP_*.cpp`（一致性夹具注释记录），
  分歧按 07 §5 仲裁。无 JIT、不改写指令流（quickening 红线）。
- 对象非移动，句柄生命周期内稳定。
- 一个 guest 线程对应一个宿主线程；解释执行由 `VmExecutionLock` 串行化，
  不宣称并行。锁序只有一个方向：执行锁 → 线程运行时互斥量 → context 表互斥
  量，反向获取一律禁止。时间预算仍通过各 context 的 tick 计数约束。
- guest native 出向调用记入 context 的 `native_depth`；持有该帧时不得停泊。
- `IntrinsicMethodDecl::implementation` 与
  `IntrinsicClassDecl::clinit_implementation` 是唯一 intrinsic 分发通道；链接器
  只搬运拥有型实现，不保存字符串标识或懒解析缓存。System/Date 的 7 个平台动作
  在 integration 装配点以成员指针直接补入 core 声明。
- 热路径在 `Call`/`EnsureClassInitialized` 入口解析一次活跃 execution，沿
  `Run`/`Step`/`Tick`/invoke 与字段家族/`EnsureInitialized`/
  `PushInterpretedFrame` 显式传引用；逐指令不得重查 thread-local 路由。
  `Execution()` 只允许出现在入口与冷路径（异常、诊断、native 帧标记）。
  一次活跃调用内 context 不可切换（`InterpreterExecutionScope` 强制），
  该引用因此在整个 `Run` 期间恒定。

## 尚未实现（记账可查）

- `runtime.jni_guest_monitors`（native 侧 JNI MonitorEnter/Exit）仍是独立的
  monitor table；两张表尚未合一，native 与解释器对同一对象加锁互不可见。
- 子线程的 guest native 调用仍走 root guest 线程的 CPU 与栈（JNI local
  reference 也记在 root thread id 下）。执行锁保证不会并发，但这不是真正的
  per-thread JavaVM attach；需要停泊时明确失败而不是凑合。
- `Thread.sleep` 推进统一 uptime 并让出执行锁，不按 Clock 截止时间停泊。
- `Object.wait(long, int)` 未声明；只有 `wait()` 与 `wait(long)`。
- 反射仅覆盖有界的 `getDeclaredMethods` / 零参整数类返回值
  `Method.invoke`；其余反射面、finalizer、GC-B 未实现。

## 测试

`tests/dexvm/interpreter_tests.cpp`（dexasm 夹具一致性：core catalog 唯一性与
代表类签名集合、System property 默认值/读写/异常、intrinsic builder 装配校验与
声明即绑定、重复方法拒绝、
直调与声明未实现的重复 miss 记账，算术边界、控制流、
数组、字段、三种 dispatch、clinit、跨帧异常、栈溢出、tick/heap 预算、两个
显式执行 context 交错调用的帧/异常/tick/monitor 隔离）；
`tests/dexvm/vm_thread_tests.cpp`（真实宿主线程执行 run()、共享对象世界、
二次 start 与无 run() 目标拒绝、isAlive、join、未捕获异常记账、interrupt、
teardown 逐线程 join、持有 native 帧时拒绝停泊）；
`tests/dexvm/vm_monitor_tests.cpp`（跨宿主线程 notifyAll 配对、recursion 深度
恢复（三层 monitor-exit 不平衡即失败）、wait/notify 所有权校验、统一 Clock
截止时间到期、无 Clock 的 timed wait 明确失败、interrupt 唤醒且抛异常前已
重获 monitor、wait 前已置位的 interrupt 不停泊、teardown 唤醒全部 waiter、
driver 阻塞时 N=2 条件 swap 放行与 driver 可运行时帧推进节拍）；
`tests/dexvm/dex_code_tests.cpp`、`tests/dexvm/dexasm_readback_tests.cpp`、
`tests/dexvm/gap_survey_tests.cpp`（survey 开/关对照：关闭即失败、桩答中性值、
命中计数、工作单排序）。
