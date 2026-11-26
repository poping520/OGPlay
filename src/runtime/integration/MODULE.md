# 子模块：runtime/integration

## 职责与依赖

生产 session 使用 [进程内窗口服务](dexvm_android/MODULE.md#uisurface-与输入)。NativeActivity 的
ROM InputQueue 绑定到当前实例的 NDK 队列；事件完成后经唤醒返回所属 Java Looper。
JNI 渲染调用释放 VM 锁时仍算已登记 EGL 生产者正在运行，其 native futex 阻塞由进程的
现有等待表查询。Java 线程退出时在 native 上下文退役前删除生产者；将 EGL 交给原生线程的
初始化线程不会继续占用帧等待。原生线程自建 EGL 仍沿 native 边界节流。
查询等待状态只读取明确的 futex waiter 身份，不使用时长、游戏或类名推测阻塞。

静态 native 导入审计与 NativeLibraryLoader 共用来源快照、APK basename/SONAME 别名和依赖
选择；逐个所选 ABI APK 根构建 Bionic namespace，用现有 lookup/version 规则聚合强/弱缺口。
未引用导入仍发布诊断及独立计数；只有非 NONE 重定位引用的强缺口计入预检失败。
不映射或执行 guest，不把全 APK 导出并成虚假全局 scope。JNI bridge 向 boundary 注入与
Java 与 NDK 共用的传感器目录和数据源、DeviceLocale provider、VFS pipe hooks，以及 Surface 引用→ROM native 句柄→WMS window 的显式
转换；转换/acquire 在 VM 锁内完成，relayout/退役同步边界窗口，退出后解除 hooks。


DexVM bridge 拥有 JNI 反射转换适配：FromReflected 使用 ReflectionRuntime 的成员元数据
取得已有 registry ID；ToReflected 根据声明成员还原真实 Method/Constructor/Field wrapper。
校验引用类型、声明类与 static 标志，在 VM 执行锁内发布调用线程 local reference；ID 不
拥有 wrapper，不另建字段存储。销毁前先停止调用，再清除 JniEnvironment 的反射 hooks。
JNI FindClass 的线程 resolver 按正在执行的 native 方法、JNI_OnLoad 的加载参数或 ROM system
loader 查找并发布真实类身份。有效 loader 非 boot 时，FindClass 把非数组点分名归一化为
斜杠名并共用类身份。boot loader 的点分名查找失败并挂起 NoClassDefFoundError。
保留平台注册表既有实现，按类 ID 发布层级及 own 成员。
被动 FindClass 查询默认注册名。启动时预发布类身份和声明层级，
新登记的 BootDex 类由显式类或成员查询按需发布成员，过程受 VM 执行锁保护。
预发布遇到实现类型非 interface 等 Java 链接错误时延后到真实 Java/JNI 使用，
不因未使用的非法类使进程装配失败；失败发布不保留递归标记。
值传输复用稳定的双向类身份缓存，成员解析仍经发布入口补齐按需物化的成员。
ThrowableFactory 在 VM 执行锁内绑定调用线程的执行上下文，构造并发布该线程的真实 local Throwable。
JNI 字段访问经解释器共享守卫：未实现非常量字段记账、挂起 UnsatisfiedLinkError，禁止侧表回落。
GC 强根包含 JNI 引用、数组 pin 和活动 MUTF-8 lease。UTF-16 pin 保活 String 的 value 数组。
进程 quiesce 只 join clone runtime 实际拥有的 child；Java-only 线程交由 Java
thread owner join，不能依赖惰性 native context 是否存在来区分。
这些 owner hooks 在调用静止后统一撤销，不使下层反向依赖 DexVM。

`GuestCpuEnvironment` 由进程拥有并冻结 CPU 配置；在 VFS 发布可撤销只读 CPU 信息与
sysfs 拓扑/标称频率。cpuinfo 遵循 Linux 3.4 ARM SMP 格式，微架构身份从不可变
虚拟 Cortex-A9 模型派生并匹配 Dynarmic Features；BogoMIPS 为 2 × 标称 MHz，板级
Revision/Serial 为无板级信息时的零值，不采样宿主。API19 `sysconf` 导出通过进程私有 RX guest 桥处理核数 selector
96/97，其余参数保持原 ABI 尾调用原 libc。重定位、dlsym 与 linker 视图使用同一导出；
不修改原 ELF 指令。DexVM bridge 从该进程取得核数，禁止另用宿主硬件事实。
同一 Cortex-A9 模型向信号 binding 注入 PL0 UNDEFINED 分类，仅对非法编码、PL0 不可访问的
CP15 和不存在的协处理器启用同步 SIGILL，见 ADR-0079。
不支持统计负载、热插拔、调频控制或周期精确模拟，见 ADR-0080。

`GuestNetworkEnvironment` 从会话 offline_connectivity 与 socket 栈模式派生不可变接口表：离线只有
API19 回环 lo，联网再加 wlan0（设备 IPv4、/24、无 IPv6），发布只读 sysfs/proc；CoreIntrinsicServices
回调与 VFS 共用同表。注册冲突整体回滚，owner 退役同步撤销文件；缺事实明确失败。不采样宿主网卡，
见 ADR-0168。

`AndroidGuestCallSession` 持有进程的 `runtime/net` 栈（模式取平台配置 `network_mode`，联网时用
系统 `HostNetwork` 或测试注入的 `host_network`），绑定 socket syscall，并经 `Network()` 交给 DexVM；
`BeginTeardown` 停止该栈以唤醒阻塞的 socket 操作。平台配置的 `host_tls` 选择 OGPlayJSSE 引擎。

第二阶段（ADR-0081）可按配置建立一个进程共享 ExecutionBudget，所有主/clone/Java
native/音频回调及重入 CPU 使用同一 Dynarmic context。并发上限可独立启用为配置核数，
总速率以百万 backend tick/秒指定；默认不设预算。BeginTeardown/Stop 先解除预算等待，
再执行原有线程退出和 finalizer，不在关闭过程中重新阻塞。Java 字节码继续遵循原 VM 锁。

组合 Bionic、guest JNI ABI、Android boundary、DexVM/框架 HLE，拥有进程会话、动态加载、
headless/NativeActivity runner 与出口报告。可依赖 jni_guest/boundary/framework/jni/bionic/
syscall/execution/vfs 及下层；下层不得反向依赖 integration。不直接持有窗口或消费输入；
executor、Clock、VFS、Profile 必须显式注入或由确定性 fixture 建立。

内容树触摸分发、intercept/CANCEL 与非 split 捕获归 ROM ViewRootImpl/ViewGroup；
integration 的 WMS/WindowSystem 管理窗口命中、焦点与输入通道，session 注入事件，不消费宿主设备。

ROM 资源 native 通过 [资源核心](../resources/MODULE.md) 解析 framework 与应用资源。
ROM ViewRootImpl 驱动 View/Drawable 的软件绘制；Bitmap/NinePatch/Paint 的 native backing 为权威，
软件 Canvas 经 Skia 写入 Surface，WindowSystem 按 WMS 图层发布不可变 RGBA 帧。

Android API 的详细行为以 [dexvm_android](dexvm_android/MODULE.md)为准；
[DexVM](../dexvm/MODULE.md)拥有解释/GC/线程，[session](../../session/MODULE.md)编排生命周期，
[capabilities](../../../capabilities.toml)登记支持范围。普通 Java 算法不得在 integration 重建。

## 进程与库加载

- AndroidGuestProcess 在任何成员创建 guest 映射之前，将 `[0x50000000,0x58000000)` 与
  `[0x6a000000,0x7f000000)` 登记为 AddressSpace 保留窗口，避免宽域 first-fit 占用尚未
  映射的固定区及不映射的指针句柄。固定布局来自 runtime/common/guest_address_layout.h，
  编译期验证各区域互不重叠且位于窗口内；既有 typed opaque handle registry 共用一个排除区。
  动态 ELF load-bias 选址同样排除窗口，起点与对齐不变。窗口内有界 arena 的 first-fit
  仍可使用；无窗口装配的 headless 等独立 runner 保留自有布局与行为。
  PrepareDexVmThread 映射失败先按既有流程回滚，再报告 guest TID、槽位、失败目标区间与
  原因，槽位可重试；线程上限、栈大小、CPU fault 隔离与文件身份不变。

- FindLibraryExport 按库身份查询完整的已加载命名空间；缺库或导出返回 optional，
  不隐式加载。创建 VM 前经现有加载入口准备随附 JNI 库的 ICU 依赖闭包；版本查询遵循 VM 锁释放/恢复协议。

- 锁顺序：持有 VM 执行锁时，禁止获取进程 `guest_call_mutex_`、`native_load_mutex_`
  或 DexVM native context 的 `call_mutex`。进入可能等待这些锁或执行 guest 的边界前，
  按完整递归深度 `ReleaseForBlocking`；正常及异常出口均 `ReacquireAfterBlocking`。
  JNI 回调在桥入口重新获取 VM 锁；释放期间只使用已复制的参数与稳定 owner，不访问 VM 状态。
  持有 AssetManager 资源锁或其他 intrinsic 宿主锁时，禁止执行字节码或释放 VM 执行锁。
  防线作用域在持锁期间跳过 `YieldToWaiter`，并让 `ReleaseForBlocking` 抛出带锁名的 `internal_invariant`。
  JNI 解释方法因 thread_stopped 展开时，保留调用线程身份转为 A32GuestCallCancelled，供
  native clone 正常退出；其他解释器错误继续传播为失败，不按消息或关闭阶段吞掉异常。

- AndroidGuestProcess 拥有一个 `VirtualProcessKernel`（应用 pid/uid、地址空间统计作 VSIZE/RSS、
  存活 guest TID 作保留号），注册内置命令并绑定 fork/vfork/execve/wait4/kill；析构时在 guest 线程
  停止后、其余成员析构前停止命令线程。DexVM 桥把包名设为应用进程名，并以该内核提供 ProcessManager
  的 spawn（三对管道，环境缺省继承进程 environ，按 PATH 解析）与 waitpid/kill 服务；VM 建立后注册
  系统属性源（持 VM 执行锁读取 SystemProperties 同一张属性表），桥析构时先释放执行锁清除该源。
- AndroidGuestProcess 注册 `/dev/urandom` (1:9) 与 `/dev/random` (1:8)，通过 VFS reader
  注入 `hal::FillSecureRandom`；每次读取生成新的完整随机字节，失败由 VFS 映射 EIO。
  两者均使用宿主 OS CSPRNG，不模拟 Linux 熵池；O_NONBLOCK 不引入 EAGAIN/熵量等待。
  支持边界与生命周期见 [ADR-0129](https://github.com/poping520/OGPlay/blob/6beee68524f76d4a90e4957c9433171f30784425/docs/adr/runtime.md#adr-0129)。
  `--diag` 下设备 open/read/close 只记录路径、flags、字节数及返回值，不记录随机内容。

- PreflightAndroidGuestLink 只用生产 Bionic namespace、API-sealed Virtual SO 元数据和 loader
  检查映射/重定位；不创建 boundary runtime、ANGLE 或 surface，缺导入明确失败。
- InitializeApi19GuestProcess 事务建立 TLS/thread-info/preinit、独立环境页、4 MiB 栈、
  SVC #1 返回 trap 与空 property area，只写受检 libc 导出槽；冲突/非法名称/写失败回滚。
  配置来自 guest；Java 环境查询读 Bionic 当前 environ，不读宿主或另存影子快照。
  默认 API19 环境在 PATH 后发布 `LD_LIBRARY_PATH=/vendor/lib:/system/lib`，供 ROM System 初始化
  `java.library.path`；不创建 `/vendor/lib`，不实现 linker 动态环境搜索。显式设置 TZ=UTC；
  guest 后续 setenv 可修改本进程时区，时间读取仍经统一 Clock，不继承宿主 TZ/时区数据库。
- AndroidGuestProcess 是 native 资源 owner。rootless create 只接收 API19 system closure，
  用 libc 建立 namespace、process memory、syscall/clone、boundary、JNI/JavaVM/root attach；
  不接受 app ELF，返回时 ApplicationModuleCount 为 0，此后只随成功动态加载增加。
  AndroidGuestCallSession.Start 是 legacy adapter；AdoptProcess 仅包装既有 owner。
  `RethrowAsyncFailure` 经 session 暴露已保存的 clone/OpenSL worker 原异常，无需再次进入
  guest；观察不消费首错，仍由原 owner 保存，退出阶段的二次异常不得覆盖。
- `/proc/self/maps` 与 `/proc/<guest PID>/maps` 为按打开冻结的 guest 地址空间快照，
  最多 8192 个区间/1 MiB。输出实际权限（含 PROT_NONE）；ELF 区间使用与 dladdr 共用的
  guest 路径，匿名区间路径为空。私有 backing 的 offset/device/inode 为 0。
  provider 在 owner 析构前同步撤销，已打开快照由 VFS 继续持有。procfs 的 stat/status 暂缺。
  NDK Bitmap hooks 在 DexVM bridge 完成构造后发布，退役等待活跃调用完成；JNI env/
  reference 解析与 pending throwable 归 bridge，boundary 仅处理 ABI。活跃像素锁是
  VM 强根，recycle 延后退役 guest view，bridge 停止线程后解除 hooks 并清理 lease。
  Bitmap 的 Java byte[] 经宿主跨度借用成为 Skia CPU 后备。NDK、Java、GLUtils 与 Canvas
  共用像素，活跃锁期间拒绝 Canvas 访问该 Bitmap。解码与密度缩放由
  [raster](../raster/MODULE.md) 执行，资源选择、流关闭与密度发布由 ROM 执行。
  低位格式/预乘 alpha 的量化及调用者无同步并发写不等于完整 Skia 验收。
- /proc/meminfo 是进程创建时的只读 GuestMemorySnapshot，来自受检 GuestProcFacts：默认
  总量 1 GiB、free=total/2、Cached 缺省 total/4，cached/buffers 可显式注入，Swap 为 0。
  free+cached+buffers 不超过 total；与已有 meminfo 内容冲突时拒绝启动。Java/JNI
  ActivityManager 共用同一快照；不读取宿主内存或动态刷新，不改变 DexVM 堆配额。
- `dl_unwind_find_exidx` 按 PC 查询 process-owned namespace 的实际 load range，返回所属模块
  经 load bias 重定位的 `PT_ARM_EXIDX` 地址与 8-byte 表项数；初始和动态模块共用 linker 锁。
- libdl handle 表归 process。dlopen 复用已装载 basename/SONAME 身份；Virtual SO 视为已装载，
  只按 basename 匹配，不看请求目录，也不受 APK 同名文件影响。其他库首次装载保留来源拼写的 guest 路径，
  `/data/app-lib/` 使用所选 ABI APK，`/system/lib/` 使用系统来源（bundled ELF 或 Virtual SO），其他路径只走 guest VFS。
  `/data/data/<包名>/lib` 是 `/data/app-lib` 的 VFS 别名（无 APK 原生库时也建立）；
  显式绝对路径先校验并解析 VFS 别名，再判定来源，别名与 APK 库共用模块身份及一次 JNI 初始化。
  普通库名查显式 APK/系统来源及 guest `/data/app-lib/`、`/vendor/lib/`、`/system/lib/`；不读宿主
  环境或实现动态 LD_LIBRARY_PATH。nullptr 为 RTLD_DEFAULT，
  libhgl.so 映射 sealed libGLESv2.so；dlsym 先查 handle scope，boundary 可回退 LookupAny，
  DEFAULT 先 sealed catalog 再 global namespace。dlclose 只减引用，不卸载；未知库/符号/
  handle/flags 进入逐线程 dlerror，不返回假地址或访问宿主文件。
- 普通 libdl handle 为 API19 ARM32 `soinfo` guest 视图地址，支持范围见
  [ADR-0076](https://github.com/poping520/OGPlay/blob/6beee68524f76d4a90e4957c9433171f30784425/docs/adr/runtime.md#adr-0076)。表从现有 namespace 的未版本化导出和
  sealed provider 投影；真实 ELF 优先于部分截获目录，st_value/load_bias 合成同一已解析地址。
  视图只暴露所属库导出，dlsym 的 dependency/sealed fallback 保持原契约。
  重复打开共享活跃记录及引用，引用归零解除 backing；退役地址不复用，失效 handle 明确失败。
  只读记录/符号/字符串/SysV hash 位于 `[0x79000000,0x7a000000)` 有界 arena，宿主更新引用
  时临时切换记录页权限；Virtual SO 的 base/size/load_bias 为 0，符号指向真实 thunk/data。
  不提供完整私有 linker 状态或其他 API/ABI 布局，不接管模块、构造器或 JNI 初始化。
- soinfo 仅投影名称、base/size/flags、符号/字符串/SysV hash、引用数及 link_map 地址/名称、load_bias（≤65536 导出、≤4 MiB）；不提供 phdr/dynamic/链表遍历、linker 写入或 debugger 协议，因为它只是当前进程的只读元数据视图。
- NativeLibraryLoader 以 ASCII 折叠后的 canonical path 为登记键，ClassLoader token 管理归属，
  维护唯一 Loading/Loaded/Failed registry：同 loader 幂等、同线程递归返回现有 handle、跨 loader 拒绝、Failed 重试稳定失败。
  record/result 的 canonical_path、soname、模块名与 dladdr/proc maps 的路径保留来源拼写：
  APK 使用 inventory，系统库使用目录规范名，VFS 文件使用节点 spelling；不存在时诊断保留请求拼写。
  app resolver 只用 selected-ABI inventory，basename 为身份，DT_SONAME 为受检别名；
  DT_NEEDED 可用二者，别名歧义、ET_DYN、依赖/重定位、constructor/JNI version 均受检。
  API 19 动态装载时，带路径的 DT_NEEDED 在精确查询未命中后可按文件名复用进程命名空间中已装载的库。
  静态导入审计没有进程装载历史，仍按精确依赖名解析。
  显式加载完成日志附 ELF 加载基址及映射起止，A32 故障的模块偏移从同一 linker namespace 的实际映射区间查得。
- [ADR-0135](https://github.com/poping520/OGPlay/blob/6beee68524f76d4a90e4957c9433171f30784425/docs/adr/runtime.md#adr-0135)：共享来源快照拥有 APK/系统库字节，process
  经显式 native callback 持有；Java registry 退役不悬空其来源。Java/native 共用 namespace
  与构造器状态，native dlopen 不登记 ClassLoader 或执行 JNI_OnLoad，后续 Java load 复用映射。
  process 装载门禁可同宿主线程重入、跨线程串行；root CPU admission 在门禁前，元数据锁不跨
  guest 执行。构造器递归前标记，依赖先于本库，DT_INIT 先于 INIT_ARRAY；失败保存原故障并
  禁止重跑，不承诺回滚 guest 副作用。native 构造调用保留 caller thread/TLS，执行前复制
  lifecycle 记录以抵抗递归扩容。仅可恢复链接错误进入 dlerror，执行控制故障沿原路径退出。
- NDK AssetManager 经 bridge hooks 校验 JNI env/thread/reference，与 NativeActivity 共享
  同一 Java 对象的 opaque 身份；不永久保活 Java 对象，GC 撤销 manager，现存 Asset
  持有独立来源。bridge 在停止线程后撤销 hooks、退役所有来源和注册。
  Java 与 NDK 使用同一资源核心 manager、配置和 APK backing。STORED FD 借用原 APK 区间，
  普通读取使用有界窗口，现存 Asset 可在 manager 退役后继续读取。

- NativeActivityRuntime 借用当前 process 的 guest 分配、模块导出查询、JNI/JavaVM 与执行器，
  只拥有 ARMv7 ANativeActivity/callbacks、路径/state backing 和真实 Activity/AssetManager global
  refs。活动 backing 地址单调分配，销毁时解除映射与注册。生命周期回调要求创建线程，
  native 执行释放 VM 锁。NDK finish、窗口格式/标志和 IME 命令从任意线程进入每实例的
  有界 FIFO，唤醒 loadNativeCode 的 MessageQueue，由该队列线程执行原版 Java 方法。
  unload 开始时关闭队列并丢弃未派发命令。
  进程收尾在 Java worker join 后、JNI 解绑前显式 Release，包括启动失败未调用 onDestroy
  的实例；Release 幂等并报告失败，析构兜底记录失败，不能掩盖原始启动异常。
  NativeActivity 的 LoadPath entry mode 不执行 JNI_OnLoad；随后 Java load 复用同一 registry
  和映射，并执行一次 OnLoad。不得启动 standalone NativeActivitySession 代替这个桥。
- Java 显式路径、逻辑库名与 native dlopen 共用系统库查询，按 ASCII 大小写不敏感匹配 bundled ELF
  与当前 API 的 BoundaryCatalog，并返回来源的规范文件名。普通库名依次查 APK、系统来源、guest VFS；
  解析别名后的直接 `/system/lib/` 路径先查系统来源再查 VFS，不以路径发布为虚拟库加载前提。
  显式 Virtual SO 不解析 ELF/依赖闭包，以空 sources 扩展进程 namespace；Java、DT_NEEDED 与 dlopen
  共用同一模块，既有 ClassLoader 规则不变，无 JNI_OnLoad 即成功，不增加 ApplicationModuleCount。
  同名 APK ELF 仍拒绝。未映射 Bionic source 持有副本，按 DT_NEEDED
  可达性加入同一 namespace。map 事务完成后执行 constructor；registry mutex 不跨 guest
  call。只有显式 root 执行 JNI_OnLoad，不把 dependency 算作 Java load；module 持有至 Stop，
  按实际完成 constructor 的逆序 finalize，成功 load 记录 sequence/soname。
- System 和 Runtime 的公开加载方法汇入 Runtime 两个接受 ClassLoader 的包内方法，
  复用同一 loader 和 application ClassLoader token。Java 边界
  为 NPE/UnsatisfiedLinkError，宿主保留 typed cause；legacy InitializeJniLibrary 只能在
  运行中、constructors 和类登记完成后调用一次，成功或无 OnLoad 才发布 ready。
- `NativeLibraryLoader::PublishSystemLibraries` 由 AndroidAppProcess 与 dex_run 在构造 DexVmGuestBridge
  之前显式调用，保证 ROM 首次创建 ClassLoader 时 `/system/lib` 已可查。仅发布注入的随包系统 ELF
  与当前 API 的全部 BoundaryCatalog 虚拟 SO，规范文件名、只读；内部 `libogplay_jni.so` 不发布，
  内部加载路径不变。已有路径或重复身份明确失败，不覆盖。ELF 的 PutReadOnlyFile reader 与加载器
  共享同一份不可变字节，VFS 节点持有 owner，不受 registry/process 退役影响，不再次复制库内容。
  虚拟 SO 文件为确定性的 ELF32 ARM ET_DYN，带 SONAME、合法 PT_LOAD/PT_DYNAMIC 与仅含空项的
  dynsym/hash；只表示存在与身份，读取文件看不到真实导出表是已知边界。任何系统路径加载（含别名、
  点段及大小写变体）先解析为目录中的虚拟 SO，不将身份文件送入 ELF 映射。
- BaseDexClassLoader.findLibrary 执行 ROM DexPathList，按应用 `/data/app-lib`、系统
  `java.library.path` 的顺序过滤存在且可读的目录，再通过 VFS 查找 System.mapLibraryName 结果；
  默认系统目录为 `/vendor/lib:/system/lib`，不存在的 vendor 目录由 ROM 跳过。所得路径由 Runtime
  包内方法的绑定交统一 NativeLibraryLoader 装载。integration 不覆盖 findLibrary 普通方法。

## DexVM / JNI 唯一身份与执行

- JNI AllocObject/NewObject* 唯一经分配回调在 VM 执行锁内选择调用线程 context，委托 AllocateJniObject，
  Java 失败以真实 pending throwable/null 返回。成功对象使用既有 local 发布/字段桥；
  发布失败移除 registry 映射，未发布实例无永久根，由普通 GC 回收。guest worker 与
  资源清理结束后撤销回调，不能跨 session 保存捕获的 VM 指针。
- DexVmGuestBridge 按 core+android catalog → 原样启动类路径 DEX → app DEX 装配；
  native/服务显式绑定，ROM 普通 Java 原样执行；存量替换/覆盖按阶段移除，新代码不得增加。已迁移平台类由 VM 发布 JNI，禁止同名 HLE 遮蔽；
  无 VM 的 native/HLE 会话不保留旧替身，独立 headless 契约和应用兼容回调除外。
- String 复用 VM 对象身份，primitive/object array 复用会话 store；jclass 双向规范化为同一 ClassObject。
  数组 jclass 直接使用 ClassObject 的 DexVM 对象身份，JNI 类型查询、引用往返和 monitor 共用该对象。
  PublishLocal 用 EnsureRegistered 原子幂等发布真实类，GC 同步清扫 registry；不为所有
  APK 类预占 global ref，只有 static native 出向的 jclass 按需缓存，发布使用调用线程身份。
  JNI 创建的 String 和数组首次类型查询在执行锁内登记真实类，数组按需导入。
  primitive kind 对应精确数组 descriptor。该按需登记
  与 GC 同锁，不增加引用根，未知 external identity 不递归导入，teardown 撤销解析器。
  native 资源清理 scope 以当前 JNI 线程 local ref 保活并暂清 pending，所有出口恢复原
  throwable；清理错误独立传给调用方，普通 JNI pending gate 保持严格。
- JNI 类解析遇 guest linkage failure 时在当前调用线程发布原 Java 错误，再返回空身份；
  真实内部错误继续传播，不能把它当作查询未命中。
- 类注册保留完整父类/direct interface 图；interface MethodID 在实际 receiver 上虚派。
  数组存储和涉及数组类的 IsAssignableFrom 复用 linker 的类、接口、数组协变与 primitive 规则；
  任一侧不属于 VM 返回 nullopt 保留 JNI 校验，不能无条件放行或把不相等视作不兼容。
  数组的继承方法使用 linker vtable 并返回声明类现有的 JNI 成员 ID。
  类查询与兼容回调在 registry/store 锁外、VM 执行锁内运行，guest 线程停止后撤销。
- DEX 字段以稳定 JniFieldId→VmFieldId 访问同一 instance/static 槽；GetFieldID 先 clinit，
  引用按调用线程转换 local ref。未接管 HLE 字段仍归 JniFieldStore，不建 DEX 影子存储。
- native 出向按 descriptor 编 A32 soft-float 帧：r0=JNIEnv、r1=receiver/jclass，64 位偶对齐、
  栈 8 字节对齐，J/D 返回 r0:r1。先 RegisterNatives，再 Java_ 导出，再记账失败；已解析
  目标的 CPU/JNI fault 不得降级为未注册，诊断保留 class/method/signature/thread 与原 cause。
  DexVM native 的所有已解析调用失败还必须保留 context token，并以缩进 cause 原样承载
  execution 层的退出来源、syscall 与 A32 现场，不能由上层重新拼成信息更少的摘要。
  入向复用 233 槽 ABI；Java 异常按 JNI 置 pending，原 throwable 和 modified-UTF8 消息保留。
  JNI 入向异常日志与出向 native 故障附带原异常的有界字段 cause 链（含 API19
  ClassNotFoundException.ex）；不执行 guest getCause/toString，循环/深度/文本截断显式标记。
- 每个 guest Java thread 有独立 A32 CPU/栈、Bionic TLS/thread-info/TID、JNI attach/local frame。
  Java EGL/GLES 经显式 token→process TID hook 使用相同线程身份，不用 Java Thread.id
  代替 native TID；GLThread 的 Java/native current context 与错误状态共享。
  native 执行释放 VM 锁，JNI 回调重获；executor 按 thread 和重入深度复用 CPU/JIT，同层复用
  缓存、不同深度隔离现场，nested 使用 suspended SP/TLS，线程退出回收全部 executor。
  DeliverSignals 以返回陷阱为目标做一次空 guest 调用，handler 在所属线程的当前 guest 栈上运行；
  嵌套时先把外层 r4-r11 写入 guest 栈，供按信号停线程的 GC 扫描。teardown 或原生故障后不再投递。
  JNI monitor 使用 VM token/对象身份，与 Java synchronized 共享状态；TID 受 Bionic 16-bit 限制。
  原生 JNI 附加线程首次 Java 调用、字段 clinit 或 monitor 操作建立独立 VM token，最多
  64 个存活附加 context；复用原生 clone CPU/SP/TLS 进行 Java→native 重入，不另起线程。
  Thread 身份/daemon 归 VM runtime 与 JNI attachment，detach 要求 context 已空闲，再释放
  monitor、context 映射、local refs/pending exception；未 attach 的调用明确失败。
  尚未执行 Java 的纯 native attachment 可直接 detach；显式 JavaVM detach 在任何状态变更前
  检查 Java/native frame，拒绝签名为 `detaching thread with interp frames`。
  附加名称传入 VM Thread；非空 group 仍明确失败并记账。
  process 暴露 warnings 只读快照，run-dex ledger 保留 warn 级命中。
- clone 宿主异常通过显式通知启动等待取消，native 调用入口、安全点及异常出口优先上报
  保存的原始子线程错误，不让后续 teardown/退出错误覆盖首错。
- Manifest `targetSdkVersion` 为 1..13 时，DexVM bridge 按 KitKat
  `workAroundAppJniBugs` 启用 JNI direct-reference 兼容；其余版本保持严格 local frame。
  兼容触发输出结构化 warn，不得由 Profile/title 分支启用。
- renewable native frame 只在真实 futex park、正字节 IO、present、audio enqueue、JNI 重入
  等可观测进展时续 watchdog；查询/EOF/wake/yield/内存管理和空转不续期。
  JNI 重入进展判别的有界限制见 [ADR-0023](https://github.com/poping520/OGPlay/blob/6beee68524f76d4a90e4957c9433171f30784425/docs/adr/diagnostics.md#adr-0023)。
- JNI direct-buffer hooks 在桥中按调用线程执行 ROM DirectByteBuffer(long,int) 构造器，
  地址/容量直接读 ROM 字段，退出时撤销 hooks；session 只持通用临时 guest 映射工具。
  Java GLES 的 Buffer 与 primitive array 参数直接取稳定 payload 地址，Bitmap native 只复制
  当前位置之后的原始字节，position 的推进由 ROM Bitmap Java 完成。同步 host span 不跨 GC
  或映射退役保存；GL 编组后进入唯一 sealed managed GLES/ANGLE。

## 退出、阻塞与 Clock

- runner 只有资源、引用、线程和生命周期均闭环后才可报告成功。Stop 先快照所有 child、
  请求退出并中断 futex，再全部 join；单个失败延迟报首错，不得跳过其余 join。
  native 顺序为 child 退出/临时唤醒 → join → guest fini/destructor → root JNI detach →
  永久 JNI monitor shutdown；finalizer 期间 MonitorEnter 必须可用，Stop 幂等。
- BeginTeardown 是不可逆、幂等的 process 退出门：封闭 EGL/GLES，取消 renewable JNI frame，
  中断当前及后续阻塞；既有 slice/boundary 安全点观察取消。RunTeardownCleanup 仅
  允许创建 owner 在退出阶段准入 live root 的同步清理，重入共享有限且不可续期的 tick
  预算；新增 Java/native 线程或模块拒绝。普通取消以控制展开停止相应线程，不能发布
  为异步进程故障。清理失败不再进入 DSO fini；原生根 exit 后也不重复 guest 析构。
  生命周期在 Java thread join 前再次中断，覆盖回调中新建的 futex wait。
    DSO fini 同样进入根清理 scope；所有 scope 还共用通过 Clock 计量的 2 秒墙钟预算。
    其前的 RunGracefulCleanup 保持线程/模块准入与图形存活供协作回调，
    只在 BeginTeardown 后关闭新线程/模块准入；
    创建 owner 的 live root 同步 scope 与 RunTeardownCleanup 共用上述累计预算。
  nanosleep 在执行前校验剩余预算，根线程清理时 futex WAIT 同样受剩余时限约束；返回/SVC/slice 边界检查超时。
  未处理 CPU stop 以 typed fault 标记 native 状态不安全：首个 catch 独占保存不可变现场
  （实际 guest TID、可选 Java native class/method/descriptor 与原异常文本，总计至多 8 KiB，
  超限注明截断），写完后 release 发布标志；后续调用 acquire 观察后在原拒绝首行下附该记录。
  嵌套/并发的后续 fault 或拒绝不得覆盖；有 logger 时仅尝试一次 `runtime.native_fault` error，
  日志失败不改变原异常。拒绝后续 guest 调用/清理并跳过 DSO fini；宿主线程回收、JNI detach
  和 monitor shutdown 仍执行，clone/OpenSL worker 的原异常重抛路径不变。
- Runtime 的 hook 与 exit/halt 普通协议归 BootDex；平台只绑定 nativeExit，发布 session
  退出标记并调用 VM.Exit，保留退出码、非 Java 展开，不从 worker 同步 Stop/join。
  宿主 Stop 仍为取消，不自动运行 hook；详见 [相关说明](../../../docs/design/dexvm.md)。
- wait/sleep/timed join 共用注入 monotonic Clock。Java 与 syscall 共用会话纳秒 uptime，
  currentTimeMillis 与原生墙钟查询共用会话墙钟原点（`wall_epoch_millis`，生产会话为启动时宿主 UTC 减去初始
  uptime，测试默认固定纪元）。时钟驱动线程上的原生 nanosleep 与到期的
  futex 限时等待经 `native_sleep_observer` 推进 uptime，下一次帧推进扣除该额度。
  ADR-0171：blocking observer 统一跟踪生命周期驱动停摆并转发 EGL pacer；无 EGL 亦适用。
  root/worker 限时等待仅补至停摆起点 uptime 加注入 RealtimeClock 的真实流逝时间，
  不超过自身 deadline；root join/退出 hook 同规则，恢复后按帧推进且不回拨。
- ROM 主 Looper 在独立 guest/宿主线程执行；MessageQueue native 负责 poll/wake 与 Clock 等待，
  lifecycle safe point 排入服务请求并等待进展，消息和 detached View.post 引用由 ROM 对象图保活。后台任务必须保留真实异常，不能以 null 结果伪造正常完成。

## VFS、Android 与媒体边界

- DexVmIoVfsAdapter 是 core IoFileSystem 到唯一会话 VFS 的桥，Java/native IO 看同一世界；
  StatVfs 共用 VFS 卷统计并保留 errno，Android 不另持宿主剩余空间或无路径统计。
  mkdir/mkdirs、list/delete/rename/read/write/flush/close 保留真实语义，异常出口关闭临时 FD。
  只透传 guest cwd/writable；Java File 布尔 API 才把 VFS 错误收敛为 false。
- APK bytes/inventory/Manifest 由上层 sealed 输入；resource/package 路径为 guest 路径，
  getPackageCodePath 挂载原 APK bytes，ContextWrapper 委托 base，不泄露 frontend 宿主路径。
  Resources/AXML、File 目录与资源枚举都读同一受检事实；AssetFileDescriptor 只提供已支持的
  逻辑长度/幂等 close，不伪造 POSIX fd/offset。
- ROM View 层级独占布局、命中与绘制。WMS 保有窗口层级、frame、焦点和输入通道；软件 Canvas 经 Skia 发布 Surface 像素，NDK/EGL producer 经同一 Surface 标识发布帧。
- ActivityThread 的调度来自进程内 AMS；WMS、输入法及 Toast 通过进程内本地 Binder 对象交接。细节见 [Android 子模块](dexvm_android/MODULE.md)。

- AudioTrack/legacy JNI/OpenSL ES 共享一个 PCM mixer，guest→player 映射不成为 GC 根，
  release/GC 回收 player；回压阻塞释放 VM 锁。OpenSL callback 经专用 A32 thread/CPU/TLS/栈，
  不隐式 attach JNI，允许 SVC 重入，失败在后续 process call 报告；混音只向 HAL 提交一次。
- VideoView 从 guest 路径原子捕获 VFS read lease，并注入 VideoSourcePlayerFactory；统一 uptime 驱动位置；取帧
  letterbox 发布，完成回调一次。工厂/路径/打开失败交付真实 error，不能回退为 completion。
  callback 按 View/source generation 保持，窗口可见性参与呈现准入。
  普通 MediaPlayer 的本地视频复用同一 decoder/Clock/PCM/合成状态；播放器为 owner，
  managed Surface 为可撤销输出目标，销毁后必须显式重绑。仅有 MediaPlayer 的会话
  也泵送原版 Handler 媒体事件，不依赖创建 AudioTrack。
  PCM 最近邻重采样饱和混入，停/暂停/结束静默；自由运行可按真实时间节流，
  手动步进保持确定性。SoundPool 解码成功才原子提交 loaded，失败保留 pending/错误事实。
  进程在 FFmpeg 可用时向 MediaPlayer 和 SoundPool 的音频 mixer 注入同一纯音频解码入口。
- legacy media 的 no-op/返回值只表示 Java 契约；volume/pitch/loaded/voice 为受检会话状态。
  资源必须真实非空且受 JNI size 限制；audio.load_movie 仅登记最多 4096 UTF-16 单元的请求，
  不等于已播放。display.change_mode/process.exit 只发布可查询请求，由上层实施窗口/清理策略。
- 网络默认离线，沿注入的 policy/transport 执行。设备、身份和服务以会话配置与模拟设备状态为准。
  Activity 当前/根身份来自 lifecycle，finish/handoff 不混为进程退出；
  SQL 持久化只用 guest VFS。Typeface/主题边界见 Android 子模块。

Debug 当前进程查询的默认 provider 在 bridge 装配后安装，析构撤销自有回调；
外部 provider 保留。VM 执行锁内读取托管堆账本和 guest 已映射页（含 PROT_NONE），
采用显式虚拟私有提交模型：托管 charge 计 dalvik，未分类 ARM RAM 计 other，KiB
向上取整。无宿主 RSS、GPU/媒体缓存或未映射预留；映射快照忙则明确缺事实。
模型边界见 [ADR-0118](https://github.com/poping520/OGPlay/blob/6beee68524f76d4a90e4957c9433171f30784425/docs/adr/runtime.md#adr-0118)。

## Runner、诊断与验证

NativeActivitySession 只支持 API19 ARMv7，执行 Bionic 初始化、ANativeActivity_onCreate、
glue child 和销毁；child 异常必须唤醒 waiter，在 root/帧/输入边界报告原始原因。
supersample_factor 受检为 1..4，guest 保持逻辑尺寸，由 ANGLE 放大并确定性 resolve。
DiagnosticState 只收稳定 ID/整数/执行进展；A32 slice observer 由上层注入，可限定宿主线程。
外部 observer 未限定时覆盖所有线程；限定后仅在该线程调用，其他线程可使用普通 HLE
快路。内部诊断、取消、预算与 clone 首错观察不受该限定影响，renewable/cleanup 仍走
runner。JNI 保留 runner 分派以先发布当前 CPU，支持 Java→native 嵌套重入；JNI fast fault
交回原 dispatcher 还原首错。GPU trace 只写
固定 raw ring（最近 2048 调用）与独立 mutex，文本延迟格式化；TryTrace 不竞争主 boundary
锁，不伪造扩展/FBO/限制。debug 层不得反向依赖 integration。

生产装配入口为 AndroidIntrinsicCatalog / AndroidCoreIntrinsicServices；catalog 只聚合，
family 声明函数同址绑定，shared 只放跨 family helper，support 承担派发；不恢复 Populate、
集中 handler 容器或字符串分发。设计背景见 [DexVM](../../../docs/design/dexvm.md)。
定向测试位于 [tests/runtime](../../../tests/runtime/) 的 native loader、guest call、JNI、preflight、
headless/NativeActivity，以及 [tests/dexvm](../../../tests/dexvm/) 的 Android/线程/布局/scheduler。
只构建受影响目标；文档修改只做 UTF-8/链接/diff 检查，人工探索不能代替 Scenario gate。

只读诊断通过 AndroidGuestProcess 转发 memory/CPU/GPU try-snapshot；NativeLibraryLoader registry 使用 try-lock，最多 128 项、每段文本最多 512 UTF-8 bytes，保留 Loading/Loaded/Failed 事实。Syscall FD/node_id 来自已知 ARM 文件 syscall 参数/返回值与 VFS 元数据；无法解析的节点保持未知。

## Java/NDK Looper 关联

进程 owner 可通过 `GuestFinalizationPolicy::forbidden` 传入上层已发布的 Java 进程退出事实，
禁止再次执行 guest DSO fini；默认 permitted 仍受原生退出、故障隔离及清理预算约束。
两种策略都完成宿主线程/资源回收，重复 Stop 不重跑 finalization。
AndroidGuestProcess::NativeTerminationSignal 只读查询根线程的信号终止事实，不改变 NativeExitCode、退出准入或停止协议。

DexVM bridge 的 Java worker 先登记 execution token，首次 native 操作才在 VM 锁外准备
独立 CPU/TLS/JNI context；Java-only 线程不占 native 池。实际 native context 仍为线程独占，
worker 最多 31 个，进程最后一槽保留给 OpenSL callback；满池明确失败，退出回收已分配的槽。
准备与退役串行，避免监视器快照从其他宿主准备 context 时把资源登记到已结束的线程。
DexVM bridge 通过 token→guest TID 显式准备 NDK Looper，Java 主线程在 Activity/JNI 启动前
已关联，HandlerThread.prepare 复用该 registry。线程退出释放内部引用，进程 teardown
先 shutdown Looper 以唤醒阻塞轮询。native clone 的存活来自 lifecycle hook，不继承父 Looper。
回调轮询经可重入的 guest 调用入口在同一个线程执行，进入前发布当前 CPU 上下文。

SurfaceStream 是无 JNI/GL/回调的进程内有界交付队列：producer 带 stream identity/epoch，
async pending 替换不重发事件；acquire 将待上传帧独立持有，commit 锁存、abort 在无更新帧时归还，
上传期间新 enqueue 保留自己的通知。single-buffer 在消费归还前明确 busy。snapshot 持不可变 RGBA
与源 PTS/transform，所有 lease 的实际 backing capacity 共享进程预算；不维护第二播放时钟。

原生单次调用的默认看门狗额度与 session 共用 runtime/common 的 100 亿 tick 常量；
现有 observable-progress 续期规则继续生效，未新增按标题配置或仅按墙钟检测。

诊断与生产共用 BoundCoreIntrinsicCatalog 的 core/platform handler 绑定；metadata-only
DumpAndroidBoundarySurfaceJson 读取同一 API19 boundary 导出目录，不创建进程/窗口或执行 guest。

JNI ExceptionDescribe owner hook 在调用线程 context 内虚派 Throwable.printStackTrace()，
输出沿 Java System.err；Java 打印异常被忽略，pending 的清除、保活与恢复由 JNI 层拥有。

## Java payload provider（ADR-0152，阶段 2）

`GuestJavaPayloadProvider` 归 AndroidGuestProcess，显式接收 AddressSpace/ledger/limits 并注入
primitive array store。小块 8-byte 对齐按尺寸分级，每个 slab 只服务一个
级别；默认 16 KiB slab / 8 KiB 小块阈值共 64 级（至 128 每 8 字节一级，其后每翻倍
区间 8 级）。直接 class lookup、各级有空位 slab 的侵入链表及空闲槽栈均为 O(1)，
不遍历全部 slab 或逐位查找；generation 元数据用散列表，插入/删除摊还 O(1)。创建 slab
或重分空 slab 的槽表初始化最多 2048 槽；映射慢路径仍 first-fit 查统一 guest 账本。
大块独立映射。映射按宿主 backing 粒度对齐，按 guest 页退役，不破坏邻页。
全局最多缓存 64 个空 slab（默认至多 1 MiB），可原址重分给其他级别；超出上限才解除
映射。cache/级别切换在分配元数据准备后提交，失败不改变已提交计数或空闲槽状态。
零长块也占独立地址/身份，地址复用更换 generation。provider 只管理 committed/VAS，逻辑
堆目标/增长/emergency 费用全归 VM Reserve。VM 装配时经 store.ConfigurePayloadHeap 显式
传 maximum/growth（0 < growth ≤ maximum ≤ 4 GiB），不另设生产堆目标或 512 MiB 提交限额。
普通提交上界推导为 min(VAS − E, F × maximum + C × S)，用饱和运算避免溢出：
S=slab bytes，C=空 slab 缓存上限，E=按宿主页对齐的 emergency reserve，
F=max(3, ceil(S/32))。3 覆盖级别取整、尾部及大块页对齐；ceil(S/32) 还覆盖一个 slab
仅存活一个零长数组（VM header 收费 32 B）的最坏稀疏碎片。默认 F=512，结果饱和到
可映射 VAS；这只是 backing 安全上界，不预提交该空间、不增加逻辑堆预算。maximum
而非当前 growth 用于计算，以允许 clearGrowthLimit；guest 映射/VAS 或 OS 提交仍可能先失败。
夹具可显式覆写 committed 限额以验证这一失败路径，生产默认无独立覆写。

ConfigureHeap 提前提交 E=256 KiB 独立受保护映射，只允许 allocation context 显式传来的
VM emergency 状态使用；普通分配始终不可借用。先尝试普通路径，bad_alloc 时应急分配
才回退到预留池。预留按 8 B 对齐分割、合并归还范围，普通 GC 释放异常/栈数组时补回；
不得因离开 emergency 强行释放仍存活的对象，静态根所持数组继续计费/保活。
VM Reserve 仍是唯一账本。预留也耗尽时抛明确 JavaPayloadEmergencyExhausted，绕过
可再次构造 OOME 的 bad_alloc/heap_budget_exhausted 转换，不递归制造异常。model 的显式 allocation context 在 store 锁外
持 VM 执行锁完成收费事务；New/import 单次收费，失败完整回滚，provider 提交/地址不足
映射 OOME，沿既有异常 emergency reserve 构造 throwable。

GC 集成枚举 environment references、MUTF-8 lease 与数组身份 pin roots，在 mark/
weak 处理前生效。停止/join Java workers、native workers 和 CPU 后才退役 Record/store view，
再释放 provider；成员顺序保证 store 早于 provider、provider 早于 ledger/AddressSpace 析构。
独立 headless 契约显式选择 host-copy provider，不作为生产 guest backing 的回退。

provider 保护映射并记录修改/restore 拒绝；AddressSpace 与 ledger 晚于所有 view 析构。
view 保活 backing、阻止 Release，每元素不经全局内存锁。guest HB/CPU quiescence 合同不变。
Stop 输出结构化 `runtime.java_payload` 聚合：kind/长度分桶计数、峰值 live、committed、
slab free、分配失败次数及各尺寸级别的 slab/总槽/活跃槽/累计分配数；不输出逐对象明细，
元素热路径不参与采样。emergency_reserved_bytes 纳入 committed，单列 emergency_uses、
emergency_live_bytes、emergency_peak_bytes；slab_free 仍只统计普通 slab，不包含预留池。

ADR-0153：process 给 guest JNI bindings 注入结构化 logger，warning ledger 按真实槽/线程/
异常类去重；普通 Java 重入不预清 JNI pending。VM 类初始化 post-clinit source 在执行锁内
按 context token 查显式 guest TID，只在该检查点将原 throwable 交解释器包装/保活，返回结果
再发布 JNI pending，不吞原异常。owner 停止执行后撤销 source。

ADR-0152 阶段 3：primitive store 以数组身份维护跨线程 pin，Elements/Critical 直接借用
guest payload；GC 在 mark/weak 前枚举非零 pin，长期 view 与不可移动 backing 保证地址稳定。
process 停止并 join 全部 Java/native 线程及 CPU 后才销毁 store，残留 pin 统一退役并诊断；
Stop 采样另记 store 的 pin_count/pinned_arrays/pinned_bytes，不与 provider 的独立底层 pin 重复计量。

JNI String 槽经进程注入 JniStringBackend。启动类路径链接后由 DexVmGuestBridge 装配
VM 实现，guest 执行结束后解除。UTF-16 借用计入 char[] 的数组 pin 账本，未配对释放及阈值
诊断经 logger 与 warning ledger 发布。MUTF-8 仍受独立复制预算约束。

实验 Paint 测量状态按 Java 对象身份拥有，GC sweep 退役；Typeface 强边仅由 Java 字段追踪。

BootDex 装配校验启动类路径的 ZIP/DEX 来源，API19 资源由封存归档提供。
系统及应用布局经 ROM Resources、XmlBlock 和 LayoutInflater 加载。ROM NinePatch 使用 native 伸缩区与原 Bitmap 像素。

Class.getDex 的 buffer 工厂复用 process GuestMemoryAccess 的 direct arena：拷贝一次完整 DEX，
经 JNI 同用的 ROM DirectByteBuffer(long,int) 构造器建立 ByteBuffer 后设只读视图，再经 AddressSpace.Protect 保护页；地址不移动，
映像随 process/VM teardown 退役，缓存与 GC 根归 ReflectionRuntime。不另设 DEX 内存分配器。
