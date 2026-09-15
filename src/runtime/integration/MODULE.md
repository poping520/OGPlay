# 子模块：runtime/integration

## 职责与依赖

组合 Bionic、guest JNI ABI、Android boundary、DexVM/框架 HLE，拥有进程会话、动态加载、
headless/NativeActivity runner 与出口报告。可依赖 jni_guest/boundary/framework/jni/bionic/
syscall/execution/vfs 及下层；下层不得反向依赖 integration。不直接持有窗口、消费输入或
实现游戏专属逻辑；executor、Clock、VFS、Profile 必须显式注入或由确定性 fixture 建立。

Android API 的详细行为以 [dexvm_android](dexvm_android/MODULE.md)为准；
[DexVM](../dexvm/MODULE.md)拥有解释/GC/线程，[session](../../session/MODULE.md)编排生命周期，
[capabilities](../../../capabilities.toml)登记支持范围。普通 Java 算法不得在 integration 重建。

## 进程与库加载

- PreflightAndroidGuestLink 只用生产 Bionic namespace、API-sealed Virtual SO 元数据和 loader
  检查映射/重定位；不创建 boundary runtime、ANGLE 或 surface，缺导入明确失败。
- InitializeApi19GuestProcess 事务建立 TLS/thread-info/preinit、独立环境页、4 MiB 栈、
  SVC #1 返回 trap 与空 property area，只写受检 libc 导出槽；冲突/非法名称/写失败回滚。
  配置来自 guest；Java 环境查询读 Bionic 当前 environ，不读宿主或另存影子快照。
- AndroidGuestProcess 是 native 资源 owner。rootless create 只接收 API19 system closure，
  用 libc 建立 namespace、process memory、syscall/clone、boundary、JNI/JavaVM/root attach；
  不接受 app ELF，返回时 ApplicationModuleCount 为 0，此后只随成功动态加载增加。
  AndroidGuestCallSession.Start 是 legacy adapter；AdoptProcess 仅包装既有 owner。
- /proc/meminfo 是启动时写入 VFS 的只读 GuestProcFacts 快照：受检 total/free，Buffers/
  Swap 为 0，Cached 为 total/4；不读取宿主内存或动态刷新。
- `dl_unwind_find_exidx` 按 PC 查询 process-owned namespace 的实际 load range，返回所属模块
  经 load bias 重定位的 `PT_ARM_EXIDX` 地址与 8-byte 表项数；初始和动态模块共用 linker 锁。
- libdl handle 表归 process。dlopen 只解析 guest basename，nullptr 为 RTLD_DEFAULT，
  libhgl.so 映射 sealed libGLESv2.so；dlsym 先查 handle scope，boundary 可回退 LookupAny，
  DEFAULT 先 sealed catalog 再 global namespace。dlclose 只减引用，不卸载；未知库/符号/
  handle/flags 进入逐线程 dlerror，不返回假地址或访问宿主文件。
- NativeLibraryLoader 以 canonical path + ClassLoader token 维护唯一 Loading/Loaded/Failed
  registry：同 loader 幂等、同线程递归返回现有 handle、跨 loader 拒绝、Failed 重试稳定失败。
  app resolver 只用 selected-ABI inventory，basename 为身份，DT_SONAME 为受检别名；
  DT_NEEDED 可用二者，别名歧义、ET_DYN、依赖/重定位、constructor/JNI version 均受检。
- APK 未命中时仅查注入 bundled 系统库；未映射 Bionic source 持有副本，按 DT_NEEDED
  可达性加入同一 namespace。map 事务完成后执行 constructor；registry mutex 不跨 guest
  call。只有显式 root 执行 JNI_OnLoad，不把 dependency 算作 Java load；module 持有至 Stop，
  按实际完成 constructor 的逆序 finalize，成功 load 记录 sequence/soname。
- System.load/loadLibrary 使用同一个 loader 和 application ClassLoader token。Java 边界
  为 NPE/UnsatisfiedLinkError，宿主保留 typed cause；legacy InitializeJniLibrary 只能在
  运行中、constructors 和类登记完成后调用一次，成功或无 OnLoad 才发布 ready。

## DexVM / JNI 唯一身份与执行

- DexVmGuestBridge 按 core+android intrinsic → curated BootDex → app DEX 装配；同签名
  intrinsic 是精确 overlay，其余解释执行。已迁移平台类由 VM 发布 JNI，禁止同名 HLE 遮蔽；
  无 VM 的 native/HLE 会话不保留旧替身，独立 headless 契约和应用兼容回调除外。
- String/primitive/object array 复用会话 store；jclass 双向规范化为同一 ClassObject。
  PublishLocal 用 EnsureRegistered 原子幂等发布真实类，GC 同步清扫 registry；不为所有
  APK 类预占 global ref，只有 static native 出向的 jclass 按需缓存。
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
- 每个 guest Java thread 有独立 A32 CPU/栈、Bionic TLS/thread-info/TID、JNI attach/local frame。
  native 执行释放 VM 锁，JNI 回调重获；executor 按 thread 和重入深度复用 CPU/JIT，同层复用
  缓存、不同深度隔离现场，nested 使用 suspended SP/TLS，线程退出回收全部 executor。
  JNI monitor 使用 VM token/对象身份，与 Java synchronized 共享状态；TID 受 Bionic 16-bit 限制。
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
  中断当前及后续阻塞；既有 slice/boundary 安全点观察取消，非 renewable finalizer 不变。
  生命周期在 Java thread join 前再次中断，覆盖回调中新建的 futex wait。
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
  GLThread。Canvas 的 ARGB backing 经唯一 boundary frame 队列发布。
- AudioTrack/legacy JNI/OpenSL ES 共享一个 PCM mixer，guest→player 映射不成为 GC 根，
  release/GC 回收 player；回压阻塞释放 VM 锁。OpenSL callback 经专用 A32 thread/CPU/TLS/栈，
  不隐式 attach JNI，允许 SVC 重入，失败在后续 process call 报告；混音只向 HAL 提交一次。
- VideoView 用 VFS HostPathFor 和注入 VideoPlayerFactory，统一 uptime 驱动位置；取帧
  letterbox 发布，完成回调一次。工厂/路径/打开失败记录 warn，并在 start 回调 completion，
  不宣称播放成功。PCM 最近邻重采样饱和混入，停/暂停/结束静默；自由运行可按真实时间节流，
  手动步进保持确定性。SoundPool 解码成功才原子提交 loaded，失败保留 pending/错误事实。
- legacy media 的 no-op/返回值只表示 Java 契约；volume/pitch/loaded/voice 为受检会话状态。
  资源必须真实非空且受 JNI size 限制；audio.load_movie 仅登记最多 4096 UTF-16 单元的请求，
  不等于已播放。display.change_mode/process.exit 只发布可查询请求，由上层实施窗口/清理策略。
- 网络默认离线，只用注入 policy/transport；设备/身份/服务仅发布已配置事实，不伪造广播、
  传感器、电话或在线服务。Activity 当前/根身份来自 lifecycle，finish/handoff 不混为进程退出；
  SQL 持久化只用 guest VFS，SAX 未支持 parse 明确失败。Typeface/主题边界见 Android 子模块。

## Runner、诊断与验证

NativeActivitySession 只支持 API19 ARMv7，执行 Bionic 初始化、ANativeActivity_onCreate、
glue child 和销毁；child 异常必须唤醒 waiter，在 root/帧/输入边界报告原始原因。
supersample_factor 受检为 1..4，guest 保持逻辑尺寸，由 ANGLE 放大并确定性 resolve。
DiagnosticState 只收稳定 ID/整数/执行进展；A32 slice observer 由上层注入。GPU trace 只写
固定 raw ring（最近 2048 调用）与独立 mutex，文本延迟格式化；TryTrace 不竞争主 boundary
锁，不伪造扩展/FBO/限制。debug 层不得反向依赖 integration。

生产装配入口为 AndroidIntrinsicCatalog / AndroidCoreIntrinsicServices；catalog 只聚合，
family Declare_* 同址绑定，shared 只放跨 family helper，support 承担派发；不恢复 Populate、
集中 handler 容器或字符串分发。迁移历史见 [DexVM 工作单](../../../docs/tasks/dexvm/README.md)。
定向测试位于 [tests/runtime](../../../tests/runtime/) 的 native loader、guest call、JNI、preflight、
headless/NativeActivity，以及 [tests/dexvm](../../../tests/dexvm/) 的 Android/线程/布局/scheduler。
只构建受影响目标；文档修改只做 UTF-8/链接/diff 检查，人工探索不能代替 Scenario gate。
