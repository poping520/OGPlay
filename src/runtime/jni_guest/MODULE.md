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

- `GuestJniAbi` 把完整 233 槽 JNIEnv 与 8 槽 JavaVM 物化为 32 位 guest 函数表、对象和
  Thumb SVC trap;reserved 槽保持 null,其余槽均有可识别地址。表与对象只读、trap 页
  RX,映射冲突完整回滚,析构后不得残留 guest 映射。
- `JniGuestCallDispatcher` 只消费精确落入上述目录的 `SVC #3`,校验 JNIEnv/JavaVM
  receiver 与非零线程后发布寄存器/栈调用帧;slot 必须在执行前显式绑定并封口,未绑定
  项按名称记账并失败,未知 trap 地址不得吞掉。
- production 只通过 `BindJniGuestSlots` 的统一 context 显式组合 Core、Class/Instance、
  Static Call、Static/Instance Field、String、Array 与 JavaVM family,随后封口 dispatcher;
  aggregate contract 以精确 slot 集合等价(当前 214 个 JNIEnv 与 4 个 JavaVM)机器验证,
  数量本身只是辅助诊断;Critical string、DirectByteBuffer、Reflection、FatalError 与
  DestroyJavaVM 属于显式 expected-unbound 清单,不得为提高覆盖率注册假 handler。Core
  binder 不得再隐式注册其他 family。引用、异常和线程状态复用同一环境,guest 输出指针在
  VM 状态变更前预检;非空 attach arguments 在实现其结构前明确失败。
- `GetStaticMethodID` 只精确查询统一 class registry;`NewStringUTF` 使用受检 guest C
  string、M3 Modified UTF-8 解码与统一 string store 后发布 local reference。
- 10 种 `CallStatic*Method` 返回类型的普通、`V`、`A` 共 30 个槽按 method descriptor
  分别解码 A32 variadic、对齐 `va_list` 与 8 字节步长 `jvalue[]`,再进入统一 invocation
  engine;小整数符号/零扩展、float/double/long 双字返回及 void 均遵循 A32 guest ABI,
  调用名与 descriptor 返回类型不符、错误 return kind、class、method、handler、array
  reference/type/region 或输出缓冲明确失败,成功查询只发布统一 Guest JNI ABI 地址。
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
- JNI guest class/object/instance family 只解析 class registry 已声明的精确名称与
  instance method descriptor;三种 NewObject 仅接受 void `<init>` 并在失败时回滚
  ref/object 映射。会话级 `JniGuestObjectRegistry` 让 guest 构造对象和 framework HLE
  预注册的 host object 共享精确 class identity;GetObjectClass/IsInstanceOf 与 30 个
  普通 instance Call/CallV/CallA 统一查询该 registry,并复用 invocation engine 的
  assignability、argument/return 校验;未声明 class/method、伪 receiver 或返回类型
  不匹配必须明确失败。
- modified UTF-8 访问族与 UTF-16 string 5 槽都解析统一 `JniStringStore`,并各用独立
  64 KiB copy-based guest arena;`isCopy` 明确写 true,lease 以 string identity +
  pointer + token 配对并 first-fit 回收,arena owner 不得在析构时反向访问可能已销毁的
  string store。UTF-16 长度与 region 以 code unit 计,NewString 完整预检 guest input 并
  在 reference 发布失败时删除 semantic object;坏引用/range/输出、wrong-string/double
  release 与 arena exhaustion 明确失败,Critical 两槽继续 unbound。
- primitive array 42 槽由统一 binder 批量接入 `JniPrimitiveArrayStore`;8 类 New/Region/
  Elements 都按 little-endian ARM32 ABI 搬运,第五个 region buffer 从 guest 栈读取并在
  semantic mutation 前完整预检。Elements 使用独立 4 MiB 有界 guest arena,严格实现
  `0`/`JNI_COMMIT`/`JNI_ABORT`、wrong pointer/double release 与类型配对；Critical 两槽
  复用相同有界 copy lease，但以独立 access kind 配对，不能与 Elements 交叉 release。
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
