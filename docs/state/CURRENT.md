# 当前状态

更新：2026-10-12。

本页保留最新运行结果、当前阻塞与验收缺口。能力范围见
[capabilities.toml](../../capabilities.toml)，实现契约见各模块 MODULE；历史过程见
[任务总览](../tasks/README.md) 与 [DexVM 索引](../tasks/dexvm/README.md)。

## 运行状态

- **Wild Blood 1.0.4（SamsungApps）**：Windows 原 APK/外部数据、沙盒副本、无 Profile/survey，
  已进入中文菜单，暂停/恢复持续出帧。[DVM-234](../tasks/dexvm/DVM-234.md) 的协作退出/公平轮转
  与 [DVM-235](../tasks/dexvm/DVM-235.md) 的每实例窗口、Surface/GL 往返及中文字幕已定向验证。
  CG 自然完成和提前跳过均返回原游戏，已进入角色操作/教学；上述路径 guestFault=null。
  [DVM-236](../tasks/dexvm/DVM-236.md) 将 Activity.finish 与进程关闭分离，结束进程不合成全栈
  onDestroy 或模块卸载式 DSO fini；owner 取消/join 后退役资源，首错/预算/幂等保持。
  CG 点击后播放中 WM_CLOSE 约 0.570 秒、自然完成后 MCP shutdown 约 0.676 秒退出 **0**，
  无 NPE、清理超时或强杀。原先 nativePause、批量 Game.onDestroy NPE 及这条进程关闭中的
  fini/nanosleep 等待均不再出现；“单独点击导致崩溃”仍未复现，不作已修复结论。
  Analytics formatDouble 缺口仍被 SDK 捕获；长时运行、完整关卡、macOS/Linux/title gate 未验收。
  证据 `.local/wb-cg-analysis-20261011-001405/`、`.local/wb-cg-analysis-20261011-002133/`，
  最终二进制跳过后关闭约 0.695 秒退出 0，证据 `.local/wb-cg-analysis-20261011-003853/`；
  macOS [WU-PERF-14](../tasks/optimization/WU-PERF-14.md) 已修复 1080p CG 预读淘汰到期帧：
  原路径点击国王村后约 4 秒出图（包含约 2 秒原片黑场），guestFault=null，退出 0；
  用户确认画面很快出现。证据 `.local/wb-cg-buffer-fix-20261011/`。
  [WU-PERF-15](../tasks/optimization/WU-PERF-15.md) 已继续分离原生图片与到期 RGBA 转换，
  字节背压及 30 项视频回归通过；同路径仍约 4 秒出图、guestFault=null、退出 0，
  整段 PCM 与同步基准一致。证据 `.local/wb-cg-staged-fix-20261011/`；第二步待用户复测，
  媒体主时钟改造与完整影音同步仍待后续。
  相关进程与 listener 已退出。Windows Release、30 项/2876 断言关闭/生命周期与
  60 项/7439 断言媒体回归通过；独立 permitted fini 的执行、失败及预算回归保持。

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

- **Lili 1.06**：macOS Release 原 APK、无 Profile/无 survey 已越过重复 Activity 的 Manifest
  拒载。通用修复保留声明/过滤器，组件查询及启动取最后配置，alias 封存继承配置；定向回归通过。
  [DVM-237](../tasks/dexvm/DVM-237.md) 已提供通用 JNI/NativeActivity Asset 身份、区间和
  guest FD；独立 ARM/双解释器及相关清理回归通过。[DVM-238](../tasks/dexvm/DVM-238.md)
  闭合 GLES2 OES 与共用映射状态，定向回归通过；原 APK 已加载引擎并进入 onCreate。
  [DVM-239](../tasks/dexvm/DVM-239.md) 接入原版 Debug.MemoryInfo/Creator 与当前进程查询，
  显式虚拟私有提交模型及双解释器/ARM JNI 回归通过；原 APK 越过内存查询。
  [DVM-240](../tasks/dexvm/DVM-240.md) 提供封存单包安装清单、独立 ApplicationInfo 与
  原版 ArrayList；4 用例/926 断言通过，原 APK 越过 getInstalledApplications。
  [DVM-241](../tasks/dexvm/DVM-241.md) 接入原版 NetworkInfo 与显式断开的逻辑 WiFi
  类型；9 用例/2323 断言及 BootDex/载荷校验通过，原 APK 继续推进到 EGL 初始化。
  [DVM-242](../tasks/dexvm/DVM-242.md) 提供 JNI 原始 VM 对象分配；5 用例/170 断言通过，
  原 APK 越过 AllocObject/EGL 参数初始化，进入 PostStartup。
  [DVM-243](../tasks/dexvm/DVM-243.md) 接入原版 StatFs 与统一 VFS 配额卷统计，包含
  SQLite 的 /data 查询；6 用例/192 断言及 BootDex/载荷校验通过，原 APK 越过空间查询。
  [DVM-244](../tasks/dexvm/DVM-244.md) 接通有界资源位图解码与独立 BitmapPixels；
  5 用例/486 断言及 BootDex/载荷校验通过，原 APK 已获得非空启动 Bitmap。
  [DVM-229](../tasks/dexvm/DVM-229.md) 补齐 setImageBitmap 的 API19 Drawable 虚派链；
  双解释器/ARM JNI 与资源 Drawable 回归 3 用例/410 断言通过，原 APK 已越过设置启动图。
  [DVM-245](../tasks/dexvm/DVM-245.md) 接通稳定窗口内容容器与追加/替换、全部子树遍历；
  双解释器/ARM JNI 和相邻窗口/Surface 回归 25 用例/1863 断言通过。最终原 APK 已越过
  addContentView，预算复现日志推进 **f=32**，presented=1、退出 0，无运行期致命首错；
  画面/交互未核验。**保留缺口**：前置复现 f=25 退出后 guest_finalize 报
  `unknown guest thread id`，退出 1；最终单次未复现，不声明已消除。
  前置复现 SDK 另捕获 resolveService flags 未支持；此前崩溃报告触发过 LocaleData 初始化失败。
  [DVM-246](../tasks/dexvm/DVM-246.md) 接通 managed Dialog/AlertDialog 有界模态显示及
  合法服务查询投影标志；22 用例/1864 断言与 BootDex/载荷校验通过。最终 MCP 原 APK
  f=160、guestFault=null、presented=2，已核验实际 Appstore required 提示；服务缺席
  保持真实失败。点击推进后当时首错：视频 Runnable 缺 getLayoutInflater()，dex_pc=12，
  因此按钮→原应用退出未验收。输入依赖的 API19 MotionEvent 轴常量已对照原版补齐。
  [DVM-247](../tasks/dexvm/DVM-247.md) 已接通 LayoutInflater Context/from/clone、独立 XML
  构建、父类型参数与追加/merge；25 用例/1518 断言、BootDex/载荷校验通过。最终原 APK
  MCP f=168 已越过 getLayoutInflater/inflate/addContentView，**最新首错**为
  SurfaceView.setZOrderMediaOverlay(Z)V，视频 Runnable dex_pc=153；完整视频/按钮退出未验收。
  快速推进后未触达视频的前置复现，关闭时再次出现既有 unknown guest thread id；不声明
  已修复。最终 reached-fault 会话 MCP shutdown 后退出 1、listener 关闭；所有 OGPlay
  进程已关闭。证据 `.local/layout-inflater-fix-20261012/`。
  证据 `.local/manifest-duplicate-fix-20261011/`、
  `.local/ndk-assets-fix-20261011/`、`.local/gles-mapbuffer-fix-20261011/`、
  `.local/debug-memory-fix-20261011/`、`.local/installed-applications-fix-20261011/`、
  `.local/connectivity-fix-20261011/`、`.local/alloc-object-fix-20261011/`、
  `.local/statfs-fix-20261011/`、`.local/bitmap-resource-fix-20261011/`、
  `.local/image-bitmap-fix-20261011/`、`.local/window-content-fix-20261011/`、
  `.local/dialog-fix-20261011/`；取证进程已退出。

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
