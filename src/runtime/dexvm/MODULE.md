# 模块：runtime/dexvm

## 职责与边界

有界 Dalvik 解释器（ADR-0017、`docs/design/dexvm/`）：负责类链接、Java 对象模型、
switch/threaded 解释、异常、线程/monitor、反射及 `java.*` core intrinsic。只加载应用 DEX
与受审 API 19 BootDex；不提供完整 Dalvik/ART、动态 classpath、JIT 或 Android 服务。

## 核心契约

### 链接、类加载、反射

- 装配顺序固定为 `RegisterIntrinsics → RegisterBootDex → RegisterDex → Link`。Boot/App 分别
  保存 DEX unit 与常量池缓存；解析使用方法所属 unit。BootDex 提供 class/field/hierarchy，
  同签名 intrinsic 只 overlay 方法；未绑定 native 明确失败。
- APK class_def 全量登记，层级/布局/vtable/iftable 懒链接。未触达可选类不阻断启动；触达后
  缺失层级、循环继承、非法覆盖明确失败。应用自带 `android.support.*` 归 app loader，真正平台
  前缀类忽略。数组按需合成并支持引用协变及 Object/Cloneable/Serializable 关系。
- `PrecheckMethod` 懒校验 opcode、寄存器/wide pair、invoke 参数、branch/payload 与
  move-result；对照 API 19 `CodeVerify.cpp`，不做全量数据流。
- 每 VM 只有稳定 application `PathClassLoader` 与 boot loader；application→boot→null。
  `findLoadedClass` 不链接/初始化/合成，`loadClass` 校验 binary name；`Class.forName` 使用真实
  caller loader。无动态定义、多 namespace 或自定义加载权限。
- `ReflectionRuntime` 是 Method/Constructor/Field metadata 与 wrapper 的唯一工厂；cache 不持
  guest ref，wrapper 可回收。DVM-143：`getMethod/getDeclaredMethod` 按名称和参数沿本类、父类、
  接口定向查找，只解析同名候选，命中后才解析返回/异常类型；`getMethods` 等枚举接口才建立
  完整 metadata。访问检查按 loader/package/member/receiver；invoke 统一处理可赋值、unbox/
  widen、虚派、boxing 与原异常身份包装。

### 对象、GC、执行

- `JavaObjectModel` 提供 session 级非移动句柄（0=null）；VM 实例/Object[] 自有存储，String/
  primitive array 复用 JNI store。clone 生成新 identity 并浅拷贝；host-backed 对象必须显式声明
  clone/destructor 策略。
- GC 是分配驱动的精确 STW mark-sweep；roots 包括 frame、结果/异常、static、JNI、Thread、
  intern/Class 与 integration roots。持 guest ref 的 intrinsic 状态用具名 state table trace/sweep；
  嵌套调用的新引用用 `RootScope`。Weak/SoftReference、JNI weak、intern 与 identity hash 遵循
  API 19；异常构造有 64 KiB 应急区。
- `Interpreter::Call` 返回值或未捕获 Java 异常；寄存器带类别 tag，默认 512 帧。invoke 只有
  interpreted/intrinsic/native bridge 三路；缺实现记账失败。致命故障附有界 guest stack、指令、
  target/参数诊断，不依赖 trace，也不输出 host 地址。
- `FastCode` 只从已预检字节码派生，不改 DEX、不持 guest ref。默认 switch；threaded 在
  GCC/Clang 用 computed goto、MSVC 用稠密 switch；两者共用 target/shape/异常/clinit 语义，
  `force_all_bridge` 是永久等价门禁。
- `VmExecutionLock` 是可重入全 VM 单写/STW 边界；阻塞按原深度释放/恢复。锁序固定为
  execution→thread runtime→context table；安全分配点外不得触发 GC。

### 线程、monitor、时间

- 一个 guest Thread 对应一个 host thread、execution context、A32 CPU、guest stack、Bionic
  TLS/JNI local frame；解释仍由全 VM 锁串行。start 虚派真实 `run()`；interrupt/join/sleep/
  park/uncaught handler/ThreadGroup 使用唯一 runtime。priority/daemon 不映射宿主调度或退出。
- monitor owner 是 context token；wait 完全释放并恢复 recursion，notify/interrupt/shutdown/
  Clock 唤醒。非 owner 操作抛 IllegalMonitorStateException。deadline 只用注入 Clock；无 Clock
  明确失败，禁止读取宿主墙钟。
- stop 每指令检查；shutdown 先 stop/join 再展开 context。class init 对同 context 重入，其他
  context 释放执行锁等待，完成/失败/teardown 均唤醒。

### Core intrinsic 与专用 runtime

- `CoreIntrinsicCatalog(services)` 按 family TU 聚合拥有型 handler；非 Android 类只能在 core，
  平台事实只经 `CoreIntrinsicServices` 注入。Builder 只声明 own members，override 必须显式，字段
  经 bound token 访问；flags 统一来自 `access_flags.h`。
- 集合、并发、IO/对象流、日期、framework 值类、Throwable、Uri、UUID/JCA、Cipher/证书等普通
  Java 逻辑归 BootDex；字段/数组是唯一状态。native 仅保留受审边界，不得恢复重复 C++ 算法。
- `IoRuntime` 只持文件资源与增量解码状态，文件仅走注入 `IoFileSystem`；相对路径不读 host cwd，
  逻辑 FileDescriptor 不存 host fd。`ZipRuntime` 复用严格 ZIP parser/inflate。
- `NetworkRuntime` 只经注入 policy/transport，默认离线；不读 host DNS/代理/证书、不在 core
  创建 socket。URL 解析不触网，未授权或未实现 I/O 明确失败。
- `NioRuntime` 以对象 identity 保存 Buffer backing/cursor；heap/direct/view 共用 storage，backing
  array 是 GC 强边。direct memory 只经强类型 guest-address 接口，临时映射始终释放。
- `UnsafeRuntime` 使用逻辑字段令牌；访问校验类型/对齐/边界，CAS 要求执行锁，引用写入保留
  GC tag/assignability。令牌不是地址或 GC root。
- guest ICU 固定为 ICU4C 51 与 `/system/usr/icu/icudt51l.dat`；令牌在 guest SO，teardown 关闭。
  `BigIntRuntime` 只提供 ASN.1/证书所需子集，不提供大数密码算术。
- DVM-142 的 PM 值类闭包归 BootDex；DVM-143 后 PackageManager 方法查询不依赖无关签名。
- ArraySet/LruCache/Pools/Property、Point/Rect、MathUtils/Patterns 与 framework exception
  归 BootDex；Point、Rect、AndroidException 不保留普通方法 overlay。

## 文件分工与不变量

- linker：`class_linker.cpp` 注册/布局/vtable，`class_linker_resolve.cpp` 解析/type relation，
  `method_precheck.cpp` 预检，`fast_code.cpp` 构建缓存。
- interpreter：`interpreter.cpp` 主循环，`interp_threaded_*.inc` threaded，
  `interpreter_context.cpp` context/锁，`diagnostics.cpp` 诊断，`vm_threads.cpp` 线程。
- intrinsic：`intrinsics/catalog.cpp` 显式聚合固定 family TU；每类唯一 `Declare_*` 与实现同址。
  禁止静态自注册、misc 巨石、空 handler、平台事实反向依赖。
- 依赖仅指向 core/loader/runtime-jni；平台 provider 经注入获得。guest 输入全部受检，未实现必须
  记账并失败。缓存不得跨可能扩容操作保存 class/member 引用或 host pointer。
- Gap survey 默认关闭，只对真实触达的平台缺口生成 0/null/void 并记账；结果不是兼容性结论。

## 尚未实现与测试

未实现 generic reflection、annotation proxy、动态 Proxy/defineClass/DexClassLoader、多 classpath
namespace、完整 Charset/PKIX/TLS/BigInt 及完整 Java/Android 平台。

定向测试位于 `tests/dexvm/`：interpreter/fast-code/linker、reflection、GC、thread/monitor、IO/
network/NIO/Unsafe、BootDex 双后端与 dexasm readback。只构建受影响目标并运行相关用例；完整
验收与 title gate 按顶层约定执行。
