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
  预检（未定义 opcode、寄存器越界含 `k35c`/`k3rc` 参数表与 wide pair、分支/payload
  目标、move-result 位置），
  规则子集对照 AOSP `CodeVerify.cpp`，不做全量数据流。
- `ClassNameCodec` / reflection linker metadata（DVM-62）：唯一受检 method/type
  descriptor 拆分与 `Class.getName`/binary-name 转换入口；linker 为每个 class 发布
  bootstrap/application defining-loader role、direct interfaces 与 flattened iftable、
  DEX/intrinsic 声明顺序的 own direct/virtual methods 和 fields。`LinkedMethod` 显式
  保存 direct/static/virtual/interface invocation category；class/method/field raw
  access flags 从 DEX 或 `IntrinsicClassBuilder` 一路保真。array defining loader 跟随
  component，primitive class 包含 `V`。这些 metadata 是 DVM-63..69 的唯一事实源，
  vtable 不得代替 declared-member query。
- `ClassLoaderFacade`（DVM-63）：每个 VM 只发布一个稳定 application
  `PathClassLoader` 与一个 `BootClassLoader`，parent 固定为 application → boot → null；
  facade 只消费 linker 的唯一 class directory，并以 initiating-loader bit 区分“已知”与
  “已由该 loader 发起”。`findLoadedClass` 只查询、不链接/初始化/合成，`loadClass`
  使用 `ClassNameCodec` 受检 binary name、按 boot/application 边界委托并忽略 API-19
  `resolve` 参数。`Class.forName(String)` 使用真实 interpreted caller loader 并初始化；
  三参数入口将 null 按 API19 归 system/application facade，显式 boot/application/custom
  role 仍只查同一 linker directory；不支持动态 classpath、多 namespace 或自定义定义权限。
- `ReflectionRuntime`（DVM-64..65）：按 declaring class 缓存 immutable Method/Constructor/Field
  metadata，并作为唯一 guest wrapper factory。wrapper 只保存 declaring `Class` 与 opaque
  declared-order ordinal；不得暴露 `VmMethodId`/`VmFieldId`/DEX index，也不得缓存 guest
  ref。每次查询重新物化普通可回收对象，accessible flag 与类型数组都属于该 wrapper。
  DVM-65 在此基础上提供 declared/public 成员查找：public Method/Field 按
  class、superclass、direct interface 递归稳定聚合与去重，Constructor 始终只查本类。
  三类 wrapper 的 API19 hashCode 与 exact declaration toString 共用 modifier/type-name
  formatter；fresh semantic-equal wrapper 不退回 Object identity hash。
- `ReflectionCodec` / invoke runtime（DVM-66）：统一 Object/ref 可赋值检查、
  primitive wrapper unbox/widen 与返回 boxing；禁止 narrowing 及 boolean/numeric
  互转。`Method.invoke` 从 interpreter 活跃 frame 取真实 caller，以
  `(defining_loader, package)` 裁决 public/private/package/protected 和 protected receiver，
  再按 declared invoke kind 选 direct/static 精确 target 或按 receiver vtable 选
  virtual/interface target。target throwable 只用持有原异常强引用的
  `InvocationTargetException` 包装，不重新物化原异常。
- `CoreIntrinsicCatalog(services)`：聚合 `intrinsics/` 下按 API family 同址定义的声明与
  handler；覆盖 Object/String/Class/Throwable、隐式异常层级、核心集合接口，
  以及 pinned libcore `java.lang` 顶层 8 个 interface
  （`Appendable`/`AutoCloseable`/`CharSequence`/`Cloneable`/`Comparable`/
  `Iterable`/`Readable`/`Runnable`，统一位于 `java_lang.cpp`）。
  `java.lang.Enum` 语义对照 pinned libcore `Enum.java`：name/ordinal 为声明式
  instance slot（解释子类继承布局）、构造器 `(String,I)` 写入、查询方法 final、
  `toString` 保持 overridable、`clone` 恒抛 CloneNotSupportedException、
  `getDeclaringClass` 按直接父类是否为 Enum 判定、静态 `valueOf(Class,String)`
  不走反射缓存——先 `<clinit>` 再按该 enum 自身同型 static 常量字段的活值按名
  匹配，null 参数 NPE、非 enum/未命中 IllegalArgumentException（消息对照 AOSP）。
  enum 子类的 `values()` 走数组 `Object.clone()`：数组可赋给 `Cloneable` /
  `Serializable`，浅拷贝顶层元素。
- `CollectionRuntime`（DVM-78）：统一拥有 java.util sequence/map、sub-list、三类
  live map view、稳定 Entry 与 fail-fast iterator 的 per-VM side state；map 节点保存
  guest virtual `hashCode` 结果和稳定 entry id，结构修改递增 `mod_count`。所有 guest
  references 由同一具名 side-table trace，死亡 owner 统一 sweep；Object.clone 仅浅拷贝
  sequence/map 内容，不复制 view/entry/iterator 游标。intrinsic handler 不保存宿主容器
  指针，也不以 `VmObjectRef` 数值代替 Java equals/hashCode。
- `IoRuntime`（DVM-79）：统一拥有 java.io input/output bytes、cursor、close 与 wrapper
  adoption side state，并通过具名 intrinsic state table 随死亡 owner 清扫。File 家族只
  使用装配方注入的 core `IoFileSystem` 窄接口；integration adapter 才可访问具体 VFS。
  无文件系统时明确失败；流 wrapper 为 single-owner transfer，clone 不复制游标或缓冲。
- `ZipRuntime`（DVM-79）：统一拥有 java.util.zip archive、当前 entry bytes/cursor 与
  close 状态；复用 loader 严格 ZIP parser/inflate，并由 intrinsic side-table hook 清扫。
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
  JNI `NewObject` 分配的 application identity 回入解释器时，经 integration 注入的
  lazy-linked layout resolver 按完整 `instance_slots` 建立 `vm_instance` 槽板并保留
  原 identity；intrinsic host object 继续使用 external/专用 store 分类。
  `String.format(String,Object[])` 当前按统一 Object[]/boxed slots 实现顺序 `%s/%d`
  与 `%%`；`%s` 支持 String/null，普通 Object virtual-toString 与其他未交付 conversion
  明确失败，不借用宿主 printf/locale。
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
  guest 可观察的 identity hash 独立于 `VmObjectRef`/记录槽：普通对象使用不回收的
  per-VM 序列，Class object 按稳定 descriptor 派生；GC 复用 handle 不复用 hash，
  catalog 重排不改变 Class hash。`Object.hashCode` 与
  `System.identityHashCode` 使用 `IdentityHashCode`（后者绕过 override，null 为
  0）；默认 `Object.toString` 按 API-19/libcore 对 receiver 虚调用 `hashCode()`，
  再转 lowercase hex；虚调用异常通过 existing-pending 通路传播原 throwable，
  禁止按 descriptor/message 重新物化。
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
  不可恢复的 `DexVmError` 在清理 interpreted frames 前自动附加一次 guest Java
  调用栈：标题含 context 与已注册的 guest thread id/name，首帧含 opcode 和可用的
  DEX method index，帧含 class/method descriptor 与 DEX PC。最内层优先、最多 64 帧，
  超限明确报告省略数；该冷路径不依赖 diagnostics trace，也不暴露宿主地址。
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
- `InterpreterBackend`（DVM-54..60）：默认 `switch_dispatch` 保持原路径；显式
  `threaded` 走 FastCode 单函数稳态循环。同帧热路径把 `ip`、`Slot* regs`、
  `ticks`/`executed` 放在局部变量，只在 bridge、异常、压帧/弹帧、yield、
  `EnsureInitialized` 回写。GCC/Clang 每个 handler 尾部 indirect goto 下一条
  opcode（不再回到共享 fetch 间接跳）；MSVC 用 `fetch_at` + 稠密
  `FastHandler` switch loop。压帧/弹帧/pending 才回到 `Run()`。家族体以 `.inc`
  拼入 `interp_threaded.cpp`。Clang 不允许 computed goto 跨过非平凡析构，
  因此 invoke 的参数 vector、`MethodMonitorScope` 与 `NativeFrame` 必须在
  尾跳前结束作用域（析构顺序与正规 `goto yield` 相同）。`force_all_bridge`
  把每条指令桥回旧 `Step()`。
  直线/对象/invoke 直达 handler 不再读 u2，也不再检查预检已证明的寄存器边界，
  但保留 tag/wide-pair/zero-as-null；算术复用 `ExecuteArithmetic`。类型、字段、
  数组元素类别与 invoke shorty 在执行锁内首执行缓存并 checked→fast。
  packed-switch 按 first-key O(1) 索引；invoke 参数走 ≤8 槽栈缓冲（超长 range
  才退回 vector）。invoke-virtual 用缓存 `vtable_index` 直取 vtable。
  解释 invoke 经 `Frame::fast_ip` 恢复，避免同帧 `IndexForDexPc`。
  `invoke_checked` 在 shorty 之后证明 J/D pair（k35c 还要求列出的 word 连续）。
  `<clinit>` / intrinsic 调用不跨 `AddClass` 悬挂 `LinkedClass&`。
  对象/invoke 慢路径仍调用共享 `InvokeIntrinsic`/`PushInterpretedFrame`/
  `EnsureInitialized`，字段与数组体在 `.inc` 中另有一份，语义对照旧内核夹具。
  DVM-58 将后端接入受检 Profile/CLI/Scenario 链；默认仍为 `switch`。
  DVM-59/60 闭合 Precheck 与 invoke wide 安全、FastCode 与 Precheck 结构反例
  诊断逐字一致、dexasm 双后端与 tick/trace 对照。exact-title gate 不是本轮
  验收，能力保持 `partial`。
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
- DVM-62..69 bounded reflection foundation 已闭合：linker/loader 是 Class、loader role、
  declared member 与 Dalvik system metadata 的唯一事实源；ReflectionRuntime 统一
  wrapper、access、conversion、invoke、Constructor/Class 实例化与 Field slot 操作，
  `reflect.Array` 使用真实 typed array store。generic/annotation proxy、多 loader 与
  动态 definition 仍是明确非目标，不返回 neutral placeholder。
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
FastCode 单函数稳态循环在 `interp_threaded.cpp`（`#include`
`interp_threaded_{straight,object,invoke}.inc`），显式执行 context
的选择、校验、thread-local 活跃路由与 `VmExecutionLock` 在
`interpreter_context.cpp`，诊断 ring/query/JSON 在 `diagnostics.cpp`，宿主线程
生命周期在 `vm_threads.cpp`。
`intrinsics/catalog.cpp` 显式聚合目录；每个 Java 类仍由唯一 TU-private
`Declare_*()` 同址声明形状与 handler，物理目录固定为 catalog 加 lang、classloading、
reflect、io、util、regex、zip、nio、net、xml、concurrent 共 11 个 API family TU。
family 只向 catalog 暴露 `Append*()`；family TU 可超过通常 800 行，但禁止
misc/common/all 巨石与静态自注册。非 Android Java family 全部归 core；需要平台事实的
少量既有行为只消费 `CoreIntrinsicServices`，不得依赖 integration。
pinned Luni `java.lang` 的确定性 public/protected 顶层 class shape 位于
`data/dexvm/api19-java-lang-surface.json`；用 `tools/dexvm_api19_surface.py` 从本地
API-19 源码生成/校验，再由 `tools/dexvm_stub_gen.py --surface` 生成当前 builder 骨架。

## 不变量

- 依赖只指向 core/loader/runtime‑jni；不依赖 runtime/framework（intrinsic 经
  `PlatformClassProvider` 形态由装配方注入 registry/目录）。
- guest 不可信：全部索引/偏移/tag 受检；未实现 opcode/intrinsic/native 记账
  且明确失败，绝不静默返回默认值。
- 语义出处：逐 opcode 对照 AOSP `vm/mterp/c/OP_*.cpp`（一致性夹具注释记录），
  分歧按 07 §5 仲裁。无 JIT、不改写指令流（quickening 红线）。
- 对象非移动，句柄在对象存活期稳定；清扫后可复用，但 guest identity hash 不复用。
- GC 根集必须覆盖全部 context 的全部帧 tagged ref、last_result/caught、pending/
  exit result、静态 ref 槽、JNI local/global、Thread 对象、intern/class 对象以及
  Android session 显式登记的外部长期引用。intrinsic 宿主状态必须通过
  `RegisterIntrinsicStateTable` 具名注册：持 `VmObjectRef` 的表提供 trace，所有表
  必须提供 sweep，需要随 `Object.clone()` 复制的表再提供 clone；现有
  throwable/builder/collection/io 表均走该唯一生命周期通道。
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
  `Run`/`Step`/`StepThreaded`/`Tick`/invoke 与字段家族/`EnsureInitialized`/
  `PushInterpretedFrame` 显式传引用；逐指令不得重查 thread-local 路由。
  `Execution()` 只允许出现在入口与冷路径（异常、诊断、native 帧标记）。
  一次活跃调用内 context 不可切换（`InterpreterExecutionScope` 强制），
  该引用因此在整个 `Run` 期间恒定。
- FastCode 的 checked→fast 翻转只能在 `VmExecutionLock` 内发生；缓存只保存稳定
  linker ID、数组元素类别及宿主数值边表，不保存 guest 引用/host 裸指针，GC 不感知。
- stop 继续由 `Tick()` 在每条指令做 relaxed load；DVM-57 未发现需要牺牲该精确
  语义的采样证据，因此不引入 mterp alt-table 对应物，RequestStop 契约不变。

## 尚未实现（记账可查）

- bounded reflection foundation 已完整闭合。仍未实现且保持明确边界的是 generic
  reflection、annotation proxy、Proxy、多 ClassLoader namespace、动态 DexClassLoader/
  defineClass 与资源 classpath 扩展。

## 测试

`tests/dexvm/interpreter_tests.cpp`（dexasm 夹具一致性：未触达缺失层级类不阻断
whole-DEX 启动、首次触达明确失败/survey 才记账；core catalog 唯一性与
代表类签名集合、System property 默认值/读写/异常、intrinsic builder 装配校验与
声明即绑定、重复方法拒绝、
直调与声明未实现的重复 miss 记账，算术边界、控制流、
数组、字段、三种 dispatch、clinit、跨帧异常、栈溢出、tick/heap 预算、两个
显式执行 context 交错调用的帧/异常/tick/monitor 隔离、跨线程 clinit 等待；
DVM-52 默认关闭、固定 ring 覆盖/筛选/采样、语义事件族、跨 context stack 与
schema-1 JSON；DVM-76 缺方法致命错误在 switch/threaded 两后端保留最内层优先的
class/method descriptor + DEX PC 调用链，且清理后 context 无残留帧）；
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
边表及畸形输入拒绝，失败诊断字符串与 Precheck 口径一致）；`tests/dexvm/interpreter_tests.cpp` 的 DVM-54/60
dexasm 夹具按 switch/threaded 双后端跑返回、异常、tick 与 trace；`force_all_bridge`
永久保留为 Step() 桥回归锚点；threaded 微基准覆盖直线、静态字段、
invoke-static，以及数组 aget/aput、packed-switch、iget/iput、invoke-virtual、
wide 算术与 instance-of，1 轮预热后 5 轮中位数只报告、不设时序断言；畸形
`k35c`/`k3rc`/`iget-wide` 与 invoke-wide 非连续/越界 pair 在两后端都抛
`invalid_register` 且 diagnostic 一致。`tests/dexvm/dex_code_tests.cpp`、
`tests/dexvm/dexasm_readback_tests.cpp`、
`tests/dexvm/gap_survey_tests.cpp`（survey 开/关对照：关闭即失败、桩答中性值、
命中计数、工作单排序）。
