# 子模块：runtime/integration

## 职责

装配 Bionic、JNI guest ABI、Android boundary 与框架 HLE,生成里程碑出口报告:API 19
guest process、Android guest call session 及其会话拥有的 Java handler 状态、link
preflight、headless/NativeActivity runner 与累计 contract。低层能力分别位于
`runtime/jni_guest`(guest JNI ABI)与 `runtime/boundary`(Android native 边界),
本模块只组合它们并持有会话生命周期。

## 依赖

位于 runtime 顶层,可依赖 jni_guest、boundary、framework、jni、bionic、syscall、
execution、vfs 及其下层模块。任何下层模块均不得反向依赖 integration。

DVM-79 的 `DexVmIoVfsAdapter` 是 DexVM core `IoFileSystem` 与具体
`VirtualFileSystem` 之间的唯一桥：Java File 族与 native `fopen` 因此仍看到同一个
沙盒世界，而 dexvm 不反向依赖 integration/VFS。adapter 保留 mkdir/list/delete、
整文件 read/write 与 flush/close 的既有真实语义，并在异常出口关闭临时 descriptor。

## 不变量

- runner 只有在所有资源、引用、线程和生命周期闭环后才能报告成功。
- Android guest call session 只在通用 A32 slice 边界调用显式 observer;observer 由上层
  注入,不得让 integration 直接依赖窗口后端或消费输入。
- `PreflightAndroidGuestLink` 只复用生产 Bionic namespace、API-sealed `BoundaryCatalog`
  生成的 Virtual SO 符号元数据与 loader 完成映射和重定位，不构造
  `AndroidBoundaryHle`、ANGLE backend 或 surface runtime；报告 guest/boundary 模块及
  relocation 数，任一缺失导入明确失败。
- `InitializeApi19GuestProcess` 事务映射统一 root TLS/thread-info/preinit、4 MiB 栈、
  `SVC #1` 返回 trap 与空 property area,并只向受检 libc 导出槽发布地址;固定布局冲突、
  非法线程/进程名或写入失败必须回滚新增映射和导出槽。
- `AndroidGuestProcess` 是 Android native 进程资源的底层所有者：rootless create 只接收
  API 19 system module closure，以 `libc.so` 建立 process-lifetime namespace，在没有
  APK application ELF 时也必须完成 Bionic process memory、syscall/clone、boundary、
  guest JNI ABI/JavaVM 与 root thread attach。请求面不得接受 application module；
  create 返回时 `ApplicationModuleCount()` 必须为 0，后续只随成功发布的动态 guest
  application module 单调增加；Stop 必须 detach root JNI thread 且幂等。
  `AndroidGuestCallSession` 的旧 `Start` 是 root-module legacy adapter；
  `AdoptProcess` 仅为 DexVM/JNI 复用既有窄接口包装 rootless owner，不追加或初始化
  application ELF。
- API 19 `/proc/meminfo` 是进程启动时写入 VFS 的只读静态快照；`GuestProcFacts` 显式
  注入虚拟设备 total/free，Buffers 与三个 Swap 字段固定为 0，Cached 固定由 total/4
  派生。默认 facts 保持既有字节，非法 total/free 明确失败；禁止观察宿主内存或动态刷新。
- `AndroidGuestProcess` 同时拥有 guest `libdl` handle 表；`dlopen` 对已加载 guest ELF 建立
  root scope，对 sealed Virtual SO 直接建立边界 handle，`dlopen(nullptr)` 返回进程级
  `RTLD_DEFAULT` 伪句柄，主机 GL 包装名 `libhgl.so` 归一化为 `libGLESv2.so` 边界 handle；
  路径只取 basename 且不会访问宿主文件系统。`dlsym` 先查该 handle 的 scope，boundary
  句柄未命中时回退 `LookupAny` 跨 sealed 模块解析（KitKat 设备上此类引擎经自身 wrapper
  重导出取得全 GL 面，故有意宽于 KitKat 单库语义）；`RTLD_DEFAULT` 先搜任意 sealed HLE
  模块（`LookupAny`，catalog 顺序首个命中）再回退当前全局 namespace；`dlclose` 对伪句柄
  no-op、其余释放引用但不卸载 process-lifetime module。未知库、符号、handle 与 flags 必须
  进入逐 guest thread 的 `dlerror`，不得返回伪造地址。
- `NativeLibraryLoader` 是 process service：logical name 与 synthetic guest path 共享按
  canonical path 唯一的 Loading/Loaded/Failed registry，保留 ClassLoader token；同 loader
  重复 load 幂等，同线程递归 loading 成功返回现有 handle，跨 loader 明确失败，Failed
  重试稳定失败。selected APK ABI inventory 是唯一 app resolver；ET_DYN/DT_SONAME、依赖、
  relocation、constructor 与 JNI version 均受检。rootless create 未映射的 Bionic guest
  source 由 loader 自有副本保留，只在应用 `DT_NEEDED` 可达时动态加入同一 namespace；
  boundary library 仍走 HLE，缺失依赖明确失败。namespace/map 事务完成后才逐 module
  调用 guest constructor，registry mutex 绝不跨 guest call；仅显式请求 root 执行
  `JNI_OnLoad`，dependency 不隐式执行。已映射 module 由 process 持有至 Stop 并按实际完成
  constructor 的逆序 finalization；每次成功 explicit load 结构化记录 sequence/soname，
  不把 `DT_NEEDED` 依赖误记为 Java load；不支持 dlclose/unmap。
- DexVM `System.load*` 通过 `DexVmAndroidContext` 注入同一个 process loader 与稳定
  application ClassLoader token。调用期间 `NativeLibraryLoader` registry mutex 不跨
  constructor/JNI_OnLoad；JNI callback 直接回到 `DexVmGuestBridge::InvokeInterpreted`，
  `VmExecutionLock` 以 owner/depth 支持同宿主线程递归，因此 A JNI_OnLoad → Java → load B
  不需释放 VM 状态；native guest-call executor 在重入时创建独立 CPU，并以外层暂停现场的
  当前 SP 作为 nested 栈顶，禁止覆盖外层寄存器/栈帧。Java 边界只观察
  NPE/UnsatisfiedLinkError，底层 typed loader reason 仍保留给 host 测试与诊断。
- legacy process 组合真实 Bionic namespace、API 19 process、syscall/clone、
  guest JNI ABI/core bindings 与 Android HLE,执行 guest init/fini 并只接受通用 A32
  frame;可选直接资源 implementation set 只安装通用 framework HLE 并拥有统一 JNI array
  store。通用 SoundPool handler 驱动同一会话拥有的 `JavaSoundPoolState`:分类 stop 批次
  按类别清理并保留指定 resource,loaded 查询按 legacy Java 契约返回 `0`/`-1`,卸载只删
  精确键;`load_*` 记录 pending request,`play_*` 仅对真实 loaded resource 创建 voice,
  pause/resume/stop、volume、pitch、reset 与 big looping 均驱动同一 voice 状态与离线 PCM
  mixer,目标不存在或参数无效时不伪造迁移。注入编码资源 loader 后,load/lazy-play 只有
  在资源读取与 Ogg 解码成功后才原子提交 loaded;失败继续保留 pending 与 mixer 错误事实。
  会话可向宿主拉取 stereo PCM16,但本层不打开设备;Profile phase、窗口和标题事实不得进入
  该会话。失败清理可显式调用 `InterruptBlockingWaits`,使当前及之后的 guest futex wait 以
  `-EINTR` 返回,避免 finalizer 把宿主生命周期线程永久阻塞。
  DVM-31 另提供只面向 DexVM EGL façade 的窄转发：查询 managed surface 是否已开、
  绑定/释放调用线程 GL currency、present 以及读取当前 ANGLE `glGetString`；不暴露
  原生 EGL handle，也不把 surface 所有权上移给 guest。
- session stop 必须先快照全部 child、请求仍运行者退出并中断 futex,再逐个 join;单个
  child 的异常只能作为首错延迟上报,禁止跳过其余 join。所有 child 完成后才可执行 guest
  fini 或让 lifecycle/dispatcher/address-space 进入逆序析构。
- session teardown 顺序固定为:请求 child 退出 → `InterruptBlockingWaits` 只做临时唤醒 →
  join 全部 child → 执行 guest fini/destructor → root JNI detach → 永久 monitor
  shutdown。`InterruptBlockingWaits` 无论由 Stop 还是外部调用,都不得永久停用 monitor;
  guest finalizer 阶段的 `MonitorEnter` 必须仍能成功,permanent shutdown 只允许发生在
  其后。
- legacy `AndroidGuestCallSession::InitializeJniLibrary` 只可在运行中的会话调用一次;调用方必须
  在 ELF constructors 完成并注册所需 Java class 后显式调用,成功或 root 无 OnLoad 时才
  发布 library-ready。失败、重复或停止后调用不得伪造完成。
- `DexVmGuestBridge`(ADR-0017)在运行中的会话之上装配 dexvm:链接
  core+android intrinsic 目录与单一 classes.dex,对象模型复用会话的
  string/primitive-array/object-array store(native 与解释器同一对象)，并在
  DexVM/JNI 边界双向适配 class identity；任一方向穿越边界的 `jclass` 都规范化为
  model 中同一 `ClassObject`，因而可直接作为 `java.lang.Class` instance JNI 方法的
  receiver。普通对象发布 local reference 前登记精确 class，GC 清扫同步移除统一
  registry 映射。出向 native 调用按
  descriptor 编组 A32 soft-float 帧(r0=JNIEnv、r1=receiver/jclass、64 位偶对齐、
  栈 8 字节对齐),解析顺序 RegisterNatives → `Java_` 导出名 → 记账明确失败；
  RegisterNatives 查找与目标执行必须分离，只有映射不存在时才允许继续查找导出，
  已解析目标的 JNI/CPU/fault 异常必须保留原始原因向上报告，禁止伪装成未注册；
  `RegisterNatives` 目标执行失败还必须把 class、method、descriptor、guest process thread
  与嵌套 cause 分行输出，使无符号 guest fault 可反查到 Java 调用边界；
  入向把全部解释类/方法及 session 尚未拥有的 code-defined intrinsic 平台类注册进
  会话 `JniClassRegistry`(impl id `dexvm.m<id>`),
  所有非数组类共用递归注册路径并保留 intrinsic/application 边界上的完整父类链，
  JNI object array 与 `IsAssignableFrom` 不得看到第二套扁平类型事实；
  FindClass/GetStaticMethodID/CallStatic* 经不变的 233 槽 ABI 命中真实 DEX 事实
  并落入解释执行(第三路由)；class identity 注册不为每个 APK 类预占 JNI global
  reference，只有真实 static native 出向调用需要 jclass 时才按 owner 懒创建并缓存，
  避免大 DEX 在执行入口前耗尽全局引用表；解释器未捕获异常按 JNI 语义置 pending。
  DVM-89 同一路径把 DEX 自有字段发布为 `dexvm.f<id>`，稳定映射 `JniFieldId` 到
  `VmFieldId`；`GetFieldID/GetStaticFieldID` 先执行 DexVM 类初始化，instance/static
  getter/setter 直接访问 `JavaObjectModel` 槽与 linker static storage，object/array 值
  按调用线程在 `VmObjectRef` 与 JNI local reference 间转换。平台 HLE 字段未被 hook
  接管时继续落入 `JniFieldStore`，禁止为 DEX 字段维护影子副本。
  每个 DexVM child 拥有独立 A32 CPU/stack、Bionic TLS/thread-info、process thread id、
  JNI attach 与逐调用 local frame；同线程重入继承 suspended SP/TLS，J/D 返回读取
  r0:r1。JNI monitor hooks 映射到 DexVM execution token 与对象身份，jclass 规范化为
  同一 Class object，使 native monitor 与 static synchronized 竞争同一状态机。
  DVM-82 进一步让 bridge 与 JNI binder 引用 process-owned 同一 `NioRuntime`；direct
  backing 仅由 `AddressSpace` 中有界 arena 分配并按强类型 guest address 校验、搬运、回收，
  Java/JNI view 不保存宿主裸指针。
  DVM-83 的 Java GLES façade 复用该 runtime 做调用期 Buffer/array/String 编组，并经
  `AndroidGuestProcess::InvokeManagedGles` 进入既有 sealed GLES binding；integration 不保存
  第二套 GL 状态，`GLUtils` 只消费既有 Bitmap backing。
  API 19 `Bitmap.Config` 作为真正的 `Enum` 发布 ALPHA_8/RGB_565/ARGB_4444/ARGB_8888，
  每个常量保留 name/ordinal/nativeInt，`sConfigs` 与 values/valueOf 使用同一组对象身份；
  不从宿主图形格式枚举猜测 Android native index。
  API 19 Context 的 package resource path 是 process 注入的 guest APK 文件路径；
  AndroidAppProcess 只读挂载原始 APK bytes，ContextWrapper 保持 AOSP base 委托，绝不暴露
  frontend 宿主路径。
  DexVM 出向 native 在 guest 执行期间释放解释器单写者锁，JNI 回调入口重新获取；native
  process TID 使用 Bionic mutex owner 可表示的 16-bit slot 范围。长驻 JNI native 帧只在
  supervisor 分发报告可观测进展时续期 watchdog：真实 futex park、正字节数据 I/O、present、
  audio enqueue 与 JNI 重入为 advanced；查询、EOF、wake/yield、内存管理及保守默认为 idle。
  连续执行和纯 syscall 空转均受 tick 预算约束；JNI 重入无法区分琐碎与实质工作，接受的
  bounded 限制见 ADR-0023。
  软件 SurfaceHolder 的 Canvas 持有 surface-sized ARGB backing，drawBitmap/drawColor 修改
  同一 buffer，unlockCanvasAndPost 转成 RGBA 后进入唯一 boundary frame 队列。
  DVM-84 的 `AudioTrack` side-table 只保存 guest owner 到 mixer player 的窄映射；DEX、
  legacy Java/JNI 与 OpenSL ES 均通过 session 注入的同一 `OpenSlesPcmMixer`，GC sweep 与
  release 都销毁 player，side-table 不反向成为 GC root。
  DVM-88 将显式 `NetworkPolicy`/`NetworkTransport` 从 session context 注入 core
  `NetworkRuntime`，默认离线且不由 integration 偷读宿主网络事实；ContentValues/Cursor/
  SQLite 的 bounded state 留在 Android integration，并只经同一 guest VFS 持久化。
- `DexVmAndroidContext` + `AndroidIntrinsicCatalog(context)`：
  android.* intrinsic 按 pilot 测量面挂接真实会话状态——Resources 由严格
  resources.arsc 事实驱动、SoundPool/MediaPlayer 直连存量 mixer(resid 即键)、
  IO 走 APK 条目与会话 VFS；API19 `Environment.getDataDirectory()` 返回稳定的
  guest `File("/data")`；`Context.getFilesDir()` 由 Activity 正常继承，返回稳定
  `/data/data/<package>/files` File 并经 VFS 真实建立目录；两者都不读取或泄漏
  sandbox 宿主路径。`AssetManager.openFd()` 对虚拟 APK 只发布目标实际消费的
  logical `AssetFileDescriptor.getLength()`，长度取受检 central-directory
  uncompressed size；`AssetManager.list()` 从同一 sealed entry directory 返回
  case-sensitive、排序去重的 direct child；logical descriptor `close()` 对空资源集
  幂等且不改写 length，不伪造 POSIX fd/offset；身份为确定性配置、
  SMS/网络记账明确失败;
  统一时间由生命周期驱动发布；`Object.wait`、`Thread.sleep` 与 timed join
  共用同一 monotonic source，`System.currentTimeMillis` 仍读取 session 时间事实。
  DexVmBridge 发布该 source 的同时在 `VmMonitorTable` 发布
  `AdvanceAndroidClock` 快进钩子：根（lifecycle）上下文在生命周期调用内的
  timed park 由钩子按停泊时长推进确定性 uptime，而不是停泊在只有它自己会
  推进的 Clock 上（DH onCreate 授权轮询黑屏的根因）。
  `Activity.isTaskRoot()` 按生命周期发布的 `task_root_activity` 句柄判定：
  Manifest launcher 打开了进程唯一 task，经 `startActivity` 切换到达的 Activity
  不是根；已退出的 handoff shell 对自身句柄仍回答 true，与平台按 token 回答一致。
  `AudioManager.isMusicActive()` 查询受控宿主音频事实：OGPlay 会话没有独立外部
  media session 时返回 false，不把游戏自身 mixer voice 误报为外部音乐。
  `TelephonyManager.listen` 在确定性离线电话服务中保存非零监听掩码，`LISTEN_NONE`
  取消登记；没有宿主 radio，因此 `getSimState`/`getPhoneType` 分别报告
  `SIM_STATE_ABSENT`/`PHONE_TYPE_NONE`，也不伪造电话状态回调。
  `SurfaceView.getHolder()` 为每个 view 返回稳定的会话内 `SurfaceHolder`；
  `addCallback` 按平台语义**追加**真实 callback 身份（同一 holder 上注册多个
  是常态：自带 `GLSurfaceView` 的 title 会同时注册 view 与 activity），重复
  注册去重、null 拒绝。`DispatchSurfaceHolderCallbacks` 由 `dex_activity`
  生命周期在 surface 就绪/尺寸变化/销毁时投递给全部注册者——自带 SurfaceView
  的 title 在收到这些之前不会碰 EGL；注册了却没有对应方法的 callback 明确
  报错，不静默跳过。surface type 由 managed EGL 固定拥有，legacy
  `setType`/`setFormat` 只是无可观察效果的设备提示。
  DVM-90 将同一 managed surface 拆成 per-holder active generation：初次 lifecycle 只激活
  live UiTree 中的 holder；动态 View 子树 attach 触发 created/changed，detach 触发 destroyed，
  callback 以事件开始时的稳定快照分派。该状态不创建第二套 Surface/像素所有权。
  `GLSurfaceView` 发布 API19 `EGLContextFactory`/`EGLConfigChooser` interface shape；两个
  setter 保存原始 guest policy identity 并纳入 GC/session teardown，但在真实 reached
  behavior 要求前不越权调用 callback 或替换 managed EGL/ANGLE context。
  `IntentFilter` 按 identity 保存 case-sensitive、有序去重 scheme 与 API19
  host/wildcard/parsed-port authority 元数据；dynamic receiver 仍不伪造 sticky
  broadcast、Uri match 或未建立的广播派发。
  `Resources.getConfiguration()` 从注入 surface/density 按 API19 固定阈值发布
  `screenLayout` size/long/compat bits；layout direction 未建立时保持 undefined。
  `Thread.setPriority` 校验 Java 1..10 范围并保存 guest 优先级事实，不伪造宿主调度优先级。
  `Thread.start()` 交给 DexVM 线程运行时（真实宿主线程，见
  `src/runtime/dexvm/MODULE.md`）；`java.util.Timer` 仍是帧边界协作队列。
  每个 `View` 的 `ViewTreeObserver` 身份稳定并保存/移除 global-layout listener；当前
  managed layout 尚不发布虚构的 global-layout 事件。
- `Bundle` 在会话内按 key 保存 String/int/long/byte[] 的真实类型和对象身份，typed getter
  对缺失键返回 Android 默认值，`containsKey`/`clear` 观察同一状态；解释 DEX 与 native
  JNI 经同一 intrinsic handler 读写。
- `URLEncoder.encode(String,String)` 实现 Java form URL encoding 的 UTF-8
  字节级契约（空格为 `+`、其余保留集外字节为大写 `%HH`），未知 charset 抛出
  `UnsupportedEncodingException`；这不扩大实际网络连接非目标范围。
- SAX factory/parser/XMLReader 的本地构造与 content-handler 身份真实保留；实际
  `parse(InputSource)` 尚无有界 XML callback pipeline，调用时抛
  `UnsupportedOperationException`，不得把网络/配置解析伪造成成功。
- android.* intrinsic 的 widget 层把每个 live guest View 一一绑定到 `runtime/ui`
  `UiNodeId`；`setContentView(I)` 经 arsc resid → typed AXML 实例化 widget intrinsic，
  android id/visibility/hierarchy 只写 UiTree，`findViewById/getId/setId` 经双向 binding
  返回同一 guest identity。tag→descriptor/class 来自固定通用 registry，`<merge>` children
  直接 attach synthetic content root，未知 structural tag/attribute 记账并明确失败。
  `setContentView(View)` 保留 Java 已构造 subtree；`ViewGroup` 动态 add/remove/update、
  LayoutParams、padding/orientation/gravity 与 geometry getter 全部读写同一 UiTree。
  listener ref 仍只由 integration 以 UiNodeId 保存；
  `runOnUiThread` 在协作单线程模型下同步执行 runnable。
- VideoView intrinsic（`dexvm_android/android_media.cpp`，ADR-0021）：
  `setVideoPath` 经 VFS
  `HostPathFor` 解析宿主文件并用注入的 `VideoPlayerFactory` 打开解码;`start`/`pause`/
  `seekTo`/`stopPlayback`/`getDuration`/`getCurrentPosition` 驱动真实播放状态,位置由
  统一 uptime 推进。`PumpVideoViews` 由 guest 循环每步调用:取帧经
  `ComposeRgbaOnCanvas` letterbox 到 surface 尺寸后交给发布回调,到达时长时在 guest
  线程回调 `OnCompletionListener` 恰好一次。工厂缺失、路径不可解析或打开失败时诚实
  回退:结构化 warn + `start()` 立即回调 completion,不伪造播放事实。
  `MixVideoPcmIntoStereo` 把所有播放中 VideoView 的音频经确定性最近邻重采样饱和
  混入宿主立体声输出,暂停/停止/播完即静默;`AnyVideoPlaying` 供前端在自由运行时
  把帧循环按真实时间节流,使每帧 16 ms 的确定性 uptime 与墙钟一致(手动步进不节流,
  保持可复现)。
- widget 点击分发（`dexvm_android/android_view.cpp`）：`setOnClickListener` 的 guest
  ref 由 integration 保存，`setVisibility/getVisibility` 读写 UiTree；`android:src`
  drawable 测量 wrap_content 图像控件。bounds 只来自 runtime/ui resolved screen frame；
  detached/不可布局 view 不消费触摸，事件降级给 `Activity.onTouchEvent`。
  `FindClickableViewAt` 按 reverse document order 命中可见且带 listener 的 view，
  `InvokeViewOnClick` 在 guest 线程回调 onClick。`View.onTouchEvent` 是默认 false 的
  overridable 基类入口，供 session 对无 listener target 的 content view 做真实虚派发。
- `audio.load_movie` 必须把非空 Java `String` 解析为最多 4096 个 UTF-16 code unit 的
  线程安全、递增序号电影请求;会话只发布最新请求与累计次数,不宣称已启动宿主播放器。
  null、未知字符串对象或超限名称必须在状态变化前明确失败。
- legacy Java media 批次按声明式 method id 重现 APK Java 层的固定 true/false/zero/-1
  与 no-op 语义,并按方法线程安全累计调用;master/per-music volume 是受检可查询状态。
  编号 sound resource 必须通过注入 loader 读取真实非空字节并受 JNI size 上限约束,
  不存在、负编号、空内容或无 loader 明确失败;不得把 Java no-op 解释成宿主播放成功。
- legacy AudioTrack framework 批次实现 PCM16 mono/stereo 的受检 minimum-buffer、stream
  constructor、play/pause/stop/release 与 byte-array region write;每个 track 以 host
  object identity 隔离并发布 playing/paused/released/bytes-written 状态。非法 format、
  mode、range、重复构造或 release 后调用明确失败；DVM-84 已把 write 与控制接入进程唯一
  PCM mixer，不再只累计字节。
- Virtual OpenSL ES PCM mixer 在 `RenderStereoAudio` 中位于 SoundPool zero-fill/mix 之后做
  additive mix，仍由上层向唯一 HAL output 提交一次。Object/Play/BufferQueue callback 经
  窄化事件队列交给专用 A32 guest thread/CPU/TLS/stack；该线程不隐式 attach JNI，callback
  内普通 SVC #2 可重入 Enqueue，失败在后续 process call 恢复而不跨 CPU callback。
- `display.change_mode` 按 legacy Java 契约把 mode `1` 记录为允许休眠、其他值为保持
  唤醒,宿主防休眠未接入时不得宣称已改变窗口策略;`process.exit` 只发布线程安全可查询
  的退出请求,不得直接终止宿主进程或以 no-op 吞掉,前端观察后仍须执行 Profile
  lifecycle 与 guest session 清理;`locale.detect_phone_language` 只从 framework 受检
  确定性 Locale 配置计算 legacy 语言索引,不读取宿主区域设置,不把游戏身份写入 handler。
- 通用 platform Java handler 只发布请求显式注入的安装 id/版本和确定性离线运营商、
  Wi-Fi、网络、音频与固件事实;字节数组和 Java String 通过统一 store 发布为受检 local
  reference,unique code、background、fully-loaded、keyboard、managed-swap、离线
  tracking sink(launch/first-run 同口径记账)与启动计数进入线程安全可查询状态。legacy framework platform 一个批次声明
  Build/VERSION 全量 APK 引用字段、SystemProperties、Settings.Secure、Context/
  ContentResolver/Telephony、Activity、Bundle、ViewRoot 与 UUID;static field 走统一
  field store,service/UUID 对象走统一 object registry,未知 key 保留 Android 空值语义
  且不读取宿主隐私。宿主未实现的浏览器、商店、付费、在线服务与 trophy 回调必须带
  method descriptor 明确失败,禁止静默 no-op 或伪造成功。
- `NativeActivitySession` 只接受 API 19 ARMv7 当前入口,执行真实 Bionic 初始化、
  `ANativeActivity_onCreate`、glue child 与完整销毁回调;阶段可由可选 observer 查询。
- guest child 异常必须唤醒同步生命周期 waiter,并在 root 继续执行、帧或输入边界转为带
  原始原因的 `NativeActivityRunError`;禁止 SDL 主线程无限等待已死亡渲染线程的首帧。
- `NativeActivityRunRequest::supersample_factor` 选择受检 1..4× 内部渲染倍率;guest 的
  EGL surface 和输出帧保持逻辑尺寸,ANGLE pbuffer、viewport 与 GPU render target 使用
  放大尺寸,swap 时通过 gles 确定性 resolve 还原。
- `NativeActivitySession` 实现 core GPU provider,快照只报告真实发生的 clear、默认
  FBO、ANGLE 后端和最近 2048 条 EGL/GLES 调用;热路径只向固定 raw ring 写 descriptor
  id 与四个寄存器,并只获取独立 trace mutex,不竞争 Android boundary 主 mutex;字符串、
  map 和十进制格式化推迟到 trace 查询;未查询到的扩展、限制和 guest FBO 不伪造。
- executor、时钟、VFS 和 profile 必须显式注入或由确定性 fixture 建立。
- 不包含平台 UI、真实 present 或游戏专属逻辑。

## 文件分工（android.* intrinsic）

`dexvm_android.h` 是唯一公共面。生产装配只调用
`AndroidIntrinsicCatalog(context)`，core catalog 的平台事实则只通过
`AndroidCoreIntrinsicServices(context)` 注入。平台类按 API 家族聚合到
`dexvm_android/<家族>.cpp`，每个类仍以 `Declare_<类名>(context)` 同址声明并直接
绑定；Java handle 家族聚合文件不受 800 行限制。`catalog.cpp` 只注册聚合，
`shared.h` 只保存确需跨家族复用的 helper/工厂，support 文件只实现
surface/video/widget 派发子系统。禁止恢复集中式 handler 容器、Populate 或
字符串分发通道。

## 测试

对应 `headless_bionic_runner_tests.cpp`、`headless_jni_contract_tests.cpp`、
`android_guest_call_session_tests.cpp`、`android_guest_framework_platform_tests.cpp`、
`android_guest_java_media_tests.cpp`、`api19_guest_process_tests.cpp`、
`android_link_preflight_tests.cpp` 与 `native_activity_runner_tests.cpp`。
