# 模块：runtime/dexvm

## 职责

有界 Dalvik 字节码解释器（ADR-0017、`docs/design/dexvm/`）：类链接、统一
Java 对象模型、解释器内核（帧/分派/三路 invoke/异常展开/`<clinit>`）与
java.* 核心 intrinsic。只解释游戏自带 DEX 的应用类；平台类永远是 intrinsic。

## 公共 API

- `DexClassLinker`：`RegisterIntrinsics`（代码定义目录，平台命名空间只能来自
  这里）→ `RegisterDex`（单一 classes.dex，dex 中平台前缀类被忽略）→ `Link`
  （只链接 VM 启动所需的 intrinsic；APK class_def 全量注册但按首次解析、实例化
  或调用才完成层级/字段布局/vtable/iftable）。未触达可选类缺失父类/接口不阻断
  进程；一旦触达，循环继承、缺失层级、final 覆盖、接口当 super、不可覆盖
  intrinsic 方法仍明确失败，survey 模式则只为真实触达的平台层级缺口合成并记账。
  常量池解析带缓存
  （`ResolveTypeIndex/ResolveMethodIndex/ResolveFieldIndex`），数组类按需合成，
  `IsAssignable` 覆盖类层级、接口、数组协变，以及数组对
  `Object`/`Cloneable`/`Serializable` 的 JLS 可赋值性。`PrecheckMethod` 懒执行结构
  预检（未定义 opcode、寄存器越界、分支/payload 目标、move-result 位置），
  规则子集对照 AOSP `CodeVerify.cpp`，不做全量数据流。
- `CoreIntrinsicCatalog()`：聚合 `intrinsics/` 下按 Java 类同址定义的声明与
  handler；覆盖 Object/String/Class/Throwable、隐式异常层级、核心集合接口，
  以及 pinned libcore `java.lang` 顶层 8 个 interface
  （`Appendable`/`AutoCloseable`/`CharSequence`/`Cloneable`/`Comparable`/
  `Iterable`/`Readable`/`Runnable`，family TU `java_lang_interfaces.cpp`）。
  `java.lang.Enum` 语义对照 pinned libcore `Enum.java`：name/ordinal 为声明式
  instance slot（解释子类继承布局）、构造器 `(String,I)` 写入、查询方法 final、
  `toString` 保持 overridable、`clone` 恒抛 CloneNotSupportedException、
  `getDeclaringClass` 按直接父类是否为 Enum 判定、静态 `valueOf(Class,String)`
  不走反射缓存——先 `<clinit>` 再按该 enum 自身同型 static 常量字段的活值按名
  匹配，null 参数 NPE、非 enum/未命中 IllegalArgumentException（消息对照 AOSP）。
  enum 子类的 `values()` 走数组 `Object.clone()`：数组可赋给 `Cloneable` /
  `Serializable`，浅拷贝顶层元素。
- `java.lang.Thread` 对照 pinned libcore `Thread.java`/`VMThread.java` 与 Dalvik
  `Thread.cpp`/`Sync.cpp`：root context 具有稳定 id=1 `main` Thread 强根；构造时
  分配 per-VM stable ID；`start()` 经实际 Thread class vtable 虚派发
  `this.run()`，基类 `run()` 才转发 target Runnable。core façade 覆盖四个构造器、
  currentThread、id/name/priority/isAlive、interrupt/interrupted、join/timed join、
  sleep、yield、holdsLock 与有界 daemon flag；priority 不伪造 host scheduler，
  daemon 不宣称驱动 session 退出。
- `java.lang.Object.clone` 是 overridable virtual intrinsic（不是
  `internalClone`）：`instanceof Cloneable` 失败抛
  `CloneNotSupportedException`（消息对照 libcore Object.java），成功则
  `Interpreter::CloneObject` 浅拷贝 payload 与 list/map/builder 侧表。
  `JavaObjectModel::CloneObject` 对照 AOSP `dvmCloneObject`：新句柄/identity，
  只复制 `vm_instance` 槽板或数组元素；string/class/host_backed 明确失败。
- 生产装配的 `JavaObjectModel` 通过 `JavaObjectInterop` 复用会话级
  `JniObjectArrayStore`：DexVM/JNI 创建、读写、克隆和清扫 `Object[]` 都使用同一
  identity/store，class identity 的双向适配由 integration 注入；不依赖
  `JniClassRegistry`。无 interop 的 isolated fixture 才使用模型内 fallback store。
- `IntrinsicClassBuilder`：工厂 `Class/RootClass/Interface` 一次声明类型头
  （descriptor、父类、接口；普通类默认父类 `Ljava/lang/Object;`，仅
  `java.lang.Object` 用 `RootClass` 显式无父类），方法按
  `Constructor/StaticMethod/VirtualMethod/FinalMethod` 声明、字段按
  `InstanceField/StaticField` 声明，另有 `ConstantInt/ConstantString` 常量与
  `ClassInitializer`（`<clinit>`）；声明即持有实现，空 handler 直接拒绝，
  未实现方法经 `UnimplementedStatic/Constructor/Virtual/Final` 显式进入
  miss/记账路径。`Build()` 在装配期校验类/方法/字段 descriptor（构造器必须
  返回 void、普通方法不得用 `<init>`/`<clinit>` 保留名）、重复成员、interface
  实例字段与整型常量的类型/范围。`BoundInstanceField/BoundStaticField` 产生声明
  token，由每个 linker 在注册时预绑定到自己的 `VmFieldId`；`IntrinsicCall` 为
  handler 提供受检的类型化参数、receiver 与 primitive/reference 字段读写，禁止
  handler 重复按字符串查字段或直接读写裸 slot。
- `JavaObjectModel`：session 级统一对象身份（VmObjectRef 句柄空间，0=null）。
  VM 实例与对象数组自有存储；字符串与基元数组委托注入的
  `JniStringStore`/`JniPrimitiveArrayStore`——native 与解释器看到同一对象。
  `CloneObject` 分配新句柄后浅拷贝 instance slot / 数组元素（对照 AOSP
  `dvmCloneObject`）。GC-B 是精确、非移动、STW 标记清除：对象记录保存原始
  `reserved_bytes`，清扫回减预算并以确定性 LIFO 空闲链复用记录/实例槽/数组槽；
  空闲记录访问明确失败。intern 字符串与 class 对象为不朽强根；JNI weak global
  不是根，目标死亡时被清空。默认堆预算 64 MiB；`SetEmergencyReserve` 仅供解释器
  物化 OOM throwable。
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
- `InterpreterConfig::diagnostics` / `Interpreter::Trace`（DVM-52）：默认容量 0
  精确关闭；启用后在构造期分配固定事件环，记录 instruction、method、exception、
  class init、monitor、native 与 GC 的稳定整数事实。指令事件可采样，event mask 与
  query filter/limit 受检；guest 热路径不分配/格式化，也不保存 host 指针，class/
  method 文本只在查询时解析。`RenderDexVmTraceJson` 输出 schema 1。
- `Interpreter::StackSnapshot`（DVM-52）：获取 `VmExecutionLock` 后枚举全部 execution
  context 的 Java 帧、dex pc、tick 与 pending exception，并用 context token 关联
  `VmThreadRuntime` 的 guest id/name/status；这是等待当前 guest call 释放全 VM 锁的
  停界查询，不宣称异步抢占。`RenderDexVmStacksJson` 输出 schema 1。
- `FastCode`（DVM-53）：由已通过 `PrecheckMethod` 的原始 u2 指令流确定性派生，
  预解码 opcode/宽度/操作数、dex pc 到内部索引、受检分支目标以及 packed/sparse
  switch 与 fill-array-data 边表；缓存按 `LinkedMethod` 懒构建并只读共享，不保存
  guest 引用、不计入 guest heap，且绝不改写原 DEX。
- `InterpreterBackend`（DVM-54）：默认 `switch_dispatch` 保持原路径；显式
  `threaded` 经 FastCode handler 表分派，GCC/Clang 使用 computed goto，MSVC 使用
  dense switch。DVM-55 已把 move/const/return/goto/if/cmp/算术族迁入直达 handler：
  不再读取 u2 或重复检查预检已证明的寄存器边界，但保留 tag/wide-pair/zero-as-null
  校验；算术复用唯一语义体。对象/invoke/switch 仍 bridge。异常展开、tick、trace
  与 switch 精确共源；stats 报告后端及 FastCode 构建次数/宿主字节数。
- `VmExecutionLock`（`Interpreter::ExecutionLock()`）：全 VM 执行锁。所有
  `Call`/`EnsureClassInitialized` 入口获取，同一宿主线程可重入；阻塞原语用
  `ReleaseForBlocking`/`ReacquireAfterBlocking` 整体释放再按原深度恢复；可注入
  一个宿主线程 id + blocked 状态 observer，回调在锁外执行，供上层观察通用 guest
  阻塞作用域，dexvm 不感知观察者的 EGL/session 用途。
  **同一时刻只有一个线程解释字节码**——这是显式记账的限制而非并发，换来的是
  linker 解析缓存、object model arena 与 intrinsic 侧表只有单写者，因此全部
  intrinsic handler 都在锁内运行。该执行锁同时是 GC 的停世界边界：GC 只在
  六类解释器安全分配指令最前端运行；阻塞原语不得跨 `ReleaseForBlocking` 在
  C++ 栈保留未根化的新鲜句柄。
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
  从 Thread 对象实际 runtime class 解析 virtual `this.run()`；ID 在构造时由
  per-VM allocator 分配，root/child identity 与 execution context 显式映射。
  `Join`/timed join 与 `Sleep` 释放执行锁后停泊，共用 monitor 表注入的 monotonic
  Clock；`Interrupt` 只写 monitor execution-token interrupt state 并唤醒
  wait/join/sleep。`Shutdown` 先 RequestStop、join 全部宿主线程，再显式展开 stopped
  context（幂等，记录保留供事后查询）。未捕获异常与 VM 错误记入
  `TakeFailure()`，由生命周期驱动在帧
  边界上报，对齐设备上的进程级默认 handler，而不是丢给 `join()` 的调用方。
  每个 child 启动/退出时经 `NativeMethodBridge` 挂接/释放独立 A32 CPU、guest stack、
  Bionic TLS/thread-info、process thread id 与 JNI local-frame 环境，因此 guest native
  帧存活时仍可安全停泊。`EnsureClassInitialized` 对同线程重入放行，其他 context
  释放执行锁等待；初始化成功、失败与 teardown 都唤醒等待者并重查粘滞状态。
  `SetStaticFieldBits` 仅接受已完成初始化的真实 guest 静态字段，供 ADR-0022
  的结论级 Profile preset 写入精确槽位；类、字段、静态性、类型或槽位不匹配
  均明确失败，禁止绕过 `<clinit>`。
- Class/Method 反射只开放真实 declared-method 枚举和零参数、int-like
  返回的调用；其余明确抛 `UnsupportedOperationException`。
- `System.getProperty(String)`、`setProperty(String,String)` 与 primitive wrapper
  property API 共享每 VM 属性表；separator 默认值来自固定 API 19 guest 事实，
  未知属性返回 null，禁止泄露宿主属性。

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
`class_linker_resolve.cpp`，方法结构预检及 `FastCode` 懒缓存入口在
`method_precheck.cpp`，构建器位于 `fast_code.cpp`。解释器主循环在 `interpreter.cpp`，
FastCode 分派骨架在 `interp_threaded.cpp`，直线族在
`interp_threaded_straight.cpp`，显式执行 context
的选择、校验、thread-local 活跃路由与 `VmExecutionLock` 在
`interpreter_context.cpp`，诊断 ring/query/JSON 在 `diagnostics.cpp`，宿主线程
生命周期在 `vm_threads.cpp`。
`intrinsics/catalog.cpp` 显式聚合目录；每个 Java 类仍由唯一
`Declare_*()` 同址声明形状与 handler，默认一类一个同名 `.cpp`。唯一文件组织
例外是 Android 4.4.4 `java.lang` Throwable hierarchy、primitive wrapper
family 与接口 family，分别位于 `intrinsics/java_lang_throwables.cpp`、
`intrinsics/java_lang_primitive_wrappers.cpp` 与
`intrinsics/java_lang_interfaces.cpp`；Java class 仍是一等逻辑单位，
family 内类级 `Declare_*()` 均为 TU-private，catalog 只调用对应 `Append*()`。
family TU 可超过通常 800 行，但禁止 misc/common/all 巨石与静态自注册。
`shared.h` 只放跨类内部 helper。原集中式 core catalog 与三个 handler 文件已
删除。pinned Luni `java.lang` 的确定性 public/protected 顶层 class shape 位于
`data/dexvm/api19-java-lang-surface.json`；用 `tools/dexvm_api19_surface.py` 从本地
API-19 源码生成/校验，再由 `tools/dexvm_stub_gen.py --surface` 生成当前 builder 骨架。

## 不变量

- 依赖只指向 core/loader/runtime‑jni；不依赖 runtime/framework（intrinsic 经
  `PlatformClassProvider` 形态由装配方注入 registry/目录）。
- guest 不可信：全部索引/偏移/tag 受检；未实现 opcode/intrinsic/native 记账
  且明确失败，绝不静默返回默认值。
- 语义出处：逐 opcode 对照 AOSP `vm/mterp/c/OP_*.cpp`（一致性夹具注释记录），
  分歧按 07 §5 仲裁。无 JIT、不改写指令流（quickening 红线）。
- 对象非移动，句柄生命周期内稳定。
- GC 根集必须覆盖全部 context 的全部帧 tagged ref、last_result/caught、pending/
  exit result、静态 ref 槽、JNI local/global、Thread 对象、intern/class 对象以及
  Android session 显式登记的外部长期引用。intrinsic 宿主状态必须通过
  `RegisterIntrinsicStateTable` 具名注册：持 `VmObjectRef` 的表提供 trace，所有表
  必须提供 sweep，需要随 `Object.clone()` 复制的表再提供 clone；现有
  throwable/builder/list/map 四表均走该唯一生命周期通道。
- GC 只由分配流决定：`gc_watermark_percent` 范围 0..100、默认 75，0 精确关闭；
  `System.gc()` 保持合法 no-op。结构化 `runtime.dexvm.gc` 日志与 stats 记录回收量、
  对象数、宿主析构次数及确定性 pause ticks，不以 wall clock 参与决策。
- host-backed 对象只有显式声明 `HostStateDestructor` 才在清扫时释放宿主状态，
  每个死亡对象恰调用一次，不执行 guest finalizer；string/primitive-array store、
  monitor、JNI weak 与身份映射均随死亡记录清理。
- 一个 guest 线程对应一个宿主线程；解释执行由 `VmExecutionLock` 串行化，
  不宣称并行。锁序只有一个方向：执行锁 → 线程运行时互斥量 → context 表互斥
  量，反向获取一律禁止。时间预算仍通过各 context 的 tick 计数约束。
- guest native 出向调用记入 context 的 `native_depth`；它用于 teardown 完整性检查，
  不再禁止停泊，因为每个 Java 线程拥有独立 native 栈/TLS。
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

- 反射仅覆盖有界的 `getDeclaredMethods` / 零参整数类返回值
  `Method.invoke`；其余反射面、finalizer、GC-B 未实现。

## 测试

`tests/dexvm/interpreter_tests.cpp`（dexasm 夹具一致性：未触达缺失层级类不阻断
whole-DEX 启动、首次触达明确失败/survey 才记账；core catalog 唯一性与
代表类签名集合、System property 默认值/读写/异常、intrinsic builder 装配校验与
声明即绑定、重复方法拒绝、
直调与声明未实现的重复 miss 记账，算术边界、控制流、
数组、字段、三种 dispatch、clinit、跨帧异常、栈溢出、tick/heap 预算、两个
显式执行 context 交错调用的帧/异常/tick/monitor 隔离、跨线程 clinit 等待；
DVM-52 默认关闭、固定 ring 覆盖/筛选/采样、语义事件族、跨 context stack 与
schema-1 JSON）；
`tests/dexvm/vm_thread_tests.cpp`（真实宿主线程执行 run()、共享对象世界、
二次 start 与无 run() 目标拒绝、isAlive、join、未捕获异常记账、interrupt、
teardown 逐线程 join、持有 native 帧时拒绝停泊）；
`tests/dexvm/thread_intrinsic_tests.cpp`（四构造器/per-VM ID、root/child
currentThread identity、virtual Thread override 优先于 Runnable、start-once、
name/priority/daemon/isAlive、单一 interrupt flag、sleep/timed join 假时钟、
holdsLock、runtime rename diagnostics 与 active/finished GC roots）；
`tests/dexvm/vm_monitor_tests.cpp`（跨宿主线程 notifyAll 配对、recursion 深度
恢复（三层 monitor-exit 不平衡即失败）、wait/notify 所有权校验、统一 Clock
截止时间到期、无 Clock 的 timed wait 明确失败、interrupt 唤醒且抛异常前已
重获 monitor、wait 前已置位的 interrupt 不停泊、teardown 唤醒全部 waiter、
driver 阻塞时 N=2 条件 swap 放行与 driver 可运行时帧推进节拍）；
`tests/dexvm/fast_code_tests.cpp`（DVM-53 指令边界/操作数/分支索引、三类 payload
边表及畸形输入拒绝）；`tests/dexvm/interpreter_tests.cpp` 的 DVM-54 双后端夹具
比较返回、异常、指令数、tick 与 trace；`tests/dexvm/dex_code_tests.cpp`、
`tests/dexvm/dexasm_readback_tests.cpp`、
`tests/dexvm/gap_survey_tests.cpp`（survey 开/关对照：关闭即失败、桩答中性值、
命中计数、工作单排序）。
