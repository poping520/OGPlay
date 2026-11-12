# guest 执行环境

执行环境让 ARM 应用代码和真实 Android Bionic 在宿主线程上运行。
CPU、memory 与 syscall 分别负责指令、地址空间和内核可见行为，
integration 将它们装配为进程；这一分工使 ABI 与宿主平台保持分离。

## 执行路径

```text
guest ELF → Loader 映射与重定位 → guest CPU
                                  ├─ 普通指令 → MemoryBus
                                  ├─ SVC #0 → syscall
                                  └─ 显式 SVC → native/JNI 边界
```

应用机器码不直接在宿主 CPU 上跳转执行。
A32/T32 的寄存器、CPSR、VFP/NEON 和线程指针由 CPU 实例持有。
主执行后端是 Dynarmic；解释器是确定性参考与诊断后端。
解释器覆盖是有界的，不能由它的存在推断完整 ARM 指令支持。
运行结果显式区分预算用尽、陷阱、指令缺口和内存故障。

## CPU 与进程共享状态

每个线程有自己的 CPU 状态，同进程 CPU 共享执行 context。
共享 context 协调 exclusive monitor、processor identity 和可选执行预算。
CPU 通过 MemoryBus 访存，不读取文件系统，也不解释 Android 服务名。
HostCallHook 只表达是否处理了陷阱及是否失败。
具体的 SVC 含义由上层装配，CPU 不依赖 JNI 或图形库。

代码失效通过显式范围通知排队到存活 CPU。
映射删除、权限撤销和地址复用会使旧 JIT 块失效。
普通数据写入不自动当作修改代码；guest 自修改代码仍需 cacheflush。
未实现指令不能当 NOP，也不能被包装成应用自己的 SIGILL。
非法编码与后端能力缺口是不同的停止原因。

独占访问使用真实宿主原子比较交换和共享 reservation 状态。
地址、对齐和页权限仍需校验。
值比较不能完整检测普通直接写入的 ABA，支持边界见 CPU 契约。
这类底层限制不能通过调整游戏数据隐藏。

## 地址空间与分页

memory 为 guest 建立独立的地址账本。
32 位地址空间的预留与实际提交分离，低地址保留区不映射。
guest 页固定为 4 KiB；宿主的后备页粒度可以不同。
权限判断以 guest 页为单位，不能由宿主页大小扩大可访问范围。

GuestAddress 表示地址，GuestRange 表示半开区间。
长度和加法使用足够宽的类型，范围溢出在访问前失败。
读、写和取指分别检查对应权限。
MemoryFault 保留访问地址、类型、原因和线程身份。
非法读取不能返回零继续运行。

受检 MemoryBus 支持观察器和精确故障。
直接数据页表只向 JIT 发布满足条件的后备页。
读写表接受无观察器的 RW 非执行页；独立读表也可发布 R/RX 数据页。
跨页、观察器和不合条件的访问回到受检路径。
页表随映射账本更新，消费者不能长期缓存脱离生命周期的裸指针。

映射替换先准备后备与初始化内容，再原子发布映射、权限和世代。
失败不留下部分已发布的映射。
write preflight 绑定映射世代，实际提交时必须确认映射没有变化。
快照恢复也发布范围变化，不能继续执行旧缓存代码。
受保护 Java payload 的 owner 会阻止覆盖其仍在使用的地址。

## 真实 Bionic 与装载

普通 libc 算法在 guest 中运行，syscall 才进入宿主兼容层。
这样 pthread、allocator 和版本相关结构体布局由原 Bionic 保持。
底层 profile 可描述不同 Android 库基线；通用 APK 启动固定 API19。
支持某个 profile 不代表其整个平台已经实跑验收。

Loader 建立 guest ELF namespace，处理依赖、符号版本、重定位和构造器。
系统库来源是随附 payload，应用库来自选定 ABI 的 APK 或显式 VFS 路径。
libdl 查询使用同一 namespace，不调用宿主动态链接器加载 ARM SO。
Virtual SO 和真实 libc 的局部导出 override 具有不同归属。

进程初始化建立 guest 栈、TLS、环境页和返回 trap。
环境变量属于 guest；默认时区不继承宿主设置。
CPU 核数与标称频率由进程创建时冻结，供 proc、sysconf 和 Java 查询共用。
标称频率不是计时器，也不代表周期精确模拟。

## syscall 边界

SVC #0 按 ARM EABI 从寄存器提取调用号与参数。
分派器负责号表、返回值和观察器，具体 binding 使用注入的底座。
未知调用记录能力缺口并返回明确的 Linux errno。
文件、内存、线程、时间和信号都通过各自受检接口实现。

匿名映射、brk 和权限变更落到同一 AddressSpace。
文件 syscall 经唯一 VFS，不能绕过沙盒直接读宿主路径。
proc 视图从进程事实或地址空间快照生成，不读取宿主 procfs。
时间 syscall 从 Clock 获取单调或墙钟值，并保留单位与溢出检查。

## 线程与 futex

```text
Bionic pthread_create
  → ARM clone syscall
  → 预检 flags / TLS / parent-child TID
  → 注册 child
  → 新宿主线程 + 新 CPU
  → 共用进程执行循环
```

这里只支持 pthread 所需的共享地址空间 clone 形态，不创建第二个系统进程。
child 从父寄存器快照派生返回值、栈和 TLS。
发布失败必须回滚 TID 写入与注册，不能留下半个线程。
线程退出发出控制请求，由 runner 完成宿主线程收尾。

FutexTable 以对齐 guest 地址为键，先比较 guest 值再等待。
WAKE N 只给已有等待者发放唤醒，不让后续等待伪成功。
信号通知只唤醒谓词检查，与 WAKE token 分离。
关闭中断具有粘性，用于解除当前及未来的阻塞。
上层负责把中断与超时结果映射为 guest errno。

## Clock 与执行预算

Clock 是整个会话唯一的时间入口。
CPU tick 表示执行预算，不能直接代替 Android uptime。
可选进程预算限制同时执行的原生 CPU 和总 tick 速率。
预算默认关闭；启用时共享 FIFO 准入和 token bucket。
退出先解除预算等待，避免清理再次被限流阻塞。
Java 解释仍遵守 VM 执行锁，预算不代表限制全部宿主 CPU 使用量。

生命周期期间的限时等待仍通过统一 Clock 推进。
按真实流逝时间补偿停摆必须受原截止时间约束。
不能为唤醒一次等待临时读取另一个宿主墙钟。

## 信号与失败传播

信号 disposition 是进程事实，mask、pending 和备用栈属于线程。
异步投递排队后由目标 guest 线程在执行边界处理。
legacy 与 RT 帧保存在受检 guest 内存中。
return 恢复寄存器、ucontext 与 VFP 状态，坏帧明确失败。

同步 SIGILL 只在虚拟 CPU 模型确认非法或无权限访问时投递。
它不是恢复任意 JIT 内部断言的方法。
首个异步 worker 故障由 owner 保存并通知主循环。
清理异常不能覆盖原始 fault；不能捕获所有故障再当正常退出。

## 阅读代码与相关资料

- [CPU](../../src/cpu/MODULE.md)、[memory](../../src/memory/MODULE.md)
- [Bionic](../../src/runtime/bionic/MODULE.md)、[syscall](../../src/runtime/syscall/MODULE.md)
- [execution](../../src/runtime/execution/MODULE.md)、[integration](../../src/runtime/integration/MODULE.md)
- [分页决定](../adr/runtime.md#adr-0011)、[线程决定](../adr/runtime.md#adr-0010)
- [信号决定](../adr/runtime.md#adr-0079)、[虚拟 CPU 事实](../adr/runtime.md#adr-0080)
- [排障手册](../playbook/TROUBLESHOOTING.md)、[当前限制](../state/KNOWN-ISSUES.md)
