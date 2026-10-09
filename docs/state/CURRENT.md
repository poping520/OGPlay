# 当前状态

更新：2026-10-09。

本页只保留最新运行结果与未闭合边界。能力状态见 [capabilities.toml](../../capabilities.toml)，
实现契约见各模块 MODULE；历史过程见 [任务总览](../tasks/README.md) 与
[DexVM 任务索引](../tasks/dexvm/README.md)。

## 运行状态

- **Wild Blood 1.0.4（SamsungApps）**：已补齐 Context.getClassLoader，复用稳定应用
  loader，ContextWrapper 虚派委托 base；TelephonyManager.getSubscriberId 按无蜂窝
  订阅语义返回 null。macOS Release 构建、ClassLoader 双解释器 2 项/194 断言及
  电话缺席/Java-JNI 一致性 2 项/1486 断言通过。原 APK/外部数据、无 Profile/
  无 survey、隔离空沙盒越过两处方法缺失并加载 libnativeinterface.so。
  [DVM-219](../tasks/dexvm/DVM-219.md) 支持空 Activity/延迟内容与固定 tzdata；Release
  定向 20 项/465 断言、payload 校验和真实 Bionic UTC/Shanghai 查询通过。
  原 content view 首错与 tzdata 缺失日志消失，主 Handler 回调进入 StartGame。
  [DVM-220](../tasks/dexvm/DVM-220.md) 已将两代传感器接口、客户端与值类归入 BootDex，
  仅保留无设备 native 边界；build/check、payload 校验、Release 构建及双解释器/JNI
  3 项/1682 断言通过。原 SensorListener 首错消失。
  [DVM-221](../tasks/dexvm/DVM-221.md) 已发布 libjnigraphics 三项 ABI，统一 Bitmap
  像素 lease；Release 构建、定向 7 项/303 断言通过。原 APK 成功加载引擎/JNI_OnLoad
  并进入 Game.onCreate。[DVM-222](../tasks/dexvm/DVM-222.md) 补齐 NFC Java 类型与
  无设备发现，不实现通信；BootDex build/check、payload 校验、Release 构建及
  NFC/sensor 双解释器 2 项/308 断言通过。原 NFC 首错消失并进入 Game.onStart；
  [DVM-223](../tasks/dexvm/DVM-223.md) 支持有界 Activity 返回栈与结果，保留普通 View
  调用方；BootDex build/check、payload、Release 构建及定向 13 项/2576 断言通过。
  原 startActivityForResult 首错消失。[DVM-224](../tasks/dexvm/DVM-224.md) 正常构造
  通知 manager 并支持已知空集合取消，发布拒绝且记账；BootDex build/check、自检、
  payload、Release 构建及定向 3 项/1808 断言通过。原 notification 异常未再出现；
  单帧运行到主动停止日志，f=1 长 guest call 后 30 秒退出阶段超时，未取得下一条
  Java 异常，阻塞原因未定位；复现进程已结束，不宣称 clean shutdown。
  最新用户复现首错为 renderer config chooser returned null；
  [BND-53](../tasks/boundary/BND-53.md) 新增真实 RGB565 backing/配置，macOS Release
  与定向 7 项/399 断言通过，原 chooser 首错消失。regular EGL 既有 separate-surface
  回归仍失败，与旧日志一致；原 boundary 架构违规仍在。
  [DVM-225](../tasks/dexvm/DVM-225.md) 将 GL 接口与 GLImpl 普通状态归 BootDex，
  native 复用既有 GLES；每个 EGLContext 独立持有 GLImpl，direct Buffer 按普通字段
  保活，heap client arrays 明确拒绝。BootDex build/check、自检、ABI/生成/载荷校验、
  macOS Release 与定向 38 项/2143 断言通过。原 glClear 分派首错消失；空沙盒
  presented=4，退出 0、无超时，本次短运行未触达下一致命首错。
  [DVM-164 后续修复](../tasks/dexvm/DVM-164.md) 使离线 HTTP/HTTPS 先构造连接，
  实际 I/O 仍拒绝，失败后重复断开安全。macOS Release 与定向 6 项/922 断言通过；
  loopback oracle 既有 pclose 收尾等待经手动结束测试子进程闭合。复制用户复现沙盒
  越过 HTTPS disconnect 空接收者首错；f=74 下一首错为 Analytics q.run pc19
  对 az.a 的空接收者 NPE，退出 1、无超时。
  [DVM-118 后续修复](../tasks/dexvm/DVM-118.md) 让 Resources.getText/getString 按原版
  TypedValue 转换简单资源值，引用/配置与虚派受检；macOS Release、定向 6 项/734 断言
  通过。复制用户沙盒复跑已到 Analytics setting appName；新首错提前在 GAThread.run
  pc65 缺 PrintStream(OutputStream) 构造器，退出 1、无超时，未再重达原一秒 TimerTask。
  [DVM-226](../tasks/dexvm/DVM-226.md) 将原版 PrintStream 归 BootDex，System 两个流
  包装独立 Java 日志端点；PrintStream 与 String 范围构造、ABI/载荷受检，macOS Release
  与定向 4 项/428 断言通过。两项 core-only fixture 缺 Activity native 装配，未通过。
  原构造器首错消失，GAThread 已打印被捕获的 NativeDecimalFormat.formatDouble 缺口，
  SDK 自行关闭 Analytics；游戏进入主视图，原 q.run NPE 未再出现。f=109 下一致命
  首错为 PushTheme.extractColors 缺 android.app.Notification 值类，退出 1、无超时。
  [DVM-227](../tasks/dexvm/DVM-227.md) 提供 Notification 有界值对象，系统模板与
  Parcel 请求记账抛可捕获 UOE，发布仍禁用。BootDex/ABI/载荷、自检、macOS Release
  与双解释器定向 2 项/362 断言通过。复制用户沙盒越过 Notification 缺类首错，
  PushTheme/C2DM 初始化已越过；f=115 下一首错为 systemStartupCheck pc27 请求
  activity 系统服务被拒绝，退出 1、无超时。Analytics formatDouble 旧错误仍被捕获。
  进程已退出，跨平台、授权路径及完整游戏未验收。
  证据 `.local/wb-classloader-analysis/`、`.local/wb-subscriber-fix/`、`.local/wb-lifecycle-fix/`、
  `.local/wb-sensor-fix/`、`.local/wb-jnigraphics-fix/`、`.local/wb-nfc-absence-fix/`、`.local/wb-activity-result-fix/`、`.local/wb-notification-cancel-fix/`、`.local/wb-rgb565-fix/`、`.local/wb-java-gl-fix/`、`.local/wb-http-lazy-fix/`、`.local/wb-resource-text-fix/`、`.local/wb-printstream-fix/`、`.local/wb-notification-value-fix/`。

  [VFS-07 Windows 复现](../tasks/vfs/VFS-07.md) 已越过外部数据挂载的文件流耗尽；
  进程打开窗口后在 prepare 报 SensorManager intrinsic 层级未注册，尚未进入游戏。
  宿主句柄池 Windows/Linux 定向通过，macOS 待原生验证。证据
  `.local/wb-windows-vfs-fix-20261009-121725/`；进程已退出。

  [DVM-228](../tasks/dexvm/DVM-228.md) 已提供 ActivityManager 内存查询，默认总量
  1 GiB；Java/JNI 与 proc 共用快照。Windows Release、BootDex/载荷及定向 8 项/1684
  断言通过。原 activity 服务首错消失，进入 onResume/surfaceCreated；f=42 下一首错
  为 Game$14.run pc20 缺 Message.obtain(Handler,int,int,int,Object)。退出 1、无超时，
  进程已退出；macOS/Linux 与完整游戏未验收。证据 `.local/activity-memory-game-20261009-132205/`。

  [DVM-85 后续](../tasks/dexvm/DVM-85.md) 已补五参数 Message.obtain；Windows Release
  与定向 3 项/197 断言通过。消息已进入主线程 Game$2.handleMessage pc358，f=36
  下一首错为 Resources.getDrawable(int) 缺失；GLThread 另捕获 EGL createContext
  failed: 12288 后退出，未定位。CLI 退出 1、无超时，进程已结束；图形/完整游戏未验收。
  证据 `.local/message-obtain-game-20261009-161853/`。

  [DVM-229](../tasks/dexvm/DVM-229.md) 接通有实际像素/颜色及 GC 保活的 Drawable→ImageView；
  [BND-53 后续](../tasks/boundary/BND-53.md) 修正 Windows 原生 RGB565 配置选择，保留 D24S8。
  Windows Release 与定向 8 项/429 断言通过；原 APK 已越过 EGL 配置错误并进入
  renderer.onSurfaceCreated。下一阻塞是子 View 未收到 onSizeChanged，渲染线程等待尺寸；
  f=0、180 秒超时后结束进程，MCP 未 ready。实际资源 Handler 尚未重达，完整游戏及
  macOS/Linux 本轮未验收。证据 `.local/drawable-egl-game-20261009-190837/`。

  [DVM-230](../tasks/dexvm/DVM-230.md) 与 BND-53 macOS 后续：Surface 独立持有真实
  RGB565+D24S8 backing，附着子树先尺寸回调再 Surface 创建；macOS Release 与定向
  10 项/848 断言通过。原 APK/用户沙盒副本已越过 chooser-null 和 onSurfaceCreated
  尺寸等待，进入 onSurfaceChanged/nativeInit；f=111 新首错为 JNI guest receiver is
  not a registered instance，退出 1、无超时、进程已结束。游戏画面/下一 JNI 缺口未验收；
  Windows/Linux 本轮未复验。证据 `.local/wb-macos-render-fix/`。

  [DVM-231](../tasks/dexvm/DVM-231.md) 补齐 JNI 创建 String/数组的按需真实类型登记；
  macOS Release 与双后端定向 8 项/417 断言通过。原 String.getBytes 首错消失，
  Game.nativeInit/GameRenderer.nativeInit 正常返回，进入 nativeRender/appInit；下一
  nativeRender 抛错后停在 teardown.guest_finalize，主 guest 持续 nanosleep，原异常正文
  尚未输出。两次取证后超时结束进程，游戏画面/clean shutdown/跨平台未验收。
  证据 `.local/wb-jni-type-fix/`。

  后续取证定位 nativeRender 首错为 LoadCoalescedFile 的 InputFile != NULL 断言：
  本地 ETC 配置存在，但 guest 请求 /mnt/sdcard，CLI 仅挂 /sdcard 且缺少对应别名。
  临时别名对照使原断言消失、appInit 进入步骤 2/3；对照改动已撤回，正式修复待实施。
  退出仍停滞，取证进程已结束，未取得游戏画面。证据 `.local/wb-solid-color-analysis/`。

  VFS-04 正式补 /mnt/sdcard 别名；[DVM-232](../tasks/dexvm/DVM-232.md) 为健康 fini
  增加累计墙钟预算/睡眠准入，隔离致命 CPU fault 后的 guest 清理，并提前记录首错。
  macOS Release 和定向回归通过；原 APK 完成 appInit，f=28785 下一首错为
  RelativeLayout.removeViewInLayout(View) 缺失。析构等待明确报预算耗尽，退出 1、无超时，
  进程已结束；完整画面/clean shutdown/跨平台未验收。证据 `.local/wb-storage-cleanup-fix/`。


- **Tales From Deep Space 1.0.0**：VideoView/packed depth/stencil 已闭合；macOS
  800×480/1280×720 菜单裁剪，BND-29 终止断言失败。
  [DVM-217](../tasks/dexvm/DVM-217.md)：1920×1080 存档重载恢复关卡通过。
  [DVM-218](../tasks/dexvm/DVM-218.md) 已修复取消误杀/首错遮蔽，先 join 再 onDestroy。
  退出仍读 0x315f0001：池先释放，布局后遍历，仍在办。
  证据 `.local/tales-exit-fix/`；进程已关闭，title gate 未验收。
  [WU-PERF-09](../tasks/optimization/WU-PERF-09.md)：Release 2560×1600 局部对照
  同 MCP/diag 对照 10.60 → 33.06 FPS（读页表/原子回调/边界与帧搬运优化，2 核）；
  不传 `--diag` 时对话/教学场景 35.91/35.52 FPS，42 项定向回归通过。
  原命令追加 `--cpu-cores 2`；60 FPS、完整关卡及退出问题未闭合，补测一轮退出超时。
  证据 `.local/tales-perf-20261005/optimization-report.md`。
  [WU-PERF-10](../tasks/optimization/WU-PERF-10.md)：本阶段收尾，后续优化按用户要求暂停；新增实际成功
  present 计数，2 核基线复验 36.71 FPS，inline exclusive/Looper/PBO 组合 38.06 FPS。
  页表访存/跨块 FPSR 保留及回调保护已定向验证，2 核对话 57.90 FPS，3 核对话
  59.26–60.05 FPS、教学 49.99 FPS（2560×1600、MCP 无 `--diag`）。
  72 项/2166 断言通过，临时取证 hook 已移除，测试进程已关闭；全场景 60 FPS、
  持续移动及退出问题仍未闭合。证据 `.local/tales-perf-20261006/optimization-report.md`。
  [WU-PERF-11](../tasks/optimization/WU-PERF-11.md)：实时视频后台解码/Clock 补时、全屏覆盖
  省读回及 STORED 媒体一次 CRC 校验已闭合。原 2560×1600、默认 1 核路径成功 present
  采样 Amazon 32.15 FPS、Frontier 24.51 FPS（源片 30/24 FPS）；57 项/986 断言及
  两段原 APK 区间 PCM 完整一致、无空读验证通过；用户复测确认两个卡顿问题已解决。
  测试会话已关闭，退出仍为既有
  0x315f0001 读取错；完整影音同步/跨平台/title gate 未验收。证据 `.local/video-playback-20261006/`。
  [WU-PERF-12](../tasks/optimization/WU-PERF-12.md)：Metal 实时双 PBO/fence 与共享 collector
  读回已实现；同步读取/手动步进保持。2560×1600、3 核同场景教学 57.59→60.13 FPS、
  对话 62.02→66.36 FPS，29 项/1244 断言通过。3 项 EGL 旧失败及既有架构违规保留，
  退出/全关卡/跨平台未验收。证据 `.local/tales-async-readback-20261006/`。
  [WU-PERF-13](../tasks/optimization/WU-PERF-13.md)：ARM64 exclusive 读侧版本校验、原子
  reservation 及 128 字节 processor 槽隔离、整数路径保留 FPSR 已定向验证。2560×1600、
  4 核无 diag：对话 93.32 FPS、教学连续窗口 88.25/86.75/85.05 FPS、关卡移动 86.08 FPS；
  源序号差与成功 present 次数差一致，27 项/767 断言通过。初始场景 80 FPS 目标已达，
  同核基线对话 54.70、教学 50.33、移动 51.21 FPS；分析进程已关闭。
  全关卡、跨平台及既有退出问题未验收。
  证据 `.local/tales-fps80-20261006/`。

- **PVZ（com.popcap.pvz_na）**：[DVM-205](../tasks/dexvm/DVM-205.md) 已修复 DEX
  注解预算误拒绝，使用共享解析缓存和分层资源预算。macOS Release 定向回归通过；
  原 APK 无 Profile/无 survey、隔离沙盒完成首帧，进入 TermsActivity 并退出 0。
  prepare 首错消失；后续帧、交互及完整游戏兼容未验收。
  证据：`.local/annotation-budget-analysis/`。

- **Dead Trigger 1.1.0**：
  - [DVM-206](../tasks/dexvm/DVM-206.md) 已补齐 BootDex KeyCharacterMap 与通用虚拟键盘
    native 查询，Java/JNI 共用 API19 映射。Release 构建、BootDex build/check、数据校验及
    双解释器 1662 断言通过。原 APK/OBB、隔离空沙盒进入第一关，W/A/Shift+D/空格
    越过缺类首错，f=3954 遇到下述 JNI 字符串构造错；默认存档 f=6 另触发
    `Dialog(Context)` 评分弹窗缺口。证据：`.local/key-character-map-fix/`。
  - macOS 第 0 帧停滞已修复：GLThread 用空回调交换领取任务，消除
    `std::function` 移动后源对象仍可调用的标准库差异。Release 构建通过；
    现有双解释器 renderer 回归旧版失败、修复后 990 断言通过。
    原 APK/OBB、默认存档、无 Profile/无 survey 推进到 120 帧；退出仍停滞，
    FMOD stop/join 等待 AudioTrack 队列写入，GLThread 已空闲并释放 VM 锁。
    取证后强制结束；本次修复未在 Windows/Linux 实跑。证据：`.local/glthread-portable-fix/`。
  - Windows 已进入第一关并持续出帧；最新隔离沙盒回放在 f=4740 遇到
    `string constructor receiver is not an unbound string instance` 后退出。
    [DVM-204](../tasks/dexvm/DVM-204.md) 的保存/重载已验证：强制结束后同一沙盒
    返回任务地图、开场剧情不再重现、加密进度哈希一致。
    证据：`.local/concurrency-audit/replay.stderr.log`、`replay-gameplay.png`，
    `.local/jni-string-{tests.log,game.stderr.log,reload.png}`。
  - 启动类/native 缺口、同进程 Service、独立 GLThread、Java/NDK 输入与混合 GLES2
    绘制已越过；随机源及 JNI 字符串长度问题已修复。cacheflush 失效与等待期间宿主
    消息泵见 [WU-PERF-08](../tasks/optimization/WU-PERF-08.md)；STREX 已改为原子 CAS，
    旧实现交错回归失败，证据 `.local/accept-stall/`；映射失效、clone 失败收敛及
    kuser ABI 见 [BND-48](../tasks/boundary/BND-48.md)。
  - 当前阻塞为上述评分弹窗/字符串构造首错与退出停滞；完整关卡、画面/音频及 title gate 未验收。

- **Angry Birds 2.3.0**：无 Profile、空沙盒运行 5000 呈现帧，滚动条方法解析错
  消失，未触发新致命首错。滚动条绘制及完整游戏兼容未验收；历史修复见
  [DVM-186](../tasks/dexvm/DVM-186.md)。

## 已交付范围

- **BootDex**：[DVM-198](../tasks/dexvm/DVM-198.md) 从最终 DEX 提取 SQLite/窗口
  资源 ID，映射随 JAR 发布。当前 ROM build/check、载荷校验与定向回归通过；
  多 ROM 尚未完整验收，Windows 构建曾临时屏蔽 C4996/C4834 警告。
- **Android/DexVM**：受审 API 19 BootDex 与 intrinsic 支持游戏直接调用；
  PackageManager 仅查询当前 APK，Manifest 元数据经统一链路传递，文件 IO 经 Libcore Posix/VFS。
- **VFS**：[VFS-01..04](../design/vfs/README.md) 的资源 backing、定位 IO、预算、
  APK/OBB range、媒体 lease、安装实例沙盒与无 Profile external/OBB 挂载已接入并定向验证。
  多实例选择要求显式 `--installation-id`。
- **Windows GUI/Dashboard**：[GUI/DASH](../tasks/gui/README.md) 的库、导入、设置、
  移除、只读诊断与独立窗口已定向验证；机型预设仍为纯数据，未接入运行时。
- **音频**：[DVM-189](../tasks/dexvm/DVM-189.md) 的 worker、MediaPlayer、增量解码与
  AudioTrack 通知已定向验证；Angry Birds 首界面背景音乐已获用户确认。
- **WebView**：[DVM-187](../tasks/dexvm/DVM-187.md) 的视图/设置基础行为与
  [DVM-188](../tasks/dexvm/DVM-188.md) 的默认禁用网页策略已定向验证。

## 其余未闭合边界

- GLSurfaceView EGL 暂停/恢复、context-loss 尚未闭合；NDK Looper callback/非 pipe fd
  不支持，native-attached 参数/反射长尾未验收。首次 GLES1 vertex-array 首错尚未重达复验。
- GC 全周期、普通直接写入的 ABA 历史跟踪、直接访存卸载 quiescence 与退出顺序风险未验收。
- 全 BootDex 类链接检查曾因 MediaPlayer overlay 与过期类数断言失败，尚未整体复验；
  `architecture.dexvm_intrinsic_layout` 因媒体源码未列入清单仍失败，见
  [DVM-190](../tasks/dexvm/DVM-190.md)。
- GUI 的导入→设置→启动→Dashboard→退出→移除全链路验收按用户安排延后；
  GUI 的 Linux/macOS 宿主未完成。移动接口仅预留，暂停握手与真机验收留待后续移植。
- 音频 raw-resource 完整链、手动步进写入闭环、custom AVIO 真实解码与完整游戏音频未验收。
  WebView 页面/JavaScript、TextToSpeech 语音服务未实现。
- TLS-03、完整 mmap/lock、系统 CA 与 Java 长尾未验收；不实现完整 Android framework、
  Binder/system_server、外部包数据库、广播投递或 Play 服务。
