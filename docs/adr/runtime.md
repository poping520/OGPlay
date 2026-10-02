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
- [ADR-0092 · 有界 WifiLock 客户端 Java 与进程内租约](#adr-0092)
- [ADR-0093 · WifiManager 两类锁共享租约与逐对象释放](#adr-0093)

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

<a id="adr-0082"></a>
## ADR-0082：有界协处理器支持与指令能力缺口隔离

日期：2026-09-29。状态：接受。任务：[BND-44](../tasks/boundary/BND-44.md)。

### 决策

ARM 用户态旧式 CP15 屏障复用 Dynarmic 已有 DMB/DSB/ISB 翻译，包括 ISB 的返回分派，
不跳过指令、不修改原 SO。支持 `MCR p15,0,Rt,c7,c10,5/4` 与 `c7,c5,4`（非 MCR2），
TPIDRURO 仍只读。普通 VFP/NEON 保留原解码路径，不把所有协处理器编码一律拒绝。

在上游通用协处理器 visitor 中保留非法编码检查和 ARM/Thumb 条件处理，未支持操作在
任何操作数副作用之前生成受控 ExceptionRaised 回调，使用私有标签携带故障前 IT 状态。
标签协议集中在 CPU 内部头文件，运行时回调明确返回 unsupported_instruction，恢复原 PC/IT
并保留已执行效果，不继续执行、不投递 SIGILL；不依赖只在 x64 实现的 Interpret terminal。
原有 InterpreterFallback 也返回能力缺口，而不伪装成非法指令。
解码内部错误单列 backend_error；真正的 undefined/unpredictable 保持原分类。这不是对
任意 JIT assert 的恢复承诺，同步 CPU fault 到 guest signal 另行建立契约。

通过 CMake 生成替换翻译单元，固定 submodule 保持干净；对规范化换行后的上游源做 SHA256
匹配检查，第三方升级必须重新审查。校验针对所修改的编译器源码，不绑定游戏或 ROM 内容。
屏障和异常属于 cpu；execution 的既有统一 stop 报告保留指令字、状态、线程和寄存器，
上层继续保留 JNI cause。当前不增加完整 MMU、CP15 特权寄存器或完整 Android 系统。

### 验证边界

覆盖 A32/T32、新旧屏障、条件跳过、IT 状态、TLS、协处理器操作族和精确失败副作用。
实际游戏仅以原故障消失及下一首错作为 reached-fault 证据，不据此宣布游戏兼容。


## ADR-0083：线程 Looper 身份与有界事件轮询

状态：接受（2026-09-29）。任务：[BND-45](../tasks/boundary/BND-45.md)。

`libandroid.so` 导出存在必须对应真实状态语义。ALooper 不再用全局固定句柄；Android module
按 guest TID 持有唯一关联，句柄单调分配并区分线程内部引用和外部 acquire 引用。
integration 将 Java execution token 映射到 native TID；主 Looper 在 Java 启动前准备，
HandlerThread prepare 接入同一 registry，退出退役。Java quit 不等同于线程退出，
原有 Java scheduler 继续拥有消息队列，不隐式复制为另一套 native 消息队列。

VFS 拥有 pipe 数据和端点存活，发布只读 readiness；boundary 通过窄 hook 查询，不依赖
VFS 实现。syscall 写入只通知重新检查。NDK poll 按所属线程读取已登记事件、wake、timeout，
shutdown 与线程退出均可中断等待。callback 与非 pipe fd 暂明确拒绝；后续扩展必须通过
显式所属线程 guest executor，不能由唤醒方执行 guest callback。Clock 是唯一时间源。

选择有界进程能力，避免引入 Binder/system_server 或复制 Android epoll/MessageQueue。

## ADR-0084：当前 APK 内的显式绑定服务

状态：接受（2026-09-29）。任务：[DVM-202](../tasks/dexvm/DVM-202.md)。

支持主 Java 线程以 BIND_AUTO_CREATE 绑定当前 APK 中启用的同进程 Service。
实例由进程注册表拥有，构造、attachBaseContext、onCreate、onBind 和连接回调在现有
主 Looper 排队执行；同一组件共享实例与本地 IBinder 对象，最后解绑调用 onUnbind/onDestroy。
连接、Intent、实例及 Binder 均参与 GC 根追踪，排队后解绑取消投递，退出释放剩余实例。
仅接受 component-only 显式 Intent；不同筛选条件、其他 flags、跨线程/跨进程与隐式正匹配
仍明确记账失败。外部服务缺席查询保持既有语义。

这是游戏进程直接调用的对象生命周期，不引入 Binder IPC、system_server、外部安装包
或支付实现。已有本地 Binder Java 对象仅作为 onBind 的原样返回值，禁止伪造连接成功。

## ADR-0085：intrinsic GLSurfaceView 的独立渲染线程

状态：接受（2026-09-30）。任务：[DVM-203](../tasks/dexvm/DVM-203.md)。

不再将 intrinsic renderer 合并到 Activity 主线程。复用 VmThreadRuntime 的 Java Thread、
解释上下文、host thread 与 JNI/TLS；GLSurfaceView$GLThread 的有界 run 通过显式 hook
调用 session 驱动，避免增加第二套线程注册与退出机制。不自动给 GLThread 建立 Looper，
ALooper_forThread 保持真实线程语义，输入队列迁移由 guest attach/detach 调用决定。
Java EGL/GLES 经 bridge 的 execution token→process TID 映射与 JNI 共用线程身份。

renderer、queueEvent、EGL 创建/current/swap/释放统一归属 GLThread；生命周期保持现有
逐帧请求协议，等待时泵送主 Looper，queueEvent 可独立唤醒空闲线程。onPause 握手期间
保留 GLThread，Activity 切换/停止时先退出并完成 native detach，再释放引用。
不替换 guest 自带 GLSurfaceView，不扩展多 View 并行渲染、EGL 暂停重建或 NDK 事件 API。

## ADR-0086：VFS 随机字符设备与 HAL 安全随机源

状态：接受（2026-09-30）。任务：[VFS-06](../tasks/vfs/VFS-06.md)。

native 库通过真实 guest Bionic open/read 获取安全随机数。VFS 提供最小只读字符设备
reader，integration 注册 `/dev/urandom` 与 `/dev/random` 并注入现有 HAL OS CSPRNG；
VFS 保持仅依赖标准库，不改 BootDex、Java 加密类或游戏代码。设备不使用打开快照，
每次非空 read 请求新字节，不使用有限 size 推断 EOF，也不接受固定种子或失败降级。

两条路径均定义为同步 OS CSPRNG 字节流，不模拟 Linux 熵池或阻塞/熵计数策略。
O_NONBLOCK 不改变该策略，OS 失败返回 EIO；没有额外 readiness/poll 接口。
只读权限、字符设备类型与 rdev 明确发布；seek/pread、lease、fsync 和修改设备的操作
分别受 VFS 契约限制。注册撤销同步等待 reader，已有描述符保留身份但读取明确失败。
dup 复用同一打开状态与操作锁，不复制随机内容。支付服务不在本能力范围。

## ADR-0087：JNI 字符串正文预算与按需副本

状态：接受（2026-09-30）。任务：[DVM-204](../tasks/dexvm/DVM-204.md)。

将 `NewStringUTF` 正文与类名/成员名/签名的 1 KiB 扫描策略分开。正文默认预算为
3 MiB Modified UTF-8 payload 和 1 Mi UTF-16 code units，扫描包含额外终止符字节；
资源上限是宿主支持边界，不宣称为 JNI 规范限制。仍复用 AddressSpace 的逐页权限校验，
明确区分预算耗尽、坏地址/权限、非法编码和分配失败，保留调用位置。

`GetStringUTFChars/GetStringChars` 共用可注入的默认 16 MiB 页对齐临时副本预算，
通过 AddressSpace 的 native mmap 范围按需取得未占用页；不新增宿主分配器或固定区域。
副本发布绑定现有 semantic access token，release 解除映射并归还预算，失败完整回滚。
析构只清理 guest 映射，不反向访问可能先销毁的 string store。成功观察接口仅供上层
记录长度与调用帧，观察失败不得替换运行结果，不记录正文或修改游戏数据。

## ADR-0088：进程级 guest 代码缓存一致性

状态：接受（2026-09-30）。任务：[WU-PERF-08](../tasks/optimization/WU-PERF-08.md)。

ARM cacheflush 仍由 syscall 绑定校验地址和参数；成功的非空范围由 SVC bridge 经
backend-neutral `Cpu::InvalidateCodeRange` 发布。CPU 不识别 Android syscall，解释器
明确无需操作，未实现的其他后端明确失败。

DynarmicExecutionContext 登记所有存活 JIT，注册、范围发布和退役使用同一锁；后端
范围失效 API 将请求排队并中断活跃执行，实际缓存更新留在执行线程。不得直接跨线程
修改寄存器或整缓存清空。包装层不能清除后端管理的 CacheInvalidation halt 位，避免
覆盖刚到达的请求；只由失效导致的零 tick 返回在 CPU 内续跑。

普通内容写入仍由 guest 主动 cacheflush 发布，不推断每次数据写入都是代码修改。
映射、权限和 backing 改变则由 memory 经显式范围订阅通知所有依赖该地址空间的 JIT，
包括独立 context；通知在账本锁外执行并在变更返回前完成，订阅退役等待正在执行的回调。
回调不得重入映射变更或订阅管理。JIT 仍只排队失效，由执行线程更新缓存；此边界不承诺直接数据访问与并发卸载的完整
quiescence 协议。窗口长帧等待另经显式宿主回调泵消息，保留 SDL owner 线程与 guest
事件分派边界。


<a id="adr-0092"></a>

## ADR-0092 · 有界 WifiLock 客户端 Java 与进程内租约

状态：Accepted。日期：2026-10-02。

### 背景

旧 WifiLock acquire/release 空操作且 isHeld 固定 false，缺失引用计数设置。
完整原版 WifiManager 依赖无线服务和 Binder，直接纳入将越过进程兼容层边界。
普通算法不能为跨平台适配而在 C++ 复制。

### 决定

在 framework guest Java 中保留公开二进制类名，按 AOSP API19 改编最小客户端：
计数、held、同步、模式切换、字符串和异常仍执行 Java；Binder monitor 改为普通 Object。
WifiManager 仅保留既有离线查询和锁工厂；平台调用为显式 native，不纳入 IPC/interface 服务。
源文件、固定工具链及哈希由既有 BootDex builder 统一编译并记录，不改动 ROM 原版文件。

C++ 只记录 owner-attached 的本进程租约与 manager 强边；权限不足抛 SecurityException，
无支持的 mode 明确 UOE；每 manager 最多 50 个不同活跃 owner，满额拒绝而不发布新租约。
GC sweep 和线程停止后的 bridge teardown 清理租约；不可 Cloneable 的锁不复制平台租约。
该限额针对逻辑租约，不复刻 AOSP 持锁期间反复切换计数模式引起的 manager 计数副作用。
引用计数和 held 保持独立，native 失败不擅自回滚 Java 的原版计数变化。

### 后果

持锁只表示应用租约成立，不表示连接成功，也不改变离线查询；各宿主共用同一 Java/CPP 路径。
WorkSource、MulticastLock、手机无线/电源服务仍为未支持范围；本能力验收不等于 title 可玩。


<a id="adr-0093"></a>

## ADR-0093 · WifiManager 两类锁共享租约与逐对象释放

状态：Accepted。日期：2026-10-02。
Supersedes：ADR-0092 的 MulticastLock 未支持边界；其余决定保持。

### 背景

应用在 WifiLock 后直接调用 createMulticastLock、引用计数及 acquire/release。
API19 两类锁共享 manager 的 50 个活跃锁上限；其服务端 multicast release 按 UID 清除
全部登记，而多个 Java 锁的 held 独立。直接复制 WifiLock 或另开配额会产生错误边界。

### 决定

MulticastLock 工厂、计数、held、同步、异常、toString 和 finalize 在 BootDex 执行 Java。
finalize 按 API19 先切换为非引用计数再 release；native 获取/释放检查
CHANGE_WIFI_MULTICAST_STATE，区别于 WifiLock 的 WAKE_LOCK 权限。

两类锁共用同一 owner→manager 租约账本，50 个不同活跃 owner 为合计上限；失败不发布
新租约，重复获取不多占名额，GC/退出沿既有清理。MulticastLock native release 仅删除
调用对象的租约，不复制 Android 同 UID 全清理副作用；另一对象持有的租约与 held 保持
一致。只提供当前游戏进程的逻辑过滤请求，持锁不承诺组播报文接收或网络连接。

### 后果

不启用宿主网卡、不加入 multicast group、不改变 NetworkPolicy。实际组播传输、
WorkSource、系统 WifiService 与 isMulticastEnabled 等未触达的查询仍不在支持范围。
所有宿主共用 Java/native 算法；对照测试需证明混合配额、不同 manager 隔离和逐对象释放。
