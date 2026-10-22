# 子模块：runtime/jni_guest

## 职责

把 M3/M8 的 JNI/JavaVM 语义物化为 32 位 guest 可调用的 ABI:JNIEnv/JavaVM 函数表与
Thumb SVC trap 的 guest 内存映射、SVC 调用帧解码与 slot 分派,以及全部 slot binding
family(core、class/instance、static call、static/instance field、string、array、
nonvirtual、monitor、JavaVM)与 root `JNI_OnLoad` 库生命周期。语义本体(对象模型、
引用、异常、调用引擎)属于 `runtime/jni`,本模块只负责 guest ABI 侧的搬运与绑定。

## 依赖

位于 `runtime/jni` 与 `runtime/execution` 之上,可依赖 loader、memory、cpu、core。
不得依赖 `runtime/boundary`、`runtime/integration`、`runtime/framework` 与 gles 模块。

## 不变量

- DVM-141 `EnsureRegistered` 在注册表锁内幂等登记真实 runtime class；冲突明确失败，
  不修改已有类型。GetObjectArrayElement 发布引用前使用元素携带的 java_class 登记，
  不使用声明 component type 猜测；该表不是 GC root，VM sweep 仍负责 Forget。
- ClassOf 可显式注入共享 semantic store 的真实类型解析器；查表/jclass 未命中时在
  registry 锁外调用。无解析器或未知 identity 明确失败，不按 method owner 推断类型；
  owner 在 guest threads 停止后撤销解析器，类型登记仍遵循 EnsureRegistered/Forget。
- FindClass 的解析回调若已发布 Java 异常，返回 null 并保留原 pending，不以查询缺失覆盖它。
- guest-memory 标量和受限 C 字符串读取统一经过私有 `jni_guest_memory.h`；整数按 A32
  little-endian 解码，名称/签名等元数据保持 1024 字节扫描上限。`NewStringUTF` 正文
  使用独立、可注入资源预算，默认最多 3 MiB Modified UTF-8 payload（另读终止符）、
  1 Mi UTF-16 code units；逐页扫描仍校验权限和地址溢出，超限不等于已证明未终止。
- `GuestJniAbi` 把完整 233 槽 JNIEnv 与 8 槽 JavaVM 物化为 32 位 guest 函数表、对象和
  Thumb SVC trap;reserved 槽保持 null,其余槽均有可识别地址。表与对象只读、trap 页
  RX,映射冲突完整回滚,析构后不得残留 guest 映射。
- `JniGuestCallDispatcher` 只消费精确落入上述目录的 `SVC #3`,校验 JNIEnv/JavaVM
  receiver 与非零线程后发布寄存器/栈调用帧;slot 必须在执行前显式绑定并封口,未绑定
  项按名称记账并失败,未知 trap 地址不得吞掉。
- sealed dispatcher 的 fast 与 slow entry 共用 receiver/thread/slot/return-width 校验和
  同一 handler；fast entry 直接借用 CPU live registers，异常在 JIT callback 内按
  thread/PC 保存为 pending fault 并转换为 host-call fault，不得跨 callback 传播；退出
  JIT 后必须由 dispatcher 恢复原 exception type/message，不得退化为 generic fault。
- production 只通过 `BindJniGuestSlots` 的统一 context 显式组合 Core、Class/Instance、
  Static Call、Static/Instance Field、String、Array 与 JavaVM family,随后封口 dispatcher;
  aggregate contract 以精确 slot 集合等价(基础 226 个 JNIEnv、注入 NIO 后 229 个，及
  4 个 JavaVM)机器验证，数量本身只是辅助诊断；DVM-82 的 DirectByteBuffer 三槽仅在
  显式注入共享 `NioRuntime` 时绑定；
  DestroyJavaVM 是唯一 expected-unbound 槽,不得为提高覆盖率注册假 handler。Core
  binder 不得再隐式注册其他 family。引用、异常和线程状态复用同一环境,guest 输出指针在
  VM 状态变更前预检。attach 解码 version/name/group，Modified UTF-8 name 传给线程 owner；
  group 为 NULL 使用默认组，非空 group 明确失败并记账；重复 attach 忽略参数。
- DefineClass 沿用 Dalvik 返回 NULL 且无异常，同时记录可查询的 warn ledger。
- Get/ReleaseStringCritical 复用 UTF-16 copy lease，access kind 与 StringChars 独立，
  禁止交叉 release；isCopy 如实返回 TRUE，三种字符串 lease 共用复制预算。
- FatalError 读取 r1 的 guest C 字符串，最多扫描 4096 字节（含终止符）；引号、反斜杠及
  非打印 ASCII 字节转义，保留原始字节。空指针、扫描上限、内存故障和地址溢出均保留
  可读前缀及读取状态，附 guest thread、LR/SP、寄存器与消息指针后沿现有宿主致命错误链
  退出，不返回 guest、不创建 Java throwable、不改变已有 pending exception。
- From/ToReflectedMethod/Field 四槽只解码 A32 引用、成员 ID 与 static 标志，委托
  JniEnvironment 的显式反射 hooks；Method 同时支持 Constructor。无 VM 后端时明确失败，
  不创建假 ID 或空 wrapper。返回 local reference 的发布由 VM owner 在执行锁内完成。
- `NewDirectByteBuffer`（DVM-82）先验证 capacity 与完整 guest read/write range，再登记
  `DirectByteBuffer` identity；地址/容量查询只消费同一 NIO side state，heap buffer 的地址返回 null、容量按 Dalvik 4.4 返回 Buffer.capacity（ADR-0153）。
  非 Buffer 的容量仍明确返回 -1。非法地址、未知引用或发布失败不得留下 object/state 半提交。
- `GetMethodID`/`GetStaticMethodID` 共用精确 registry 查找与 owner 类初始化；`NewStringUTF` 使用受检 guest C
  string、M3 Modified UTF-8 解码与统一 string store 后发布 local reference；遵循 API 19
  Dalvik，null C 指针直接返回 null `jstring`。非空输入读取失败必须按层级排版保留 slot、
  guest thread、LR/SP、r0-r3 与原始 cause，使动态注册 native 的外层 Java/JNI 上下文仍能
  定位到具体 guest 调用点；分别报告扫描预算、内存故障、编码错误及分配失败。
  可注入成功读取观察器，仅发布调用帧与两种长度，诊断失败不得改变 JNI 结果。
- 10 种 `CallStatic*Method` 返回类型的普通、`V`、`A` 共 30 个槽按 method descriptor
  分别解码 A32 variadic、对齐 `va_list` 与 8 字节步长 `jvalue[]`,再进入统一 invocation
  engine;小整数符号/零扩展、float/double/long 双字返回及 void 均遵循 A32 guest ABI,
  调用名与 descriptor 返回类型不符、错误 return kind、class、method、handler、array
  reference/type/region 或输出缓冲明确失败,成功查询只发布统一 Guest JNI ABI 地址。
  static call 与 static field 的 primitive/reference 返回编码共用同一 value codec；void
  是否允许由调用方策略显式决定，原 family 错误文案保持稳定。
- guest `RegisterNatives` 将完整 ARM32 12-byte method 数组、字符串、descriptor、class 与
  target 先整批校验/resolve,再一次提交到唯一 `JniNativeRegistry`;Thumb bit 保持不变,
  `ResolveJniRegisteredNativeCall` 把 mapping 交给通用 A32 executor。`UnregisterNatives`
  只清除指定 class,非法 count/range/method 或注销后的解析必须明确失败。
- static field 19 槽(`GetStaticFieldID` 与 9 对 getter/setter)与 instance field 19 槽
  共用类型编码和统一 `JniFieldStore`;field ID 只精确查询统一 class registry,槽类型必须
  匹配 descriptor,static/instance ID、descriptor 与 getter/setter kind 不得混用。
  instance receiver 先经当前 JNIEnv resolve,再由 `JniGuestObjectRegistry` 取得 receiver
  class。word、符号扩展、float bits 与 long/double 双字遵循 A32 soft-float word-pair
  ABI,64 位 setter 第 4 参数从对齐 guest 栈读取;错误 class/reference/field/kind/type/
  栈地址均明确失败。
  static/instance FieldID binding 只共享校验→查类→查字段骨架，storage kind 与错误上下文仍分开。
- JNI guest FindClass 经 registry 的显式 owner resolver 按需发布真实链接类，不预载全部骨架。
  成员 ID 精确匹配描述符与 static；缺失返回 NULL，并挂起 NoSuchMethodError/NoSuchFieldError，
  缺类挂起 NoClassDefFoundError。无效引用/参数仍沿宿主绑定错误，pending 门禁不放宽。
  骨架非常量字段访问记账并挂起 UnsatisfiedLinkError；ABI 零值仅为带异常的返回哨兵，
  不表示读取成功，也不回落侧表。常量经共享 VM 存储返回真实值。
  JNI guest class/object/instance family 解析 class 引用；AllocObject 只搬运强类型引用与调用线程，由 JniEnvironment owner 回调
  初始化类、创建零值 VM 对象并发布 local ref，不复用 NewObject 的构造调用。
  未配置分配后端不能返回假对象；Java 分配错误返回 null、保留 pending exception。
  该 family 的
  instance method descriptor;三种 NewObject 仅接受 void `<init>` 并在失败时回滚
  ref/object 映射。会话级 `JniGuestObjectRegistry` 让 guest 构造对象和 framework HLE
  预注册的 host object 共享精确 class identity；`FindClass` 返回的 `jclass` 同时是
  `java.lang.Class` 的合法 instance receiver。GetObjectClass/IsInstanceOf 与 30 个
  普通 instance Call/CallV/CallA 统一查询该 registry,并复用 invocation engine 的
  assignability、argument/return 校验；伪 receiver 或返回类型
  不匹配必须明确失败。成员查找异常消息保留 class name、member name 与 descriptor。
  DVM-158 按 API 19 非 CheckJNI Dalvik 通用兼容语义，允许 CallObjectMethod/ V / A
  调用 void 实例方法：方法照常执行、结果丢弃并返回 null；不依赖 Profile，其他返回族
  错配继续明确失败。
- modified UTF-8 访问族与 UTF-16 string 5 槽都解析统一 `JniStringStore`，通过 AddressSpace
  页账本按需映射 copy buffer，共享默认 16 MiB 页对齐总预算；release 解除映射并归还预算。
  编码分别受正文预算约束，`isCopy` 明确写 true，lease 以 string identity +
  pointer + token 配对，owner 不得在析构时反向访问可能已销毁的
  string store。API 19 Dalvik 的 null modified-UTF8 string 兼容语义保持一致：length 返回 0、
  chars 返回 null 且不改写 `isCopy`；`ReleaseStringUTFChars` 与 Dalvik 一样忽略 `jstr`，null
  指针无操作，非空指针只按已发布 lease 身份释放，未知或重复指针继续明确失败。
  UTF-16 长度与 region 以 code unit 计,NewString 完整预检 guest input 并
  在 reference 发布失败时删除 semantic object;坏引用/range/输出、wrong-string/double
  release 与资源预算耗尽明确失败，分配/复制/发布失败回滚映射、预算与 semantic access；
  Critical 两槽使用独立 kind/arena，与 StringChars 共用 UTF-16 lease 实现和总预算。
- primitive array 42 槽由统一 binder 批量接入 `JniPrimitiveArrayStore`;8 类 New/Region/
  Elements 都按 little-endian ARM32 ABI 搬运,第五个 region buffer 从 guest 栈读取并在
  semantic mutation 前完整预检。八类 Elements 与 PrimitiveCritical 直接借用唯一 guest payload，
  isCopy=FALSE；零长也返回有效地址。重复获取同址并增加 store 的数组身份 pin，非零 pin 是 GC 强根。
  与 Dalvik 一致，不校验 elems 指针，不区分 Elements/Critical；COMMIT 无操作，其余 mode（含非法值）
  仅解除一次 pin，不复制/写回/回滚。无配对只发布结构化 warn 和 warning ledger，含数组/实际槽/guest LR，
  不中止且不改变状态；每数组首次超过 10 次 pin 也 warn。pin 不绑定线程，detach 不释放。
  guest 绑定需要 guest provider，无地址时明确失败，不保留复制回退；String arena/lease 合同不变。
  全部 guest 线程和 CPU 停止并 join 后统一退役剩余 pin，记录数组数和字节数。
  object array
  3 槽复用会话级 `JniGuestObjectRegistry::ObjectArrays()` 并验证 initial/set
  assignability；该 store 同时注入 DexVM object model，native/解释路径共享 identity
  与元素事实。`GetArrayLength` 通过
  显式 `Contains` 区分 object/primitive store,不使用异常探测。创建后 local reference
  发布失败必须删除 semantic array。
- nonvirtual 30 槽与 virtual/static call 共用 descriptor 驱动的 A32 word cursor、
  va_list、jvalue 与返回编码;仅按 JNI ABI 从 r3 取得 method,并从 guest 栈取得首个
  Java 参数或 V/A pointer,随后调用唯一 `JniInvocationEngine::InvokeNonvirtual` 完成
  method/class/receiver 语义。ThrowNew 完整读取并校验 Modified UTF-8 message,在
  runtime/jni 保存 throwable identity、exception class 与 message;ExceptionDescribe 写
  结构化 `runtime.jni.exception` diagnostic,且不得清除或替换 pending throwable。
- MonitorEnter/MonitorExit 只解析统一 JNIEnv reference 后进入 environment 的唯一 monitor
  backend,不返回 fake success；默认语义位于 runtime/jni，DexVM 装配时由 hooks 委托给
  `VmMonitorTable`。JavaVM detach 通过同一 backend 释放该 guest thread 的 ownership。
- guest JNI library lifecycle 只从显式 root module 自身选择唯一 exported/defined
  function `JNI_OnLoad`,不误调用 ELF 依赖的同名导出;调用帧固定为统一 guest JavaVM、
  null reserved,保留 ARM/Thumb symbol state,返回只接受 JNI 1.1/1.2/1.4/1.6。

## 测试

对应 `tests/runtime/` 下的 `jni_guest_abi_tests.cpp`、`jni_guest_bindings_tests.cpp`、
`jni_guest_instance_calls_tests.cpp`、`jni_guest_library_lifecycle_tests.cpp` 与
`jni_native_registry_tests.cpp`。

WU8-2b：primitive Region、对象元素与 string Region 越界在 buffer 访问前经 ThrowableFactory
发布 Dalvik 类型/消息并返回 void/NULL；非法 handle、类型或内存故障不做笼统转换。
MonitorExit 仅将 non-owner 映射为 JNI_ERR + IllegalMonitorStateException，失败不改变锁状态；
Ensure/Push 请求容量失败映射为 JNI_ERR + OOME，Push 与失败 MonitorExit 按 Dalvik 替换 pending。

ADR-0153：JNI fast/slow 分派通过显式 enter/leave hooks 将实际槽、guest 线程及 LR 交给
runtime/jni 做 pending 诊断，guest binding 不另加 pending 拦截；helper 不重复记报。
GetDirectBufferCapacity 按 Dalvik 4.4 Jni.cpp:2758–2759 对 heap Buffer 也返回 capacity，
这是历史实现差异（JNI 规范与后续 ART 为 -1）；heap GetDirectBufferAddress 仍 NULL。
