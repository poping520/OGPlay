# 子模块：runtime/integration

## 职责与依赖

静态 native 导入审计与 NativeLibraryLoader 共用来源快照、APK basename/SONAME 别名和依赖
选择；逐个所选 ABI APK 根构建 Bionic namespace，用现有 lookup/version 规则聚合强/弱缺口。
未引用导入仍发布诊断及独立计数；只有非 NONE 重定位引用的强缺口计入预检失败。
不映射或执行 guest，不把全 APK 导出并成虚假全局 scope。JNI bridge 向 boundary 注入与
Java 同源的空传感器目录、DeviceLocale provider、VFS pipe hooks，以及 Surface 引用→active holder→window 的显式
转换；转换/acquire 在 VM 锁内完成，holder changed/destroyed 同步边界窗口，退出后解除 hooks。


DexVM bridge 拥有 JNI 反射转换适配：FromReflected 使用 ReflectionRuntime 的成员元数据
取得已有 registry ID；ToReflected 根据声明成员还原真实 Method/Constructor/Field wrapper。
校验引用类型、声明类与 static 标志，在 VM 执行锁内发布调用线程 local reference；ID 不
拥有 wrapper，不另建字段存储。销毁前先停止调用，再清除 JniEnvironment 的反射 hooks。
JNI FindClass 的显式 resolver 在 VM 执行锁内按需链接并发布 own 成员，保留平台注册表
既有实现；被动 FindClass 不递归调用 resolver。预发布遇到实现类型非 interface 等 Java
链接错误时延后到真实 Java/JNI 使用，不因未使用的非法类使进程装配失败；失败发布不保留递归标记。
值传输复用稳定的双向类身份缓存，成员解析仍经发布入口补齐按需物化的成员。异常 factory 发布真实 local Throwable。
JNI 字段访问经解释器共享守卫：骨架非常量字段记账、挂起 UnsatisfiedLinkError，禁止侧表回落。
GC roots 同时包含 JNI 引用与字符串 store 的活动 chars/UTF8 lease；Release 前不得扫除
相应字符串。进程 quiesce 只 join clone runtime 实际拥有的 child；Java-only 线程交由 Java
thread owner join，不能依赖惰性 native context 是否存在来区分。
这些 owner hooks 在调用静止后统一撤销，不使下层反向依赖 DexVM。

`GuestCpuEnvironment` 由进程拥有并冻结 CPU 配置；在 VFS 发布可撤销只读 CPU 信息与
sysfs 拓扑/标称频率。API19 `sysconf` 导出通过进程私有 RX guest 桥处理核数 selector
96/97，其余参数保持原 ABI 尾调用原 libc。重定位、dlsym 与 linker 视图使用同一导出；
不修改原 ELF 指令。DexVM bridge 从该进程取得核数，禁止另用宿主硬件事实。
不支持统计负载、热插拔、调频控制或周期精确模拟，见 ADR-0080。

第二阶段（ADR-0081）可按配置建立一个进程共享 ExecutionBudget，所有主/clone/Java
native/音频回调及重入 CPU 使用同一 Dynarmic context。并发上限可独立启用为配置核数，
总速率以百万 backend tick/秒指定；默认不设预算。BeginTeardown/Stop 先解除预算等待，
再执行原有线程退出和 finalizer，不在关闭过程中重新阻塞。Java 字节码继续遵循原 VM 锁。

组合 Bionic、guest JNI ABI、Android boundary、DexVM/框架 HLE，拥有进程会话、动态加载、
headless/NativeActivity runner 与出口报告。可依赖 jni_guest/boundary/framework/jni/bionic/
syscall/execution/vfs 及下层；下层不得反向依赖 integration。不直接持有窗口、消费输入或
实现游戏专属逻辑；executor、Clock、VFS、Profile 必须显式注入或由确定性 fixture 建立。

内容树触摸分发、reverse-Z hit-test、intercept/CANCEL 与非 split 捕获归 integration，
session 宿主输入和 guest Window 入口共用相同状态与算法；integration 不消费宿主设备。

实验主题 provider 的资源选择与文件验证通过 APK owner 显式回调注入；六槽与 indices
完整解析、校验后发布。叶 drawable 的不可变像素按 Java 对象身份持有并随 sweep/teardown
释放，不建立全局强根；布局与几何不在值桥写入。同 ROM 属性 format/枚举表决定投影类型，
ROM 字符串独立使用 cookie2/StringBlock；cookie1 保持 APK。封存的默认背景 selector/ninepatch
可提供状态、padding/密度尺寸与像素 backing；选中叶在现有 bitmap 平面冻结，guest draw 仍明确失败。
同 ROM color 目录按 selector/item、android:color/state_* 结构封存原始 XML，经 ROM
ColorStateList.createFromXml 构造；标量颜色引用取同 ROM ARSC，未知结构/属性/引用失败。

Android API 的详细行为以 [dexvm_android](dexvm_android/MODULE.md)为准；
[DexVM](../dexvm/MODULE.md)拥有解释/GC/线程，[session](../../session/MODULE.md)编排生命周期，
[capabilities](../../../capabilities.toml)登记支持范围。普通 Java 算法不得在 integration 重建。

## 进程与库加载

- FindLibraryExport 按库身份查询完整的已加载命名空间；缺库或导出返回 optional，
  不隐式加载。平台属性查询与其他 native 边界遵循同一 VM 锁释放/恢复协议。

- 锁顺序：持有 VM 执行锁时，禁止获取进程 `guest_call_mutex_`、`native_load_mutex_`
  或 DexVM native context 的 `call_mutex`。进入可能等待这些锁或执行 guest 的边界前，
  按完整递归深度 `ReleaseForBlocking`；正常及异常出口均 `ReacquireAfterBlocking`。
  JNI 回调在桥入口重新获取 VM 锁；释放期间只使用已复制的参数与稳定 owner，不访问 VM 状态。

- AndroidGuestProcess 注册 `/dev/urandom` (1:9) 与 `/dev/random` (1:8)，通过 VFS reader
  注入 `hal::FillSecureRandom`；每次读取生成新的完整随机字节，失败由 VFS 映射 EIO。
  两者均使用宿主 OS CSPRNG，不模拟 Linux 熵池；O_NONBLOCK 不引入 EAGAIN/熵量等待。
  支持边界与生命周期见 [ADR-0129](../../../docs/adr/runtime.md#adr-0129)。
  `--diag` 下设备 open/read/close 只记录路径、flags、字节数及返回值，不记录随机内容。

- PreflightAndroidGuestLink 只用生产 Bionic namespace、API-sealed Virtual SO 元数据和 loader
  检查映射/重定位；不创建 boundary runtime、ANGLE 或 surface，缺导入明确失败。
- InitializeApi19GuestProcess 事务建立 TLS/thread-info/preinit、独立环境页、4 MiB 栈、
  SVC #1 返回 trap 与空 property area，只写受检 libc 导出槽；冲突/非法名称/写失败回滚。
  配置来自 guest；Java 环境查询读 Bionic 当前 environ，不读宿主或另存影子快照。
  默认 API19 环境显式设置 TZ=UTC；guest 后续 setenv 可修改本进程时区，时间读取仍经
  统一 Clock，不继承宿主 TZ/时区数据库。
- AndroidGuestProcess 是 native 资源 owner。rootless create 只接收 API19 system closure，
  用 libc 建立 namespace、process memory、syscall/clone、boundary、JNI/JavaVM/root attach；
  不接受 app ELF，返回时 ApplicationModuleCount 为 0，此后只随成功动态加载增加。
  AndroidGuestCallSession.Start 是 legacy adapter；AdoptProcess 仅包装既有 owner。
  `RethrowAsyncFailure` 经 session 暴露已保存的 clone/OpenSL worker 原异常，无需再次进入
  guest；观察不消费首错，仍由原 owner 保存，退出阶段的二次异常不得覆盖。
- `/proc/self/maps` 为当前 guest 地址空间的按打开快照，最多 8192 个合并区间/1 MiB；
  包含 PROT_NONE，输出真实 guest 读写执行权限。现有 backing 为私有匿名页（包括复制的
  ELF 段），offset/device/inode 为 0、pathname 为空；不推测原 ROM 文件身份。
  provider 只捕获 address space，由注册句柄在地址空间析构前同步撤销，构造失败同样回收。
  不读取宿主 maps，不实现完整 procfs 或 `/proc/self/stat`。
  NDK Bitmap hooks 在 DexVM bridge 完成构造后发布，退役等待活跃调用完成；JNI env/
  reference 解析与 pending throwable 归 bridge，boundary 仅处理 ABI。活跃像素锁是
  VM 强根，recycle 延后退役 guest view，bridge 停止线程后解除 hooks 并清理 lease。
  BitmapPixels 在逻辑 ARGB 和 guest 格式存储间转移唯一权威值，Java/GLUtils/Canvas
  共用它；成功写入与最后 unlock 推进像素版本，活跃原生锁期间不能仅按版本复用投影。
  图片 codec 对 encoded bytes/尺寸/像素有先验上限，密度缩放半像素 bilinear 在预乘
  颜色空间完成，返回逻辑 ARGB；不声称完整 Skia 像素一致性。BitmapFactory 普通协议
  在 Java，native 不复制资源选择/关闭/默认 Options 算法。
  低位格式/预乘 alpha 的量化及调用者无同步并发写不等于完整 Skia 验收。
- /proc/meminfo 是进程创建时的只读 GuestMemorySnapshot，来自受检 GuestProcFacts：默认
  总量 1 GiB、free=total/2、Cached 缺省 total/4，cached/buffers 可显式注入，Swap 为 0。
  free+cached+buffers 不超过 total；与已有 meminfo 内容冲突时拒绝启动。Java/JNI
  ActivityManager 共用同一快照；不读取宿主内存或动态刷新，不改变 DexVM 堆配额。
- `dl_unwind_find_exidx` 按 PC 查询 process-owned namespace 的实际 load range，返回所属模块
  经 load bias 重定位的 `PT_ARM_EXIDX` 地址与 8-byte 表项数；初始和动态模块共用 linker 锁。
- libdl handle 表归 process。dlopen 复用已装载 basename/SONAME 身份；首次装载保留请求路径，
  `/data/app-lib/` 使用所选 ABI APK，`/system/lib/` 使用 bundled 源，其他路径只走 guest VFS。
  普通库名查显式 APK/系统来源及 guest `/data/app-lib/`、`/vendor/lib/`、`/system/lib/`；不读宿主
  环境或实现动态 LD_LIBRARY_PATH。nullptr 为 RTLD_DEFAULT，
  libhgl.so 映射 sealed libGLESv2.so；dlsym 先查 handle scope，boundary 可回退 LookupAny，
  DEFAULT 先 sealed catalog 再 global namespace。dlclose 只减引用，不卸载；未知库/符号/
  handle/flags 进入逐线程 dlerror，不返回假地址或访问宿主文件。
- 普通 libdl handle 为 API19 ARM32 `soinfo` guest 视图地址，支持范围见
  [ADR-0076](../../../docs/adr/runtime.md#adr-0076)。表从现有 namespace 的未版本化导出和
  sealed provider 投影；真实 ELF 优先于部分截获目录，st_value/load_bias 合成同一已解析地址。
  视图只暴露所属库导出，dlsym 的 dependency/sealed fallback 保持原契约。
  重复打开共享活跃记录及引用，引用归零解除 backing；退役地址不复用，失效 handle 明确失败。
  只读记录/符号/字符串/SysV hash 位于 `[0x79000000,0x7a000000)` 有界 arena，宿主更新引用
  时临时切换记录页权限；Virtual SO 的 base/size/load_bias 为 0，符号指向真实 thunk/data。
  不提供完整私有 linker 状态或其他 API/ABI 布局，不接管模块、构造器或 JNI 初始化。
- soinfo 仅投影名称、base/size/flags、符号/字符串/SysV hash、引用数及 link_map 地址/名称、load_bias（≤65536 导出、≤4 MiB）；不提供 phdr/dynamic/链表遍历、linker 写入或 debugger 协议，因为它只是当前进程的只读元数据视图。
- NativeLibraryLoader 以 canonical path + ClassLoader token 维护唯一 Loading/Loaded/Failed
  registry：同 loader 幂等、同线程递归返回现有 handle、跨 loader 拒绝、Failed 重试稳定失败。
  app resolver 只用 selected-ABI inventory，basename 为身份，DT_SONAME 为受检别名；
  DT_NEEDED 可用二者，别名歧义、ET_DYN、依赖/重定位、constructor/JNI version 均受检。
- [ADR-0135](../../../docs/adr/runtime.md#adr-0135)：共享来源快照拥有 APK/系统库字节，process
  经显式 native callback 持有；Java registry 退役不悬空其来源。Java/native 共用 namespace
  与构造器状态，native dlopen 不登记 ClassLoader 或执行 JNI_OnLoad，后续 Java load 复用映射。
  process 装载门禁可同宿主线程重入、跨线程串行；root CPU admission 在门禁前，元数据锁不跨
  guest 执行。构造器递归前标记，依赖先于本库，DT_INIT 先于 INIT_ARRAY；失败保存原故障并
  禁止重跑，不承诺回滚 guest 副作用。native 构造调用保留 caller thread/TLS，执行前复制
  lifecycle 记录以抵抗递归扩容。仅可恢复链接错误进入 dlerror，执行控制故障沿原路径退出。
- NDK AssetManager 经 bridge hooks 校验 JNI env/thread/reference，与 NativeActivity 共享
  同一 Java 对象的 opaque 身份；不永久保活 Java 对象，GC 撤销 manager，现存 Asset
  持有独立来源。bridge 在停止线程后撤销 hooks、退役所有来源和注册。
  `ApkAssets/StoredApkAsset` 共用封存 APK 的受检 STORED offset/length，首次完整 CRC
  校验后缓存事实；open/read 不复制整个资源，压缩读取沿用 loader 区间接口。
  每次直接 FD 为完整只读 guest APK 的独立 VFS descriptor，caller 自行 close；
  不暴露宿主 FD，不随 Asset.close 关闭 FD。source 存活期间 APK 字节/元数据不可变，
  manager 弱持 context，打开的 Asset 保持 sealed context；source 不保活 Java manager。
- NativeActivityRuntime 借用当前 process 的 guest 分配、模块导出查询、JNI/JavaVM 与执行器，
  只拥有 ARMv7 ANativeActivity/callbacks、路径/state backing 和真实 Activity/AssetManager global
  refs；回调要求创建线程，native 执行释放 VM 锁，销毁退役注册并解除引用。
  进程收尾在 Java worker join 后、JNI 解绑前显式 Release，包括启动失败未调用 onDestroy
  的实例；Release 幂等并报告失败，析构兜底记录失败，不能掩盖原始启动异常。
  NativeActivity 的 LoadPath entry mode 不执行 JNI_OnLoad；随后 Java load 复用同一 registry
  和映射，并执行一次 OnLoad。不得启动 standalone NativeActivitySession 代替这个桥。
- APK 未命中时仅查注入 bundled 系统库；未映射 Bionic source 持有副本，按 DT_NEEDED
  可达性加入同一 namespace。map 事务完成后执行 constructor；registry mutex 不跨 guest
  call。只有显式 root 执行 JNI_OnLoad，不把 dependency 算作 Java load；module 持有至 Stop，
  按实际完成 constructor 的逆序 finalize，成功 load 记录 sequence/soname。
- System.load/loadLibrary 使用同一个 loader 和 application ClassLoader token。Java 边界
  为 NPE/UnsatisfiedLinkError，宿主保留 typed cause；legacy InitializeJniLibrary 只能在
  运行中、constructors 和类登记完成后调用一次，成功或无 OnLoad 才发布 ready。
- BaseDexClassLoader.findLibrary 经平台绑定调用 NativeLibraryLoader.FindLibraryPath，
  与 LoadLibrary 共用 selected-ABI APK 优先、bundled system 回退的逻辑名解析；返回
  `/data/app-lib/<soname>` 或 `/system/lib/<soname>`，可供 System.load/native dlopen 使用。
  查询不装载、不运行 constructor/JNI、不登记 registry；非法或缺失名称返回 null，未注入 loader 明确失败。

## DexVM / JNI 唯一身份与执行

- JNI AllocObject/NewObject* 唯一经分配回调在 VM 执行锁内选择调用线程 context，委托 AllocateJniObject，
  Java 失败以真实 pending throwable/null 返回。成功对象使用既有 local 发布/字段桥；
  发布失败移除 registry 映射，未发布实例无永久根，由普通 GC 回收。guest worker 与
  资源清理结束后撤销回调，不能跨 session 保存捕获的 VM 指针。
- DexVmGuestBridge 按 core+android intrinsic → curated BootDex → app DEX 装配；同签名
  intrinsic 是精确 overlay，其余解释执行。已迁移平台类由 VM 发布 JNI，禁止同名 HLE 遮蔽；
  无 VM 的 native/HLE 会话不保留旧替身，独立 headless 契约和应用兼容回调除外。
- String/primitive/object array 复用会话 store；jclass 双向规范化为同一 ClassObject。
  PublishLocal 用 EnsureRegistered 原子幂等发布真实类，GC 同步清扫 registry；不为所有
  APK 类预占 global ref，只有 static native 出向的 jclass 按需缓存，发布使用调用线程身份。
  原生创建的 String/primitive/object array 首次 JNI 类型查询时，仅在共享 store 确认存活后，
  于 VM 执行锁内导入并登记真实类；primitive kind 对应精确数组 descriptor。该按需登记
  与 GC 同锁，不增加引用根，未知 external identity 不递归导入，teardown 撤销解析器。
  native 资源清理 scope 以当前 JNI 线程 local ref 保活并暂清 pending，所有出口恢复原
  throwable；清理错误独立传给调用方，普通 JNI pending gate 保持严格。
- JNI 类解析遇 guest linkage failure 时在当前调用线程发布原 Java 错误，再返回空身份；
  真实内部错误继续传播，不能把它当作查询未命中。
- 类注册保留完整父类/direct interface 图；interface MethodID 在实际 receiver 上虚派。
  JNI 数组兼容/IsAssignableFrom 复用 linker 的类、接口、数组协变与 primitive 规则；
  任一侧不属于 VM 返回 nullopt 保留 JNI 校验，不能无条件放行或把不相等视作不兼容。
  兼容回调在数组锁外、VM 执行锁内运行，guest 线程停止后撤销。
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
  JNI 重入进展判别的有界限制见 [ADR-0023](../../../docs/adr/diagnostics.md#adr-0023)。
- process-owned NioRuntime 与 JNI 共用 direct backing；AddressSpace 有界 arena 使用强类型
  guest address，不保存宿主指针。Java GLES 编组后进入唯一 sealed managed GLES/ANGLE。

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
  nanosleep 在执行前校验剩余预算，返回/SVC/slice 边界检查超时；不会因真实睡眠续期。
  未处理 CPU stop 以 typed fault 标记 native 状态不安全，拒绝后续 guest 调用/清理并跳过
  DSO fini；宿主线程回收、JNI detach 和 monitor shutdown 仍执行，原错由调用方保留。
- Runtime 的 hook 与 exit/halt 普通协议归 BootDex；平台只绑定 nativeExit，发布 session
  退出标记并调用 VM.Exit，保留退出码、非 Java 展开，不从 worker 同步 Stop/join。
  宿主 Stop 仍为取消，不自动运行 hook；详见 [DVM-150](../../../docs/tasks/dexvm/DVM-150.md)。
- wait/sleep/timed join 共用注入 monotonic Clock，currentTimeMillis 使用会话时间事实。
  根 timed park 可经 AdvanceAndroidClock 补时；worker 只有 EGL driver-blocked 或 root
  joining 时可复用串行 deadline 补时，纯 Java timed hook 不依赖帧推进，不新增宿主时间源。
- 主 Looper/scheduler 由 lifecycle safe point 泵送；View.post 的 detached 与 scheduled 引用
  均为 GC 根。后台任务必须保留真实异常，不能以 null 结果伪造正常完成。

## VFS、Android 与媒体边界

- DexVmIoVfsAdapter 是 core IoFileSystem 到唯一会话 VFS 的桥，Java/native IO 看同一世界；
  StatVfs 共用 VFS 卷统计并保留 errno，Android 不另持宿主剩余空间或无路径统计。
  mkdir/mkdirs、list/delete/rename/read/write/flush/close 保留真实语义，异常出口关闭临时 FD。
  只透传 guest cwd/writable；Java File 布尔 API 才把 VFS 错误收敛为 false。
- APK bytes/inventory/Manifest 由上层 sealed 输入；resource/package 路径为 guest 路径，
  getPackageCodePath 挂载原 APK bytes，ContextWrapper 委托 base，不泄露 frontend 宿主路径。
  Resources/AXML、File 目录与资源枚举都读同一受检事实；AssetFileDescriptor 只提供已支持的
  逻辑长度/幂等 close，不伪造 POSIX fd/offset。
- View↔UiNode 一一绑定，identity/listener 由 integration 持有，几何/命中只读唯一 UiTree。
  XML inflation 用通用 registry，未知结构明确失败；LayoutParams/Bundle/Configuration、
  framework 值类等普通状态与算法归 BootDex，当前具体支持面见 [Android 子模块](dexvm_android/MODULE.md)。
- SurfaceHolder 按 view 保持身份与 active generation；callback 去重、null 拒绝，按事件
  快照向全部注册者分发，缺方法明确失败。attach/detach 驱动对应事件，不新建像素 owner。
  EGL policy setter 只保留受 GC 追踪的策略身份，未支持行为不越权调用；render mode 不创建
  GLThread。Canvas 的 ARGB backing 使用 holder buffer 尺寸；与 NDK/EGL producer 经纯像素
  handoff 冻结到共同 presentation，再缩放至 View frame。窗口 holder 保留原软件基帧。
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
- legacy media 的 no-op/返回值只表示 Java 契约；volume/pitch/loaded/voice 为受检会话状态。
  资源必须真实非空且受 JNI size 限制；audio.load_movie 仅登记最多 4096 UTF-16 单元的请求，
  不等于已播放。display.change_mode/process.exit 只发布可查询请求，由上层实施窗口/清理策略。
- 网络默认离线，只用注入 policy/transport；设备/身份/服务仅发布已配置事实，不伪造广播、
  传感器、电话或在线服务。Activity 当前/根身份来自 lifecycle，finish/handoff 不混为进程退出；
  SQL 持久化只用 guest VFS，SAX 未支持 parse 明确失败。Typeface/主题边界见 Android 子模块。

Debug 当前进程查询的默认 provider 在 bridge 装配后安装，析构撤销自有回调；
外部 provider 保留。VM 执行锁内读取托管堆账本和 guest 已映射页（含 PROT_NONE），
采用显式虚拟私有提交模型：托管 charge 计 dalvik，未分类 ARM RAM 计 other，KiB
向上取整。无宿主 RSS、GPU/媒体缓存或未映射预留；映射快照忙则明确缺事实。
模型边界见 [ADR-0118](../../../docs/adr/runtime.md#adr-0118)。

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
family Declare_* 同址绑定，shared 只放跨 family helper，support 承担派发；不恢复 Populate、
集中 handler 容器或字符串分发。迁移历史见 [DexVM 工作单](../../../docs/tasks/dexvm/README.md)。
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

SurfaceStream 是无 JNI/GL/回调的进程内有界交付队列：producer 带 stream identity/epoch，
async pending 替换不重发事件；acquire 将待上传帧独立持有，commit 锁存、abort 在无更新帧时归还，
上传期间新 enqueue 保留自己的通知。single-buffer 在消费归还前明确 busy。snapshot 持不可变 RGBA
与源 PTS/transform，所有 lease 的实际 backing capacity 共享进程预算；不维护第二播放时钟。

原生单次调用的默认看门狗额度与 session 共用 runtime/common 的 100 亿 tick 常量；
现有 observable-progress 续期规则继续生效，未新增按标题配置或仅按墙钟检测。

诊断与生产共用 BoundCoreIntrinsicCatalog 的 core/platform handler 绑定；metadata-only
DumpAndroidBoundarySurfaceJson 读取同一 API19 boundary 导出目录，不创建进程/窗口或执行 guest。

bridge 可接收固定 API19 骨架字节，在 BootDex 之后、应用 DEX 之前交给 linker 建索引；不在 integration 合成成员或引入新的 JNI 查找规则。

JNI ExceptionDescribe owner hook 在调用线程 context 内虚派 Throwable.printStackTrace()，
输出沿 Java System.err；Java 打印异常被忽略，pending 的清除、保活与恢复由 JNI 层拥有。

## Java payload provider（ADR-0152，阶段 2）

`GuestJavaPayloadProvider` 归 AndroidGuestProcess，显式接收 AddressSpace/ledger/limits 并注入
primitive/String store；NIO 尚未迁移。小块 8-byte 对齐按尺寸分级，每个 slab 只服务一个
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

GC 集成同时枚举 environment references、String lease 与数组身份 pin roots，在 mark/
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

阶段 4：同一 process-owned provider 同时为 primitive 和不可变 UTF-16 String 分配，
成员顺序保证 String/array store 均先于 provider 退役。Stop 增加 String 独立 live/peak/fail/pin
统计与 kind=8 的长度分桶（沿用 0、1..16、17..64、65..256、257..1024、1025..8192、>8192）；
committed/slab_free/应急池与尺寸级别属于共享 provider，不按 String/array 重复归属。
UTF-16 direct pin 的未配对/阈值诊断经 logger 与 warning ledger 发布；MUTF-8 复制协议不变。

实验 Paint 测量状态按 Java 对象身份拥有，GC sweep 退役；Typeface 强边仅由 Java 字段追踪。

实验 TextView 构造经 ROM View/Theme/TypedArray 解析，宿主独占文本输入；
文本/样式在 UiNode，独立 singleLine 策略在同一 View adapter 状态中；
Typeface/compound Drawable 身份由 View adapter 强边 trace/sweep，不镜像 Java mText/mLayout。
测量及 TextPaint/Editor 保持明确失败；不改变生产 Legacy 路径。

实验 Button 三个 ROM 薄构造保留真实 buttonStyle，复用 TextView 输入/测量和身份 owner；
默认不支持资源明确失败，应用可显式覆盖。Legacy 固定控件投影仅作用于 Legacy owner。


ADR-0162 / DVM-306：AssembleAndroidContext 在 catalog 注册/VM 建立前读取同一份 BootDex
归档，校验固定 java-traversal v1 模式及 DEX/成员表/主题投影 SHA-256 与主题 schema，
再选择不可变 context owner；未知模式、错配、缺投影或无标识实验制品明确失败，无自动
Legacy fallback。无标识生产制品仍 Legacy。frontend 三入口共用此装配，显式注入不可变
平台无 accessibility 服务事实，生产不设置测试 bool；测试覆盖入口保留。
本阶段不开放 test-root、不改变 StepFrame；模式选择不等于生产遍历/E2E 验收。

JavaTraversal 的 owner 在 bridge 装配主 guest context 状态表时绑定；View 构造只校验，
不能通过换宿主线程重新绑定。请求/调度表为按稳定窗口根保存的弱 Java 身份，
GC sweep 清理对应根与请求；不进入 session 强根，完整投影独立保留。


DVM-308：JavaTraversal 帧遍历由 lifecycle owner 在输入前、mutation 批后及 Surface 回调前
调度；重复屏障不推进逻辑 tick，尺寸通知由 ROM layout 唯一派发。纯绘制重新冻结投影，
Compose 只消费完整快照、不进入 guest。managed-window adapter 不创建 AttachInfo/IME/WMS；
已附着 View 的窗口/application token 为每窗口稳定的进程内身份，未附着返回 null；
窗口退役后旧 token 失效。窗口根通过 GC 强边保活 token，token 不反向保活窗口；
transact/跨进程 marshalling 以及 JavaTraversal 的 show/hide IME 请求记账并明确失败。
Activity 窗口记录关联 Java 内容根并独占生命周期可见事实，该事实随 owner 完整投影发布；
producer/holder 使用同一快照的可见性。UiTree 不附着第二棵 guest 层级，Legacy 时序保持。

JavaTraversal owner 事件由 AndroidInputEvent 保留共同快照，GC 根覆盖未终止捕获的 Java
窗口/父链；owner 帧屏障统一泵送 managed Surface，回调后重验代次。窗口 holder 与 View
holder 分型，不引入系统附着或 WMS；详细边界见 dexvm_android/MODULE.md。


JavaTraversal共同发布冻结逐窗口完整投影、视频像素与bitmap版本；session视频/UI/modal
消费者显式共享同一lease，不在consumer读取当前producer/绑定表。窗口退役只清构建
身份，旧不可变presentation可继续保留；缺匹配新窗口投影时拒绝替换。

有界主题投影携带封存的 projected_references 与 ARSC 类型编号；provider 严查名称、
package/type 与数量，任意同 ROM 类型均可作为 terminal 引用/resourceId。生成器锁定
framework-res.apk 并交叉核对共享 R 常量，只投影名称到真实 ID，不按属性名或加载能力
筛选、不预先解析目标内容。消费者实际调用才加载，未支持目标按 ADR-0147 记账失败；
既有 ColorStateList/edit_text 加载范围保持，详见 dexvm_android MODULE。

DVM-329：JavaTraversal 的干净屏障不执行 guest、不扫描投影；只在真实 Java 布局/绘制请求、
首次附着或窗口输入变化后进入遍历。宿主消费快照前校验 NodeId 属于所持完整投影，
未发布绑定等待下一 owner 发布；不弱化 guest 不可达请求的明确失败。
