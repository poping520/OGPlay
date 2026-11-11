# 模块：session

## 职责

编排 APK identity、VFS、进程内 Android 服务、输入、统一 Clock 与运行检查点。
guest 主线程在独立宿主线程持续执行 ROM Looper.loop。session 提交启动、挂起、恢复和关闭请求给 AMS，向 WMS 输入分发提交事件，经合成器读取已提交的像素。

## 公共 API

- `AndroidAppProcess::Create/StartApplication/StartLauncherActivity/Stop`：拥有 Manifest、
  native inventory/ABI、rootless guest process、动态 loader、DexVM、Application root 与
  Activity lifecycle；Create 只准备 API 19 system ELF，不预载 APK `.so`。Manifest MAIN/LAUNCHER
  是唯一根入口；无 native inventory 保持 Java-capable
  process mode。ActivityThread.attach(false) 通过本地 AMS 投递 bindApplication。
  主队列首次空闲后发布 ROM Application，再允许启动 launcher。SDL 操作归宿主前端。

  DVM-77 把同一 sealed Manifest 的 package/version/target SDK/application
  label/icon/meta-data/requested permissions 注入 DexVM Android context；requested permissions
  是 bounded compatibility process 的显式 granted set，不宣称模拟 protection level。
  安装事实装配后显式封存单包清单，当前 guest 仅安装本次 APK；独立未装配 context
  不视为空安装清单，也不读取宿主或 Launcher 的应用列表。
  同一 sealed Manifest 的 activity/activity-alias 也注入 context，供 `getPackageInfo`
  在 `GET_ACTIVITIES` 下发布当前包 Activity 元数据；Activity 主题注册按组件名以后声明覆盖，
  alias 直接使用 loader 封存的继承主题。receiver 声明独立注入，供当前包
  `getReceiverInfo/queryBroadcastReceivers` 使用，逐过滤器事实不合并。`AndroidAppProcessRequest` 同时把显式 `GuestProcFacts` 原样传给 native process；session
  还接收前端已解包的 curated API 19 Boot DEX，并与应用 DEX 一起交给 bridge；session
  可接收受审 API19 Roboto-Regular 与 DroidSansFallback bytes，交给 UI 按固定顺序校验为
  不可变链并绑定主树；独立宿主树经 context 工厂同源注入，不读取宿主/应用字体。
  不选择 Boot 类、不读取宿主内存，也不从 Profile 隐式覆盖虚拟设备 `/proc` 事实。
  外部 guest slice observer 及其可选宿主线程限定原样传给 native process。呈现帧的 UI
  overlay 在自有 RGBA8 buffer 上合成；经 renderer 认证的全透明 overlay 省略扫描，
  仍先校验尺寸，混色算法与视频叠放顺序保持一致。
- `LoadTitleProfileText` / `LoadTitleProfile`：仅接受 v4 的 identity，
  旧 schema 和未知字段明确失败；Profile 是“仅 identity 的精确身份覆盖层”。
- `SelectApkCompatibilityProfile`：按 package 与可选 version/hash 约束选择覆盖；
  无匹配或纯 Java APK 可走 generic startup，ABI 只由 APK inventory resolver 决定。
- `TitleProfileCatalog::LoadDirectory`：稳定加载目录，校验非法及交叠身份；已删除旧的
  单根库 exact adapter，前端只投影 profile id。
  `LoadOptionalDirectory` 在目录缺失时返回空 catalog，已有目录使用相同校验。
- `DexActivityLifecycle` 启动或切换时等待主队列空闲。每步推进 Clock，提交输入和媒体 tick，并按帧率上限排队 vsync、放行 EGL swap。手动步进等待主队列处理到期工作、软件绘制和已登记 EGL 生产者完成或阻塞后读取合成器。实时前端在 step 前用 ReadyForFrame 查询，等待时继续处理宿主事件，直接调用 step 也执行同一等待。等待不持有 VM 执行锁，主线程的 guest 阻塞允许 GL 继续完成生命周期握手。EGL 生产者等待超过 30 秒时放行步进并记录警告，生产者下一次 swap 或可观测阻塞后恢复双向同步。
- `MapAndroidInput` 将通用 HID/SDL scancode 映射到 Android keycode，指针使用逻辑窗口坐标；WMS 拥有按键焦点、触摸目标和捕获。SDL 文本经本地 IMMS 的活跃输入连接交付。
- `FrameService` 接收合成器的不可变 RGBA 帧。全屏不透明图层满足尺寸、裁剪和覆盖条件时复用像素 lease。空图层产生黑底，干净帧保留发布序列。
- `Stop` 区分宿主关闭、guest exit 和运行故障。健康关闭在 Clock 预算内等 AMS 确认销毁，再停止音频、Java/native worker 和图形；保留原始故障与进程退出码。

- `BuildControlConfig` / `LoadControlOverride`（ADR-0142）：只合并“内置默认 → 用户
  input.toml”。内置仅鼠标/触摸直通与 Esc→Back，不提供官方区域或游戏键位。
  用户层只读取 zone/binding；schema 1、UTF-8、不超过 200 行，input.toml 中未知键跳过，
  按键编辑器 JSON 请求中的未知键明确失败。look 绑定字段为 zone、sensitivity、
  sensitivity_y、invert_y（布尔）、settle_frames、max_offset_x、max_offset_y、regrip
  （single/handoff）；button/key 可选布尔 release_pointer；lock 绑定只有 source/action/mode（INP-10）。
  后层绑定替换同模式（或 `*`）共享输入源的默认绑定，none 解绑，区域按名称替换；
  未放置区域明确失败。`FormatControlLayer`/`SaveControlLayer` 保留 TOML 往返与原子替换，
  `DecodeControlLayerJson`/`WriteControlLayerJson` 共用同一 zone/binding 校验。
  `LoadControlOverrideIfPresent` 把缺失文件视为空用户层，不读取 Profile 或 bundled 模板。
- `Session::OpenEmpty/Close/State/Step/UntilFrame/Pause/Resume`：确定性会话原语。
- `AudioOutputPump`：会话拥有的音频消费；实时 worker 按设备队列补 PCM，离线路径按
  统一 Clock 差值换算帧数并保留不足一帧的余数。frontend 只注入 HAL 输出。设备
  Submit/水位查询失败时置位 `DeviceFailed` 并停止继续提交。设备、mixer 和 callback
  异常统一先调用显式 FailureInterrupt 唤醒 producer，再保存原异常并交还主循环。
  未注入 `sound_resource_loader` 时默认走 `LoadEncodedAudioWindow`。
  MCP 手动步进按 lifecycle Clock tick 差推进离线混音；许可 step 正在执行且 PCM writer
  被队列背压阻塞时，独立消费者可先混音释放 writer，避免主 Looper 写入与 step 后消费互等。
  提前消费帧从后续 Clock 额度扣回，不重复推进音频；无 step 或无背压不消费，不提交 HAL。
  消费者错误先 interrupt writer，再交还主循环，不能静默绕过写入。
  宿主协作停止期间恢复消费并暂启实时 worker，在生命周期回调结束、guest 线程退役前停止。
  mixer/callback 异常保留原异常并交还主循环，设备提交失败单独记录。进程按 stream 初始化 native gain，
  视频混音底座按每播放器 stream 应用增益；VideoView 固定 MUSIC，普通 MediaPlayer
  按已设置 stream，不在 session 再次缩放。

## 不变量

- Title Profile 仅接受 schema 4，root 只允许 schema/identity；identity
  只允许 package/version_code/so_sha256。文件 UTF-8、纯数据、不超过 200 行。
- Profile 不拥有运行时、资源、入口或音频策略。
  API 19 与 VM/guest call 默认值由通用实现决定；逻辑表面尺寸由用户设置提供；Manifest 和 APK inventory
  提供入口与 ABI，前端按标准 Android 路径装配 OBB 和外部目录。
- exact-key table 校验共用 TOML 私有入口，各 table 声明允许键集合与上下文。
- 身份必须精确匹配声明条件；`src/` 不出现标题、厂商或包名分支。
- 生命周期清理顺序固定，状态推进由固定帧步进驱动，不依赖 sleep。
- app process 发布逻辑键盘（id=-1）与鼠标映射触摸（id=0）；触摸轴范围采用逻辑窗口坐标。
  QueueInput 将宿主设备编号归一到这两个 id，Java 与 NativeActivity 输入队列共用该映射。
- app process 状态只允许 DexVmReady→ApplicationStarted→ActivityResumed→Stopped；
  frontend 不得跳过 Application 或直接选择/初始化 APK ELF root。
- Application 初始化失败必须先清除临时 root，且不得打开 surface、初始化或构造 Activity。

## 禁止

- 不复制每游戏帧循环。
- 不在代码中出现包名、游戏名、厂商名或补丁地址。
- 不把导出符号、文件探针或模糊哈希当作游戏身份。
- 不恢复 Profile 声明的 JNI 调用序列或 Java handler 映射。

## 测试

`tests/session/` 覆盖 v4 身份/拒绝旧字段、生命周期、输入与确定性；
标准入口/音频见 application_startup 与 audio_track 测试，标准挂载见 run_apk_vfs 测试；
`rom_window_integration_tests.cpp` 通过 APK 验证 ROM 窗口、输入与呈现。

`tools/validate_title_profiles.py` 提供独立目录门禁。

DVM-105：纯 Java APK 也建立 NativeLibraryLoader，用于平台 Java 类加载 bundled JNI。
APK selected ABI 仍为空，application ELF 数仍为零。内部空 ARMv7 库视图只用于 API 19
系统进程，不表示 APK 自带 native 库。

DVM-107：根启动向 lifecycle 分别传实际 launcher descriptor 与 manifest component name，
alias 只影响实例化目标，Activity 的组件身份保留 alias。根启动和切换均在 attach base 后、onCreate 前附加 ComponentName/Intent；
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

DexActivityLifecycle 成功启动与恢复后向已连接 DiagnosticState 发布 running 阶段；无需启动写盘 coordinator，阶段 generation 仍由 DiagnosticState 维护。

退出前记录并退役已失败调用的 root JNI 异常；本地服务在 scheduler 关闭前解绑。
native 清理失败仍继续关闭进程和 surface；前端保留运行首错，独立报告清理错误。

本地视频与软件 Canvas/EGL 通过各自的 ROM Surface 进入窗口合成器。

DVM-218/236、ADR-0095/0115：进程 Stop 显式区分 host shutdown、guest exit 与运行 fault，
退出事实优先。健康宿主关闭在既有预算内经 AMS 请求 pause、stop 与 destroy 并等待 ROM 确认；期间 VM 仍可创建 Java/native 线程，音频消费保持推进。回调后先调用显式
`before_guest_shutdown` 停止并回收音频消费者，再 BeginTeardown、VM shutdown/join；
超时传播原错并记 `session.lifecycle.cleanup_timeout`，到期后不继续 guest 回调。
进程资源在销毁确认后退役；普通 Activity.finish 仍按实例执行 destroy 并传播异常。
进程关闭拒绝新转换/输入，关闭 scheduler、join native/Java worker 后由 owner 退役媒体/图形
资源，再执行仍允许的 native cleanup。退出预算覆盖重入且不续期，首错保持、重复 Stop 幂等。
进程 Stop 不执行模块卸载式 DSO fini；底层显式 permitted 卸载/停机的执行与失败传播保持。
Java/native 进程退出事实发布后不再调用 guest 生命周期或 JNI 清理；原生
exit/exit_group 的真实退出码与展开规则保持。Stop 失败仍 join、flush、detach、关闭 surface；
取消不冒充卸载，native finalization 失败仍报告。

AndroidAppProcess 在 native 创建前由封存 Manifest 的 supports-gl-texture 决定默认厂商
压缩族：明确的单一已支持 ATC/PVRTC 声明（可伴 ETC1）收窄发布，无/未知/多厂商声明
保持 all。记录声明及策略来源，Profile 不提供纹理格式覆盖，不按名称猜资源。

AndroidAppProcessRequest 的原生单次调用默认预算引用 runtime/common 的统一看门狗常量
（100 亿 tick），与 AndroidGuestProcessRequest/AndroidGuestCallSessionRequest 一致；
Profile 和用户设置均不覆盖。

已解包 BootDex 显式交给 bridge；session 不读取宿主类路径。

ROM ActivityThread 在 Application 类初始化前以 ContextImpl.getCacheDir 设置 java.io.tmpdir，
按清单的 largeHeap 调用 VMRuntime.clearGrowthLimit。ActivityThread 使用独立 ContextImpl 附着 Activity。
具体窗口服务、Surface、输入和本地 Binder 边界见
[DexVM Android 契约](../runtime/integration/dexvm_android/MODULE.md)。
