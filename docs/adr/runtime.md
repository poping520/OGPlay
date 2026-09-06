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
