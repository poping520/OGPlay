# 当前状态

更新：2026-10-10。

本页保留最新运行结果、当前阻塞与验收缺口。能力范围见
[capabilities.toml](../../capabilities.toml)，实现契约见各模块 MODULE；历史过程见
[任务总览](../tasks/README.md) 与 [DexVM 索引](../tasks/dexvm/README.md)。

## 运行状态

- **Wild Blood 1.0.4（SamsungApps）**：Windows 原 APK/外部数据、用户沙盒副本、
  无 Profile/无 survey，已进入中文菜单并验证暂停恢复持续出帧。
  [DVM-234](../tasks/dexvm/DVM-234.md) 的有界协作退出、Java 等待预算、公平轮转、
  root 宿主绑定与异常分派 context 保持已完成；Windows Release、25 项/1830 断言通过。
  直接关窗及暂停恢复后关窗分别约 0.547/0.563 秒退出 1，无超时/强杀；原 nativePause
  预算耗尽与 Surface 等待消失。**当前首错**：第二个保留 Game 实例 onDestroy 对
  静态 m_sInstance 的空接收者 NPE。Analytics 的 formatDouble 缺口仍被 SDK 捕获。
  正常退出、长时间暂停、完整关卡及最新改动的 macOS/Linux/title gate 未验收。
  证据 `.local/dvm234-close-20261010-131033/`、`.local/dvm234-pause-20261010-130750/`；
  进程与 listener 已退出。先前启动缺口与 append 修复见 DexVM/VFS 任务记录。
  [DVM-235](../tasks/dexvm/DVM-235.md) 已实现每实例窗口、Surface/GL 归属与可逆暂停。
  国王村 CG 实际播放并显示中文字幕；自然结束与提前跳过均返回原游戏，
  自然结束后已进入角色操作/教学画面，无 guest fault。两条路径关窗无清理超时，
  仍因上述独立 onDestroy NPE 退出 1。失败驱动收尾曾出现的 native finalizer
  `nanosleep` 超时根因未闭合，不能用正常播放后的退出替代其验收。
  证据 `.local/wb-cg-analysis-20261010-231301/`、`.local/wb-cg-analysis-20261010-232049/`。
  最终二进制播放中 MCP shutdown 约 0.608 秒退出 1，无超时/强杀；证据
  `.local/wb-cg-analysis-20261010-232641/`，进程与 listener 已退出。
  Windows Release、36 项/3004 断言定向及 UI 31 项/202 断言通过；BootDex/载荷校验通过。

- **Tales From Deep Space 1.0.0**：macOS 已验证
  [1920×1080 存档重载恢复关卡](../tasks/dexvm/DVM-217.md)；
  [视频后台解码/Clock 补时](../tasks/optimization/WU-PERF-11.md) 的两处卡顿获用户确认解决。
  [WU-PERF-13](../tasks/optimization/WU-PERF-13.md)：Release 2560×1600、4 核、无 diag，
  对话 93.32 FPS、教学 85.05–88.25 FPS、移动 86.08 FPS，初始场景 80 FPS 目标已达；
  27 项/767 断言通过。**当前阻塞**：退出仍读 0x315f0001，池先释放、布局后遍历，
  见 [DVM-218](../tasks/dexvm/DVM-218.md)。800×480/1280×720 菜单裁剪与 BND-29
  终止断言仍未闭合；全关卡 FPS、完整影音同步、跨平台/title gate 未验收。
  证据 `.local/tales-fps80-20261006/`、`.local/tales-exit-fix/`；分析进程已关闭。

- **Dead Trigger 1.1.0**：Windows 已进入第一关并持续出帧；
  [DVM-204](../tasks/dexvm/DVM-204.md) 已验证同沙盒保存/重载返回任务地图，
  开场剧情不重复、加密进度哈希一致。**当前阻塞**：隔离沙盒 f=4740 报
  `string constructor receiver is not an unbound string instance`；默认存档 f=6
  另缺 `Dialog(Context)` 评分弹窗。macOS 第 0 帧停滞已修复并推进 120 帧，退出仍在
  FMOD stop/join 等待 AudioTrack 队列写入；该修复 Windows/Linux 未实跑。
  证据 `.local/concurrency-audit/`、`.local/jni-string-{tests.log,game.stderr.log,reload.png}`、
  `.local/glthread-portable-fix/`。取证进程已结束；完整关卡、画面/音频及 title gate 未验收。

- **PVZ（com.popcap.pvz_na）**：[DVM-205](../tasks/dexvm/DVM-205.md) 修复注解预算误拒绝。
  macOS 原 APK、无 Profile/无 survey、隔离沙盒完成首帧，进入 TermsActivity 并退出 0；
  后续帧、交互及完整兼容未验收。证据 `.local/annotation-budget-analysis/`。

- **Angry Birds 2.3.0**：无 Profile、空沙盒运行 5000 呈现帧，滚动条方法解析错消失，
  未触发新致命首错；绘制与完整兼容未验收。见 [DVM-186](../tasks/dexvm/DVM-186.md)。
  首界面背景音乐已获用户确认。

## 已交付范围

- **BootDex/Android**：受审 API19 BootDex 与 intrinsic 支持游戏直接调用；当前 ROM
  build/check、载荷校验通过。PackageManager 仅查询当前 APK，Manifest 元数据统一传递。
  [DVM-198](../tasks/dexvm/DVM-198.md) 从最终 DEX 提取 SQLite/窗口资源 ID，映射随 JAR 发布。
- **VFS**：[VFS-01..04](../design/vfs/README.md) 的资源 backing、定位 IO、预算、APK/OBB
  range、媒体 lease、安装实例沙盒与无 Profile 挂载已定向验证；多实例需显式 installation-id。
  [VFS-07](../tasks/vfs/VFS-07.md) 宿主句柄池 Windows/Linux 已验证，macOS 待原生验证；
  [VFS-08](../tasks/vfs/VFS-08.md) 已支持通用 O_APPEND 与原子 EOF 写。
- **GUI/Dashboard**：[GUI/DASH](../tasks/gui/README.md) 的库、导入、设置、移除、只读诊断与
  独立窗口已定向验证；机型预设仍为纯数据，未接入运行时。
- **音频/WebView**：[DVM-189](../tasks/dexvm/DVM-189.md) 的 worker、MediaPlayer、增量解码与
  AudioTrack 通知已定向验证；[DVM-187](../tasks/dexvm/DVM-187.md)/
  [188](../tasks/dexvm/DVM-188.md) 提供 WebView 基础配置、回调与默认禁用网页策略。

## 其余未闭合边界

- GLSurfaceView 按 View 暂停/恢复与 Context 保留/重建已定向验证；实际 backend context-loss、
  多窗口/透明 Activity 与跨平台返回未验收。首次 GLES1 vertex-array 首错
  未重达复验。NDK Looper callback/非 pipe fd 不支持，native-attached 参数/反射长尾未验收。
- GC 全周期、普通直接写入的 ABA 历史跟踪、直接访存卸载 quiescence 与其他退出顺序风险未验收。
- 全 BootDex 类链接检查曾因 MediaPlayer overlay/过期类数断言失败，尚未整体复验；多 ROM
  未完整验收。EGL 旧回归及 boundary 架构违规仍未闭合；
  `architecture.dexvm_intrinsic_layout` 媒体清单缺项见 [DVM-190](../tasks/dexvm/DVM-190.md)。
- GUI 导入→设置→启动→Dashboard→退出→移除全链路按用户安排延后；Linux/macOS GUI 未完成。
  移动接口仅预留，暂停握手与真机验收留待后续移植。
- 音频 raw-resource 完整链、手动步进写入闭环、custom AVIO 真实解码与完整游戏音频未验收；
  WebView 页面/JavaScript、TextToSpeech 未实现。
- TLS-03、完整 mmap/lock、系统 CA 与 Java 长尾未验收；不实现完整 Android framework、
  Binder/system_server、外部包数据库、广播投递或 Play 服务。
