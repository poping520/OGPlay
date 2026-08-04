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
- `MapArmKernelHelpers`：映射 Linux ARM 最后一页兼容 ABI，提供 memory barrier、32 位
  cmpxchg、TPIDRURO 与版本字；其他 helper 地址预填显式 trap，不执行零指令。
- `VirtualFileSystem`：以规范化 Android 绝对路径建立 ASCII 大小写不敏感索引，提供
  APK/OBB/外置统一事务挂载、来源事实与隔离文件描述符 open/read/write/seek/close；
  路径逃逸和权限错误携带 Linux errno，只有外置挂载默认可写。
- `BindAndroidFileSyscalls`：将 `open/openat/read/write/lseek/close` 绑定到 VFS；路径和
  buffer 均从受检 guest 内存复制，坏指针及 VFS 错误转换为 Linux errno。
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
- JNIEnv 全表完成前，缺槽位必须 trap，不得静默返回零。
- 框架类绑定声明式；对象模型允许宿主对象与未来 VM 对象共存。

## 禁止

- 不实现 Binder/system_server/Zygote/完整 ART。
- 不包含包名、厂商名、绝对补丁地址或游戏专属 Java 回调。

## 测试

`tests/runtime/` 契约基准来自 AOSP/真机行为。
