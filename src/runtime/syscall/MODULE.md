# 子模块：runtime/syscall

## 职责

实现 Android ARM Linux syscall 目录、SVC bridge、ARM kernel helper、VFS syscall 适配，以及
exit/exit_group/clear-child-tid 所需的 guest 线程生命周期状态。

## 依赖

依赖 `cpu`、`memory`、`hal` 与 `runtime/vfs`；不得依赖 Bionic、JNI、framework、execution
或 integration。

## 不变量

- `A32SyscallDispatcher::SetDiagnosticObserver` 在 `DispatchOutcome` 分类完成后发布 frame、
  result 与 progress；它与既有业务 observer 分离，未注入时不改变 syscall 行为。
- 未实现和未知 syscall 统一返回 `-ENOSYS` 并可观测。
- syscall 除 Linux 返回值外必须发布 watchdog 进展类别；正字节 read/pread/write/pwrite 与
  真实驻留的 futex wait 为 advanced，查询、EOF/零字节、wake/yield、内存管理及默认均为
  idle。类别清单集中在 dispatcher，未知 family 禁止自动续期。
- guest 地址必须经受检内存访问；时间源只使用统一 Clock。
- 非固定匿名私有 `mmap2` 在 `[0x60000000,4 GiB)` 调用 memory 的原子 first-fit
  映射，依据真实账本避开已占区（包括 TLS/栈/PROT_NONE）；无单调游标或独立空闲表，
  munmap 后空洞自动可复用，失败不消耗地址。brk 状态用独立锁串行化。
  非固定 hint 仍为可忽略建议；匿名私有 MAP_FIXED 调用 `ReplaceAnonymous` 原子替换，
  长度向上对齐到 4 KiB，成功返回请求地址；未对齐/非法参数返回 EINVAL，低地址 guard
  返回 EPERM，范围溢出或 backing 失败返回 ENOMEM。文件映射沿用相同选址/替换原语。
- 文件 mmap2 的 pgoff 单位为 4096 字节；支持 PRIVATE 的 R/RW 与只读 fd 的 SHARED R。
  读取 VFS 节点快照后一次发布内存，尾页补零；失败不消耗地址或破坏固定目标。
  文件执行映射、整页超 EOF、共享回写与其他未支持组合返回 ENOSYS，由 dispatcher 记账；
  SHARED WRITE 的只读 fd 返回 EACCES，非普通文件 ENODEV，坏 fd EBADF。
  文件绑定通过 dispatcher 共享间接状态提供同一个 VFS，先后绑定次序均支持。
  syscall 串行维护文件映射区间，munmap/固定覆盖先准备拆分，再提交内存与区间账本；
  共享 R 区间持有 VFS 节点读保护，最后区间释放或进程 owner 退役时解除。
  mprotect 不允许共享 R 增加写入，也不允许文件映射增加执行权限，返回 ENOSYS 并记账。
  mmap2 接受本模型无可观察差异的提示位 DENYWRITE/EXECUTABLE/LOCKED/NORESERVE/POPULATE/
  NONBLOCK/STACK（bionic pthread 栈带 NORESERVE）；GROWSDOWN、HUGETLB 与未知位 ENOSYS 记账。
  私有文件映射复制期间临时冻结节点，并发写入得到 EBUSY 并记账。
  madvise 保留 WU-0088：advice 0–4、12–15 校验区间，DONTNEED 清零匿名可写页；与文件映射
  区间重叠的 DONTNEED 返回 ENOSYS 记账（Linux 从文件重填，快照无来源），其余 advice 如实成功。
- memory 登记的 provider 保护范围：munmap/mprotect 和 MAP_FIXED 任意部分重叠均返回
  `-EPERM` 并由 owner ledger 记账，整次操作不改映射。mremap 检查原区与 FIXED 目标，
  命中保护范围同样 EPERM；普通 mremap 仍返回 ENOSYS 并记账，没有扩展通用搬移实现。
  errno 沿用本模块低地址/策略拒绝 EPERM、非法参数 EINVAL 的约定。
- `read/write/pread64/pwrite64` 共用 `file_transfer.h`，以 64 KiB 临时缓冲分块，
  不再因请求超过 16 MiB 返回 `-EINVAL`；单次请求截到 `0x7ffff000`。
  短传输即停止，后续块的内存/VFS 错误返回已完成字节，首块错误返回 errno。
  定位 IO 沿用 seek/restore（不承诺并发原子性），错误路径同样恢复原 offset。
- API 19 `nanosleep` 受检读取 32-bit timespec；绑定 signal runtime 时，未屏蔽信号或线程退出
  可中断 host 等待，返回 EINTR，按需写回剩余 timespec；信号留给目标 CPU 安全边界投递，
  SA_RESTART 不重启睡眠。成功时不访问 remainder。只有实际等待后报告 advanced，
  零时长、等待前中断及参数错误为 idle；宿主等待计时使用统一 Clock，不推进 guest ticks。
  可由进程 owner 注入非零睡眠准入回调，在实际 sleep 前拒绝超出退出预算的请求；
  控制失败原样展开，不伪造睡眠成功，默认无回调时语义保持。
- API 19 futex wait 的 timeout 是 guest 32-bit 相对 timespec；超时返回 `-ETIMEDOUT`，
  非法 timespec 与坏指针分别明确返回 `-EINVAL`/`-EFAULT`。
- guest `mmap/mprotect` 可表达解释执行所需的逻辑 RWX；AddressSpace 的宿主 backing 不授予
  execute。`ARM_cacheflush(start,end,0)` 验证范围已映射；SVC bridge 对成功的非空范围
  调用 `Cpu::InvalidateCodeRange`，发布到同进程所有代码缓存后才恢复 guest。非法请求
  和空范围不失效缓存；解释器直接取指。见 [ADR-0088](../../../docs/adr/runtime.md#adr-0088)。
- ARM kuser v5 提供真实 DMB 屏障、32/64 位 compare-exchange 及 TLS；compare-exchange
  包含前后屏障，失去 reservation 时重试，r0 为零且 C=1 当且仅当交换成功。
  64 位入口使用自然对齐目标和 guest 栈，保存非 clobbered 寄存器；访存错误保留 CPU fault。
- `pipe` 必须先验证完整两元素输出数组，再原子创建 VFS descriptor pair；发布失败回收
  两端，不泄漏半完成状态。
- 线程状态只能按 running → exit-requested → exited → reap 前进。exit-requested 必须持久保存
  来源（host/exit/exit_group）、requester、退出码和 syscall PC/LR；进程组退出的每个受影响
  线程共享同一请求事实，供即时错误与后续诊断读取。宿主失败通知可由已 exited 但未 reap
  的线程发起，终止其余存活线程；syscall 来源仍要求活跃调用者，已退出状态不回退。
- 进程 signal runtime 共享 dispositions，按线程保存 mask/pending/备用栈/活动帧；旧与 RT
  action/suspend/return ABI 独立编组；tkill 只接受当前进程活跃 TID，tgkill 额外校验当前 PID。
  两者共用投递及等待唤醒，保留原 syscall/PC/LR 归因。标准信号合并，
  由目标线程安全边界投递，禁止发送方改写目标 CPU。细节与未支持边界见
  [ADR-0079](../../../docs/adr/runtime.md#adr-0079)。SIGABRT 默认终止继续保留退出来源。
- signal return 通过 syscall outcome 显式恢复整个 CPU 状态，bridge 不再覆盖其 r0。
  guest frame 保存 ucontext/VFP；坏帧明确失败。clone mask 继承与线程退役由上层通知。
- action 保留 SA_NOCLDSTOP 供查询；对非 SIGCHLD 无效果，不扩展子进程/作业控制支持面。
- 信号通知不制造 futex WAKE token；目标 wait 的谓词检查 pending/退出，SA_RESTART 仅
  重启无超时 futex WAIT，有超时等待中断为 EINTR。sigsuspend 退出请求可取消。
- fd 0/1/2 与 `/dev/log/*` 由进程内诊断端点表保存访问模式和存活状态；stdin 是无宿主输入的
  EOF 端点，stdout/stderr 的 `write`/`writev` 先受检搬运 guest bytes，再交给
  上层 sink；内核日志使用进程内 synthetic descriptor，不访问宿主设备。单次 payload 上限
  1 MiB，iovec 上限 64，普通 descriptor 仍走同一 VFS。dup 保留端点身份，close 后查询和
  新写入返回 EBADF，不因 fd 数字继续截获；不提供这些端点的 fstat/seek/终端 ioctl。

## 测试

对应 `tests/runtime/syscall_tests.cpp`、guest thread lifecycle 与 SVC bridge 测试。

## 文件元数据与目录（ADR-0020）

VFS 字符设备使用 ARM stat64 的 S_IFCHR、st_rdev（偏移 32）与 DT_CHR；文件大小为 0
不限制 read。open 放行 O_NOCTTY（VFS 没有控制终端）及字符设备 O_NONBLOCK，普通文件
的非阻塞 IO 仍明确 EINVAL。dup(41) 共享 VFS 打开状态。
fcntl(55)/fcntl64(221) 共用 F_GETFL：按真实 VFS/诊断端点状态返回 ARM O_ACCMODE、O_APPEND、
O_NONBLOCK、O_DIRECTORY、O_NOFOLLOW、O_LARGEFILE，不含创建标志与 FD_CLOEXEC。
未知/已关闭 fd 返回 EBADF；其他 fcntl 命令继续 ENOSYS 并记账。
open/openat 显式解码 ARM O_APPEND=0x400，传入 VFS 打开状态，不由 syscall seek 模拟。
writev 完整预检向量/输入，在共享资源预算内合并后只提交一次 Write；向量上限 64，
每段至多 1 MiB，坏输入/超预算在写入前失败。大于 64 KiB scratch 的 append write/
pwrite64 也在共享预算内一次暂存并提交，保持整条追加记录的节点锁；非 append IO
沿用原分块/短 IO 语义。pwrite64 保持顺序 offset，append fd 使用 Linux EOF 写入规则。
不提供 fcntl 状态修改、跨进程/NFS 追加或通用 Linux open flags。

`BindAndroidFileMetadataSyscalls`（`syscall_file_metadata.cpp`）把
mkdir/rmdir/unlink/rename 及其 `*at` 变体、stat64 家族、`getdents64`、
`access`、`ftruncate`、`fsync`/`fdatasync` 与 `pread64`/`pwrite64` 绑到与
`BindAndroidFileSyscalls` 同一个 VFS。`struct stat64` 按 Android ARM 自然对齐
布局（104 字节，非 x86 的打包布局）编组：`st_mode` 在 16、`st_size` 在 48、
`st_blksize` 在 56、`st_blocks` 在 64、`st_ino` 在 96，44 处的填充必须保持 0。
guest libc 的 `__swhatbuf` 就是从 104 字节栈帧的偏移 56 读 `st_blksize`；若按
96 字节打包布局编组，guest 读到的 64 位 `st_size` 高位字会是我们的
`st_blksize`，`fopen`+`fread` 会把每个文件都看成 TB 级并 malloc 失败。
`linux_dirent64` 记录 8 字节对齐且只发完整记录；两者偏移由机器测试锁定。guest `open(O_DIRECTORY)` 取得快照目录 fd；open flags 按
ARM EABI 布局解码（`arch/arm` 的 `O_DIRECTORY=040000`、`O_NOFOLLOW=0100000`、
`O_LARGEFILE=0400000`，不是 asm-generic 值），bionic `opendir()` 的真实组合由
机器测试锁定。记录装不下时回退 cursor 并返回当前页，下一次继续。`fstat64` 直接查询 descriptor 元数据，
不改变文件 offset，目录 fd 的 fsync/fstat 也有明确语义。`st_mode` 的权限位来自 VFS 真实 writable 事实，时间戳
保持 0（唯一时间源是统一 Clock）。`chdir(12)`/`fchdir(133)` 共用 VFS cwd，
`getcwd(183)` 写入含 NUL 路径并返回字节数；缓冲不足 ERANGE，非法 guest 地址 EFAULT，
已删除 cwd ENOENT。`*at` 的 AT_FDCWD 相对路径使用同一 cwd；其他 dirfd 相对路径
仍明确 `-ENOTSUP`。`flock`、`*xattr`、`inotify*` 等维持 `-ENOSYS` 记账。
