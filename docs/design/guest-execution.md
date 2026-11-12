# guest 执行环境

执行环境把 ARM 应用代码与真实 Android Bionic 组织成一个可在宿主运行的进程。
CPU 负责指令语义，memory 负责地址空间与访问权限，syscall 提供内核可见行为，
integration 将这些机制装配起来。guest 是应用看到的 Android 环境，宿主是承载它的系统；
这两者的地址、线程号和页面粒度分别管理。

以原生线程读取一个文件为例：Bionic 在 ARM 中准备参数，执行 SVC，syscall 通过 VFS 读取，
再把结果写回 guest 内存。普通函数继续执行 guest 指令，只有需要外部能力时才越过边界。
这样既保留库内部 ABI，也让文件和内存错误有明确的检查位置。

## 指令怎样进入执行循环

```text
guest ELF → Loader 映射与重定位 → 每线程 CPU
                                  ├─ 普通指令 → MemoryBus
                                  ├─ SVC #0 → syscall → VFS / 内存 / 同步
                                  └─ 显式 SVC → native / JNI 边界
```

Loader 先建立 ELF 的映射、符号关系与重定位。CPU 随后从 guest PC 取指，
保存 A32/T32 核心寄存器、CPSR、VFP/NEON 与线程指针。
SVC 是 ARM 请求外部服务的指令；SVC #0 使用 Linux EABI 调用号，
其余已声明入口由上层注入的处理器解释。

主执行后端 Dynarmic 将 ARM 指令动态翻译为宿主代码；解释器保留为确定性参考与诊断后端。
两者共用状态与停止结果，后者的指令覆盖较窄。
执行循环区分预算用尽、陷阱、能力缺口、非法指令和内存故障，
因而可以判断是应用执行到了坏指令，还是后端尚未提供该指令。

CPU 经 MemoryBus 访问内存。MemoryBus 是执行器所需的受检访存接口，
不认识文件名、Java 类或图形服务。HostCallHook 是可选的宿主调用钩子，
只报告已处理、未处理或故障，让上层能够连接 fast call 而不把 Android 语义放入 CPU。
分层理由见[运行时模块决定](../adr/runtime.md#adr-0013)。

## 地址空间与宿主页为何分开

memory 维护独立的 32 位 guest 地址账本。4 GiB 地址预留与实际后备提交分开，
低地址区保留为空，以便非法空指针附近访问形成明确故障。
GuestAddress 表示受检地址，GuestRange 表示半开区间；长度与加法在访问前检查溢出。
这些类型让“地址”和“字节数”在跨层传递时仍有清晰含义。

Android guest 页固定为 4 KiB，宿主可能使用更大的后备页。
如果直接把宿主 mprotect 粒度作为 Android 权限，修改一页就可能连带改变相邻 guest 页。
因此权限在 guest 页账本中独立记录，宿主后备保持可读写且不可执行，
取指、读、写分别按 Android 权限检查。

这一选择来自[分页决定](../adr/runtime.md#adr-0011)。
它支持不同宿主页大小，代价是执行器需要经过受检接口或受保护数据页表，
无法把任意 guest 地址当作长期有效的宿主指针。
MemoryFault 会保留地址、访问种类、原因与线程身份，便于沿调用链归因。

## 受检访存与 JIT 快径

常规 MemoryBus 路径提供权限检查和观察器。JIT 对满足条件的页可以使用直接数据页表，
省去每次访存的通用分派：写表只发布无观察器的 RW 非执行页，
独立读表还能发布 R/RX 数据页。跨页、取指、带观察器或权限不符的访问仍回到受检路径。

跨页访问说明了权限权威在哪里。考虑四字节读取从 guest 页尾开始，后半段落入下一页。
即使宿主恰好把这两页放在同一个较大后备页中，guest 第二页仍可能未映射或没有 read 权限，
执行器不能仅凭第一个宿主指针可读就完成整次访问。

```text
guest 第 1 页：可读  [ ... | 两字节 ]
guest 第 2 页：无权限 [ 两字节 | ... ]
                           ↓
                   受检跨页路径 → fault
```

因此跨页访问回到 MemoryBus，错误保留实际地址和访问种类；
启用观察器时同一读取也回到受检路径，诊断不会因为快径而漏掉访问。
直接页表只是权限账本的一种发布形式，不是可以单独存在的第二套内存权威。

快径与慢径共享同一映射权威。映射、保护、替换和恢复快照会同步更新页表并发布范围通知，
消费者据此失效旧代码或视图。快径的复杂度集中在生命周期协调，
特别是并发直接访问与卸载的完整 quiescence 尚未验收，见[已知问题](../state/KNOWN-ISSUES.md#执行与平台)。
quiescence 指确认所有在途访问已经离开旧映射的静默状态。

映射替换先准备后备并初始化，再发布映射、权限和世代。
世代是识别映射是否已被修改的版本标记。某 handler 已检查输出可写，
另一个线程随后改变映射，最终提交就不能继续使用旧结论：
写入预检取得的票据绑定这个版本，提交时若版本变化就重新验证，
避免“检查通过之后，地址已被重用”的输出错误。
这不等于所有 native 调用都能回滚外部副作用；提交仍可能因为映射变化失败，
诊断需要保留这种失败，不能把它报成正常写入。
Java 稳定 payload 的受保护 owner 还会阻止覆盖仍被借用的范围。

## 代码缓存与独占访问

JIT 编译结果依赖原映射。删除映射、撤销权限或复用地址后，
范围失效通知会排队到存活 CPU，由各执行线程清理旧块。
普通数据写入则不自动推断为自修改代码；guest 通过 cacheflush 表达代码更新。
这是把映射生命周期与内容修改分开，避免每次写数据都触发代码失效。

同进程 CPU 共享执行 context，其中协调 processor identity、exclusive monitor 与可选预算。
exclusive monitor 记录 LDREX/STREX 一类独占访问的 reservation。
提交使用真实宿主原子比较交换，并继续检查地址、对齐和权限；
单靠只覆盖回调的互斥锁无法与 JIT 直接写竞争。

值比较仍检测不到“普通直接写改值后又恢复”的 ABA。
当前实现没有完整模拟 ARM reservation granule 的所有写入历史，
这项边界留在[CPU 契约](../../src/cpu/MODULE.md)。
文档中的共享 monitor 描述因此不等于完整硬件内存模型承诺。

## 为什么运行真实 Bionic

[真实 Bionic 决定](../adr/runtime.md#adr-0002)把普通 libc、libm 和 pthread 算法留在 guest。
pthread 对象布局、allocator 和 TLS 都与 Android 库版本有关；
若在宿主重写对应函数，就需要同步维护这些私有 ABI。
在 syscall 边界适配，可以继续使用库自身的结构和清理协议。

Loader 为 guest 建立唯一 ELF namespace，即参与符号解析的一组库及其查找关系。
系统库来自随附 payload，应用库来自选定 ABI 的 APK 或显式 VFS 路径。
libdl 查询也使用这份 namespace，ARM SO 的符号不会交给宿主动态链接器解析。
Virtual SO 是提供合成导出的边界库，其归属与真实 libc 的局部热点拦截分开。

进程初始化建立栈、TLS、环境页和返回 trap。
环境变量、时区、CPU 核数与标称频率属于注入的 guest 事实，
而不是从宿主任意拼接。底层可以描述多个 Bionic 基线，
通用 APK 启动使用 API19；某基线可装配不代表相应平台已验收。

## 一次文件 syscall 的往返

Bionic 的文件调用按 ARM EABI 把调用号与参数放入寄存器，执行 SVC #0。
分派器解码参数，文件 binding 将 guest 路径交给唯一 VFS，
按受检内存接口写入结果并返回字节数或 Linux errno。
未知 syscall 会记录缺口并明确返回错误。

这条路径让 Java IO、原生 IO 与数据库共享文件身份和沙盒规则。
同理，匿名映射与 brk 使用同一 AddressSpace，时间调用使用统一 Clock，
proc 视图从进程事实和映射快照生成。各类 syscall 因此都需要精确的参数与错误映射，
不能把宿主同名 API 的返回值直接当作 Android 行为。

## pthread 创建与等待

```text
Bionic pthread_create
  → clone syscall：检查 flags / TLS / TID 输出区
  → 登记 child，派生寄存器、栈和 TLS
  → 一个新宿主线程 + 独立 CPU
  → 共享地址空间与进程执行循环
  → 退出通知 / clear-child-tid / join
```

clone 只覆盖 pthread 所需的共享地址空间形态。
child 从父状态派生，获得自己的返回值、SP 和线程指针；发布失败回滚登记及 TID 写入。
退出由 runner 和线程组完成收尾，而不是从 guest exit 直接结束承载整个兼容层的宿主进程。
选择原因见[pthread 决定](../adr/runtime.md#adr-0010)。

FutexTable 以对齐 guest 地址为键，先比较内存值再登记等待。
WAKE N 的许可只交给已有 waiter，未来等待不会消费旧许可。
定向信号只唤醒谓词检查，关闭中断则保留粘性，解除当前和后续阻塞。
这些唤醒原因分开，才能在上层映射成正确 errno，而不是把所有返回都当作正常唤醒。

真实线程使 native 阻塞、Java 回调与音频工作可以交错发生，
代价是共享内存、错误传播和 teardown 都要处理并发。
Java 字节码仍在可重入 VM 执行锁内串行解释，
完整释放与恢复阻塞锁深度的机制见[DexVM](dexvm.md)。

## Clock 与执行预算

Clock 是会话唯一时间入口，CPU tick 表示执行量预算而不是 Android uptime。
可选进程执行预算用 FIFO 准入和 token bucket 控制原生 CPU 并发及 tick 速率，默认关闭。
启用时边界调用交回 runner 释放名额，关闭先解除预算等待。
这种预算不限制所有宿主工作，也不改变虚拟 CPU 查询事实。

生命周期期间若驱动释放 VM 锁，等待者通过注入的真实流逝时间补偿 Clock，
补偿受各自截止时间约束。直接跳到截止时间会把短暂阻塞误判成超时，
因此采用[停摆补时决定](../adr/runtime.md#adr-0171)的收敛规则。
时间读取仍集中在统一服务中，DexVM 本身没有第二个宿主时钟。

## 信号与故障分类

信号 disposition 属于进程，mask、pending 与备用栈属于线程。
发送方只登记 pending，目标线程在 CPU 安全边界建立受检 ARM 信号帧并执行 handler。
sigreturn 恢复 ucontext、寄存器与 VFP 状态，坏帧形成明确失败。
这样既保留 ARM ABI，也避免用宿主信号栈承载 guest 协议。

guest 故障与宿主能力缺口要分别报告。一条 ARM 指令可能因为缺页停止，
也可能属于后端尚未实现的合法指令；前者说明应用访问环境不满足要求，
后者说明执行器能力有缺口。把二者都转成 SIGILL，会让应用信号处理器掩盖兼容层本身的问题。
因此[信号决定](../adr/runtime.md#adr-0079)只把虚拟 CPU 确认的非法或无权限指令分类为同步 SIGILL，
后端未实现指令与内部故障仍保留原停止原因，不能靠投递信号掩盖。

同理，一个 clone worker 退出并不自动意味着整个应用正常结束。
worker 的首个故障交给进程 owner 并唤醒主循环，同时解除其他等待；
如果只标记线程结束，主线程可能永久等待一个再也不会被提交的结果。
有界清理继续释放宿主资源，清理错误另行保存，不改写原故障身份。
这套区分增加了停止结果和诊断状态的种类，
换来的是可以沿同一路径证明“原始错误已消失，下一首错是什么”。

完整内存序、ABA 和卸载静默验收仍保留在已知问题中，
不会因局部原子操作正确就被整体关闭。

## 契约、决定与操作

- [CPU](../../src/cpu/MODULE.md)、[memory](../../src/memory/MODULE.md)
- [Bionic](../../src/runtime/bionic/MODULE.md)、[syscall](../../src/runtime/syscall/MODULE.md)
- [execution](../../src/runtime/execution/MODULE.md)、[integration](../../src/runtime/integration/MODULE.md)
- [虚拟 CPU 事实](../adr/runtime.md#adr-0080)、[统一线程与 Clock](../adr/runtime.md#adr-0004)
- [排障手册](../playbook/TROUBLESHOOTING.md)、[当前限制](../state/KNOWN-ISSUES.md)
