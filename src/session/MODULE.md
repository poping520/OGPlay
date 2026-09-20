# 模块：session

## 职责

编排 APK compatibility Profile、VFS、DexVM Activity 生命周期、存档、输入、
统一 Clock 和运行检查点。
Dex activity 每帧在 guest 回调前泵送主 Looper，到帧尾只通过
`AdvanceAndroidClock` 推进 Android 单调时钟；停止时先关闭 scheduler，再停止 guest 线程。

## 公共 API

- `AndroidAppProcess::Create/StartApplication/StartLauncherActivity/Stop`：拥有 Manifest、
  native inventory/ABI、rootless guest process、动态 loader、DexVM、Application root 与
  Activity lifecycle；Create 只准备 API 19 system ELF，不预载 APK `.so`。Manifest launcher
  是默认入口，显式 compatibility entry 才覆盖；无 native inventory 保持 Java-capable
  process mode。DVM-77 把同一 sealed Manifest 的 package/version/target SDK/application
  label/icon/meta-data/requested permissions 注入 DexVM Android context；requested permissions
  是 bounded compatibility process 的显式 granted set，不宣称模拟 protection level。
  同一 sealed Manifest 的 activity/activity-alias 也注入 context，供 `getPackageInfo`
  在 `GET_ACTIVITIES` 下发布当前包 Activity 元数据。`AndroidAppProcessRequest` 同时把显式 `GuestProcFacts` 原样传给 native process；session
  还接收前端已解包的 curated API 19 Boot DEX，并与应用 DEX 一起交给 bridge；session
  不选择 Boot 类、不读取宿主内存，也不从 Profile 隐式覆盖虚拟设备 `/proc` 事实。
- `LoadTitleProfileText` / `LoadTitleProfile`：严格读取 legacy v1/v2 与 optional v3
  纯数据 TOML；v1 仅适配仍有效的 compatibility 字段，不恢复 native-call/Java replay，
  v3 只要求 package/api_level，版本和 `.so` hash 是可选 applicability guard。
- `SelectApkCompatibilityProfile`：按 package 与可选 version/hash guard 选择 override；
  无 Profile、旧 hash 不命中和纯 Java APK 都返回成功的 generic selection。ABI 只在
  v1/v2 旧 applicability 中校验，不覆盖 APK process ABI resolver。
- `TitleProfileCatalog::LoadDirectory/Match`：稳定加载目录；`Match` 仅保留为 v1/v2
  package/versionCode/SHA-256 exact adapter，新启动路径使用 optional selector。
- `MatchApkTitleProfile` / `PrepareApkProfileLaunch`：以 binary Manifest 和全部 APK ARM
  native library 事实选择唯一根库，再交给统一 Bionic 依赖闭包规划；无匹配返回空，
  闭包错误不发布部分计划。
- `SummarizeApkProfileMatch` / `FindApkProfileSummary`：向上层只读发布稳定 profile
  id/name 与“是否声明 required external mount”布尔事实，调用方无需解析或遍历
  Profile 数据结构；后者供缓存 id 刷新状态。
- `ResolveProfileLaunchDescriptor`：`runtime.entry.launch_activity` 存在时优先使用，
  否则使用 Manifest launcher；两者均不存在时明确失败。
- `ApplyProfileStaticPresets`：逐项初始化真实 DEX class（包括 `<clinit>`），再按声明的
  primitive/String 类型写真实 static field；类、字段、类型、范围或初始化不一致即失败，
  每次写入记录 `reason`。
- `StartDexApplication` / `DexActivityLifecycle`：先按
  resolve→`<clinit>`→construct→attach base Context→虚派 `onCreate` 建立稳定的 process
  Application root，再实例化入口 Activity 并解释执行 onCreate/onStart/onResume、
  renderer surface/frame、输入、suspend/resume 与 surfaceDestroyed/onStop/onDestroy；
  未捕获 Java 异常携带具体 class descriptor、消息和解释器栈失败。每帧同时泵 VideoView
  与 AudioTrack position notification；音频回调只在生命周期解释器单写者线程执行，guest 异常使 lifecycle
  失败。首次 Surface traversal 前按 ADR-0024 冻结 onStart/onResume 后已存在的 worker
  context，并有限轮 yield，直到每个 worker 被观测到 park 或终止；期间新线程不追入，
  每轮 host progress 等待有 2ms wall-time 上限且不争抢 execution lock，超限写结构化 warn
  后 fail-open。初始 focus 仍留到下一 frame，不并入握手；窗口焦点
  事实只由 lifecycle 写入，先更新再虚派 Activity 与 attached View，Suspend/Resume、
  Activity 切换和 Stop 共用去重转换。输入按
  managed view 命中规则分发触摸与 click。pause 在 guest `onPause` 后调用持久状态 flush 回调；clean stop
  在线程停止后、guest finalizer 前再次调用，失败向上层传播。
  guest EGL swap 在 intrinsic 内 publish，不由 lifecycle 再次 present。lifecycle
  仅在 guest-owned GLSurfaceView 路径注册 driver 线程，并在每帧尾推进条件 swap
  pacer。进程 teardown 在任何 guest 回调前永久退役 Java/native 图形入口、发布
  renewable JNI frame 取消并唤醒 blocking wait；回调期间可能新建等待，因此 join
  前再次 interrupt。该顺序是 OGPlay 进程退出策略，不伪称 AOSP 在
  `surfaceDestroyed` 回调前使 Surface 失效。
  注入诊断状态时，Stop 额外发布 begin、guest callbacks、scheduler、thread join、
  persistence、guest finalize、surface close 与 complete 阶段；阶段只供取证，不改变退出
  顺序、异常传播或 ADR-0025 取消语义。Suspend 同样发布 begin/complete；Android app
  process 向统一快照注入 DVM trace/Java 栈、monitor owner/waiter、EGL pacer 与 GLES trace
  的不等待 provider，并在 provider owner 析构前清除回调。
  intrinsic-renderer 不安装 observer。driver 可运行时维持一帧一 swap，driver 停泊于
  guest 阻塞原语时由执行锁 observer 放行 GLThread。停止在 shutdown/join guest Java
  线程前唤醒 pacer。surface callback 前按通用 render-driver 事实分流：intrinsic
  renderer 保留打开线程 GL currency；guest-owned GLSurfaceView 显式释放后交给其
  GLThread。GLSurfaceView queueEvent 在 renderer callback 前由同一 current GL 线程排空；
  continuous 每帧绘制，WHEN_DIRTY 仅在初始帧或 requestRender 后消费一次绘制请求，事件
  即使不触发绘制也会执行。
  未捕获 Java 异常文本按失败阶段、exception、message、stack trace 分行输出；原始类描述符、
  消息、方法与 pc 不改写。
- `MapAndroidInput`：在 HAL 与 Android guest 边界把通用 USB HID/SDL 物理 scancode
  转为 API 19 keyCode，把左右 modifier/caps/num 转为 metaState，并保留当前布局 Unicode、
  repeatCount、scanCode 与 eventTime；未知物理键明确成为 `KEYCODE_UNKNOWN`。
  Activity switch 在旧 `onDestroy` 后推进 UI content generation，清空旧 UiTree binding/
  listener，再构造新 Activity；旧 Activity UI 不得参与新一帧 draw/input。入口 Activity
  实例化时把自身句柄发布为 `task_root_activity`（进程唯一 task 的根，
  `Activity.isTaskRoot()` 的判定依据），switch 到达的 Activity 不是根。
- frontend 取回最终 present frame 后必须交 session `ComposePresentedFrame` 与 cached UI
  overlay 做整数 source-over；纯 View Activity 在 UiTree dirty 且没有 guest renderer 时由
  lifecycle 发布不透明软件基帧，再走同一合成入口。screenshot/window 只读取返回结果，
  video 不依赖 UI。EditText 键盘编辑和 ScrollView 手势都更新 UiTree 的唯一状态，绘制、
  裁剪与命中使用同一次布局产生的 frame。
- touch DOWN 的 gesture ownership 与 click eligibility 独立：touch-only false 立即回退
  Activity，touch-only true 保持 capture；clickable 且带 click listener 时未消费的
  UP-inside 才 onClick，touch 消费则禁止 click。`setClickable(false)` 阻止触摸点击但
  保留监听器。隐藏/删除/UP-outside 取消 click，hit-test 仍只读 UiTree frame。
  无 listener target 时按 reverse-Z、deepest-first 遍历命中点下的 guest View；基础
  `View.onTouchEvent` 在 clickable 时消费 DOWN，未覆盖的非 clickable View 返回 false。
  只有消费 DOWN 的实际 receiver 才建立 MOVE/UP capture，未消费事件再回退
  `Activity.onTouchEvent`。receiver detach 或 Activity switch 清空 capture，禁止旧 View
  跨 generation 收事件。
- ScrollView 仅在可滚动且累计位移超过 `8dp` slop 后接管；接管向原 listener/deep target
  发送 `ACTION_CANCEL` 并撤销 click/capture。纯 View 软件基帧发布前确认不存在 renderer、
  active SurfaceHolder、VideoView 或 holder Canvas producer。
- `AssembleProfileVfs`：把已导入数据与 Profile mount 精确配对，在全新 VFS 中挂载并
  校验 required mount、manifest 和 working directory；
  `ResolveProfileWorkingDirectory` 为普通 Android 进程提供 `/` 默认 guest cwd，Profile
  显式数据目录仍优先；
  `FlushProfileVfsAtLifecycleBoundary` 是 pause/clean stop 共用的 `FlushAll` 适配点。
- `ApplyProfileInput` / `ApplyProfileAudio` / `ResolveProfileSoundPoolPath`：只消费 Profile
  的通用 input id、source/path 与资源占位符，不按标题猜测。
- `QuirkRegistry::Load/LoadPackaged/Validate`：严格加载 `data/quirks.toml` 并交叉验证
  Profile 引用；源码模式额外验证测试文件/用例存在，发行模式保留测试引用形状验证。
- `ProfileAssetBundle`：拥有已导入 VFS/audio 字节并拒绝非规范路径、大小写歧义、重复项
  和空资产。
- `Session::OpenEmpty/Close/State/Step/UntilFrame/Pause/Resume`：确定性会话原语。
- `AudioOutputPump`：会话拥有的音频消费；实时 worker 按设备队列补 PCM，离线路径按
  统一 Clock 差值换算帧数并保留不足一帧的余数。frontend 只注入 HAL 输出。设备
  Submit/水位查询失败时置位 `DeviceFailed` 并停止继续提交。设备、mixer 和 callback
  异常统一先调用显式 FailureInterrupt 唤醒 producer，再保存原异常并交还主循环。
  未注入 `sound_resource_loader` 时默认走 `LoadEncodedAudioWindow`。
  MCP 手动步进不启动实时 worker，只按 lifecycle Clock tick 差推进离线混音；mixer/callback
  异常保留原异常并交还主循环，设备提交失败单独记录。进程按 stream 初始化 native gain，
  VideoView 的 MUSIC gain 只作用于视频音轨，不二次缩放其他混音结果。

## 不变量

- Title Profile 接受 legacy schema 1/2 与 optional schema 3，统一归一到
  `dex_activity` 架构；未知 root/runtime 字段在加载期失败。
- exact-key table 校验共用 TOML 私有入口；各 schema/table 只声明允许键集合和上下文名称。
- v1/v2 `runtime.dexvm.interpreter` 只接受 `switch`/`threaded`；省略时固定归一为
  `switch`，Profile 只表达通用 backend 选择，不承载 title 专属解释器行为。
- 游戏身份信息只有 Title Profile 一个来源；`src/` 不出现标题、厂商或包名分支。
- Profile 文件为 UTF-8 纯数据且不超过 200 行；未知字段、路径逃逸、非法或歧义身份失败。
- runtime 单次 guest call 预算省略时保持受检默认值，只接受 1..100 亿；DexVM heap、
  frame 与 tick 预算均有上限。
- `runtime.entry` / `runtime.presets` 只有在 required data manifest 非空时才允许，避免把
  未提供数据误报为已安装。
- static preset 必须在 class 初始化完成后写入，并保留字段真实类型；失败不得发布部分
  启动成功。preset 类型白名单先于值解码校验，未知引用类型不能被值类型错误掩盖。
- v1/v2 identity ABI 只接受 `armeabi` 与 `armeabi-v7a` 且仅用于旧 applicability；
  v3 禁止 ABI/root-library 字段，APK 匹配不得猜 main library。
- VFS 输入必须按 guest 根与 source 双重命中；额外输入和 required 文件缺失明确失败。
- SoundPool pattern 只接受一个 `{resource}` 或 `{resource:0N}`（N 为 1..9）。
- enabled quirk 必须有注册定义和可定位测试；源码/CI 在打包前验证测试文件与用例存在，
  packaged runtime 至少严格保留其引用形状；未注入注册表时不得进入匹配目录。
- 生命周期清理顺序固定，状态推进由固定帧步进驱动，不依赖 sleep。
- app process 状态只允许 DexVmReady→ApplicationStarted→ActivityResumed→Stopped；
  frontend 不得跳过 Application 或直接选择/初始化 APK ELF root。
- Application 初始化失败必须先清除临时 root，且不得打开 surface、初始化或构造 Activity。

## 禁止

- 不复制每游戏帧循环。
- 不在代码中出现包名、游戏名、厂商名或补丁地址。
- 不把导出符号、文件探针或模糊哈希当作游戏身份。
- 不恢复 Profile 声明的 JNI 调用序列或 Java handler 映射。

## 测试

`tests/session/` 覆盖 v1 adapter、v2 exact、v3 optional、入口/预置、VFS、生命周期、
输入与确定性；
`ui_compositor_tests.cpp` 锁定透明 overlay 基线不变和局部合成 exact pixels；
`tests/dexvm/widget_click_tests.cpp` 锁定 touch/click ownership 四组合、无 listener 的深层
View override、reverse-Z fallback、capture 及取消路径；
`tools/validate_title_profiles.py` 提供独立目录门禁。

DVM-105：纯 Java APK 也建立 NativeLibraryLoader，用于平台 Java 类加载 bundled JNI。
APK selected ABI 仍为空，application ELF 数仍为零。内部空 ARMv7 库视图只用于 API 19
系统进程，不表示 APK 自带 native 库。

DVM-107：根启动向 lifecycle 分别传实际 launcher descriptor 与 manifest component name，
alias 只影响实例化目标，Activity 的组件身份保留 alias。Profile launcher override 使用
显式覆盖类名。根启动和切换均在 attach base 后、onCreate 前附加 ComponentName/Intent；
旧实例保留自己的引用，不从进程 current_intent 动态读取。当前 APK 内受限隐式解析与显式
非根 alias 切换同样分别携带 Manifest 组件名和实际 target Activity descriptor。

DVM-112/128：AndroidAppProcess 把 sealed Manifest 的 application enabled、service 过滤器
及独立 `AndroidGuestPlatformConfig.android_id` 复制到同一 DexVmAndroidContext，并显式标记
inventory 已就绪；独立 VM 未装配时保持
未知，不能把缺少信息当作服务不存在。服务信息不参与 Activity 启动或产生绑定状态。
同一平台配置的 `strict_webview_errors` 只控制 Web facade 的开发诊断策略；默认关闭，
session 不据此创建浏览器、网络或 Activity 能力。

DVM-121：从 sealed Manifest 发布 application theme 与逐 Activity theme；alias 继承
目标 Activity，0 回退 application。integration 在 Activity.onCreate 前应用对应资源 id，
session 不解析 style/颜色或创建完整 Android Theme。


DVM-150：显式 guest Runtime/System.exit 先完成 BootDex hook 协议，halt 跳过 hook；VM 退出码
已发布的生命周期调用展开进入 Stop，不计为 guest fault。Application 内退出后不再启动
launcher；已退出 VM 的 Stop 不再调用 Java 生命周期回调。宿主 Stop 仍是已有取消/回收路径，
不隐式运行 hook；没有最后 non-daemon 线程结束自动退出或 hook 超时成功语义。挂起的原版
hook 可阻塞正常 exit，强制取消不能宣称正常 hook 完成。详见 ADR-0056。
