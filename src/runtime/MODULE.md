# 模块：runtime

## 职责

装载真实 Bionic，并实现 syscall、完整 JNI/JavaVM、可选 DEX 解释器和 Android 框架 HLE。

## 公共 API

- `A32SyscallDispatcher`：按 ARM EABI 号分派 syscall；声明与实现分离，未知或未实现调用
  统一记入能力账本并返回 `-ENOSYS`；M2 目录覆盖 120 个常见调用并可按组查询覆盖率，
  observer 在所有返回路径收到 frame/result 供无头报告与诊断使用。
- `DispatchAndroidArmSupervisorCall`：只将 `SVC #0` 按 r7 + r0-r6 ARM EABI 转为
  syscall frame，并保留调用时完整 A32 状态，写回有符号返回值到 r0；其他 SVC 留给
  显式 HLE 边界。
- `BindAndroidTimeSyscalls`：将 `clock_gettime/gettimeofday` 绑定到统一 Clock 和受检 guest
  内存；错误 clock ID、溢出和坏指针分别返回 Linux errno。
- `BindAndroidMemorySyscalls`：实现匿名私有 `mmap2`、`munmap`、`mprotect`、`brk` 与
  `madvise` 基础 advice；支持 `PROT_NONE` guard page，`MADV_DONTNEED` 确实清零可写
  匿名页，页对齐、地址范围和 W^X 失败均转换为明确 Linux errno。
- `BindAndroidThreadSyscalls`：把 `futex WAIT/WAKE` 装配到 M1 真线程 FutexTable，支持
  PRIVATE flag、精确 WAKE N、值不匹配和受检地址错误；`sched_yield` 让出宿主线程。
- `BindAndroidSignalSyscalls`：按 guest thread 隔离 legacy/RT signal mask 与 alternate
  signal stack；不可屏蔽信号、stack 启停、指针/尺寸和 Linux errno 均受检。
- `BindAndroidProcessSyscalls`：实现 API 23 allocator 使用的 `PR_SET_VMA_ANON_NAME`；
  `PROT_NONE` 范围也按映射存在性校验，名称通过显式 sink 进入运行报告而非丢弃。
- `BindAndroidArmPrivateSyscalls`：将 `__ARM_NR_set_tls` 绑定显式的当前 guest 线程
  pointer setter；未知线程与 setter 失败返回明确 errno。
- `BindAndroidThreadLifecycleSyscalls`：将 `set_tid_address/exit/exit_group` 绑定统一
  guest 线程状态机；exit 只发出停止请求，由执行循环完成宿主线程退出。
- `BindAndroidCloneSyscall`：只接受 pthread 所需的共享地址空间 ARM `clone` 形态，按
  flags 条件解码 parent/child TID 与 TLS 指针，保留完整 parent CPU 上下文，并交给显式
  spawner 提交线程创建。
- `GuestThreadCloneCommitter`：串行预检 parent/child TID guest 写入，按 Linux flags 写回
  TID，并以 TLS/clear-child-tid 原子注册 child；失败返回 errno 且不发布 child 状态。
- `GuestCloneThreadRuntime`：从 clone 保存的 A32 状态派生 child 的 r0=0、8 字节对齐 SP、
  TID 与 TLS；未请求 `CLONE_SETTLS` 时继承 parent TPIDRURO，通过 GuestThreadGroup 启动
  真实宿主线程并进入统一 SVC/exit 执行循环。
- `SelectBionicProfile` / `RouteBionicSymbol`：只接受 API 19/22/23，固定真实 guest Bionic
  库、宿主 HLE 边界库及少而明确且确有 handler 的 mem 拦截表；pthread 保持真实 Bionic
  ABI，并在 clone/futex/TLS syscall 边界映射到宿主真线程。
- `ExecuteBionicMemoryIntercept`：受检执行 memcpy/memmove/memset/memcmp/strlen；完整范围
  预检、重叠方向、字符串上限、A32 返回值和吞吐基准均有契约。
- `BionicHleSymbolProvider` / `BuildBionicLinkNamespace`：在固定 HLE thunk 区注册可反查的
  边界符号，将 libc 选择性拦截、虚拟边界库和真实 guest ELF 装入统一链接命名空间。
- `SelfCheckBionicProfile`：对 API 19/22/23 的真实 libc/libdl 执行完整多模块映射、版本化
  解析与重定位，并核验 malloc、pthread、文件 IO 出口及 exidx 等运行事实。
- `CreateBionicTlsBlock` / `DestroyBionicTlsBlock`：按 API 19/22/23 的 64 个 ARM Bionic
  TLS slot 契约建立线程独立 guest block，初始化 self/thread/preinit 并提供 TPIDRURO 基址。
- `BuildGuestInitializationPlan` / `BuildGuestFinalizationPlan` / `ExecuteGuestLifecycle`：
  把统一链接器的模块顺序展开为加 load bias 的 DT_INIT/array/DT_FINI guest 调用并执行。
- `GuestThreadLifecycle`：线程安全保存 running/exit-requested/exited、thread pointer、
  clear-child-tid 与退出码，原子校验 parent 并注册 child，提供单线程/进程组退出、写零 +
  futex 唤醒和显式 reap 状态机。
- `RunAndroidArmGuestThread`：按 tick 预算运行 CPU，循环消费 Linux SVC，并在 exit 请求
  后完成 clear-child-tid/futex 清理；每次 syscall 后把 lifecycle 中的新 thread pointer
  提交给当前 CPU，其他 trap 和未处理 SVC 原样返回。
- `RunHeadlessBionicEntry`：事务装载无界面 NDK 依赖闭包，建立 root TLS/stack、运行 guest
  init/普通 C 入口/fini，并把真实 pthread child、syscall 账本及 VFS 输出汇总为出口报告。
- `RunHeadlessJniContract`：运行无图形累计契约样本，以注入的 guest native executor 闭合
  HLE→native 与 native→Activity HLE，并核验 VM、字段、字符串、数组、引用和异常资源。
- `MapArmKernelHelpers`：映射 Linux ARM 最后一页兼容 ABI，提供 memory barrier、32 位
  cmpxchg、TPIDRURO 与版本字；其他 helper 地址预填显式 trap，不执行零指令。
- `VirtualFileSystem`：以规范化 Android 绝对路径建立 ASCII 大小写不敏感索引，提供
  APK/OBB/外置统一事务挂载、来源事实与隔离文件描述符 open/read/write/seek/close；
  路径逃逸和权限错误携带 Linux errno，只有外置挂载默认可写。
- `BindAndroidFileSyscalls`：将 `open/openat/read/write/lseek/close` 绑定到 VFS；路径和
  buffer 均从受检 guest 内存复制，坏指针及 VFS 错误转换为 Linux errno。
- `JniReference/JniMethodId/JniFieldId`：固定为 32 位 guest opaque handle，不继承
  64 位宿主指针宽度；JNI primitive 使用 Android ABI 的精确宽度。
- `JniNativeInterfaceSlots`：以 Android NDK `jni.h` 冻结索引 0..232 的 233 槽目录，
  包含 4 个保留槽和 229 个函数槽，可按名称双向查询。
- `JniFunctionTable`：初始化期显式绑定函数 thunk 后封口；未绑定函数调用记录稳定
  capability ID 与 LR 并抛出 `JniUnimplementedCall`，不得返回伪造值。
- `JniReferenceTable`：以单一 32 位 handle 空间管理线程/frame 独占 Local、进程共享
  Global 与可清除 WeakGlobal；对象身份显式区分 host 与未来 DEX VM 来源。
- `JniExceptionState`：按 guest 线程保存 pending throwable 对象身份；提供
  Throw/Occurred/Check/Clear 底座，并在 pending 时只放行检查、清理和资源 release 白名单。
- `JniEnvironment`：原子装配线程的引用表与异常状态，闭合 GetVersion、local frame、
  Local/Global/WeakGlobal 引用和异常检查/清理等首批常用 JNIEnv 操作。
- `JniJavaVm`：冻结 8 槽 JNIInvokeInterface，闭合 GetEnv、AttachCurrentThread、
  AttachCurrentThreadAsDaemon 与 DetachCurrentThread 的稳定 env 和 daemon 语义。
- `ParseJniFieldDescriptor/ParseJniMethodDescriptor`：解析 primitive、对象、255 维以内数组
  与 void 返回，输出参数槽数，拒绝非法类名、尾随内容和超过 255 参数槽的方法。
- `EncodeJniModifiedUtf8/DecodeJniModifiedUtf8`：按 UTF-16 code unit 严格编解码 JNI
  Modified UTF-8；NUL 使用 `C0 80`，代理项保持六字节形式，拒绝四字节 UTF-8 与过长编码。
- `JniStringStore`：以统一 host 对象身份保存 UTF-16 字符串，提供 UTF/UTF-16 长度、
  region、chars/UTF/critical 租约及配对 release，供常用字符串槽共享。
- `JniPrimitiveArrayStore`：统一管理八种零初始化 primitive array，提供类型化 region、
  elements/critical 副本及 copy-back、commit、abort 释放模式。
- `JniObjectArrayStore`：保存对象身份及其声明类，按 Java 父类关系约束初值和逐元素写入，
  null、越界、未知数组和不兼容赋值均有独立语义。
- `JniNativeRegistry`：按类身份、方法名和严格描述符事务注册 guest native 地址，
  支持重载精确查询、幂等重注册、冲突拒绝与按类 UnregisterNatives。
- `JniClassRegistry`：事务装载声明式类、方法和字段，提供稳定强类型 ID、父类查找、
  可赋值判断及遵循继承/静态/构造器规则的成员查找。
- `JniFieldStore`：按 descriptor 生成 Java 默认值并隔离实例字段，静态字段按声明共享；
  访问统一检查字段类别、对象类兼容性和精确值类型。
- `JniInvocationEngine`：把 `...`/`V`/`A` 参数源归一化为类型化值，按描述符校验参数
  与返回值，并分别执行虚调用、指定类非虚调用和静态调用。
- `JniCommonSlotDirectory`：把已有环境、类、调用、字符串、primitive/object array、字段、
  native 注册与 JavaVM 行为映射到稳定 thunk 并封口函数表；未覆盖槽继续记账并 trap。
- `FrameworkLifecycleHle`：声明式安装 Object、Context、ContextWrapper、Bundle、Activity
  类与生命周期方法；Activity 通过 ContextWrapper 继承 Context，资源服务可在独立 HLE
  安装后接管 `Context.getAssets`，未安装时明确报告 missing handler，
  通过统一 invocation handler 执行严格状态迁移并输出确定性事件序列。
- 子域按 `bionic/syscall/jni/dex/framework` 分文件，禁止巨型 dispatcher。

## 不变量

- 未实现调用记入能力账本并明确失败；syscall 返回 ENOSYS。
- 普通 libc/libm/libdl 符号默认执行真实 guest Bionic；只有声明表命中才进入宿主拦截。
- HLE 符号必须由 provider 显式注册且位于固定 thunk 区；边界库不得作为真实 guest ELF 装载。
- Bionic TLS slot 0 必须自指，slot 1 必须指向非空 guest `pthread_internal_t`；其余 slot
  除可选启动 preinit 外均零初始化。
- lifecycle 在调用前完整校验；init array 正序、fini array 逆序，0/-1 哨兵跳过且地址
  回绕明确失败。
- guest 线程状态只能按 running → exit-requested → exited → reap 前进；ID 在 reap 前
  不得复用。
- clear-child-tid 清理失败不得回退 exited 状态；坏地址和未对齐地址通过 completion
  状态显式报告，成功时最多唤醒一个 waiter。
- JNIEnv 保持完整 233 槽 ABI；M3 按常用度分批实现，任何未实现槽都必须记账并 trap，
  不得静默返回零或从表中删除。
- JNI 表只能在 guest 启动前绑定并封口；四个保留槽始终为空且不可调用。
- JNI reference、method ID 与 field ID 必须保持不同强类型，公共 API 不暴露宿主指针。
- Local 引用不得跨 guest 线程使用，detach/pop 必须销毁所属引用；Global/WeakGlobal
  跨线程共享但删除类别必须匹配，容量与失效访问都明确失败。
- pending exception 不得被第二个 Throw 静默覆盖；普通 JNI 调用必须在执行前经过线程
  异常门禁，detach 后不得残留 pending 状态。
- JniEnvironment 附着任一子状态失败时必须回滚，引用与异常线程状态不得分裂。
- JavaVM 不支持的 JNI 版本必须返回 JNI_EVERSION，未附着线程返回 JNI_EDETACHED；
  重复 attach 返回原 env，且不得改变首次附着的 daemon 属性。
- GetFieldID/GetMethodID 与所有调用变体必须共用严格描述符解析结果，不得各自猜测参数布局。
- NewStringUTF/GetStringUTF* 必须共用 Modified UTF-8 编解码器；不得把标准 UTF-8
  四字节序列或原始 NUL 当作 JNI Modified UTF-8 payload 接受。
- chars/critical 访问必须用对象、访问类别和 token 精确配对；存在活动访问时不得销毁对象。
- primitive array region 与 release 数据必须保持元素类型和长度；commit 保留租约，
  abort 不回写，普通 release 回写并关闭租约。
- object array 的非 null 元素必须携带已注册声明类，且可赋值到数组创建时冻结的元素类。
- RegisterNatives 必须先验证整批声明再提交；同键不同地址不得覆盖，重载必须按描述符区分。
- 类必须按父类优先注册；成员键由名称和严格描述符组成，构造器不得从父类继承。
- 字段值必须与声明 descriptor 精确匹配；实例字段按对象身份隔离，继承到子类的静态字段
  仍只有声明对应的一份存储。
- 三种调用变体必须进入同一参数校验路径；虚调用按 receiver class 选 override，
  非虚调用固定 method ID 声明实现，静态/实例类别不得混用。
- 只有存在真实下游行为的槽才允许进入 thunk 目录；Install 后两张表同时封口，
  任何未绑定 JNI/JavaVM 槽必须保留原有可观测失败路径。
- 框架类绑定声明式；对象模型允许宿主对象与未来 VM 对象共存。
- Activity 生命周期必须按 construct→create→start→resume→pause→stop→destroy 前进；
  跳步、重复构造和未知对象调用不得静默接受。
- 无界面 JNI 契约只有在双向调用、数据、引用、异常和 VM detach 全部闭合时才允许成功。

## 禁止

- 不实现 Binder/system_server/Zygote/完整 ART。
- 不包含包名、厂商名、绝对补丁地址或游戏专属 Java 回调。

## 测试

`tests/runtime/` 契约基准来自 AOSP/真机行为。
