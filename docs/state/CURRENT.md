# 当前状态

更新：2026-10-12。

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
  **最新首错**：视频 Runnable 已越过 setZOrderMediaOverlay，停于
  `TextView.bringToFront()V`，dex_pc=226；完整视频/按钮退出未验收。
  MCP shutdown 后故障退出 1，进程/listener 已关闭；本次无 `unknown guest thread id`，
  但该历史问题曾重现，未声明修复；SDK 曾捕获 resolveService flags，崩溃报告曾触发 LocaleData 初始化失败。
  证据 `.local/surface-media-fix-20261012/`；前置能力及过程见任务单。

- **Angry Birds 2.3.0**：[DVM-186](../tasks/dexvm/DVM-186.md) 无 Profile/空沙盒运行
  5000 呈现帧，滚动条解析错消失、无新致命首错，背景音乐获确认；绘制/完整兼容未验收。

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
  两项旧 EGL 回归仍失败，boundary 架构违规及 `architecture.dexvm_intrinsic_layout` 媒体清单缺项
  未闭合，见 [DVM-190](../tasks/dexvm/DVM-190.md)。
- [DVM-189](../tasks/dexvm/DVM-189.md) 音频 worker/增量解码/MediaPlayer/AudioTrack 通知定向通过；
  raw-resource 完整链、手动音频写入、custom AVIO 真实解码及完整游戏音频未验收。
  [DVM-187](../tasks/dexvm/DVM-187.md)/[188](../tasks/dexvm/DVM-188.md) 仅 WebView 基础配置/回调/
  默认禁用网页策略；页面/JavaScript/TextToSpeech 未实现。
- TLS-03、完整 mmap/lock、系统 CA/Java 长尾未验收。不实现完整 Android framework、
  Binder/system_server、外部包数据库、广播投递或 Play 服务。
