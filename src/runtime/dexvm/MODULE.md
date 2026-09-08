# 模块：runtime/dexvm

## 职责

有界 Dalvik 字节码解释器（ADR-0017、`docs/design/dexvm/`），负责类链接、统一 Java
对象模型、解释器内核（帧/分派/三路 invoke/异常展开/`<clinit>`）及 java.* core
intrinsic。解释应用 DEX 与受审 API 19 curated Boot DEX；完整平台库仍不装载。

## 公共 API

### 链接、类加载与反射

- `DexClassLinker`：按 `RegisterIntrinsics` → `RegisterBootDex` → `RegisterDex` → `Link`
  装配。Boot/Application 各有独立 `DexUnitId`、字节、image 与 type/method/field cache；
  解释方法及 encoded value/catch/string/type/field/method 索引必须使用其所属 unit，禁止
  隐式落到 application image。Boot jar 中 class_def 全量登记；与 intrinsic 同类时 DEX
  提供 class/field/hierarchy，精确同签名 intrinsic method 保持 overlay，其余方法解释执行，
  未 overlay 的 native 方法拒绝。intrinsic 引用的父类/接口允许在 BootDex 注册后由 Link
  解析；接口继承但未声明的方法使用 abstract Miranda vtable 槽，不加入 own members。应用 `classes.dex` 中真正的平台前缀类仍被忽略，但 APK 自带的旧
  `android.support.*` 支持库仍由 application loader 定义。APK class_def 全量登记，但层级、字段
  布局、vtable/iftable 仅在首次解析、实例化或调用时完成；未触达可选类的缺失层级不阻断启动，
  触达后的循环继承、缺失层级、final 覆盖、interface-as-super 和不可覆盖 intrinsic 方法明确
  失败，survey 仅为真实触达的平台缺口合成并记账。常量池解析缓存
  `ResolveTypeIndex/ResolveMethodIndex/ResolveFieldIndex`；数组类按需合成；`IsAssignable`
  覆盖类、接口、数组协变及数组到 `Object`/`Cloneable`/`Serializable`。`PrecheckMethod`
  懒校验未知 opcode、寄存器（含 `k35c`/`k3rc` 参数与 wide pair）、分支/payload 目标和
  move-result 位置；long 的 2addr shift distance 是单槽 int，不能按 wide pair 预检。规则子集对照 AOSP `CodeVerify.cpp`，不做全量数据流。
- `ClassNameCodec` 与 linker reflection metadata（DVM-62）：唯一受检的 method/type
  descriptor 拆分及 `Class.getName`/binary-name 转换入口。每类发布 defining-loader role、
  direct interfaces、flattened iftable，以及按 DEX/intrinsic 声明顺序排列的 own methods/
  fields；`LinkedMethod` 保留 direct/static/virtual/interface 类别，class/method/field raw
  access flags 全程保真。数组 loader 跟随 component，primitive class 包含 `V`。这些 metadata
  是 DVM-63..69 的唯一事实源，declared-member 查询不得以 vtable 代替。
- `ClassLoaderFacade`（DVM-63）：每 VM 一个稳定 application `PathClassLoader` 和一个
  `BootClassLoader`，parent 为 application → boot → null；只消费 linker 的唯一 class
  directory，以 initiating-loader bit 区分“已知”和“由该 loader 发起”。`findLoadedClass`
  只查询，不链接、初始化或合成；`loadClass` 校验 binary name，按 boot/application 边界委托，
  忽略 API 19 `resolve`。`Class.forName(String)` 使用真实 interpreted caller loader 并初始化；
  三参数入口将 null 视为 system/application facade，显式 boot/application/custom role 仍只查
  同一目录。不支持动态 classpath、多 namespace 或自定义定义权限。
- `ReflectionRuntime`（DVM-64..65）：按 declaring class 缓存 immutable Method/
  Constructor/Field metadata，是唯一 guest wrapper factory。wrapper 仅保存 declaring
  `Class` 与 opaque declared-order ordinal，不暴露 `VmMethodId`/`VmFieldId`/DEX index，也不缓存
  guest ref；每次查询重新物化可回收对象，accessible flag 和类型数组归 wrapper 所有。
  declared/public 查询中，
  public Method/Field 按 class、superclass、direct interface 稳定递归聚合去重，Constructor
  只查本类。三类 wrapper 共用成员数组/ordinal 查询骨架及 API 19 modifier/type-name、hashCode、
  exact declaration toString；语义相等的新 wrapper 不使用 Object identity hash。
- `ReflectionCodec` / invoke runtime（DVM-66）：统一 ref 可赋值、primitive wrapper
  unbox/widen 和返回 boxing，禁止 narrowing 与 boolean/numeric 互转。`Method.invoke` 从活跃
  frame 获取真实 caller，按 `(defining_loader, package)` 判断 public/private/package/
  protected（含 protected receiver），再按 declared invoke kind 精确选择 direct/static target，
  或按 receiver vtable 选择 virtual/interface target。target throwable 仅由持有原异常强引用的
  `InvocationTargetException` 包装，不重新物化。

### Core intrinsic 与运行时状态

- `CoreIntrinsicCatalog(services)`：聚合 `intrinsics/` 中按 API family 同址定义的声明和
  handler，覆盖 Object/String/Class、Throwable 栈 native、pinned
  libcore 的 7 个 `java.lang` 顶层接口（`Appendable`/`AutoCloseable`/`CharSequence`/
  `Cloneable`/`Comparable`/`Readable`/`Runnable`，统一位于 `java_lang.cpp`）。
  `Iterable` 和 java.io 序列化接口来自 BootDex。
  `java.lang.Enum` 语义对照 pinned libcore `Enum.java`：name/ordinal 为可继承的声明式实例槽，
  构造器 `(String,I)` 写入；查询方法 final、`toString`
  可覆盖、`clone` 恒抛 `CloneNotSupportedException`；`getDeclaringClass` 按直接父类判断；
  `valueOf(Class,String)` 先初始化，再从本 enum 同型 static 常量字段活值按名匹配；
  `getSharedConstants(Class)` 为 Boot DEX 提供按 `ACC_ENUM` 字段收集、ordinal 排序/连续性
  校验的 typed array，缓存作为 GC 强根；null 抛
  `NullPointerException`，非 enum/未命中抛 `IllegalArgumentException`。enum `values()` 经数组
  `Object.clone()` 浅拷贝。
  StringBuffer/StringBuilder 用 descriptor 参数化的同一声明；仅含 `()`/`(String)` 构造器的
  残留平台异常共用基类初始化入口；java.lang 异常家族来自 BootDex。
- 集合家族（DVM-103）：List/Collection/Map、实现、视图、迭代器、Arrays/Collections、
  Observable/Observer 与 Random 均归 API 19 BootDex；对象字段和数组是唯一状态，
  不保留 CollectionRuntime 或集合 intrinsic。Tree/Sorted/Navigable、Weak/Identity/Enum
  及 concurrent 容器一并选入；迁移不承诺全部序列化、XML 或并发调度长尾。
- 执行器（DVM-110）：ScheduledThreadPoolExecutor/FutureTask、执行器接口、普通工厂包装类、
  completion service 与拒绝策略均归 BootDex，删除旧 FutureTask/串行 executor handler。
  延时、FIFO、定频/定延迟、取消/中断、异常和 shutdown 策略由原版 Java 处理；队列、任务、
  结果与等待者形成普通 GC 图。worker 是真实 guest/host 线程，Clock 与 AQS/Unsafe park
  复用现有机制，不建立第二套 scheduler。普通单线程/固定池/定时池工厂受检，privileged
  工厂、动态安全上下文和高争用压力测试未纳入，不改变单通道解释执行边界。
- BootDex 与 intrinsic 字段合并后，own_static_fields 必须以 DEX 声明顺序为首部，
  encoded_array 初始值逐项对应同序字段；不得让 overlay 声明顺序改变整数、wide 或引用初值。
- ICU（DVM-122）：日期、日历和 pattern 算法归固定 API 19 BootDex；审计登记的
  LocaleData/ICU/TimeZone/NativeDecimalFormat native 经 guest JNI 调用 payload ICU4C 51。
  formatter 逻辑令牌完全位于 guest SO，owner 清扫与 VM teardown 调用 close；宿主不保存 ICU 对象。
  固定 ICU 数据挂载为 `/system/usr/icu/icudt51l.dat`，不得恢复嵌入数据或宿主 ICU 链接。
  Locale ISO 语言/国家枚举也来自固定 ICU，缓存与防御性 clone 由 BootDex ICU Java 持有。
  CoreIntrinsicServices.default_timezone（默认 GMT）仅用于 Java 默认时区未设置或被重置时，
  getDefault/setDefault 共用 DEX 静态字段，clone 与缓存均按 VM 隔离。
- `IoRuntime`（DVM-79/91/104/109）：只拥有文件资源与增量字符解码器，不保存 Java 引用。
  Input/OutputStream、Reader/Writer、内存/Buffer/Filter/Data/Pushback/LineNumber/Sequence 流
  归 API 19 BootDex；字段和数组是唯一普通流状态，删除 wrapper-adoption 宿主转移接口。
  Object streams、描述符/字段辅助类的协议与 handle 均执行原版 Java，source/sink 和
  handle 通过普通字段形成 GC 强边；禁止恢复 C++ 协议。字符解码为宿主自有的标准六字符集状态，
  不链接 ICU converter。
  available 不等待底层新数据，嵌套调用抛回原 throwable；不重新创建异常。
  File 仅使用注入的 `IoFileSystem`，具体 VFS 在 integration adapter；相对路径不读宿主 cwd，
  mkdir 不递归，权限操作不伪造成功。逻辑 FileDescriptor 不保存 host/native fd，借用关系不变。
  InputStreamReader 使用固定 ICU 51 标准字符集增量解码，并持有 Reader.lock（source）的
  guest monitor；FileReader 经 FileInputStream 和该适配器读取。FileWriter 仍是有界文件适配器。
  Externalizable 协议 1/2、数组、默认 UID、私有读写及替换回调均执行原版 Java。
- 后续纯 Java 家族（DVM-104）：Objects/StringTokenizer/BitSet/MathContext、事件/beans、
  CountDownLatch/Semaphore/CyclicBarrier、普通 atomic/数组、ChoiceFormat/MessageFormat、
  X500 名字/DER 与指定 key spec 由 BootDex 拥有。AtomicLong 只保留 VMSupportsCS8 native，
  普通 CAS 复用 Unsafe 与执行锁；同步器复用真实 guest 线程、park/monitor 与统一 Clock。
  Charset/String 标准六编码及 Locale 大小写调用固定 ICU；完整 Charset provider/encoder/
  decoder API 和完整数字 formatter 不因选类自动可用。
- `BigIntRuntime`（DVM-104/106）：为 ASN.1 标签键和证书长序列号提供每 VM 单调逻辑
  令牌和规范化小端 magnitude。17 个分配/拷贝/比较/符号/编解码原语受检，单次输入最多
  1 MiB；BigInt owner 清扫释放令牌，其余 18 个 native 明确失败。不提供大数密码学算术。
- `ZipRuntime`（DVM-79）：管理 archive、当前 entry bytes/cursor 和 close 状态，复用 loader 的
  严格 ZIP parser/inflate，并由 intrinsic state-table hook 清扫。
- `NetworkRuntime`（DVM-88）：管理 InetAddress endpoint、Socket、stream、datagram 的 per-VM
  state，guest ref 仅经具名 state table trace/sweep。core 只用注入的 `NetworkPolicy`/
  `NetworkTransport`，默认离线；host allowlist、TLS、datagram 分别校验，未注入或未授权明确
  失败。core 不创建 host socket，不读系统 DNS/代理/证书库，也不依赖 Android connectivity。
  teardown 必须在 transport 存活期关闭 channel，再清空 endpoint/stream/packet state。
  API 19 `URL(String)` 对 http/https/file 绝对地址只做解析并保存 protocol/authority/host/
  port/path/query/ref，getter 与 external form 不进入网络策略；`openConnection/openStream`
  才是 I/O 边界：HTTP(S) 默认离线抛 `UnknownHostException`，policy 开启但未实现 HTTP
  façade 时明确失败；file URL I/O 尚未接入 VFS，抛 `IOException`。
  `URLEncoder/URLDecoder` 的纯 UTF-8 form codec 不进入 NetworkRuntime，百分号转换只调用仓库
  固定的 Boost.URL；不支持的 charset 明确失败。
- `NioRuntime`（DVM-82/83）：以 JNI object identity 索引 Buffer state；heap array、direct
  guest memory 与 view 共用 storage，view 仅复制 position/limit/mark/order；`slice`/
  `duplicate`/`asReadOnlyBuffer` 保留 receiver concrete class，不把 direct view 物化为 heap
  concrete。backing array 是 GC 强边；owner 死亡时 sweep，clone 共享 backing 并复制 cursor。
  core 仅用注入的强类型 guest-address allocate/validate/read/write/release 接口；缺 allocator
  时 `allocateDirect` 明确失败。scoped memory operation 对 direct Buffer 返回 position 地址，
  对 heap/view 使用临时 guest memory 并按方向 copy-back；成功或异常都释放，且不移动 cursor。
  bulk get/put 共用 range/remaining 预检，但保留方向、异常类型和搬运语义。
- `java.lang.Thread`：对照 pinned libcore `Thread.java`/`VMThread.java` 与 Dalvik
  `Thread.cpp`/`Sync.cpp`。root context 持有 id=1 的 `main` Thread 强根；新线程构造时分配
  per-VM stable ID。`start()` 按实际 Thread class vtable 虚派发 `this.run()`，仅基类
  `run()` 转发 target Runnable。core façade 覆盖 8 个构造器、currentThread、属性、
  interrupt、join/sleep/yield/holdsLock、ThreadGroup 枚举、Thread.State、park 单许可、
  interrupt action 和有界 daemon flag；priority 不映射 host scheduler，daemon 不驱动 session
  退出。context ClassLoader 复用稳定 application/bootstrap identity：root 默认 application，
  child 继承创建者，setter 只更新 guest 字段且允许 null，不增加 namespace。
  Thread.localValues 供 BootDex ThreadLocal 使用；Runtime.availableProcessors 返回执行锁
  对应的单 guest 执行通道事实 1。System.nanoTime 使用统一单调 Clock 的毫秒值转纳秒，
  未注入 Clock 明确失败，不读取宿主时间。
- `java.lang.Object.clone` 是可覆盖 virtual intrinsic（不是 `internalClone`）：
  `instanceof Cloneable` 失败抛
  `CloneNotSupportedException`，成功则由 `Interpreter::CloneObject` 浅拷贝 payload 及
  builder side state；集合 clone 由 DEX 复制字段/数组。`JavaObjectModel::CloneObject` 对照 AOSP `dvmCloneObject`，分配
  新 identity，仅复制 `vm_instance` slots 或数组元素；string/class/host-backed 明确失败。
  JNI `NewObject` 的 application identity 回入解释器时，经注入的 lazy layout resolver 建立完整实例槽并保留
  identity；intrinsic host object 仍属 external/专用 store。`String` 保持 VM 的唯一 UTF-16
  owner；两个 `String.format` intrinsic 仅实现 API 19 `Locale.getDefault`/`StringBuilder`/
  `Formatter` 包装链。格式解析、虚 `toString`、格式异常、Locale 与日期转换执行 BootDex，
  `AbstractStringBuilder`/Appendable 和整数 append 只桥接既有 builder 状态；不借用 host
  printf/locale。
- 生产 `JavaObjectModel` 经 `JavaObjectInterop` 复用 session `JniObjectArrayStore`，使
  DexVM/JNI 的 Object[] 创建、读写、clone、sweep 共用 identity/store；class identity 双向
  适配由 integration 注入，不依赖 `JniClassRegistry`。仅 isolated fixture 使用内部 fallback。
- `IntrinsicClassBuilder`：以 `Class/RootClass/Interface` 声明类型，以
  `Constructor/StaticMethod/VirtualMethod/FinalMethod`、`InstanceField/StaticField`、
  `ConstantInt/ConstantString` 和 `ClassInitializer` 声明成员。普通类默认继承
  `Ljava/lang/Object;`，只有 `java.lang.Object` 用 `RootClass`；声明即持有实现，空 handler
  拒绝，未实现入口由 `UnimplementedStatic/Constructor/Virtual/Final` 显式进入 miss/记账。
  `Build()` 校验 descriptor、`<init>`/`<clinit>` 保留名、重复成员、interface 实例字段及整型
  常量范围。
  `BoundInstanceField/BoundStaticField` token 在每个 linker 注册时绑定到本地 `VmFieldId`；
  `IntrinsicCall` 提供受检参数、receiver 和字段访问，handler 不得字符串查字段或读写裸 slot。
  `IntrinsicEnumBuilder` 统一生成平台 enum 的常量、`$VALUES`、初始化及精确类型
  `values/valueOf`；payload 只经显式 factory/hook 扩展。
  DEX/Dalvik flags、校验 mask 和 API 19 reflection mask 统一由 `access_flags.h` 发布。

### 对象、解释执行与线程

- `JavaObjectModel`：session 级 `VmObjectRef` 句柄空间（0=null）。VM 实例与 `Object[]` 自有
  存储，String/primitive array 委托注入的 `JniStringStore`/`JniPrimitiveArrayStore`；
  `CloneObject` 以新句柄浅拷贝实例槽/数组元素。GC 为精确、非移动、STW mark-sweep；记录保存
  `reserved_bytes`，回收预算并以确定性 LIFO 复用记录/实例槽/数组槽，访问空闲记录明确失败。
  BootDex WeakReference 的 Reference.referent 不参与强标记；STW 清扫前清空死 referent，
  按 API 19 ReferenceQueue 链入队并唤醒等待者，不保留宿主弱引用侧表。DVM-109 选入
  SoftReference：普通 GC 保留 referent，分配压力 GC 清除仅软可达对象并入队；Phantom 未选入。
  DEX 字符串常量与 Class object 为不朽强根；显式 String.intern 使用弱 canonical 表，保留
  首次 receiver 身份，常量加载可提升为强根，清扫删除死条目后才复用 handle。
  JNI weak global 非根且随目标清空；默认 heap 64 MiB，
  `SetEmergencyReserve` 为 VM 隐式异常构造提供最多 64 KiB 应急分配，嵌套不重置额度；
  异常构造作用域恢复原模式，额外 32 个内部帧保证栈满时仍能构造 StackOverflowError。identity hash 独立于可复用句柄：普通对象使用
  不回收的 per-VM 序列，Class 按 descriptor 稳定派生。`Object.hashCode` 与
  `System.identityHashCode` 经 `IdentityHashCode` 使用该身份（后者绕过 override，null=0）；
  默认 `Object.toString`
  虚调用 receiver `hashCode()` 后转小写十六进制，异常沿 existing-pending 传播。
- `Interpreter`：`Call(method, args)` 在当前 host thread 执行至完成，返回 `VmCallOutcome`
  （值或未捕获 Java 异常、消息和栈）。寄存器带 uninit/cat1/wide/ref tag，并允许零值放宽；
  默认每指令 1 tick、最大 512 帧，溢出产生真实 `StackOverflowError`。invoke 分解释压帧、
  intrinsic 拥有型实现和
  `NativeMethodBridge` 三路；缺实现/bridge 均记账并明确失败。`CreateExecutionContext` 建立显式
  context，`Call(context, ...)` 隔离帧、pending exception、tick、结果和 monitor recursion，
  同时共享 linker/object model/catalog；默认 `Call` 使用原单线程 context。
  `RequestStop(context)` 每指令检查并以 `thread_stopped` 退出；host join 后可用
  `UnwindStoppedExecutionContext` 清理重入遗留帧，未 stop 或尚有 native frame 时拒绝，活动栈
  也不能被 `DiscardExecutionContext` 丢弃。不可恢复 `DexVmError` 在清帧前附一次最多 64 帧的
  guest stack（含 context/thread、opcode、可用 method index、descriptor 和 DEX PC），不依赖
  diagnostics，也不暴露 host address。
- `InterpreterConfig::diagnostics` / `Interpreter::Trace`（DVM-52）：容量 0 时精确关闭；启用后
  使用固定事件 ring，记录 instruction、method、exception、class-init、monitor、native 和 GC
  的整数事实，支持采样及受检 filter/
  limit，热路径不分配、格式化或保存 host pointer。`RenderDexVmTraceJson` 输出 schema 1。
  `Interpreter::StackSnapshot` 在取得执行锁后枚举 context 的 Java 帧、DEX PC、tick、pending
  exception，并关联 thread id/name/status；这是等待 safe point 的停界查询，不是异步抢占；
  阻塞/try-lock 入口仅共用锁内投影，各自保留锁获取协议。`RenderDexVmStacksJson` 输出 schema 1。
  `TryTrace`/`TryStackSnapshot`/`VmThreadRuntime::TrySnapshot` 用于 stall snapshot，任何相关锁
  无法立即取得即返回 busy。`VmMonitorTable::TrySnapshotAll` 分开报告 entry waiter 与
  `Object.wait()` notify set，后者不构成指向 owner 的 wait-for edge。
- `FastCode`（DVM-53）：从通过 `PrecheckMethod` 的原始 u2 流确定性派生，预解码 opcode、宽度、
  operand、DEX-PC 索引、受检 branch target 及 payload 边表；按 `LinkedMethod` 懒缓存、只读
  共享，不保存 guest ref、不计 guest heap，也不改写 DEX。
- `InterpreterBackend`（DVM-54..60）：默认 `switch_dispatch`；显式 `threaded` 使用 FastCode
  单函数循环。稳态把 ip/regs/ticks/executed 留在局部，仅在 bridge、异常、帧变化、yield、
  初始化时回写；GCC/Clang 用 indirect goto，MSVC 用稠密 switch，`force_all_bridge` 可逐指令
  回旧 `Step()`。热路径省略预检已证明的边界，但保留 tag/wide-pair/zero-as-null；类型、字段、
  数组类别与 invoke shorty 首次在执行锁内缓存。packed-switch O(1)，invoke 参数优先用 8 槽
  栈缓冲，virtual 通过缓存 vtable index；`invoke_checked` 校验 J/D pair。两后端共用
  `InvokeIntrinsic`/`PushInterpretedFrame`/`EnsureInitialized` 语义，运行期 target 统一经
  `SelectInvokeTarget`，边界统一经 `MethodShape` 校验。Profile/CLI/Scenario 可显式选择，默认
  仍为 switch；exact-title gate 未闭合，能力为 `partial`。
- threaded 家族体由 `interp_threaded_{straight,object,invoke}.inc` 拼入。Clang computed goto
  不得跨越非平凡析构，因此 invoke 的参数 vector、`MethodMonitorScope` 和 `NativeFrame` 必须在
  尾跳前离开作用域；对象/invoke 慢路径继续共用旧内核，字段/数组体保持语义对照。整数除零不与
  浮点语义合并，`<clinit>`/intrinsic 调用不得跨 `AddClass` 悬挂 `LinkedClass&`。
- `VmExecutionLock`（`Interpreter::ExecutionLock()`）：所有 `Call`/`EnsureClassInitialized` 入口
  获取的可重入全 VM 执行锁；阻塞时用 `ReleaseForBlocking`/`ReacquireAfterBlocking` 按原深度
  释放/恢复。可注入 host thread
  id 与 blocked observer，回调在锁外。任一时刻仅一个线程解释字节码，linker cache、object
  arena 与 intrinsic state 因此单写；所有 handler 在锁内。该锁也是 GC STW 边界，GC 仅在
  六类安全分配指令前触发，阻塞前不得在 C++ 栈留下未根化的新句柄。
- `VmMonitorTable`（`vm_monitors.h`，`Interpreter::Monitors()`）按 AOSP `vm/Sync.cpp`
  `waitMonitor` 语义实现 session 对象 monitor，以 execution-context token 为 owner。
  `Enter`/`Exit`
  提供跨线程互斥；`Wait` 校验 owner、登记 wait-set、保存并清零 recursion、完全释放 monitor，
  唤醒后先重获 monitor/恢复 recursion，再返回或抛 `InterruptedException`。非 owner 操作抛
  `IllegalMonitorStateException`；唤醒源为 notify/notifyAll/Clock/Interrupt/Shutdown。
  timed wait 只用 `SetTimeSource` 注入的 Clock，无 Clock 明确失败；条件变量轮询不参与截止判定。
  根 context（`kRootLifecycleToken`）可在有界 peer 窗口后经 `SetClockAdvance` 推进到 deadline；
  worker 仅在 `SetClockDriverBlockedProbe` 证明 driver
  阻塞时推进。多 waiter 串行补差，notify/interrupt 可先赢；未注入快进钩子则保持停泊语义。
  thread 结束时 `ReleaseAll` 清理 monitor 与 wait-set 资格。
- `VmThreadRuntime`（`vm_threads.h`）：一个 guest Java thread 对应一个 `hal::StartHostThread`
  host thread 和独立 execution context，共享 linker/object/JNI identity。`Start` 在调用方解析实际类的虚
  `this.run()`；ID、Thread identity 与 context 显式映射。join/sleep 释放执行锁并使用上述
  Clock/deadline 协调；interrupt 写 context interrupt state 并唤醒 wait/join/sleep。
  `VmThreadSnapshot` 发布 `none/sleeping/joining/monitor` wait state，只描述真实 park 区间。
  host `Yield()` 用 progress generation 确认一次 handoff，guest yield 不承诺公平。Shutdown 先
  stop、join，再
  幂等展开 context 并保留记录。未捕获异常先经线程 handler、再经 VM 默认 handler；handler
  异常忽略，其余异常/VM error 写入 `TakeFailure()`，由 lifecycle 在帧边界上报。stack trace
  复用 safe point；Java `getStackTrace/getAllStackTraces` 只投影当前 VM 的真实线程。system/main
  ThreadGroup 仅提供稳定归属、名称与存活枚举；`Thread.parkFor/parkUntil/unpark` 复用 monitor/
  Clock，Thread.State 仅投影已有 wait state，deprecated stop/suspend/resume/destroy 明确未实现。
  每个 child 挂接独立 A32 CPU、guest stack、Bionic TLS/thread-info、process thread id 与 JNI
  local frame，因此 native frame 存活时可停泊。class init 对同 context 重入放行，其他 context
  释放执行锁等待；完成、失败、teardown 都唤醒。`SetStaticFieldBits` 仅写已初始化的真实 guest
  static field，供 ADR-0022 精确 preset；任何形状或槽位不匹配均失败，不绕过 `<clinit>`。
- DVM-62..69 reflection foundation 已闭合：ReflectionRuntime 统一 wrapper、access、conversion、
  invoke、实例化和 field slot；`reflect.Array` 使用真实 typed array store。generic/annotation
  proxy、多 loader 与动态 definition 不返回中性占位。
- `System.getProperty(String)`/`setProperty(String,String)` 与 primitive wrapper property API
  共用 per-VM 属性表；separator 默认值固定为 API 19 guest 事实，未知键返回 null，不泄漏 host 属性。
- Gap survey 默认关闭。`EnableGapSurvey()` 仅为缺失的已触达平台类/方法合成 0/null/void 桩并
  逐次记账；`GapSurveyHits()`/`RenderGapSurveyJson()` 输出按次数排序的工作单。调用方必须标明
  survey 结果不是兼容性结论；关闭时缺失能力明确失败。survey 桩的 null/0 若触发 host accessor
  `object_model_failure`，转换为 guest NPE 以继续收集；非 survey 保持硬失败。流程见
  `docs/playbook/NEW-TITLE.md`。

## API 19 Unsafe

DVM-101/ADR-0031：`Interpreter::Unsafe()` 拥有 per-VM `UnsafeRuntime`，字段查询只保存
稳定 `VmFieldId` 元数据并返回逻辑令牌；访问校验 receiver/type，复用唯一字段槽与 JNI
数组存储。数组 base=16，scale 按 A32 元素宽度，拒绝错类型、未对齐和越界访问。
所有 plain/volatile/ordered/CAS 入口要求持有 `VmExecutionLock`；CAS 期间禁止分配、
回调或阻塞。引用写入保留 GC 标签和可赋值性，不把逻辑 offset 转成任何宿主/guest 地址。
令牌不持反射 wrapper 或对象强根，不因 wrapper 回收失效。
`tests/dexvm/unsafe_tests.cpp` 与 `fixtures/unsafe.dexasm` 验证字段互通、CAS、GC、
array、caller、allocation、Clock/permit 与真实 BootDex atomic/AQS 初始化。

## 文件分工

- linker：`class_linker_internal.h` 持有 `DexClassLinker::Impl`；注册/布局/vtable 在
  `class_linker.cpp`，解析与
  assignability 在 `class_linker_resolve.cpp`，预检与 FastCode 缓存入口在
  `method_precheck.cpp`，构建在 `fast_code.cpp`。
- interpreter：主循环在 `interpreter.cpp`，threaded 循环以 `#include`
  `interp_threaded_{straight,object,invoke}.inc` 组成；context/执行锁在
  `interpreter_context.cpp`，诊断在 `diagnostics.cpp`，gap survey 在 `gap_survey.cpp`，host thread
  生命周期在 `vm_threads.cpp`。
- intrinsic：`intrinsics/catalog.cpp` 显式聚合；每个 Java 类由唯一 TU-private `Declare_*()`
  同址定义 shape/handler。目录固定为 catalog 加 lang、classloading、reflect、io、util、regex、
  zip、nio、net、xml、concurrent 11 个 family TU；仅以 `Append*()` 暴露给 catalog，禁止
  misc/common/all 巨石或静态自注册。非 Android family 全归 core；平台事实只经
  `CoreIntrinsicServices` 注入；其中环境查询只能读取 integration 受检发布的 guest
  Bionic `environ`，不得观察宿主环境。API 19 java.lang shape 位于
  `data/dexvm/api19-java-lang-surface.json`，由 `tools/dexvm_api19_surface.py` 校验、
  `tools/dexvm_stub_gen.py --surface` 生成 builder 骨架。

## 不变量

- 依赖只指向 core/loader/runtime-jni，不依赖 runtime/framework；平台 registry/catalog 只经
  `PlatformClassProvider` 注入。
- guest 输入不可信：索引/偏移/tag 全部受检；未实现 opcode/intrinsic/native 记账并明确失败，
  不返回伪成功。opcode 对照 AOSP `vm/mterp/c/OP_*.cpp`，分歧按 07 §5 仲裁；无 JIT，不
  quicken 或改写指令流。
- 对象非移动，存活期句柄稳定；回收后句柄可复用，但 identity hash 不复用。
- GC root 覆盖所有 context frame tagged ref、结果/异常、static ref、JNI local/global、Thread、
  intern/Class 及 Android session 注册的长期引用。intrinsic host state 必须通过
  `RegisterIntrinsicStateTable` 具名注册：持 guest ref 的表提供 trace，所有表提供 sweep，
  clone-sensitive 表另提供 clone。活跃 intrinsic receiver/ref 参数自动进入 execution-local
  临时根；跨 nested guest call 的新引用必须以 `Interpreter::RootScope` 栈式保活，不得提升为
  session 永久根。
- GC 只由分配驱动：`gc_watermark_percent` 为 0..100（默认 75，0=关闭），`System.gc()` 为
  合法 no-op。结构化 `runtime.dexvm.gc` 日志/stats 使用确定性 pause ticks，不以 wall clock
  决策。host-backed
  对象仅在声明 `HostStateDestructor` 时清理，恰好一次且不执行 guest finalizer；相关 store、
  monitor、JNI weak 与 identity mapping 随死亡记录清理。
- 一个 guest thread 对应一个 host thread，但解释执行由 `VmExecutionLock` 串行。锁序固定为
  execution → thread runtime → context table；反向获取禁止。native 出向调用记入
  `native_depth`，仅用于 teardown 完整性，不禁止拥有独立 native stack/TLS 的线程停泊。
- `IntrinsicMethodDecl::implementation` 与 `IntrinsicClassDecl::clinit_implementation` 是唯一
  intrinsic 分发通道；linker 只搬运拥有型实现。System 的 5 个平台动作在 integration
  以成员指针补入 core 声明；Date 不再有平台 handler。
- 活跃 execution 只在 `Call`/`EnsureClassInitialized` 入口解析，随后沿 `Run`/`Step`/
  `StepThreaded`/`Tick` 及 invoke/field/init 路径显式传递；逐指令不得查 thread-local。
  `Execution()` 仅用于入口和异常/诊断/native 标记；`InterpreterExecutionScope` 保证调用期间
  context 不变。
- FastCode checked→fast 仅在执行锁内；缓存不持 guest ref 或 host pointer。intrinsic 只声明
  own members；覆盖必须使用 `OverrideMethod`/`FinalOverrideMethod`，普通 VirtualMethod 与
  父签名冲突在链接期失败。invoke cache 按 `(method index, InvokeKind)` 隔离，descriptor 只在
  链接期生成一次 `MethodShape`（ADR-0028）。
- lazy array、primitive class 与 survey method 追加期间，class/method/field/extras 地址稳定，
  活跃 Frame 才可缓存 metadata 引用。owner-attached state 优先使用 `OwnedStateTable<State>`，
  明确 trace/clone policy；session root、owner state、非对象 identity 不得混用（ADR-0029）。
- switch/threaded 可有不同 operand/dispatch 实现，但 target 必须经 `SelectInvokeTarget`，
  intrinsic 参数/返回类别必须经 `MethodShape`。`Tick()` 每条指令检查 stop；缺乏采样证据前不
  引入 mterp alt-table 等价物，`RequestStop` 契约不变。

## 尚未实现（记账可查）

bounded reflection foundation 已闭合；generic reflection、annotation proxy、Proxy、多
ClassLoader namespace、动态 DexClassLoader/defineClass 和 resource classpath 扩展仍明确不支持。

## 测试

- `tests/dexvm/interpreter_tests.cpp`：链接/触达、catalog/builder、执行与异常、双 context、clinit、预算、
  diagnostics/stack，以及 switch/threaded 致命栈诊断。
- `tests/dexvm/vm_thread_tests.cpp` / `tests/dexvm/thread_intrinsic_tests.cpp` /
  `tests/dexvm/vm_monitor_tests.cpp`：host thread、共享
  identity、生命周期与 teardown、Thread API/GC roots、monitor recursion/ownership、统一 Clock、
  interrupt 及 driver-blocked deadline 协调。
- `tests/dexvm/fast_code_tests.cpp` 与双后端 dexasm：解码、branch/payload、畸形输入、返回/异常/tick/trace
  等价，畸形 `k35c`/`k3rc`/`iget-wide` 与 invoke-wide pair 均报 `invalid_register` 且诊断一致；
  `force_all_bridge` 为永久回归锚点。微基准预热 1 轮后取 5 轮中位数，仅报告不设断言。
- `tests/dexvm/dex_code_tests.cpp` / `tests/dexvm/dexasm_readback_tests.cpp`：DEX 读取与往返；
  `tests/dexvm/gap_survey_tests.cpp`：
  survey 开关、桩返回、命中计数和工作单排序。

DVM-104 的 ZIP 适配器调用 BootDex FilterInputStream 构造以保持源强引用；read/skip/available
使用解压后 entry 游标，mark/reset 明确不支持，close 经父类关闭原始源且幂等。

## DVM-105 Cipher 边界

- Cipher/CipherSpi、JCA provider 查找和 Conscrypt AES 普通方法由 BootDex 拥有。
  GuestNativeStatic 仅允许 static/native、无 handler 的显式 JNI 声明；未登记 BootDex
  native 仍拒绝。NativeCrypto 的 11 个 AES 方法进入真实 guest JNI。
- Security 初始化仅配置 AndroidOpenSSL；provider 构造只注册五种 AES transformation
  与 OS entropy service。OpenSSLCipherContext 构造绑定逻辑 token 和 owner。
- GC sweep 只排队，结束 sweep 后经正常 native frame 执行 cleanup，保存/恢复当前异常
  和返回值；teardown 在 guest process 停止前释放余下资源。native context 不持 Java 引用。
- DVM-115/ADR-0043：Throwable、StackTraceElement、java.lang 异常家族、IOException/
  InvocationTargetException 共 53 类执行 BootDex。消息/cause/suppressed/栈为普通 Java 字段，
  原状态表仅保存故障诊断栈；私有对象流回调正常持久化该对象图。nativeFillInStackTrace
  保存方法 ID/dex PC int[]，nativeGetStackTrace 展开普通栈元素；不保存宿主指针，
  无源文件/行号数据时显示 Unknown Source。消息、打印、重复帧与数组复制归原版 Java。

DVM-107：framework Pair/Sparse/ComponentName 与 PrintWriter 的普通算法归 BootDex，
StringBuilder/StringBuffer 共用 CharSequence 区间 append 原语：UTF-16 索引，虚派
length/charAt、null 视为 "null"、自追加取原片段、越界不改变原 buffer。测试覆盖双后端
ComponentName 的短名称打印与 UTF-16/null/自追加/异常边界。

DVM-108：UUID/JCA MessageDigest 和 Conscrypt 摘要普通方法归 BootDex。
TrackGuestNativeResourceField 仅接受实例 long 字段和 static (J)V cleanup，重复登记幂等，
同字段冲突拒绝。普通字段为 mutable token 的唯一事实源；GC 按 (cleanup,token) 聚合全部
已登记字段及 owner，仍有标记 owner 则保留，死亡字段清零，sweep 后调用 JNI 清理。
浅 clone 共享 token 不提前释放，teardown 清零所有字段，失败与未尝试的 cleanup 保留可重试。
JavaObjectModel.VisitLiveObjects 仅枚举已分配记录，调用者持 VM 锁，visitor 不得分配对象。

DVM-109：ObjectStreamClass 的六个 native 委托 ReflectionRuntime，签名读取唯一成员
元数据；构造器使用 per-VM 受检逻辑 token，分配实际子类后只调用指定无参父类构造器。
首个非 Serializable 父类与访问检查由原版 Java 选择，不复制序列化策略；构造异常保持身份，
不套 InvocationTargetException。JNI 风格 constructor lookup 和 hasClinit 触发初始化，
hasClinit 沿类层级查询并按 AOSP 清除失败。静态初始化的非 Error 异常统一包装为
ExceptionInInitializerError（cause 保持身份及 GC 强边）；Error 原样传播，后续访问 NCDFE。
Field.get 的初始化失败通过 Java throw 传播，不能返回错误类型的 Void 值。
VMStack.getClasses 从当前执行栈跳过调用者和反射帧，为原版对象流查询 loader；
Modifier/Void/SoftReference/Proxy 普通逻辑来自 BootDex。未调用的 getFieldL、Proxy 的两个
生成 native、VMStack 其余四个 native 显式未实现，触达记账失败。此迁移不扩展动态代理生成、
自定义类加载器或宿主侧表对象的完整序列化状态。

DVM-110：类型关系将接口视为引用类型，支持 Runnable[] → Object[] 协变；primitive class
仅与自身可赋值，不因合成类形状而成为 Object 子类。threaded 的 intrinsic invoke 捕获
VmJavaThrow 时必须保留 existing 身份，与 switch 后端一致，不重建已有 throwable。
FutureTask 的 ExecutionException cause 以及其 GC 强边由原版结果字段与通用异常机制维护。

DVM-112：API19 ServiceConnection 接口选入 BootDex，沿用普通接口链接、分派与 GC。

DVM-118：Math 的普通算法/随机数状态迁入 BootDex，24 个 native 按 AOSP 的 libm
边界执行；两后端复验舍入、NaN、signed zero、整数边界与原语，不承诺 StrictMath。
服务查询信息及未匹配结果属于上层平台边界，不在 DexVM core 引入服务目录或 Binder。
