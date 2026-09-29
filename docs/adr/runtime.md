# 运行时基础与模块边界

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0001 · 采用进程级 HLE 兼容层](#adr-0001)
- [ADR-0002 · 默认加载真实 AOSP Bionic](#adr-0002)
- [ADR-0004 · 真线程与统一时钟从内核建立](#adr-0004)
- [ADR-0009 · Bionic 基线改为 API 19/22/23](#adr-0009)
- [ADR-0010 · pthread 保留 Bionic ABI并在 syscall 边界映射宿主线程](#adr-0010)
- [ADR-0011：固定 4 KiB guest 页并分离宿主后备粒度](#adr-0011)
- [ADR-0013 · Runtime 子模块边界](#adr-0013)
- [ADR-0016 · 受保护的 Dynarmic 数据页表](#adr-0016)
- [ADR-0018 · Runtime 拆出 jni_guest 与 boundary 子模块](#adr-0018)
- [ADR-0076 · API 19 ARM32 linker 元数据视图](#adr-0076)

- [ADR-0078 · Guest proc 文件的按打开映射快照](#adr-0078)
- [ADR-0079 · 进程内 ARM guest 信号投递](#adr-0079)

<a id="adr-0001"></a>

## ADR-0001 · 采用进程级 HLE 兼容层

- 状态：Accepted
- 日期：2026-08-03

### 背景

目标是让 2010–2016 年 NDK 老游戏跨平台运行，同时避免完整 Android 系统的无限范围。

### 决定

翻译游戏 ARM 机器码，在 syscall、JNI、Android 框架和图形/音频边界使用宿主实现。
只实现游戏进程会直接调用的能力，不实现 Binder、system_server、Zygote 或完整 ART/Dalvik。

### 后果

启动和调试成本低，但兼容 API 必须逐项积累；所有缺失能力都必须显式、可观测、可回归。

<a id="adr-0002"></a>

## ADR-0002 · 默认加载真实 AOSP Bionic

- 状态：Accepted
- 日期：2026-08-03

### 背景

DEMO 重实现 libc 的细节偏差会令 guest 静默走入错误路径，并难以维护 Android 多版本行为。

### 决定

正式版默认加载 API 19/22/25 的 AOSP Bionic，只选择性拦截性能热点、pthread 和宿主边界，
在 `svc #0` 处 HLE 约 120 个 syscall。

### 后果

ABI 保真度由真实 Bionic 提供；项目必须先完成 syscall、TLS、futex 与真线程。发行库必须
来自可追溯的 AOSP 构建，设备提取物只能作为开发期行为 oracle。

<a id="adr-0004"></a>

## ADR-0004 · 真线程与统一时钟从内核建立

- 状态：Accepted
- 日期：2026-08-03

### 背景

协作式调度和伪成功同步原语无法支撑真实游戏，分散时间源也会破坏确定性回放。

### 决定

一个 guest 线程对应一个宿主线程和独立 JIT 上下文；同步以 futex 语义为核心。全部时间源
依赖同一个 Clock，支持实时、固定步长、暂停和倍率。

### 后果

内存、TLS 和 HLE 对象必须线程安全；确定性模式需受控串行化，不得退回伪线程模型。

<a id="adr-0009"></a>

## ADR-0009 · Bionic 基线改为 API 19/22/23

- 状态：Accepted
- 日期：2026-08-03
- Supersedes：ADR-0002 中的 API 版本矩阵，不改变“默认加载真实 AOSP Bionic”的决定

### 背景

M2 可用的开发期 Bionic oracle 覆盖 Android API 19、22、23；原计划的 API 25 不再是
当前支持目标。版本矩阵必须在 loader、syscall 和 Bionic profile 开发前统一。

### 决定

M2 支持的 Android API 固定为 19、22、23。ROM 或设备提取物只导入被 Git 忽略的本地
oracle 目录，用于 ABI 分析和行为比较；发行数据仍必须来自可追溯的 AOSP 构建产物。

### 后果

- loader、Bionic profile、syscall 差异表和测试矩阵统一使用 19/22/23；
- API 25 不再属于当前支持范围；
- 本地 oracle 清单不得包含设备身份、外部绝对路径或访问凭据。

<a id="adr-0010"></a>

## ADR-0010 · pthread 保留 Bionic ABI并在 syscall 边界映射宿主线程

- 状态：Accepted
- 日期：2026-08-04
- Supersedes：ADR-0002 中“pthread 函数默认由宿主拦截”的部分，不改变真实 Bionic 主路线

### 背景

API 19/22/23 的 `pthread_t`、mutex、condition variable、TLS 和退出清理均为版本相关的
Bionic 内部 ABI。若在 `pthread_create/join/mutex_*` 函数入口替换成另一套宿主对象模型，
就需要伪造这些内部结构，并重新引入 DEMO 已出现过的“返回成功但没有真实语义”风险。

M2 累计样本证明，真实 Bionic 可以通过受检的 `clone`、futex、set_tls、signal、mmap 与
线程退出 syscall，在每个 guest 线程对应一个宿主线程的模型上正确运行。

### 决定

- pthread 函数保持 guest execution，由真实 Bionic 维护其版本 ABI；
- 线程创建、等待、TLS 和内存的宿主映射发生在 syscall/CPU 边界；
- 函数级选择性拦截只发布确有 handler 和性能基准的 mem 热点；
- mmap/open 继续在 syscall 层拦截，log 继续作为结构化 HLE 边界库。

### 后果

- 三个 Bionic 版本共享同一宿主线程内核，不需要伪造三套 pthread 内部结构；
- pthread 真并行和 join 语义由累计样本验证，未实现 syscall 仍进入能力账本；
- 新增函数级拦截前必须同时提供生产 handler、行为测试和性能基准，不能只登记 thunk。

<a id="adr-0011"></a>

## ADR-0011：固定 4 KiB guest 页并分离宿主后备粒度

- 状态：接受
- 日期：2026-08-04

### 背景

Android ARMv7 ABI 与本项目支持的 API 19/22/23 Bionic 以 4 KiB 页面为基础。此前
`AddressSpace` 直接把宿主页尺寸暴露为 guest 页尺寸，在 Apple Silicon 的 16 KiB
宿主页上会使 ELF 段、kernel helper、`clone` 栈与 `madvise` 范围产生错误的对齐和权限
语义；同一宿主页中的多个 4 KiB guest 页也无法通过宿主 `mprotect` 独立控制。

### 决定

`AddressSpace` 的 guest 页尺寸固定为 4096 字节。映射存在性与 read/write/execute 权限
都按 guest 页独立记账；宿主 reservation 按实际宿主页尺寸提交可读写、不可执行的后备
存储，仅在该宿主页覆盖的所有 guest 页均未映射时释放。

CPU 仍只能通过 `CheckedMemoryBus` 访问或取指，由它在读写宿主后备前强制检查 guest
账本。`AddressSpace` 不提供可绕过检查的公共宿主指针，因此 W^X 与 execute-only 等
guest 语义不依赖宿主页面保护粒度。

### 结果

- 4 KiB 对齐的 Android ELF 与 syscall 范围在 Windows、Linux、macOS 上语义一致。
- Apple Silicon 上共享一个 16 KiB 宿主页的四个 guest 页可独立映射、保护和释放。
- 宿主页面保护不再作为 guest 权限的第二份状态，避免两份权限账本失配。
- 若未来引入可直接访问 guest 后备的 JIT fastmem，必须先增加显式的受保护访问契约；
  当前决定不授权绕过 `CheckedMemoryBus`。

<a id="adr-0013"></a>

## ADR-0013 · Runtime 子模块边界

日期：2026-08-04

### 背景

`include/ogplay/runtime/` 已积累 31 个公共头文件，`src/runtime/` 有 35 个实现文件，JNI、
框架 HLE、Bionic、syscall、guest 执行和累计装配共处一个目录，模块所有权与依赖方向不再清晰。

### 决定

Runtime 拆为七个子模块，公共头和实现目录保持镜像：

- `jni`：JNI/JavaVM ABI、对象模型、引用、异常、字符串、数组、类、字段和调用。
- `framework`：只依赖 JNI（Asset 额外依赖 VFS）的声明式 Java 框架 HLE。
- `bionic`：API profile、真实库自检、TLS 与选择性 libc 边界。
- `syscall`：ARM Linux syscall、kernel helper、VFS 适配和 guest 线程退出状态。
- `execution`：guest init/fini、线程执行循环和 clone 后的宿主线程运行。
- `vfs`：与 Android 路径和挂载来源有关、但不依赖 JNI/syscall 的文件系统核心。
- `integration`：无界面累计 runner/contract，只负责装配，不提供低层能力。

依赖方向固定为：

`integration -> framework -> jni`，`integration -> execution -> bionic`，
`integration -> execution -> syscall -> vfs`；`framework` 只允许 Asset 子域额外依赖 `vfs`。

`guest_thread_lifecycle` 归入 `syscall`，因为 syscall 声明直接拥有 exit/clear-child-tid 状态；
`guest_thread_runner` 与 `guest_clone_thread_runtime` 归入 `execution` 并单向依赖 syscall，避免循环。

### 迁移规则

- 每个 `src/runtime/<submodule>/` 必须有独立 `MODULE.md`。
- 公共头迁移到 `include/ogplay/runtime/<submodule>/`，实现目录与其镜像。
- 每个 WU 只迁移一个可独立验证的切片；旧 include 转发头只能临时存在并必须可搜索。
- 先完成目录迁移，再按相同边界拆分 CMake target；不得在同一 WU 混合行为变更。

### 后果

模块契约和物理目录一致，后续可以用构建 target 强制依赖方向。迁移期间 include 路径会逐步
变化，但每个提交保持可构建、可测试和可回滚。

### 实施状态

`WU-0146` 迁移 VFS，`WU-0147` 按用户授权以单个纯机械 WU 迁移其余公共头、实现、include
与 CMake 路径。runtime 根目录不再保留散落生产文件，文档布局测试对此持续门禁。

<a id="adr-0016"></a>

## ADR-0016 · 受保护的 Dynarmic 数据页表

- 状态：Accepted
- 日期：2026-08-09
- Supersedes：ADR-0011 中“CPU 只能通过 `CheckedMemoryBus` 访问数据、不得获得宿主页
  指针”的部分；固定 4 KiB guest 页及独立权限账本仍然有效。

### 背景

Dynarmic 在 callback-only 模式下会让每次普通 guest 数据访存进入 C++ 虚调用、互斥锁、
权限遍历与小端搬运。Debug exact-APK 稳态采样显示该路径占据主要 CPU 时间，而 Android
ARMv7 游戏的大多数可写数据页具备稳定的 read/write、不可执行权限。

ADR-0011 要求未来 fastmem 先定义显式受保护访问契约。本 ADR 建立该契约，不改变固定
guest 页语义，也不允许任意裸指针绕过权限账本。

### 决定

- `AddressSpace` 拥有生命周期稳定、按 4 KiB guest 页索引的 32 位直接数据页表。
- 只有已映射且权限允许 read/write、禁止 execute 的页才发布对应宿主页首地址；未映射、
  只读、可执行或其他权限组合一律发布 null 并回退 `CheckedMemoryBus`。
- Map/Protect/Unmap/RestoreSnapshot 与权限账本在同一临界区同步更新页表。
- 安装 memory observer 时 `CheckedMemoryBus` 不暴露页表，watchpoint/trace 必须继续观察
  每次访问；取指也始终走 execute 权限回调。
- Dynarmic 仅将该表用于数据访问，并对跨页的 8/16/32/64 位访问强制回调，以完整验证
  两侧页面。回调路径继续产生带地址、访问类型和线程号的 `MemoryFault`。
- callback-only 后端关闭无效且会阻止 Arm64 register get/set elimination 的逐访存 halt
  检查；边界 fault 仍由现有回调停止状态上报。

### 后果

- 常见 RW 数据访存不再进入 C++ callback，Debug 运行显著降低边界成本。
- observer、权限边界、可执行页和跨页访问保持可诊断的 soft-MMU 语义。
- 每个地址空间增加一张 1,048,576 项宿主指针表；这是 32 位 guest 空间固定、可预估的
  内存成本。
- 任何扩大直接页资格或允许并发修改页表的工作都必须另行证明权限、失效和线程同步
  契约，不得仅以性能为由放宽。

<a id="adr-0018"></a>

## ADR-0018 · Runtime 拆出 jni_guest 与 boundary 子模块

日期：2026-08-11

### 背景

ADR-0013 将 runtime 拆为七个子模块时,`integration` 的定位是"无界面累计 runner/contract,
只负责装配,不提供低层能力"。M5..M8 期间,guest JNI ABI 边界(约 3200 行)与
Android native/GLES 边界(约 5400 行)全部落入 `integration`,使其增长到 32 个实现文件、
超过 1 万行,MODULE.md 不变量超过 300 行,已无法通读核对;"只装配"的契约与代码事实
持续背离。多个文件(`android_boundary_hle.cpp`、`android_boundary_gles.cpp`、
`android_guest_call_session.cpp`)超过 800 行上限。

### 决定

从 `integration` 拆出两个新的 runtime 子模块,公共头与实现目录保持镜像:

- `jni_guest`:guest 侧 JNI ABI 物化与绑定。包含 JNIEnv/JavaVM 表映射(`jni_guest_abi`)、
  SVC trap 分派(`jni_guest_dispatch`)、全部 slot binding family(core/static call/
  static field/string/array)与 root `JNI_OnLoad` 生命周期。依赖 `jni`、`execution`、
  loader、memory、cpu 及以下;不依赖 boundary 与 integration。
- `boundary`:Android native 边界。包含 `android_boundary_hle` 主分派、GLES2/GLES1
  边界组件、boundary symbol 目录、`GuestGlContext` 共享 GL 状态与 `A32CallFrame`。
  依赖 gles 模块、memory、cpu、loader 及以下;不依赖 `jni` 与 `jni_guest`。

`integration` 收敛为装配层:API 19 guest process、Android guest call session 及其
Java handler 绑定、link preflight、headless/NativeActivity runner 与累计 contract。

依赖方向更新为:

`integration -> jni_guest -> jni`,`integration -> jni_guest -> execution`,
`integration -> boundary -> (gles 模块)`;其余方向沿用 ADR-0013。
`jni_guest` 与 `boundary` 互不依赖。

### 迁移规则

- 沿用 ADR-0013 先例:纯机械迁移,不改 `ogplay::runtime` 命名空间、不改任何行为;
  经用户授权,每个子模块以单个机械 WU 一次迁完,提交保持可构建、可测试、可回滚。
- MODULE.md 不变量随文件迁移到对应新契约;`integration` 契约同步收敛,总量只减不增。
- `cmake/CheckDocumentationLayout.cmake` 的子模块清单同步追加 `jni_guest` 与 `boundary`。
- 会话拥有的 Java handler 状态(platform/movie/media)保留在 `integration`:迁往
  `framework` 会引入 framework 对 `jni_guest` 对象模型的向上依赖,违反 ADR-0013;
  待 session 级 Java object-model 统一(现有 backlog)后再评估。

### 后果

`integration` 从 32 个实现文件收敛到约 11 个,三份契约分别可通读;后续可用独立
CMake target 强制 `boundary` 不依赖 JNI、`jni_guest` 不依赖 GLES。include 路径一次性
变化,由同一 WU 内的构建与全量测试兜底。

<a id="adr-0076"></a>

## ADR-0076 · API 19 ARM32 linker 元数据视图

- 状态：Accepted
- 日期：2026-09-28

### 背景

旧 native 加载器会把 dlopen 结果当作 Bionic 私有 `soinfo*`，直接读取 ELF 符号与 SysV hash。
整数句柄不满足这个 ABI。依据固定 [android-4.4.4_r2 linker.h](https://raw.githubusercontent.com/aosp-mirror/platform_bionic/android-4.4.4_r2/linker/linker.h)
与 [dlfcn.cpp](https://raw.githubusercontent.com/aosp-mirror/platform_bionic/android-4.4.4_r2/linker/dlfcn.cpp)，
仅提供当前进程的有界元数据视图。

### 决定

- API19 ARM32 普通句柄改为 guest 中 `soinfo` 记录地址；process-owned registry 继续拥有
  身份、引用与关闭状态。重复打开同一活跃库返回同一地址，引用归零退役 backing，地址不复用。
  RTLD_DEFAULT 独立处理；RTLD_NEXT 与其他 ABI 布局不在本次支持范围。
- 只支持受审字段读取：name、base/size、flags、strtab/symtab、nbucket/nchain/bucket/chain、
  ref_count、link_map 的地址/名称及 load_bias。其余结构槽为保留区，不提供 phdr、dynamic、
  链表遍历、重定位表、init/fini、linker 写入或 debugger 协议。
- 从唯一 ELF namespace 的未版本化导出与 sealed boundary provider 生成 ELF32 symbol/string
  与 SysV hash 表；真实 ELF 优先于该库的部分 boundary 截获目录。st_value 按已解析地址与
  load_bias 投影，包括 SHN_ABS/host intercept；不重新装载或另建符号地址来源。
  Virtual SO 无 ELF image，base/size/load_bias 为 0，符号值为实际 guest thunk/data 地址。
- 视图只暴露所属库导出；dlsym 保留原有 dependency scope 与 sealed fallback 契约。
  普通表只读，宿主在 registry 锁内更新引用时临时切换记录页权限。每个进程使用
  `[0x79000000,0x7a000000)` 有界 arena，单个视图最多 65536 导出、4 MiB 数据。
- 模块、构造器、析构与 JNI 初始化仍归当前 process/native loader；本能力只投影查询事实。
  不引入 Android linker 进程、宿主动态库、低地址映射、游戏或库名特判。

### 后果

需要通用 guest 载荷验证 ELF/Virtual SO 直接查询与 dlsym 地址及调用一致、引用/退役和失败路径。
私有 ABI 兼容只承诺上述读取范围；新的写入、字段或布局依赖必须单独评审。

<a id="adr-0078"></a>

## ADR-0078 · Guest proc 文件的按打开映射快照

- 状态：Accepted
- 日期：2026-09-29
- 实施：[VFS-05](../tasks/vfs/VFS-05.md)

### 决定

进程直接读取的 proc 事实通过 integration 注入唯一 VFS；VFS 提供通用按打开生成的
只读文件，memory 只提供有界映射元数据。回调在 VFS 锁外执行，注册句柄撤销时等待
在途调用；打开快照独立拥有数据和预算，避免多次短读跨越映射世代。
首期仅发布 `/proc/self/maps`，覆盖真实匿名 guest 页和权限，不以空文件或虚假 access
成功跳过运行库检查。现有 ELF 是复制进匿名页，暂不伪造文件路径/偏移/inode。

### 后果

原版 Java/native IO 共用路径、权限、读与 seek。新增 proc 节点按实际消费者扩展，
不引入系统服务或宿主 `/proc`；文件映射来源身份需在未来引入真实 file-backed mmap
时由唯一映射账本持有，不能从库名推测。

<a id="adr-0079"></a>

## ADR-0079 · 进程内 ARM guest 信号投递

- 状态：Accepted
- 日期：2026-09-29
- 实施：[BND-41](../tasks/boundary/BND-41.md)

### 决定

信号状态由进程 syscall dispatcher 唯一拥有：handler 进程共享，mask/pending/备用栈/活动帧
按 guest thread 隔离。clone 在启动 child 前继承 mask；不复制 pending 或备用栈。
保持 guest 与 host 线程 1:1，只由目标线程在 CPU 安全边界建立 guest 栈帧并执行 handler；
发送方不访问目标 CPU。主调用、clone 和 headless 共用状态及最多 50000 tick 的检查间隔。
不安装宿主 OS 信号、不引入 Android 系统进程或特定引擎分支。

按 API19 ARM EABI 分别编组旧 sigaction/sigsuspend/sigreturn 与 RT 入口，信号帧使用
ucontext/siginfo/VFP 布局，恢复 guest 可编辑的上下文和 mask。返回桥为惰性分配的 RX guest
页，不要求栈可执行。futex 通过显式中断谓词及通知连接信号状态，通知不产生 WAKE token；
无超时 futex WAIT 在 SA_RESTART 下重执行原 syscall，否则 EINTR。

### 边界与后果

首期支持 tgkill 投递标准信号及注册 handler、默认忽略/终止、屏蔽与合并、嵌套、备用栈、
SA_SIGINFO/SA_RESTORER/SA_RESTART/SA_NODEFER/SA_RESETHAND。进程内 tgkill 验证 PID/TID；
无 handler 的 SIGABRT 保留原有 fatal 诊断。实时队列、作业控制、外部进程/定时器信号、
同步 CPU fault 转信号尚未实现；其他宿主阻塞 IO 不承诺及时投递，有超时 futex 中断返回
EINTR。坏帧/栈溢出明确失败，不吞掉信号或绕过 handler。

ABI 依据本地 AOSP 4.4.4 bionic 的 arch-arm syscall 与 asm/{signal,ucontext,sigcontext}.h，
并核对 [Linux v3.4 ARM signal.c](https://github.com/torvalds/linux/blob/v3.4/arch/arm/kernel/signal.c)
及 user_vfp 布局。定向双后端测试和 APK 首错推进分别记证据，后者不等于完整 GC/游戏验收。

<a id="adr-0080"></a>
## ADR-0080：进程统一拥有虚拟 CPU 查询事实

日期：2026-09-29。状态：接受。任务：[BND-42](../tasks/boundary/BND-42.md)。

### 决策

核数和标称主频在启动时冻结为 `GuestCpuConfig`，默认 1 核/1000 MHz。允许范围分别为
1..32 和 1..10000；所有核心固定在线且同频。该参数表达 guest 的虚拟硬件配置，不映射
宿主亲和性、线程配额、Clock 或执行速度。CPU 名称/指令标志不开放任意覆盖；Features
只发布现有 Dynarmic ARMv7 的受支持子集，不复制真机 VFPv4 或厂商身份。

integration 发布只读 `/proc/cpuinfo`、CPU sysfs 的 possible/present/online/offline、
每核 online 与 cpufreq 的 min/max/current 标称 kHz。发布冲突明确失败，进程销毁撤销，
旧 FD 保留原快照。已存在的 meminfo 不阻止 CPU 节点发布。

API19 原版 Bionic 的在线核数查询依赖 `/proc/stat`。本阶段没有真实 CPU 时间统计，
因此不生成虚假的零利用率文件。仅替换 libc 导出的 sysconf 符号位置，使用进程私有 RX
A32 桥处理 `_SC_NPROCESSORS_CONF=96` 和 `_SC_NPROCESSORS_ONLN=97`；其他 selector
保留参数、LR 和栈，BX 尾调用原版 ARM/Thumb sysconf，保留其返回值及 errno。
桥进入同一 linker namespace，重定位/dlsym/soinfo 共用，不修改 ROM ELF 指令或固定偏移。
libc 内部不经导出符号的私有调用不属于该覆写契约。

DexVM bridge 从该进程注入 CoreIntrinsicServices，Runtime.availableProcessors 与
Java Posix 的这两个 sysconf selector 共享核数；其他 Java selector 继续记账失败。
CLI 与单游戏设置接入同一启动配置；全局设备预设仍保持数据层边界。

### 验收边界

覆盖配置范围、VFS 快照/隔离/只读/撤销、ARM 与 Thumb fallback、Java 与 GUI 传递，
再复现同一 APK 首错。后续真实并行度、负载统计与速度控制需要独立设计及验证；
CPU coprocessor 首错也不并入本工作单。
<a id="adr-0081"></a>
## ADR-0081：独立于硬件查询的原生 CPU 执行预算

日期：2026-09-29。状态：接受。任务：[BND-43](../tasks/boundary/BND-43.md)。

### 决策

第一阶段的核数和 MHz 是查询事实；第二阶段增加默认关闭的执行策略：可按虚拟核数限制
同时执行的 A32/T32 CPU，并独立限制每个进程的总 backend tick/秒。guest 与 host 线程
继续 1:1，Java 字节码仍使用现有 VM 单写锁；HLE、Java 解释器与宿主 CPU 占用不计入该配额。
不对标称 MHz 作周期精确映射，不改变 guest Clock、指令语义、watchdog 或宿主亲和性。

CPU 下层拥有 ExecutionBudget，integration 将唯一预算注入进程 Dynarmic context，供
root、clone、DexVM native、音频 callback 和嵌套 JNI CPU 共用。并发名额由 FIFO 队列分配；
速率采用 aggregate token bucket，最多积累 max(rate/20, 1) tick（50 ms），每片最多
消费半桶且不超过 50000 tick，使宿主粗粒度等待仍有补充额度的余量。
运行前预留，运行后按 RunResult.ticks_consumed 结算并退还余量；计量沿用后端既有 block
预算语义，不声称等于物理周期或逐条退休指令数。单片上限 50000，不改变总调用预算。

限额模式下关闭 fast SVC 内联分派，SVC 先退出 Run 并释放名额，再由既有 runner 进入
JNI/HLE/syscall；阻塞调用和重入不能占住上一层执行名额。不引入跨层反向依赖。
统一 Clock 提供单调时间；等待最多每 2 ms 重检，通知可提前唤醒。取消移除排队项，RAII
处理所有异常出口。BeginTeardown/Stop 切入不可逆 drain，唤醒并解除配额，允许现有退出
检查和 guest finalizer 完成；drain 期间不再承诺执行限额，生命周期依旧负责终止线程。

### 边界

速率是上限且允许有限突发，不保证宿主能达到请求速度；默认模式保留原 fast SVC 路径。
CLI/GUI 参数及启动日志区分查询核数、标称 MHz、并发限制和百万 tick 速率；不把该控制
扩展为完整 Android 调度器、CPU 热插拔、负载统计或 `/proc/stat`。首错复现仍与兼容验收分开。
