# 当前状态

更新：2026-10-13。

能力见
[capabilities.toml](../../capabilities.toml)，契约见各模块 MODULE；历史及证据明细见
[任务总览](../tasks/README.md)、[DexVM 索引](../tasks/dexvm/README.md)及各任务单。

## 运行状态

- **Wild Blood 1.0.4（SamsungApps）**：Windows 原 APK/外部数据、沙盒副本、无 Profile/survey，
  已到中文菜单及角色教学；暂停/恢复出帧，CG 自然完成/跳过均返回游戏，guestFault=null。
  [DVM-236](../tasks/dexvm/DVM-236.md) 退出定向验证通过，约 0.57–0.70 秒退出 0，无 NPE/超时/强杀；
  “单独点击崩溃”未复现，不作修复结论，Analytics formatDouble 仍被 SDK 捕获。
  macOS [WU-PERF-14](../tasks/optimization/WU-PERF-14.md) 的 CG 首图延迟修复获确认；
  [WU-PERF-15](../tasks/optimization/WU-PERF-15.md) 同路径约 4 秒出图（含约 2 秒原片黑场），
  guestFault=null/退出 0、整段 PCM 与同步基准一致，待用户复测。
  媒体主时钟/完整影音同步、长时/全关卡及跨平台/title gate 未验收，取证进程/listener 已退出。
  证据 `.local/wb-cg-analysis-20261011-003853/`、`.local/wb-cg-buffer-fix-20261011/`、
  `.local/wb-cg-staged-fix-20261011/`。

- **Tales From Deep Space 1.0.0**：macOS 已验证
  [1920×1080 存档重载](../tasks/dexvm/DVM-217.md)，
  [WU-PERF-11](../tasks/optimization/WU-PERF-11.md) 两处视频卡顿获确认解决；
  [WU-PERF-13](../tasks/optimization/WU-PERF-13.md) Release 2560×1600/4 核/无 diag，
  初始场景 80 FPS 目标已达。**阻塞**：[DVM-218](../tasks/dexvm/DVM-218.md) 退出读
  0x315f0001（池先释放、布局后遍历）；800×480/1280×720 菜单裁剪、BND-29 终止断言未闭合。
  全关卡 FPS/影音同步/跨平台/title gate 未验收，分析进程已关闭。
  证据 `.local/tales-fps80-20261006/`、`.local/tales-exit-fix/`。

- **Dead Trigger 1.1.0**：Windows 已到第一关并持续出帧；
  [DVM-204](../tasks/dexvm/DVM-204.md) 同沙盒保存/重载返回任务地图，开场不重复、加密进度哈希一致。
  **阻塞**：隔离沙盒 f=4740 报 `string constructor receiver is not an unbound string instance`；
  默认存档 f=6 另报缺 `Dialog(Context)`。macOS 第 0 帧停滞修复后推进 120 帧，退出仍在
  FMOD stop/join 等待 AudioTrack 写入；该修复 Windows/Linux 未实跑。
  完整关卡/画面/音频/title gate 未验收，取证进程已结束。证据 `.local/concurrency-audit/`、
  `.local/jni-string-{tests.log,game.stderr.log,reload.png}`、`.local/glthread-portable-fix/`。

- **PVZ（com.popcap.pvz_na）**：[DVM-205](../tasks/dexvm/DVM-205.md) 修复注解预算误拒绝；
  macOS 原 APK、无 Profile/survey、隔离沙盒完成首帧，到 TermsActivity 后退出 0。
  后续帧/交互/完整兼容未验收，证据 `.local/annotation-budget-analysis/`。

- **Lili 1.06**：[DVM-247](../tasks/dexvm/DVM-247.md) XML 膨胀及
  [DVM-248](../tasks/dexvm/DVM-248.md)/[249](../tasks/dexvm/DVM-249.md) Surface 层序/开孔、
  普通 MediaPlayer 本地视频已通过双解释器/真实制品/无 AudioTrack 会话验证。
  最新 Windows Release 原 APK、无 Profile/survey：f=160 presented=2，
  实际 Appstore required 提示已核验，外部服务缺席保持真实失败。
  View.bringToFront/ViewGroup.bringChildToFront 已接 UiTree 同父重排；受管生命周期动态挂载的
  SurfaceView 延至下一 traversal 创建，addView 后注册的 holder 回调可收 surfaceCreated（均定向通过）。
  macOS Release 复现：视频 holder created/changed→StopVideo removeView 收 destroyed，视频 Surface 未创建的停滞消失。
  ViewGroup 子节点访问/移除 API 按 API19 对齐（定向通过）。启动图停滞根因为 JNI 构造的 FileDescriptor
  （NDK asset fd）被 MediaPlayer 拒绝→MovieError 级联撤销第二段影片，UE3 停在“影片播放中”不渲染；
  现按 int descriptor 解析 guest VFS fd（定向通过）。macOS Release 复现：两段开场影片 Surface 完整创建/销毁，
  presented 5833+ 进入 3D 场景，guestFault=null；Kiwi Appstore 提示框可见。
  OK 跨帧手势被每帧重复失焦同步取消已修复；点击/Enter/暂停与输入重置双解释器定向通过。
  macOS 原 APK 两次 OK 分别关闭 SDK 排队提示；后续 UE3 输入查询缺失的 MotionEvent `ACTION_*`
  静态常量已按 API19 补齐，双解释器 ARM JNI/输入定向通过；原 APK 点击 Tap to Start 到 START 菜单，
  guestFault=null、MCP shutdown 退出 0。证据 `.local/lili-dialog-fix-20261013/`、
  `.local/lili-motion-constants-fix-20261013/`，不作完整游戏验收。
  通用 Surface 时序修复后 macOS 原 APK 再次完成两段影片和两次 OK，进入 CONTINUE 存档菜单，
  presented=4649、guestFault=null、退出 0；证据 `.local/surface-lifecycle-fix-20261013/`。
  影片画面在 UE3 不 swap 期间应不可见（据宿主仅随 guest 帧合成推断，未实测）；音频/按钮退出未验收。
  证据 `.local/lili-stall-20261012/`、`.local/lili-fd-20261012/`。
  macOS 3D 发白根因已定位：同时发布 ATC/PVRTC 时，UE3 缓存文件名选 ATITC，纹理反序列化选 PVRTC；
  [BND-54](../tasks/boundary/BND-54.md) 已实现通用压缩格式策略，依据原 APK 的 supports-gl-texture 声明选择 ATC，无 Profile；
  macOS Release 原 APK profile=none、family=atc source=manifest，纹理/颜色恢复，presented=3536、guestFault=null、边界 GL errors=0、退出 0。
  查询/上传/Context/Java/Profile 定向通过；Windows/Linux、完整游戏及正式 title gate 未验收。
  根因证据 `.local/lili-white-20261013/`，修复证据 `.local/lili-manifest-texture-20261013/`。
  MCP shutdown 后故障退出 1，进程/listener 已关闭；本次无 `unknown guest thread id`，
  但该历史问题曾重现，未声明修复；SDK 曾捕获 resolveService flags，崩溃报告曾触发 LocaleData 初始化失败。
  证据 `.local/surface-media-fix-20261012/`；前置能力及过程见任务单。

- **Angry Birds 2.3.0**：[DVM-186](../tasks/dexvm/DVM-186.md) 无 Profile/空沙盒运行
  5000 呈现帧，滚动条解析错消失、无新致命首错，背景音乐获确认；绘制/完整兼容未验收。

- **Asphalt 5 1.1.3（SamsungApps）**：[DVM-19](../tasks/dexvm/DVM-19.md) 既有支持发生启动回归：
  `44f9e343` 延迟 Surface traversal 与 intrinsic GLSurfaceView 首次启动顺序冲突，首次 getHolder
  仍 pending、未激活，EGL10 拒绝并报 `12299`（BAD_NATIVE_WINDOW）。现按 API19 构造时登记
  holder、getHolder 只读、遍历完成激活/尺寸发布后开放 renderer；冷启动、动态挂载、回调重入、
  暂停恢复、窗口切换及媒体 Surface 双解释器定向通过。macOS 原 APK 到语言选择界面，
  presented=1096、guestFault=null、MCP shutdown 退出 0；未重跑正式 title gate/全关卡。
  证据 `.local/asphalt5-egl-analysis-20261013/`、`.local/surface-lifecycle-fix-20261013/`。

- **Dungeon Hunter（Samsung P1000，versionCode 355）**：既有安装器→主游戏跳转发生回归；
  `2202a3561` 新增 launchMode 解析及 blanket 拒绝，原 APK 安装器为 singleInstance、主游戏为
  singleTask，GameInstaller.run 中 startActivity 被拒绝。[DVM-250](../tasks/dexvm/DVM-250.md)
  已按 API19 补齐同包有界 task/实例复用及 onNewIntent；双解释器/既有结果、GL/Surface、
  metadata 定向通过。macOS 原 APK/既有 Profile/外部数据、沙盒副本完成安装器→主游戏→
  开场视频→原游戏恢复，呈现 240 帧、退出 0，未出现新的致命首错；跨平台/title gate/全关卡未验收。
  证据 `.local/dungeonhunter-launchmode-analysis-20261013/`、`.local/activity-task-fix-20261013/`。

## 通用底座与保留缺口

- API19 BootDex/intrinsic 按游戏直接调用提供能力，当前 ROM build/check/载荷校验通过；
  PackageManager 仅当前 APK。
  [VFS](../design/vfs/README.md) 的 backing/定位 IO/预算/APK-OBB range/lease/沙盒已定向验证，
  多实例需显式 installation-id；[VFS-07](../tasks/vfs/VFS-07.md) 句柄池 Windows/Linux 通过、macOS
  待原生验证，[VFS-08](../tasks/vfs/VFS-08.md) 支持 O_APPEND/原子 EOF 写。
- [GUI/Dashboard](../tasks/gui/README.md) 库/导入/设置/移除/只读诊断/独立窗口定向通过；
  机型预设仍为数据。导入→设置→启动→Dashboard→退出→移除全链路按用户安排延后，
  Linux/macOS GUI 未完成；移动接口仅预留，暂停握手/真机验收待移植。
- GLSurfaceView 按 View 暂停/恢复和 Context 保留/重建定向通过；实际 context-loss、多窗口/
  透明 Activity/跨平台返回未验收，首次 GLES1 vertex-array 首错未重达。
  NDK Looper callback/非 pipe fd 不支持，native-attached 参数/反射长尾未验收。
- GC 全周期、直接写入 ABA 跟踪、直接访存卸载 quiescence 及其他退出顺序风险未验收。
  全 BootDex 类链接曾因 MediaPlayer overlay/过期类数断言失败，尚未整体复验；多 ROM 未验收。
  两项旧 EGL 回归断言已按错误返回/per-View 契约维护并定向通过；boundary 架构违规及
  `architecture.dexvm_intrinsic_layout` 媒体清单缺项
  未闭合，见 [DVM-190](../tasks/dexvm/DVM-190.md)。
- [DVM-189](../tasks/dexvm/DVM-189.md) 音频 worker/增量解码/MediaPlayer/AudioTrack 通知定向通过；
  raw-resource 完整链、手动音频写入、custom AVIO 真实解码及完整游戏音频未验收。
  [DVM-187](../tasks/dexvm/DVM-187.md)/[188](../tasks/dexvm/DVM-188.md) 仅 WebView 基础配置/回调/
  默认禁用网页策略；页面/JavaScript/TextToSpeech 未实现。
- TLS-03、完整 mmap/lock、系统 CA/Java 长尾未验收。不实现完整 Android framework、
  Binder/system_server、外部包数据库、广播投递或 Play 服务。
