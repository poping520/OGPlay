# DexVM 与 ROM Java 执行

DexVM 让应用 DEX 和 API19 ROM Java 在同一个对象模型中解释执行。
它提供类链接、调用、异常、线程和 GC，平台服务通过 integration 注入。
这里 ROM Java 指随附 jar 中的原版实现，BootDex 是启动类路径及其构建加载机制的惯称。
目标是执行游戏进程依赖的字节码，范围不包括完整 Dalvik/ART、DEX JIT 或 AOT。

保留 Java 算法的意义在于副作用也能自然发生。
例如一次 ROM 布局构造会读取资源、创建子对象、注册监听器并排队工作，
这些都不适合用宿主按游戏手工重放。
[有界解释器决定](../adr/dexvm.md#adr-0017)因此选择复用字节码，
代价是 VM 和 native 边界需要提供被调用的真实语义。

## 装配与启动类路径

```text
core / Android catalog → 启动类路径 DEX → 应用 DEX
                                ↓
                      惰性链接与成员物化
                                ↓
                解释执行 / intrinsic / native bridge
                                ↓
                    同一对象图、异常与 GC
```

catalog 是平台类型和绑定的目录，intrinsic 是宿主实现的方法处理器。
装配先登记目录，再按 manifest 归档顺序读取 ogplay.jar 和原样 ROM jar，最后登记应用 DEX。
ReadBootClasspath 读取受检归档，BootClasspathDexFiles 提取 DEX，
每个 unit 保留自己的常量池和解析缓存。

常量池索引只在所属 DEX 内有意义。因此解析跟随方法所属 unit，
不能把另一个 DEX 中数值相同的索引当作同一成员。
DEX 来源使用拥有型存储保活，避免解析缓存引用调用方已经销毁的临时字节。
这种组织多了来源与缓存管理，却让多 DEX 与 ROM/应用混合调用有明确归属。

类加载采用 bootstrap parent-first，同一描述符先登记的定义胜出。
同一类描述符可能同时出现在 boot 与 application 中，选择之后只保留一个权威定义，
应用副本不会向父定义混入字段或注解。若把应用同名字段合并进 ROM 类，即使方法看似能调用，
实例布局、反射枚举和 JNI 字段 ID 也可能相互矛盾。
因此类注册、成员解析与 Class 镜像都围绕确定的 loader 和定义来源，
调用缓存也保留 caller 和所属 unit，不把一个名字或索引当成全 VM 通用成员键。

ROM 身份先登记，层级与成员在触达时物化，
使未使用的可选类不因依赖不完整阻断 VM 装配。
惰性处理只延后有成本的链接，实际触达时仍检查缺类、缺成员及访问规则。

## 一次对象方法调用怎样完成

以 DEX 调用一个对象方法为例，解析器先按 caller 所属 unit 找到类与完整 descriptor。
DexClassLinker 建立父类、接口、实例字段布局、vtable 与 iftable；
vtable 是虚方法分派表，iftable 保存接口调用关系。
方法身份包含名称、参数和返回类型，访问及 static/virtual 种类在对应阶段核对。

若调用要求类初始化，VM 先进入独立初始化状态机。
同 context 可重入，其他线程释放执行锁等待；首次失败保存错误，后续使用按失败类状态传播。
因此缓存命中也不能绕过 caller 权限或失败状态。
类链接与初始化分开，使“知道这个类有哪些方法”不会无条件执行它的 clinit。

调用目标最终进入解释、intrinsic 或 native bridge。
缺失实现记录能力命中并失败；缺类、缺成员和种类错误则按 Java linkage error 表达。
内部坏句柄或执行控制错误保持 VM 故障身份，
以便区分应用能 catch 的异常和需要中止兼容层执行的故障。

方法结构预检检查 opcode、寄存器、wide pair、分支和 payload。
wide pair 是占用两个 DEX 寄存器的 64 位值，payload 在这里指 switch/数组等指令附属数据。
这是一项有界结构检查，不是完整 Dalvik 数据流 verifier。
当前装配和加载支持边界见 [DexVM 契约](../../src/runtime/dexvm/MODULE.md)。

## 两种解释内核为什么共用语义

默认 switch 内核逐条解释 DEX。threaded 内核从已预检字节码建立 FastCode，
即用于快速分派的派生指令缓存；GCC/Clang 使用 computed goto，MSVC 使用稠密 switch。
原 DEX 不改写，FastCode 不生成机器码，也不保存 guest 对象引用。

两种内核共用调用、异常、类初始化、对象和安全点逻辑，FastCode 保留回到 DEX PC 的映射。
这样加速的是分派，不需要维护第二套对象语义，catch 范围和诊断位置仍能指向原字节码。
派生缓存与 bridge 路径需要保留，不适合快径的操作继续进入共享实现。

执行 frame 保存方法、寄存器与类别 tag，避免把引用和同位型整数互换；
Interpreter::Call 将返回值或未捕获异常显式交给调用者，
而不是依赖外层猜测寄存器含义。

## 对象图与 native peer

JavaObjectModel 使用非移动逻辑句柄，零表示 null。
实例和静态字段保存普通 Java 状态的唯一权威，对象数组由共享 store 保存元素身份。
JNI 字段访问、Java 字节码和反射因此能够看到同一个对象，而不需要同步 C++ 影子字段。
数组协变由元素实际类型判断，对象数组不会仅按声明 component type 生成错误对象身份。
clone 创建新身份并浅拷贝引用，host-backed 对象另有显式清理策略。

native peer 指 Java 对象关联的下层资源，例如文件、Canvas 或媒体播放器。
owner 是负责该资源寿命的对象；state table 是登记这类关联状态并参与 trace/sweep 的表。
[所有权决定](../adr/dexvm.md#adr-0029)选择“owner 存活才追踪 child”，
避免仅因为 C++ 表以对象为 key，就反过来永久保活 owner。
为此每个持有 guest 引用的附属状态都要有 trace，owner 表都要有 sweep；
trace 枚举可达引用，sweep 在回收时释放附属状态。

回收与关闭不是同一个动作。GC 判断 Java 可达性，显式 close 表达应用释放资源，
进程 teardown 则终止剩余使用者并收尾所有资源。
这三条路径（以及 finalizer）可能先后到达同一个 native peer，
所以它们共享关闭事实而各自保留触发条件，同一资源也不会被重复释放。

## 稳定 payload 为什么不复制写回

String 与 primitive array 的 payload 指对象实际内容字节。
JavaPayloadProvider 在 guest 地址空间分配稳定 backing，
数组记录、长期 typed view、Java/JNI 和 NIO 共同使用这份存储。
对象头、逻辑句柄和 GC 仍由 VM 管理，并不引入完整 Dalvik heap ABI。

如果每次 JNI Get 都复制，native 长期借用与 Java/NIO 访问就可能观察到不同内容。
[稳定 backing 决定](../adr/dexvm.md#adr-0152)因此选择直接共享字节，
代价是需要保护映射寿命并正确处理 pin，而不是依靠 Release 时的覆盖写回。
共享字节没有额外承诺无同步数据竞争的顺序。

pin 是将对象作为 GC 强根的借用登记，borrow 则保活 backing 和其 owner。
设想应用建立一个 primitive array，传给 native，随后再用 Java 读取：
数组对象身份保存在 VM，native 得到的是 JNI 引用；
GetByteArrayElements 才借用实际 payload 并登记 pin，
Java 数组指令与 native 指针最终访问同一份字节。

```text
Java array handle ─→ 数组记录 ─→ stable payload ←─ JNI Elements 地址
          ↑                          ↑
       GC 强根 / pin             Java / NIO 访问
```

图中的数组引用、借用地址和保活关系各不相同。
GetByteArrayElements/GetStringChars 返回相同内容地址，重复借用分别计数；
Release 解除一次 pin，既不把字节复制回第二个数组，也不恢复借用前内容。
Modified UTF-8 是另一种编码表示，需要编码副本与配对释放，
可见“共享对象”并不等于所有表示都能零转换。
零长对象也可以有合法借用地址，空长度与 null 身份分别表达。

如果 native 忘记 Release，GC 仍将对象视为强可达，真实内存会一直保留；
不能为了让内存数字下降就回收仍被 native 使用的 backing。
诊断把逻辑堆、后备提交、地址空间和 pin 分开计量，以便区分压力来源。

String backing 不可变，可变输入和 substring 使用独立快照。
allocateDirect 与 NIO view 共用真实数组 backing；外部 direct 地址则是非拥有借用。
这些路径的精确 Release、映射保护与收费规则留在
[JNI 契约](../../src/runtime/jni/MODULE.md)和[装配契约](../../src/runtime/integration/MODULE.md)。

## GC 怎样找到仍在使用的对象

GC 使用分配驱动的精确 STW mark-sweep。
STW 表示收集时暂停相关 Java 执行，mark 沿强根标记对象，sweep 回收不可达对象及附属资源。
根包括执行 frame、静态字段、JNI、线程、类镜像、intern 和 integration 明确登记的活动对象。
嵌套调用用 RootScope 保护临时引用，普通宿主局部变量不会自动成为根。

静态引用扫描只读取已分配槽，不为 GC 主动物化类。
这避免回收意外触发链接和初始化副作用，也使惰性装配与 GC 边界一致。
精确扫描要求 frame 中引用 tag 和各 state table 的 trace 完整，
新增附属对象状态时需要同时明确其可达关系。

VM Reserve 是 Java 堆的唯一收费入口。initial target、growth limit 与 maximum 分别表示
初始目标、通常增长上限与最大上限；先尝试回收，再按策略增长。
JNI 创建与导入复用这份账本，payload provider 只管理后备提交，
避免逻辑对象和实际存储各自重复收取一份堆额度。

[堆档位决定](../adr/dexvm.md#adr-0172)让默认限制与虚拟 API19 设备事实一致，
Manifest largeHeap 按 ROM 绑定应用顺序放开活动上限。
这避免按某个游戏临时扩堆；应用真实 OOM 则需要保留分配证据，不能回退更大旧档位。

Weak、Soft、Phantom 与 finalizer 按既定顺序处理。
FinalizerReference 的登记和队列留在 Java，对应 Daemons 在真实线程执行；
runFinalization 等待进入时的批次，不无限追逐后来新增任务。
finalizer 需要真实线程执行并可能调用其他对象，不能在 sweep 中同步重放 Java 回调。
把 finalizer 图留在 ROM、把 native sweep 留在资源层，多了一个交接协议，
但收集器不必反向依赖任意 guest 执行；native sweep 也独立承担资源安全回收，
不寄望 Java finalizer。

## Java 线程与执行锁

一个 guest Thread 对应一个宿主线程和 execution context，即该线程的 VM 执行身份与栈状态。
Java 字节码由可重入 VmExecutionLock 串行解释，阻塞时释放全部递归深度，恢复后重建。
这允许其他线程在当前线程等待 I/O 或 native 时进入 Java 回调，
同时控制对象图并发修改的复杂度，理由见[真实线程决定](../adr/runtime.md#adr-0004)。

monitor 用 context token 标识持有者，wait 释放并恢复全部重入层数。
notify、interrupt、shutdown 与 timeout 保留不同唤醒原因，
非持有者操作按 Java 规则抛 IllegalMonitorStateException。
期限来自统一 Clock，关闭预算不会因重复等待被续期。

JNI attach 登记已有宿主线程，不再新建线程。
释放执行锁不改变 context 的宿主归属，类初始化等待、monitor 和 native 阻塞都遵守同一锁协议。
停止先发布 stop/wake，再 join，最后销毁 context，
以免仍在等待的线程观察到已释放的 VM 状态。

## 异常、反射与加载边界

throwable 保存对象身份、类、消息和 DEX 栈位置。
catch 按类型匹配，跨帧展开与 JNI pending 交接使用同一异常对象。
Method.invoke 对目标 Java 异常进行标准包装，宿主控制故障继续沿控制路径传播。
诊断读取 cause 不执行 guest，避免观察异常本身又引发新副作用。

ReflectionRuntime 是成员 metadata 与 wrapper 的唯一工厂，
反射 Method 只是成员身份的一种 Java wrapper。
反射、JNI 和字节码共享元数据工厂与调用逻辑，访问检查、参数适配与目标异常的差异
限制在各入口协议中，而不是为每个入口再实现一次方法体。
定向查询只解析候选，枚举时再物化完整集合；调用复用访问检查、装箱、拆箱与数值 widening。
Class 镜像保留代表类型的 VM 身份，实例槽存储 ROM 字段并参加 GC 扫描。
Class 与 AnnotationAccess 的普通方法执行 ROM。Class.getDex 首次访问时将所属 DEX 映射到
只读 guest 页，交给 ROM Dex.create，并按 DEX 单元保活结果。泛型 Signature 由原版 Java parser 处理。
Proxy 使用 ROM 校验和有序接口缓存，AnnotationRuntime 仍有受限合成路径，
其统一类身份缺口见[已知问题](../state/KNOWN-ISSUES.md#执行与平台)。

应用 PathClassLoader 标识封存 APK，父链为 boot→null。
DexFile native 查询已登记单元，classpath stream 只读取显式归档，
loadLibrary 交给平台 loader。这保留稳定的 application/boot 边界，
动态 classpath、自定义加载权限等完整 VM 能力则不在当前范围内。

## 存量 intrinsic 与支持边界

ROM 普通方法应执行原样 Java，宿主实现 native 和进程内服务。
当前 catalog 仍存在整类定义和普通方法 handler 覆盖，
链接器的先登记及 merge 路径决定它们如何生效。
这些是待移除的存量，不能据此增加新的 ROM 替换。

核查入口是 CLI 的 `dexvm dump-surface`，导出实际绑定后对照 ROM class_def、
own method flags 与 catalog，区分 native、abstract 声明和真正覆盖的方法体。
类可链接、表面 implemented 与实际行为是三个不同事实，
[未实现成员决定](../adr/dexvm.md#adr-0147)要求在实际执行边界明确暴露缺口。

精确命令见[工具入口](../../tools/README.md)，未闭合替换与验证范围见
[ROM 替换问题](../state/KNOWN-ISSUES.md#rom-replacements)。
本文解释的是当前执行机制，不是完整 API19 或游戏兼容验收声明。

## 契约、决定与操作

- [DexVM](../../src/runtime/dexvm/MODULE.md)、[intrinsics](../../src/runtime/dexvm/intrinsics/MODULE.md)
- [JNI](../../src/runtime/jni/MODULE.md)、[装配](../../src/runtime/integration/MODULE.md)
- [有界解释器](../adr/dexvm.md#adr-0017)、[对象图](../adr/dexvm.md#adr-0029)
- [稳定 payload](../adr/dexvm.md#adr-0152)、[堆策略](../adr/dexvm.md#adr-0172)
- [开发参考与验证](../playbook/DEV-REFERENCE.md)、[缺口](../state/KNOWN-ISSUES.md)
