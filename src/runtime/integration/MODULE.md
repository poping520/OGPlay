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

## 不变量

- runner 只有在所有资源、引用、线程和生命周期闭环后才能报告成功。
- Android guest call session 只在通用 A32 slice 边界调用显式 observer;observer 由上层
  注入,不得让 integration 直接依赖窗口后端或消费输入。
- `PreflightAndroidGuestLink` 复用生产 Bionic namespace 与 Android boundary 完成映射和
  重定位但不执行 guest;报告 guest/boundary 模块及 relocation 数,任一缺失导入明确失败。
- `InitializeApi19GuestProcess` 事务映射统一 root TLS/thread-info/preinit、4 MiB 栈、
  `SVC #1` 返回 trap 与空 property area,并只向受检 libc 导出槽发布地址;固定布局冲突、
  非法线程/进程名或写入失败必须回滚新增映射和导出槽。
- `AndroidGuestCallSession` 组合真实 Bionic namespace、API 19 process、syscall/clone、
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
- session stop 必须先快照全部 child、请求仍运行者退出并中断 futex,再逐个 join;单个
  child 的异常只能作为首错延迟上报,禁止跳过其余 join。所有 child 完成后才可执行 guest
  fini 或让 lifecycle/dispatcher/address-space 进入逆序析构。
- session teardown 顺序固定为:请求 child 退出 → `InterruptBlockingWaits` 只做临时唤醒 →
  join 全部 child → 执行 guest fini/destructor → root JNI detach → 永久 monitor
  shutdown。`InterruptBlockingWaits` 无论由 Stop 还是外部调用,都不得永久停用 monitor;
  guest finalizer 阶段的 `MonitorEnter` 必须仍能成功,permanent shutdown 只允许发生在
  其后。
- `AndroidGuestCallSession::InitializeJniLibrary` 只可在运行中的会话调用一次;调用方必须
  在 ELF constructors 完成并注册所需 Java class 后显式调用,成功或 root 无 OnLoad 时才
  发布 library-ready。失败、重复或停止后调用不得伪造完成。
- `DexVmGuestBridge`(ADR-0017)在运行中的会话之上装配 dexvm:链接
  core+android intrinsic 目录与单一 classes.dex,对象模型复用会话的
  string/primitive-array store(native 与解释器同一对象)。出向 native 调用按
  descriptor 编组 A32 soft-float 帧(r0=JNIEnv、r1=receiver/jclass、64 位偶对齐、
  栈 8 字节对齐),解析顺序 RegisterNatives → `Java_` 导出名 → 记账明确失败;
  入向把全部解释类/方法注册进会话 `JniClassRegistry`(impl id `dexvm.m<id>`),
  FindClass/GetStaticMethodID/CallStatic* 经不变的 233 槽 ABI 命中真实 DEX 事实
  并落入解释执行(第三路由);解释器未捕获异常按 JNI 语义置 pending。
  J/D 出向返回值暂记账明确失败。
- `DexVmAndroidContext` + `AndroidIntrinsicCatalog/RegisterAndroidBuiltins`:
  android.* intrinsic 按 pilot 测量面挂接真实会话状态——Resources 由严格
  resources.arsc 事实驱动、SoundPool/MediaPlayer 直连存量 mixer(resid 即键)、
  IO 走 APK 条目与会话内存文件、身份为确定性配置、SMS/网络记账明确失败;
  统一时间由生命周期驱动发布(System.currentTimeMillis/Thread.sleep 同源)。
- android.* intrinsic 的 widget 层（TextView/EditText/ImageView/VideoView/对话框等）
  只持有状态、不做宿主渲染:`setContentView(I)` 经 arsc resid → APK 布局条目 →
  `ParseBinaryXmlElements` 实例化 widget intrinsic,`android:id` 进入 view registry
  供 `findViewById` 查询,文档序首元素登记为 content view;`runOnUiThread` 在协作
  单线程模型下同步执行 runnable。
- VideoView intrinsic(`dexvm_android_video.cpp`,ADR-0021):`setVideoPath` 经 VFS
  `HostPathFor` 解析宿主文件并用注入的 `VideoPlayerFactory` 打开解码;`start`/`pause`/
  `seekTo`/`stopPlayback`/`getDuration`/`getCurrentPosition` 驱动真实播放状态,位置由
  统一 uptime 推进。`PumpVideoViews` 由 guest 循环每步调用:取帧经
  `ComposeRgbaOnCanvas` letterbox 到 surface 尺寸后交给发布回调,到达时长时在 guest
  线程回调 `OnCompletionListener` 恰好一次。工厂缺失、路径不可解析或打开失败时诚实
  回退:结构化 warn + `start()` 立即回调 completion,不伪造播放事实。
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
  mode、range、重复构造或 release 后调用明确失败;PCM 输出混音尚未接线时能力保持
  partial。
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

`dexvm_android.h` 是唯一公共面（`AndroidIntrinsicCatalog` +
`RegisterAndroidBuiltins`）。私有 `dexvm_android_internal.h` 声明批次入口与
跨批次 helper；类声明按平台面分在 `dexvm_android_catalog_*.cpp`，handler 按
同一分面分在 `dexvm_android_{activity,io,device,media,graphics,misc}.cpp`。
新增能力应加进对应分面文件，而不是让单文件继续增长。

## 测试

对应 `headless_bionic_runner_tests.cpp`、`headless_jni_contract_tests.cpp`、
`android_guest_call_session_tests.cpp`、`android_guest_framework_platform_tests.cpp`、
`android_guest_java_media_tests.cpp`、`api19_guest_process_tests.cpp`、
`android_link_preflight_tests.cpp` 与 `native_activity_runner_tests.cpp`。
