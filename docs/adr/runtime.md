# 运行时基础与模块边界

返回 [ADR 索引](README.md)。本文件只记录现行决定；旧文由 Git 保存。

- [ADR-0001 · 采用进程级 HLE 兼容层](#adr-0001)
- [ADR-0002 · 真实 AOSP Bionic 与版本基线](#adr-0002)
- [ADR-0004 · 真实线程与统一时钟](#adr-0004)
- [ADR-0010 · pthread 保留 Bionic ABI，在 syscall 边界映射宿主线程](#adr-0010)
- [ADR-0011 · 固定 4 KiB guest 页并分离宿主后备粒度](#adr-0011)
- [ADR-0013 · Runtime 子模块边界与显式装配](#adr-0013)
- [ADR-0080 · 进程统一拥有虚拟 CPU 查询事实](#adr-0080)
- [ADR-0079 · 进程内 ARM guest 信号投递](#adr-0079)
- [ADR-0169 · API19 两级 UI 字体链与会话同源注入](#adr-0169)
- [ADR-0168 · 从连接拓扑发布本地接口查询视图](#adr-0168)
- [ADR-0170 · 仅托管默认进程，独立进程服务按不可解析回答](#adr-0170)

- [ADR-0171 · 生命周期停摆期间统一时钟按真实流速推进](#adr-0171)

<a id="adr-0001"></a>

## ADR-0001 · 采用进程级 HLE 兼容层

- 状态：Accepted
- 日期：2026-08-03

### 背景

目标是让 Android 老游戏跨桌面平台运行，避免完整系统带来的无限范围。

### 决定

翻译游戏 ARM 机器码，在 syscall、JNI、Android API 与图形/音频边界提供进程内兼容。
只实现游戏进程会直接调用的能力；不引入 Binder IPC、system_server、Zygote、完整 ART/Dalvik、
Play 服务或现代支付/社交/反作弊。进程内 Java 对象协议不等于系统服务或跨进程实现。
游戏差异只进入 data/profiles；src 中禁止游戏名、厂商名、包名及专属分支。

### 后果

兼容 API 逐项积累；未实现能力必须记账、可查询并明确失败，不能以空对象或零值冒充成功。

<a id="adr-0002"></a>

## ADR-0002 · 真实 AOSP Bionic 与版本基线

- 状态：Accepted
- 日期：2026-08-03

### 背景

自行重写 libc 会复制版本相关 ABI，发行输入也需要可追溯。

### 决定

默认执行真实 guest Bionic；底层 profile 支持 API 19/22/23，APK 通用启动固定 API 19。
普通 libc/libm/libdl 算法不在宿主重写；仅受审热点与进程边界允许选择性拦截。
libdl 的 linker service 接入唯一进程 ELF namespace，不调用宿主动态链接器加载 guest 库。
发行库须来自可追溯 AOSP 构建并校验来源、哈希与许可证；设备提取物只作本地行为 oracle。

ROM jar 与 framework-res.apk 基线统一为 AOSP `android-4.4.4_r2.0.1` 原版；禁止混入 CM 衍生声明。
固定 SHA-256（生成制品还须通过各自来源/成员审计）：

- `core.jar`：`60b6cdf3eded2fdf157c6166402477e4f72ade215b334817439960522c0e64ee`
- `framework.jar`：`422e232e121425268f04044f5349072ce42d7b4403267d21569e8ce70d07bc4b`
- `framework2.jar`：`5866d4e62e5fe44ffaf6ff441b6c361baeb1f8b91e30bb555481ef13ef837973`
- `ext.jar`：`e95d35989f0e1b438b3b6fea2777a798a0398f457a32875cdadfe3e2573a4ca2`
- `android.policy.jar`：`660fa192659bf02285cfeb559d3fad547a72d4986aa295ef5a9453b2b2372ad1`
- `conscrypt.jar`：`c7d0849cde38cd0d0dc99d89ee61bd33a65618567fc18171d6605dc6b8e15004`
- `framework-res.apk`：`5beb897ee2ede51f02e2fdf1a004ce3f12d7d5cd938b56672f70a7cba96471e3`


### 后果

多个 Bionic profile 共用 syscall、TLS、futex 与宿主线程底座；支持 profile 不等于相应平台已验收。

<a id="adr-0004"></a>

## ADR-0004 · 真实线程与统一时钟

- 状态：Accepted
- 日期：2026-08-03

### 背景

伪线程、伪成功同步和分散时间源无法支撑游戏并发与确定性回放。

### 决定

一个 guest 线程对应一个宿主线程及独立执行上下文；原生执行使用各自 CPU/栈/TLS。
Java 字节码由可重入 VM execution lock 串行解释，阻塞须按完整递归深度释放并恢复锁。
时间读取、超时、音视频推进和退出 deadline 统一经 Clock；不从各模块读取独立宿主时间。
实时与手动步进的消费策略分开，暂停、倍率及固定步长由统一时钟协议表达。

### 后果

内存与资源生命周期必须线程安全；确定性模式可受控串行化，但不能退回虚构 guest 线程。

<a id="adr-0010"></a>

## ADR-0010 · pthread 保留 Bionic ABI，在 syscall 边界映射宿主线程

- 状态：Accepted
- 日期：2026-08-04

### 背景

pthread 对象、TLS 与退出清理是版本相关的 Bionic 内部 ABI，不宜另造宿主替身。

### 决定

pthread 函数继续执行真实 Bionic，由 clone、futex、set_tls、内存及退出 syscall 映射宿主资源。
一个 guest 线程对应一个宿主线程；线程创建、等待和清理使用同一 guest 生命周期账本。
函数级热点拦截只发布有真实 handler、行为验证及性能依据的入口，不能仅登记 thunk。
文件与映射在 syscall 层处理，日志作为结构化边界服务提供。

### 后果

避免维护多套伪 pthread 结构；未支持的 syscall 继续明确返回错误并记账。

<a id="adr-0011"></a>

## ADR-0011 · 固定 4 KiB guest 页并分离宿主后备粒度

- 状态：Accepted
- 日期：2026-08-04

### 背景

ARMv7 Android 的 4 KiB 页面语义不能随宿主 16 KiB 等页粒度改变。

### 决定

guest 页固定 4096 字节，映射与读写执行权限逐 guest 页独立记账。
宿主后备保持可读写、不可执行；同宿主页内的相邻 guest 页不得互相影响。
取指、observer、跨页与不满足快径权限的访问经受检 MemoryBus。
数据快径仅通过显式受保护页表发布：写表只含 RW 非执行页，独立读表只含 readable 页。
映射、保护、替换、卸载与快照恢复同步更新账本、页表及范围失效通知。
CPU 不得保存脱离此生命周期的宿主裸指针；直接访问与并发卸载的完整 quiescence 仍未验收。

### 后果

Android 权限不依赖宿主 mprotect 粒度；快径优化不得绕过观察器或制造第二份权限权威。

<a id="adr-0013"></a>

## ADR-0013 · Runtime 子模块边界与显式装配

- 状态：Accepted
- 日期：2026-08-04

### 背景

JNI、CPU 执行、Android native 与 Java 平台服务需要独立所有权，避免 integration 成为实现巨石。

### 决定

runtime 按 common、jni、dexvm、framework、bionic、syscall、execution、vfs、database、ui、
jni_guest、boundary、debug 与 integration 分工，每个生产目录维护 MODULE。
jni 拥有 JNI 语义；dexvm 拥有解释执行与 Java 对象模型；jni_guest 只物化 guest JNI ABI。
bionic 拥有真实库 profile；execution 单向依赖 bionic/syscall，syscall 单向使用 VFS 与下层。
boundary 拥有 Android native/GLES ABI，依赖 gles 与显式窄服务，不依赖 JNI、VM 或 integration。
jni_guest 与 boundary 互不依赖；framework 的声明式 HLE 只通过 JNI 及受限资源接口工作。
integration 装配进程、桥与平台接缝；Android family 归其 dexvm_android 子模块，不在低层复制。
上层向下依赖，回调通过显式接口；ui 不执行 guest 或保存 Java 引用，VFS 不识别媒体消费者。

### 后果

目录、公共接口与依赖方向一致；具体资源所有权和支持边界由对应 MODULE 定义。

<a id="adr-0079"></a>

## ADR-0079 · 进程内 ARM guest 信号投递

- 状态：Accepted
- 日期：2026-09-29

### 背景

原生运行库会直接使用 ARM 信号 ABI，但兼容层不需要引入宿主信号处理或 Android 系统进程。

### 决定

syscall dispatcher 唯一拥有进程 dispositions，mask/pending/备用栈/活动帧按 guest 线程隔离。
clone 仅继承 mask；发送方只登记 pending，不改写目标 CPU。目标宿主线程在 CPU 安全边界
建立 ARM EABI guest 帧、执行 handler，并从受检 sigreturn 恢复上下文。
旧与 RT 入口独立编组；信号帧使用 ucontext/siginfo/VFP，返回桥使用 RX guest 页。
进程内 tkill/tgkill 校验真实 TID/PID；标准信号合并，支持屏蔽、嵌套、备用栈及已实现 flags。
futex 通知不制造 WAKE token；SA_RESTART 只重启无超时 WAIT，其余中断按既有 errno 返回。

### 后果

不安装宿主 OS handler；不实现实时信号队列、作业控制、外部进程/定时器信号或 CPU fault
自动转信号。坏帧、坏栈明确失败；不能以此宣称完整 Linux 信号或游戏 GC 兼容。

<a id="adr-0168"></a>

## ADR-0168 · 从连接拓扑发布本地接口查询视图

- 状态：Accepted
- 日期：2026-10-28
- 背景：API19 ROM NetworkInterface 直接枚举 sysfs/proc 并以 Posix ioctl 查询地址；
  单独提供 ConnectivityManager 的断开状态不足以表达内核接口视图。
- 决定：integration 从同一 offline_connectivity 事实派生唯一不可变接口表，
  离线时仅有 API19 时代 Linux 回环 lo；只读 VFS 发布和地址查询共用该表。
  发布冲突回滚，禁止读取宿主网卡或复用既有接口事实。
  Posix 从“不扩展 socket”放宽到 AF_INET/AF_INET6、SOCK_DGRAM、protocol 0 的
  接口查询描述符，由 IoRuntime 管理，只允许三项 IPv4 地址 ioctl 与幂等 close。
  本地查询不执行网络 IO，不受 transport NetworkPolicy 限制；缺拓扑/在线状态
  仍记账并明确失败。地址对象由既有 ROM InetAddress 构造路径建立。
- 后果：ROM NetworkInterface 可枚举离线回环，普通算法仍归 Java；不增加通用 socket、
  netlink/getifaddrs/if_indextoname 或在线 provider。验证见 [DVM-343](../tasks/dexvm/DVM-343.md)。

<a id="adr-0169"></a>

## ADR-0169 · API19 两级 UI 字体链与会话同源注入

- 状态：Accepted
- 日期：2026-10-28

ADR-0114（历史记录见 Git）单独注入 DroidSansFallback，无法覆盖 © 等默认 sans-serif
字形，另建宿主 Dialog 树也可漏注入。改为 AOSP android-4.4.4_r2.0.1
frameworks/base/data/fonts/system_fonts.xml 默认 sans-serif Regular 的 Roboto-Regular，
再取 fallback_fonts.xml 中的 DroidSansFallback，组成受审两级子集；不引入其他字体。

两文件原样随 payload 发布并各自固定 SHA-256，整链不可变，按顺序取首个覆盖字体。
会话主树提供唯一字体来源，独立宿主分区经统一工厂创建，复制保持同一资源身份；
UiTree 构造显式接受字体。ASCII 继续原 5x7 像素与度量，粗斜体继续合成。
Paint、TextView 和文本输入共享 UI 覆盖判定，失败携带码点并保留原记账及 Java 转换。

只支持独立 BMP，保留 shaping/combining/bidi、补充平面失败及整链128项缓存上限。
不读取宿主或应用字体，不实现完整 Android 字体匹配、系统服务与排版。
范围与验证见 [DVM-344](../tasks/dexvm/DVM-344.md)。

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

### 修订 · 2026-10-29

CPU 微架构身份属于不可变虚拟 CPU 模型，必须与 Dynarmic 实际支持的 Features 一致；
当前采用 Cortex-A9 r0p0 的 MIDR（0x410fc090），Processor revision 与分解字段同源。
这不代表具体真机；板级 Hardware 保持 OGPlay virtual ARMv7，Revision/Serial 使用内核
无板级信息时的零值，不伪造厂商、板型或序列号。`/proc/cpuinfo` 按 Linux v3.4 ARM
`c_show` 的 SMP 格式输出（单核亦保留 processor 组与空行、Features 尾空格）。
BogoMIPS 按虚拟 delay loop 每周期一轮定义为 2 × frequency_mhz，非宿主采样或执行限速。
CPU sysfs 与 sysconf 行为不变；不增加 nvmap 或 NV system clock。

<a id="adr-0170"></a>

## ADR-0170 · 仅托管默认进程，独立进程服务按不可解析回答

- 状态：Accepted
- 日期：2026-10-29

### 背景

APK 可声明独立进程或 isolated Service；拉起这些进程及其 Messenger/Binder IPC
超出进程级兼容层范围。调用方可能忽略 start/bind 失败，直接抛 UOE 会中断默认进程。

### 决定

仅托管应用默认进程。当前包显式目标的应用及服务均启用时，若服务 process 不同于
应用进程（应用未指定时为包名），或带 FLAG_ISOLATED_PROCESS（0x0002），按不可解析回答。
沿 API19 ActiveServices.retrieveServiceLocked 返回 null 的通道：startService 返回 null，
bindService 返回 false；后者仍先登记 dispatcher，允许正常解绑，不构造服务、不发连接或断开回调。
每次命中记账 dexvm.service_resolution，并以 runtime.service.secondary_process
结构化 warn 记录 api/component/process，采用 first_and_count 限流。
PackageManager 查询保留清单事实；不改变其他组件或扩展同进程 started 生命周期。

### 后果

PackageManager 能查到、AMS 不可托管的视图差异是有意为之。依赖独立进程功能的游戏
会失去该功能，但记账可查；不引入独立 guest 进程、Binder IPC 或游戏专属 quirk。

<a id="adr-0171"></a>

## ADR-0171 · 生命周期停摆期间统一时钟按真实流速推进

- 状态：Accepted
- 日期：2026-10-29

### 背景

生命周期驱动释放 VM 锁执行长 native 调用期间，watchdog 自己将 Clock 跳到其 10 秒
截止时间，导致只执行数毫秒的 finalizer 被错误判超时。root 的直接补时也有同类风险。

### 决定

取代 WU-0017 及 dexvm_bridge 中“root/驱动阻塞时限时等待快进到截止时间”的规则。
生命周期驱动的 execution lock blocking observer 统一记录释放与重新竞争锁的事件，
并转发 EGL pacer 原有阻塞逻辑；无 EGL 的会话同样跟踪。
停摆起点记录真实时间与 uptime，root/worker 的 sleep、wait、join、park 每次轮询仅补至
起点 uptime 加停摆以来真实流逝时间，且不超过等待者自己的截止时间。并发补时串行收敛。
真实时间由 integration 持有的统一 hal::RealtimeClock 注入；dexvm 不读取宿主时钟。
驱动恢复后继续按帧推进，不回拨；root join 与 Runtime.exit hook 复用同一规则。
有界生命周期 ExecutionDeadline 的 steady deadline 不变。
SystemClock.sleep 按 API19 循环 Thread.sleep，忽略中断直至达到 uptime 目标，再恢复中断。

### 后果

启动期轮询型 sleep 会耗费真实时长，与真机一致；确定性测试用 NowFunction 注入假真实时间。
补时不代表启动、长时运行或游戏兼容验收。
