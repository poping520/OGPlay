# 模块：runtime/dexvm

## 职责与边界

有界 Dalvik 解释器（ADR-0017、`docs/design/dexvm/`）：负责类链接、Java 对象模型、
switch/threaded 解释、异常、线程/monitor、反射及 `java.*` core intrinsic。只加载应用 DEX
与受审 API 19 BootDex、固定 ROM 骨架元数据；不提供完整 Dalvik/ART、动态 classpath、JIT 或 Android 服务。

## 核心契约

### 链接、类加载、反射

- 装配顺序固定为 `RegisterIntrinsics → RegisterBootDex → RegisterDex → Link`。Boot/App 分别
  保存 DEX unit 与常量池缓存；解析使用方法所属 unit。BootDex 提供 class/field/hierarchy，
  同签名 intrinsic 只 overlay 方法；未绑定 native 明确失败。
  ADR-0158 显式组合由封存 JAR 的 `META-INF/ogplay/member-sources.json` 传入
  `RegisterBootDex`：完整 ROM 形状/字段/clinit 唯一；逐签名选择 ROM 字节码、受审 handler
  或缺口失败。ROM 来源清除旧 handler，失败来源禁用旧 handler 与字节码；额外 intrinsic
  成员、未批准 overlay/native 拒绝。未启用时沿用既有 overlay 契约。
  构建期固定输入、flags、方法体 hash、字段初始化策略并验证依赖；运行时不接受动态配置。
  组合字段逐项选择 rom/failure：前者由 Java 唯一维护，后者保持 ROM 布局但访问经
  is_unimplemented/RequireImplementedField 记账失败；被选字节码不得读写 failure 字段。
  clinit 必须执行原版 ROM，其依赖按常规规则准入；无空初始化或自动跳过。
  ADR-0159 仅对组合普通方法的非唯一 virtual/interface 调用开放：构建封存逐调用点的全部
  implemented/failure 目标 ledger，应用覆写不在构建期集合。direct/static/super、精确接收者、
  failure 字段及动态入口仍严格检查，clinit 传递闭包与默认 selector 不变。
  runtime 不将 ledger 中的 failure 目标升为实现；支持路径须以运行记账零命中验收。

  DEX 解析失败保留 loader 的 reason/offset，并标识 boot/application 来源及输入字节数。
- APK class_def 经同源重复校验；已登记 bootstrap 身份按 parent-first 胜出，不登记或混合
  APK 副本的层级、字段、方法、注解与 clinit，不要求字节码相同。常量池经现有惰性解析
  指向同一父类身份；同包 APK 独有类仍归 application。层级/布局/vtable/iftable 懒链接。
  未触达可选类不阻断启动；触达后
  缺失层级、循环继承、非法覆盖只使触达的类及其依赖类不可用。bootstrap 未提供的类型
  即使位于 android.* 也从应用 DEX 加载；数组按需合成并支持引用协变及 Object/Cloneable/Serializable 关系。
- vtable 按名称和完整签名匹配继承槽，允许重写缩小可见性；父方法 private 不构成重写，
  按 Dalvik 4.4 b/7301030，包私有方法跨包/loader 仍覆盖同名同原型父槽。
  intrinsic 父类遵循相同规则；构成重写时仍拒绝 final 方法及未准入的 intrinsic 覆盖。
  不可访问的覆盖通过注入 Logger 以 first_and_count 限流记录双方类和方法；
  Interpreter.SetLogger 同步给 linker，早期链接的警告暂存并在首次注入时发布。
  调用解析仅找到相反 static/virtual 类别的方法时，经两后端统一转为 guest
  `IncompatibleClassChangeError`，供 Java catch 处理。
- ADR-0150：guest 链接/解析失败使用独立 JavaLinkageError，两后端共用 Java 异常展开，
  `Call` 同样返回真实 throwable。首次类链接失败传播原错误，再使用抛 NCDFE；成员解析失败
  只影响当前指令。缺方法/字段为 NSME/NSFE，种类不符为 ICCE、不可访问为 IAE，
  new abstract/interface 为 InstantiationError，abstract 分派为 AME；惰性结构预检为 VerifyError。
  Dalvik 4.4 final 方法覆盖失败经链接 bail 抛 VirtualMachineError（Class.cpp:2848）。
  成功缓存不得绕过 caller 访问校验或失败类状态。内部 handle/布局/受审 intrinsic 契约破坏、
  无法解析的 DEX 输入与受限执行器故障仍为 DexVmError，禁止按其消息猜测 Java 异常。
  survey 沿用既有显式记账/替身策略，不为这些失败添加中性返回。
- `PrecheckMethod` 懒校验 opcode、寄存器/wide pair、invoke 参数、branch/payload 与
  move-result；对照 API 19 `CodeVerify.cpp`，不做全量数据流。
- 每 VM 只有稳定 application `PathClassLoader` 与 boot loader；application→boot→null。
  `findLoadedClass` 不链接/初始化/合成，`loadClass` 校验 binary name；`Class.forName` 使用真实
  caller loader。classpath URL/stream 经注入的 sealed bootstrap/APK 查找及按来源读取，
  application parent-first；jar:file URL 仅标识已登记 guest 归档，不开放宿主文件、动态
  归档/定义、多 namespace 或自定义加载权限。
- `ReflectionRuntime` 是 Method/Constructor/Field metadata 与 wrapper 的唯一工厂；cache 不持
  guest ref，wrapper 可回收。DVM-143：`getMethod/getDeclaredMethod` 按名称和参数沿本类、父类、
  接口定向查找，只解析同名候选，命中后才解析返回/异常类型；`getMethods` 等枚举接口才建立
  完整 metadata。JNI ID 转 Method 通过完整 descriptor（含返回类型）定向查找，不解析
  无关方法的类型。访问检查按 loader/package/member/receiver；invoke 统一处理可赋值、unbox/
  widen、虚派、boxing 与原异常身份包装。

- 运行时注解的 class/field/method/parameter 与默认值均来自本成员所属 DEX unit，统一由
  AnnotationRuntime 物化；Method/Constructor 注解不继承，参数有 item 时按原始 list.size
  发布独立二维数组（含显式 0），无 item 时才按原型发布空矩阵，不补 synthetic 参数。
  Signature 为 system 元数据，由原版 libcore GenericSignatureParser 与 Type 实现解析，
  支持 class/field/method/constructor 泛型、type variable、wildcard 和 generic array；无签名
  回退原始类型，畸形/缺失类型保留原版 Java 异常。enclosing declaration 从受检元数据取得，
  不按名称猜测。通用 Proxy 生成仍明确失败。

### 对象、GC、执行

- Posix.statvfs 经 IoRuntime/IoFileSystem 查询 GuestFileSystemStats，检查计数不变量与
  API19 long 范围后调用原版 StructStatVfs 构造。errno 穿透；未知统计后端记账失败。
  StatFs 的快照/restat/异常转换/字节算法与旧 int 截断执行 BootDex，不提供 fstatvfs。
- AllocateJniObject 使用当前调用线程 context，按 API19 拒绝不可实例化类，运行类初始化
  与分配安全点后创建真实继承布局的零值实例，不执行实例构造器或自动创建 native peer。
  clinit/堆预算/宿主分配错误返回 Java throwable；成功和异常均交给 owner 发布引用。
- `JavaObjectModel` 提供 session 级非移动句柄（0=null）；VM 实例/Object[] 自有存储，String/
  primitive array 复用 JNI store。clone 生成新 identity 并浅拷贝；host-backed 对象必须显式声明
  clone/destructor 策略。同一 Object[] 的区间复制由对象模型委托共享 JNI store 或本地存储，
  先验证范围，再重叠安全地批量移动既有引用；跨数组仍逐元素校验并保留失败前缀。
  JNI primitive array 导入时通过显式 interop callback 取得 kind 对应的真实数组类，
  不将数组声明成 Object 或按元素宽度合并类型；无 VM interop 的独立 store 不推造类 ID。
- primitive Record 缓存与 store 共持的长期 typed view；aget/aput 只做 Record/边界检查和
  下标读写，无 store map/锁、accessor 构造、AddressSpace Read/Write 或单元素 vector。
  Java 多线程对同一数组允许普通数据竞争，store 不再串行化元素访问；long/double backing
  至少 8-byte 对齐，发布仍依赖 monitor/volatile/pthread/futex，不承诺多元素一致快照。
  sweep 先退役 Record view，再由 store 退役 entry view/释放 backing。VM Reserve 是唯一
  堆账本；native New 创建即收费，import 不重复收费，JNI 导入 String 补计既有 payload。
- GC 是分配驱动的精确 STW mark-sweep；roots 包括 frame、结果/异常、static、JNI、Thread、
  intern/Class 与 integration roots。持 guest ref 的 intrinsic 状态用具名 state table trace/sweep；
  嵌套调用的新引用用 `RootScope`。Weak/SoftReference、JNI weak、intern 与 identity hash 遵循
  API 19；异常构造有 64 KiB 应急区。allocation context 在 VM 执行锁内显式查询 emergency
  状态，primitive/String store 将该事实传给 provider，允许使用预提交 backing 应急池；不靠
  全局/TLS 推断 owner。provider 应急池耗尽明确失败，不再次构造 OOME。
- guest native 资源字段回收规则显式指定无效 token（默认 0，API19 ZIP streamHandle 为 -1）；
  未初始化的 0 不登记资源，GC/teardown 清字段时写回对应关闭标记并调用静态释放入口。
  资源清理保存并 root VM 原异常/结果，通过 NativeMethodBridge 清理 scope 隔离 native
  pending，退出恢复原状态；失败及未尝试资源回队列，普通 JNI pending 行为另见 ADR-0153。
  外部宿主 GC 遇到绑定到其他宿主的默认 execution 时只扫堆，将 guest 清理留队，
  由合法 execution 所有者的后续 GC/退出释放，不借用活动 Java 栈。
- System.gc/Runtime.gc 显式调用同一 STW 收集器，普通回收保留 SoftReference、压力回收清软引用。
  weak/soft 清理先于 finalizer 保活，phantom 在复标记后清理/入队；入队唤醒原版队列等待者，
  无队列的引用同样清空。新 invoke 前废弃旧 result 槽，不能将过期返回值当强根。
- 对象模型完成实例分配后通过显式 observer 调用 FinalizerReference.add，解释器只识别真正
  覆盖 finalize 的类；Java referent/zombie/list 是唯一状态。原版 Daemons 用真实 guest/host
  守护线程执行 finalize，异常记录后吞掉；复活对象不再第二次自动登记。runFinalization
  等待进入时批次（含正在执行者），不等待后续新任务；固定 ROM 的 LIFO sentinel 越过旧任务
  时由 VM 批次快照补足。watchdog 使用统一 Clock 的 10 秒阈值，原版 handler/guest exit(2)
  不终止宿主进程。详见 ADR-0151。
- native 关闭在释放 VM 锁前按已登记 cleanup/token 占有 owner 字段并保活；成功退役、异常恢复。
  与 Java finalizer、GC/teardown 共用关闭事实，不能重复关闭 peer，也不将非法 token 伪作成功。
- Thread runtime 核对对象身份而非可复用 handle；死 Thread 的 host/context tombstone 延至
  teardown 回收，新 identity 可合法复用其句柄，不重启旧 Java Thread。
- Runtime maxMemory 是当前 growth limit，totalMemory 是已提交目标（至少 allocated），
  freeMemory 为 total-allocated；VMRuntime 的利用率按 Dalvik 1024 刻度并限制于 0.2..0.8，
  clearGrowthLimit 将上限升至 maximum，native allocation/free 独立计数并按堆配置驱动 GC/
  finalization 水位。不查询宿主剩余内存。
- 堆按 ADR-0060 区分 initial target、growth limit 与 maximum：越过当前目标先 GC，再增长，
  越过增长上限时执行 before-OOM GC 后才失败；GC 后按 live set、利用率和 min/max free 调整目标。
- Throwable 栈以方法身份与 DEX PC 保存，物化时读取同 DEX unit 的 source/debug_info；无调试信息保留未知位置，真实 native 入口保留 Native Method 帧。check-cast 的 CCE 消息使用可读类名与数组维度。
- `Interpreter::Call` 返回值或未捕获 Java 异常；寄存器带类别 tag，默认 512 帧。invoke 只有
  interpreted/intrinsic/native bridge 三路；缺实现记账失败。致命故障附有界 guest stack、指令、
  target/参数诊断，不依赖 trace，也不输出 host 地址。
- `FastCode` 只从已预检字节码派生，不改 DEX、不持 guest ref。默认 switch；threaded 在
  GCC/Clang 用 computed goto、MSVC 用稠密 switch；两者共用 target/shape/异常/clinit 语义，
  `force_all_bridge` 是永久等价门禁。
- `VmExecutionLock` 是可重入全 VM 单写/STW 边界；阻塞按原深度释放/恢复。锁序固定为
  execution→thread runtime→context table；安全分配点外不得触发 GC。
  锁竞争按 FIFO 准入；普通字节码每 1024 tick 的稳定安全点可向等待宿主让出，保留
  guest monitor、完整递归深度及 frame roots，不发布真实阻塞或重置总 tick 预算。

### 线程、monitor、时间

- 一个 guest Thread 对应一个 host thread、execution context、A32 CPU、guest stack、Bionic
  TLS/JNI local frame；解释仍由全 VM 锁串行。start 虚派真实 `run()`；interrupt/join/sleep/
  park/uncaught handler/ThreadGroup 使用唯一 runtime。priority/daemon 不映射宿主调度或退出。
- monitor owner 是 context token；wait 完全释放并恢复 recursion，notify/interrupt/shutdown/
  Clock 唤醒。非 owner 操作抛 IllegalMonitorStateException。deadline 只用注入 Clock；无 Clock
  明确失败，禁止读取宿主墙钟。
  有界生命周期 scope 另使用统一 Clock 的 steady deadline；Java 指令、monitor/睡眠与
  clinit 等待检查同一不可续期预算。到期以非 Java 控制故障退出、移除等待账本，
  不返回伪造通知；正常 Java wait/异常仍按原规则恢复 monitor。
- execution context 进入时验证活动宿主归属；阻塞释放 VM 锁不解除归属，禁止异宿主
  重入或删除活动 context。JNI 附加线程注册既有 context，不创建宿主线程；Thread 身份
  按需初始化并使用 JNI attachment 的可选名称，存活期间进入 GC roots，detach 后终止并唤醒 join。
  root Thread 在 APK 生命周期中存活到 Shutdown；只有独立 run-dex 在 main 返回后调用
  `FinishRootThread`，按 dalvikvm DetachCurrentThread 使 root 死亡并唤醒其 joiner。
  生产 bridge 还将默认 root execution 绑定到创建宿主，空闲时也禁止由后台借用。
  Java worker 的 run 与未捕获异常分派共用其 execution scope；分派期间的异常引用
  显式保活，处理器中的 Java/native 调用不回落到默认 root。
- stop 每指令检查；shutdown 先 stop/join 再展开 context。class init 对同 context 重入，其他
  context 释放执行锁等待，完成/失败/teardown 均唤醒。
- host lifecycle 的 worker-progress 等待使用显式 wall-time 上限，且不获取 execution lock；
  常驻 runnable guest 不能阻断 Surface 等后续平台事件发布。普通 guest `Thread.yield()`
  仍使用既有 `Yield()` 语义。

- System.out/err 经 LogOutputStream 提交文本与“是否行尾”事实：默认写结构化日志（空片段不记），
  设置 `SetConsoleSink` 时按原字节（含空行、无行尾片段）交给宿主控制台，仅 run-dex 使用。
  可选 error sink 接收 Java System.err；未注入时继续汇入原 console sink。通道归 Java 日志流实例，
  不按文本或调用栈猜测，System.setOut/setErr 与自定义 PrintStream 仍由 Java 决定目标。
  `InterpreterConfig::enable_assertions` 只对应 dalvikvm `-ea` 全局开关，APK 运行保持关闭。

### Core intrinsic 与专用 runtime

- `Runtime.availableProcessors` 与 `Posix.sysconf` 的 API19 核数 selector 96/97 消费同一
  `CoreIntrinsicServices.cpu_cores`（由 bridge 注入进程配置）；不从 VM 锁或宿主硬件推导。
  其他 Java sysconf selector 仍记账并明确失败；不宣称完整 Posix sysconf 支持。

- `CoreIntrinsicCatalog(services)` 按 family TU 聚合拥有型 handler；非 Android 类只能在 core，
  平台事实只经 `CoreIntrinsicServices` 注入。Builder 只声明 own members，override 必须显式，字段
  经 bound token 访问；flags 统一来自 `access_flags.h`。
- 手写 intrinsic 的可覆写性必须与 AOSP 4.4 ROM 一致，以 `framework_contract` 的 A1 为门禁；
  ROM final 类的方法不计 A1。已实现的模板方法对 this 的调用须经既有 VM 虚分派入口，
  默认被调 handler 保留原有 C++ 行为，应用覆写与 super 调用分别进入覆写和原 handler。
- 集合、并发、IO/对象流、日期、framework 值类、Throwable、Uri、UUID/JCA、Cipher/证书等普通
  Java 逻辑归 BootDex；字段/数组是唯一状态。native 仅保留受审边界，不得恢复重复 C++ 算法。
- ADR-0154 允许显式撤换已有 intrinsic：ROM Java 整体拥有字段和普通方法，只保留已有受审
  native overlay；宿主不得访问私有字段或保留替代状态。Thread/Handler 等生命周期耦合另行设计。
  DatagramPacket 状态归 Java；DatagramSocket 经 VM 虚派 getter/setter 读取 payload 区间、
  地址和端口、提交接收长度及来源，NetworkRuntime 只消费一次性的 Endpoint/payload 快照，
  不保存 packet 侧表。InputSource 的值状态和 Appendable 接口归 ROM，SAX 解析边界仍明确失败。
- `IoRuntime` 只持文件资源与增量解码状态，文件仅走注入 `IoFileSystem`；API 19
  File/FIS/FOS/RAF/FileChannelImpl 普通算法来自 BootDex，Posix 文件 native 是唯一入口。FIS/FOS 的
  OpenFileDescription 保存隔离的 VFS descriptor 与访问模式，同一逻辑 FileDescriptor 共享
  VFS offset；FileChannel 的当前受检操作也只消费该状态，定位传输保持源 offset 且不伪造 mmap。
  RandomAccessFile 的定位、长度与截断同样走该 descriptor；IoFileSystem 错误携带真实 VFS errno。
  `Posix.mkdir/remove/rename` 不经 bool 降级，VFS 的 ENOENT/EEXIST/EACCES/ENOSPC 等原样构造
  `ErrnoException`，使原版 `File.mkdirs()` 能在 ENOENT 时递归创建父目录。
  相对路径不读 host cwd，逻辑 FileDescriptor 不存 host fd。`ZipRuntime` 复用严格
  ZIP parser/inflate。
- `NetworkRuntime` 只经注入 policy/transport，默认离线；不读 host DNS/代理/证书、不在 core
  创建 socket。InetAddress 与 InetSocketAddress 状态归 BootDex 字段，NetworkRuntime 只保存
  socket/stream/datagram 资源；URI 普通值语义归 BootDex，URL 解析不触网，未授权或未实现
  I/O 明确失败。
- `ProxySelector` 是明确的无进程代理薄边界：`getDefault()` 返回 null，使 API 19 Apache
  RoutePlanner 选择直连；不读取宿主代理，也不登记 `Proxy`/默认 selector 的 BootDex 闭包。
- URLConnection/HttpURLConnection/HttpsURLConnection 的请求属性、超时、method 和默认
  socket factory/hostname verifier 状态来自 API 19 BootDex；core 只 overlay connect 与
  URL.openConnection 的 NetworkRuntime 边界。HTTP/HTTPS openConnection 仅正常构造对象，
  离线也可配置请求；policy 在实际 DNS/connect/I/O 时拒绝，未连接或失败后可安全 disconnect。
  封存 classpath 的只读连接复用原版父类状态与
  ByteArrayInputStream；URL.openStream 虚派连接的 getInputStream。默认 NetworkPolicy disabled。真实客户端
  TLS/HTTPS 由 OGPlayJSSE 经 raw transport 与 guest libssl 完成，不在宿主重复加密。
  SSLEngine、server TLS 和公开互联网 CA 不在当前 complete 范围。
- `System.lineSeparator` 在类初始化时通过初始 `line.separator` property 冻结，后续 property
  修改不影响 `lineSeparator()`；System 仍是 VM 启动及 native 平台边界，不整体迁入 BootDex。
- API 19 初始 property 固定提供 guest `java.home`、`java.io.tmpdir` 与 `user.dir`；Android
  bridge 在执行任何 Java 代码前以 VFS working directory 覆盖 `user.dir`，禁止读取宿主 cwd。
- `NioRuntime` 以对象 identity 保存 Buffer backing/cursor；heap/direct/view 共用 storage，backing
  array 是 GC 强边。allocateDirect 创建真实 byte[]，容量计入 Java 堆；direct 地址与数组 payload
  相同，GC 是唯一回收方，任一视图或单独数组引用都可保活。JNI WrapDirect 仍非拥有、无数组。
  direct memory 只经强类型 guest-address 接口，临时映射始终释放。
- `UnsafeRuntime` 使用逻辑字段令牌；访问校验类型/对齐/边界，CAS 要求执行锁，引用写入保留
  GC tag/assignability。令牌不是地址或 GC root。
- guest ICU 固定为 ICU4C 51 与 `/system/usr/icu/icudt51l.dat`；令牌在 guest SO，teardown 关闭。
  `BigIntRuntime` 保持受检逻辑令牌、编码值和 owner/GC 生命周期；有界加/乘/word 乘/
  移位/幂/商余数通过值快照调用 guest libcrypto 临时 BIGNUM，成功后原子提交。
  不传令牌作地址，不新增持久 registry，不提供完整大数密码算术。
- DVM-142 的 PM 值类闭包归 BootDex；DVM-143 后 PackageManager 方法查询不依赖无关签名。
- 本地 Binder、IBinder/IInterface、Parcel、ResultReceiver/IResultReceiver 的普通 Java 协议归
  BootDex；integration 只覆盖 Binder 身份/线程策略 native 与 Parcel 字节/引用 native 边界。
- ArraySet/LruCache/Pools/Property、Point/Rect、MathUtils/Patterns 与 framework exception
  归 BootDex；Point、Rect、AndroidException 不保留普通方法 overlay。
- API 19 Settings 公开 Java API 归 BootDex，包括 moved-key 路由、数值转换与异常；只有私有
  NameValueCache 的有界存储访问由 Android integration overlay，不引入 provider/Binder。
- API 19 ext.jar 的 Terms/Restlet Apache HTTP 闭包归 BootDex；Commons Logging 的反射实现类
  显式列入配方，`org.apache.http.entity` 的 12 个类完整入集。HTTP socket 仍只经
  NetworkRuntime，Apache 客户端 TLS 走同一 OGPlayJSSE 与 raw transport；SSLEngine 与
  server TLS 仍明确失败，不因类可解析而宣称完整 Apache 或公开互联网 CA。
- API 19 `Animation$AnimationListener` 作为独立嵌套接口进入 BootDex，只提供类型、访问标志
  与三个抽象回调签名，供应用类链接和接口分派；不纳入 `Animation` 本体或动画执行。签名中的
  `Animation` 仅作类型描述符，不因此承诺动画框架。

## 文件分工与不变量

- linker：`class_linker.cpp` 注册/布局/vtable，`class_linker_resolve.cpp` 解析/type relation，
  `method_precheck.cpp` 预检，`fast_code.cpp` 构建缓存。`DefineRestrictedAnnotationClass`
  只在 `Link()` 之后按已加载 annotation interface 追加实现类。
- interpreter：`interpreter.cpp` 主循环，`interp_threaded_*.inc` threaded，
  `interpreter_context.cpp` context/锁，`diagnostics.cpp` 诊断，`vm_threads.cpp` 线程。
  `annotation_runtime.cpp` 物化受限注解实例与成员取值。
- intrinsic：`intrinsics/catalog.cpp` 显式聚合固定 family TU；每类唯一 `Declare_*` 与实现同址。
  禁止静态自注册、misc 巨石、空 handler、平台事实反向依赖。
- 依赖仅指向 core/loader/runtime-jni；平台 provider 经注入获得。guest 输入全部受检，未实现必须
  记账并失败。缓存不得跨可能扩容操作保存 class/member 引用或 host pointer。
- 固定 BootDex 类可按类显式准入其原版 native 声明到 guest JNI；准入不定义类或复制成员，
  未导出的符号仍明确失败。该机制用于 API 19 `NativeCrypto`、`CRC32` 与压缩流 native，
  并由制品审计校验签名集合。
- Gap survey 默认关闭；开启后与 API19 骨架共存（类层次、常量、成员声明与正常模式一致），仅对被调用的未实现
  方法/构造器（骨架、intrinsic、BootDex 声明，含 JNI/反射入口）、被访问的非恒定未实现字段及 API19 未声明成员生成
  0/null/void 并记账（来源、通道、首次序号/时间/前 5 帧栈、次数）；结果永远不是兼容性结论。

## 尚未实现与测试

Field 与 Class 共享运行时注解查询：runtime-visible 递归 encoded value、AnnotationDefault、
`@Inherited` 超类继承与受限实现类成员分派。Method.getDefaultValue 读取声明默认值。
Method/Constructor/参数注解查询仍明确未实现。未实现 generic reflection、完整 annotation
proxy、动态 Proxy/defineClass/DexClassLoader、多 classpath
反射字段、方法、构造器元数据按成员类别独立按需构建；字段查询不得解析无关方法签名。
namespace、完整 Charset/PKIX/TLS/BigInt 及完整 Java/Android 平台。

定向测试位于 `tests/dexvm/`：interpreter/fast-code/linker、reflection、GC、thread/monitor、IO/
network/NIO/Unsafe、BootDex 双后端与 dexasm readback。只构建受影响目标并运行相关用例；完整
验收与 title gate 按顶层约定执行。

### DVM-150 Runtime shutdown

Runtime singleton、hook List、shuttingDown 与 add/remove/exit/halt 普通方法由 pinned BootDex
拥有；不得建立 C++ hook 注册表。nativeExit 保存 per-VM 有符号退出码，停止所有已有 execution
context，通过 thread_stopped 展开而非 Java Throwable；退出后 Call 明确拒绝。RequestShutdown
只发布停止和唤醒，不 join；进程所有者在 guest 栈退出后负责 Shutdown/join。Thread 未捕获
异常仍走现有 handler/进程失败策略，不为 hook 吞异常。宿主取消不等于 Runtime.exit。

### DVM-151 Builder

AbstractStringBuilder/StringBuilder/StringBuffer、IntegralToString 与 RealToString 的普通 Java
逻辑执行固定 API19 BootDex；前三者及 IntegralToString 无 intrinsic overlay。builder 的
value/count/shared 是唯一状态，无宿主 builder map。String 内部构造按既有 immutable
store 契约复制快照；GC 追踪 builder.value，Java 自行维护 shared/COW 与序列化 hooks。
RealToString 只保留 bigIntDigitGenerator native，字段经绑定 handle，临时数值不跨调用保存。

`Interpreter::TrySnapshot` 只尝试 VM 执行锁，复制堆边界、对象/登记类/已链接类与已有调用计数；忙时立即返回 nullopt。method_calls 是解释调用，不是三路总数；GC 暂停使用统一 Clock 累计实际 mark/sweep/资源回收区间，不含等待执行锁的时间。

直接 native 调用将 VmJavaThrow 转为 VmCallOutcome，保留原异常身份和消息。

诊断 `DexClassLinker::DumpSurfaceJson` 对专用装配实例执行链接（不初始化类），导出实际成员、
BootDex 优先来源、父类/接口、encoded/intrinsic 常量和逐类链接错误。implemented 表示有 handler、
字节码或 abstract 契约；动态 guest JNI 没有静态绑定证据时保守标 unimplemented-ledger，
并附 guest-jni-unverified，不等价于证明目标后端不存在或全部行为已实现。

### API19 骨架（ADR-0147）

`RegisterSkeletonSurface` 在应用注册/Link 前装配固定索引数据，仅持原始字节与类偏移，
不创建全表面类/成员。ResolveDescriptor/Java loadClass 首次解析物化 bootstrap 类型；
findLoadedClass 仍不合成。普通启动禁止全量建类；显式 dump-surface 才枚举全表面。
现有类首次链接前补 own members，既有类的层次、成员、handler 与字段所有权优先。
已存在的继承 handler 也不被骨架覆盖；补入父声明不会反向要求原手写声明改为 OverrideMethod。
新骨架类的父类/接口按同一路径解析，四 ROM 之外的未满足依赖仍明确失败。
新骨架自身声明的重写方法不能借用父 handler：它保留 own 声明并在调用时失败。
这种无方法体声明的 override 准入按固定 ROM 父成员标志校验，避免手写 final/访问标志近似
阻断原版结构；不修改手写标志/handler，也不放宽应用字节码的 override 校验。

生成方法（含构造器）为未绑定 intrinsic，调用进入既有能力记账及 UnsatisfiedLinkError；
生成 clinit 只是未实现声明，不安装初始化 hook。常量独立初始化，非恒定状态不伪造。
生成的非恒定 static/实例字段带 is_unimplemented，switch/threaded 的 sget/sput/iget/iput
在访问存储前记 `dexvm.skeleton.field.*` 并抛 UnsatisfiedLinkError；不会返回默认零/null。
常量保留原始数值位与 UTF-16 字符串；不覆盖手写/BootDex 的初始化值。
Survey 保留骨架：未实现方法/字段命中时记账（仍写 ledger）并返回 0/null/void，字段读写落在零初始化槽位；
仅 API19 不存在的成员/类沿用合成中性桩。关闭 survey 即恢复上述失败语义；该模式不代表兼容支持。
JNI/反射查询与枚举复用真实 own-member 表面和修饰符；存在但未实现的成员查找成功。
解释器、反射和 JNI owner 共用 `RequireImplementedField`，禁止非常量字段回落零值存储。
JNI 缺成员挂起 NoSuchMethodError/NoSuchFieldError，反射查询抛对应 NoSuch*Exception；
Method.invoke/Constructor.newInstance 将未实现目标的 UnsatisfiedLinkError 包为 InvocationTargetException。
这些语义不代表骨架行为已支持；Unsafe/序列化等其余字段访问通道不在此项验收范围。

ADR-0153：类初始化的 post-clinit 检查可经显式 owner pending source 交接同线程 JNI
throwable，按 Dalvik Class.cpp:4485–4497 将类标 failed，非 Error 包装并保留 cause、Error
保留身份；已初始化类不消费外部 pending。回调只在检查点调用，停止执行后撤销，不引入
JNI 环境反向依赖。普通 Call/NewObject 不预先清除 JNI pending。

ADR-0152 阶段 4：String 内容自创建起由显式 provider 的不可变 UTF-16 block/view 唯一持有，
Region/MUTF-8/hash/equals/intern 键从同一内容读取；无影子 value/offset/count 字段。公开 char[]、
builder 与 substring 保持独立复制快照。StringStore 共享 VM allocation context：NewString/JNI
创建统一收费一次，导入补未收费项，已有实例 BindString 只增收 payload；失败回滚，sweep
由 store 退还自身费用并扣除实例剩余费用。String pin 与 UTF8 lease 均为 mark/weak 前强根；
Chars/Critical 按 String 身份跨线程计数并忽略 release 指针/kind；不增加内存序承诺。

平台默认属性由 platform_tables.h 定义；物化后以单一 live BootDex Properties 为权威，
VM 持有 GC 根，get/setProperty 与 getProperties 共用对象。CoreIntrinsicServices 提供
显式来源初始化回调；guest uname、passwd、ICU 与随附元数据为默认属性来源。
