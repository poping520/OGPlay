# DexVM 与 ROM Java 执行

DexVM 解释应用 DEX 和 API19 ROM Java，提供统一对象模型、异常与线程语义。
它位于 Loader/JNI 之上，平台服务由 integration 注入。
目标是让进程依赖的字节码真实运行，不提供完整 Dalvik/ART 或 Java JIT。

## 装配与启动类路径

```text
core / Android intrinsic catalog
  → 启动类路径 DEX（归档顺序）
  → 应用 DEX
  → 惰性链接、成员物化
  → 解释 / intrinsic / native bridge
```

启动类路径由自有 ogplay.jar 和原样 ROM jar 构成。
ReadBootClasspath 读取固定归档，BootClasspathDexFiles 提取其中 DEX。
每个 DEX unit 保留自己的常量池与解析缓存。
解析以方法所属 unit 为准，不能用另一个 DEX 的同值索引。

类身份遵守 bootstrap parent-first。
同一描述符先登记者胜出，应用副本不能混入父类的字段或注解。
ROM 类身份先登记，层级、字段布局与方法表在触达时物化。
因此未使用的可选类型不会因为依赖不完整阻止整个 VM 装配。
真正触达的链接错误仍按 Java 语义失败。

## 类链接与初始化

DexClassLinker 构建父类、接口、实例布局、vtable 和 iftable。
方法匹配使用名称与完整 descriptor，包含返回类型。
private、final、static/virtual 与访问权限都在各自阶段校验。
数组类型按需合成，并保持引用协变和标准接口关系。

类初始化不是普通方法调用的同义词。
同 context 可以重入初始化，其他线程释放执行锁等待结果。
首次失败保存错误；后续使用按失败类状态传播。
常量池缓存不能绕过 caller 访问检查或已失败类状态。
GC 扫描静态引用时只看已分配槽，不为扫描主动物化类。

Method 结构预检校验 opcode、寄存器、wide pair、分支与 payload。
它是有界结构校验，不等同于完整 Dalvik 数据流 verifier。
缺类、缺成员和种类错误使用明确 Java linkage error。
内部句柄损坏和受限执行器故障继续是 VM 控制错误。
不能根据一段错误消息猜测要抛哪种 Java 异常。

## 解释内核

默认 switch 内核直接解释 DEX 指令。
threaded 内核从通过预检的字节码建立 FastCode 派生缓存。
GCC/Clang 使用 computed goto，MSVC 使用稠密 switch。
缓存不改原 DEX 字节，不生成机器码，也不保存 guest 对象引用。

两种内核共享调用、异常、类初始化、对象与安全点逻辑。
FastCode 保存回到 DEX PC 的映射，诊断和 catch 范围仍以原 PC 表示。
不适合快路径的操作通过共享 bridge 执行。
force_all_bridge 可用于比较语义，不是另一套对象模型。

每个执行 frame 保存方法、寄存器及类别 tag。
wide 值与引用不能仅按数值位型互换。
invoke 只有解释、intrinsic 和 native bridge 三条实现路径。
缺失实现记录能力命中并明确失败。
返回结果与未捕获异常由 Interpreter::Call 显式交给调用者。

## 对象与稳定 payload

JavaObjectModel 使用非移动逻辑句柄，零表示 null。
实例字段和静态字段是普通 Java 状态的唯一权威。
对象数组由共享 store 保持身份，Java 与 JNI 不维护第二份元素。
clone 创建新身份并浅拷贝，host-backed 对象需显式清理策略。

String 与 primitive array 使用 JavaPayloadProvider 分配稳定 backing。
生产 provider 在 guest 地址空间中拥有受保护 block。
长期 typed view 与数组 record 共用同一字节，不在每次 aget/aput 复制。
Java、JNI 和 NIO 因而可以观察到同一 payload 的修改。

JNI pin 是 GC 强根，borrow 则保活 backing 与 owner。
零长对象也可以有有效地址，不能用 null 假装空数组的合法借用。
UTF-16 String backing 不可变；公开构造与 substring 使用独立快照。
Modified UTF-8 编码保留独立副本和配对释放规则。
共享 backing 不为无同步的 guest 数据竞争增加保证。

## 堆与 GC

GC 是分配驱动的精确 STW mark-sweep。
roots 来自执行 frame、静态字段、JNI、线程、类镜像、intern 和 integration。
持有引用的 intrinsic state table 参与 trace/sweep。
嵌套调用使用 RootScope，不能把临时宿主变量当作自动 GC root。

VM 的 Reserve 是 Java 堆唯一收费入口。
initial target、growth limit 与 maximum 分开，先回收再决定增长。
JNI 创建立即收费，首次导入只补尚未收费部分。
payload provider 管后备提交，不建立第二份 Java 逻辑堆配额。

Weak/Soft/Phantom 与 finalizer 按既定顺序处理。
普通回收保留软引用，压力回收可清理。
FinalizerReference 的 Java 字段维护登记状态，原版 Daemons 在真实线程执行终结。
runFinalization 等待进入时的批次，不无限等待后来新增任务。
异常构造与 native 资源回收具有明确的应急和失败边界。

native peer 回收通过登记的 token/cleanup 入口执行。
显式 close、Java finalizer、GC 与 teardown 共用关闭事实。
清理期间保存原结果与异常身份，失败资源可回队列。
不能重复释放，也不能把坏 token 当作已经关闭。

## Java 线程、monitor 与执行锁

一个 guest Thread 对应一个宿主线程和 execution context。
Java 字节码在可重入 VmExecutionLock 内串行执行。
阻塞时按完整递归深度释放，恢复时重建原深度。
这允许其他线程回调 Java，而不是用伪线程轮询模拟并发。

monitor owner 使用 context token，wait 释放并恢复全部重入层数。
notify、interrupt、shutdown 与 timeout 是不同唤醒原因。
非 owner 操作抛 IllegalMonitorStateException。
期限只由注入 Clock 决定；关闭 budget 不能被重复等待续期。

JNI 附加线程登记已有宿主，不为 attach 再创建一条线程。
活动 context 验证宿主归属，释放 VM 锁不等于解除归属。
类初始化等待、native 阻塞与 monitor 等待都必须遵守锁顺序。
停止先发布 stop/wake，再 join，最后展开和销毁 context。

## 异常与反射

throwable 保留对象身份、类、消息和 DEX 栈位置。
Java catch 按目标类型匹配，跨帧展开与 JNI pending 交接共用身份。
Method.invoke 的目标异常按 Java 规则包装，不把宿主控制故障包装为 Java 异常。
Throwable 诊断读取 cause 时不执行 guest 代码。

ReflectionRuntime 是成员元数据和 wrapper 的唯一工厂。
定向查询只解析同名候选，枚举时才物化完整成员集合。
访问检查考虑 caller、loader、package 和 receiver。
参数赋值、装箱、拆箱与数值 widening 通过共享调用路径完成。

运行时注解从成员所属 DEX unit 解码。
泛型 Signature 交给原版 Java parser；缺失签名回退原始类型。
Proxy 通过原版 Java 校验与有序接口缓存生成实现类。
AnnotationRuntime 仍有受限合成类路径，统一迁移缺口见已知问题。

## 类加载与存量 intrinsic

应用 PathClassLoader 标识封存 APK，父链为 boot→null。
DexFile native 查询已登记单元，不提供动态定义或任意宿主 classpath。
classpath stream 只读取显式封存归档。
loadLibrary 经平台 loader 解析，不自行打开宿主库。

普通 ROM Java 应原样执行，宿主只实现 native 与进程内服务。
当前 catalog 仍有整类定义和带 handler 的普通方法覆盖。
链接器的先登记规则及 merge 路径决定这些定义如何生效。
它们是待移除存量，不是新增替换的许可。

核查时用 CLI 的 dexvm dump-surface 导出实际绑定，
对照 ROM class_def、own method flags 与 catalog 声明。
CLI 参数与输出说明见 [工具入口](../../tools/README.md)。
需要区分 native、abstract 声明与真正覆盖原版方法体。
表面 implemented 只表示有实现路径，不能证明语义或游戏兼容。

## 契约、决定与操作

- [DexVM](../../src/runtime/dexvm/MODULE.md)、[intrinsics](../../src/runtime/dexvm/intrinsics/MODULE.md)
- [JNI](../../src/runtime/jni/MODULE.md)、[装配](../../src/runtime/integration/MODULE.md)
- [有界解释器](../adr/dexvm.md#adr-0017)、[对象图](../adr/dexvm.md#adr-0029)
- [稳定 payload](../adr/dexvm.md#adr-0152)、[堆策略](../adr/dexvm.md#adr-0172)
- [开发参考与验证](../playbook/DEV-REFERENCE.md)、[缺口](../state/KNOWN-ISSUES.md)
