# 当前状态

更新：2026-11-10。

本文件是项目当前状态的快照：只写“现在怎样”，不写过程。修改时覆盖对应行，不追加流水；
修复过程、验证明细及证据写入任务单。能力见 [capabilities.toml](../../capabilities.toml)，
契约见各模块 MODULE，任务见 [任务总览](../tasks/README.md)。

JavaTraversal默认；九场景与Legacy持平（DVM-331，仅启动路径）。无返回值的显示设置忽略并记警告（DVM-372）。
启动类路径改为 ogplay.jar + 原样 API19 ROM jar（core/conscrypt/ext/framework/framework2/android.policy），成员来源审计与配对制品已删除；未绑定 native 调用时抛 UnsatisfiedLinkError。

## 游戏进度

“到达点”指最近一次原 APK 实测；以下游戏均未通过正式 title gate、全关卡或跨平台验收。

| 游戏 | 平台 | 到达点 | 阻塞 / 待办 | 任务 |
| --- | --- | --- | --- | --- |
| Island Castaway 1.3 | macOS | CG 带字幕，PLAY 进入第一关；菜单/第一关及按钮音效已验证非零 PCM 提交 SDL | OpenSL 必需 AndroidConfiguration 缺失致无声已修；SurfaceView 默认格式误用 alpha 致黑块闪烁已修，连续 48 帧检查通过；原生磁盘空间弹窗、退出 `runFinalizersOnExit` 仍在 | [BND-56](../tasks/boundary/BND-56.md)、[SBX-16](../tasks/sandbox/SBX-16.md) |
| TML 1.0 | macOS | 标题；三点击 survey 零命中；基础场景退出 0 | 正式交互/稳定性待验 | [LUI-18](../tasks/layoutui/LUI-18.md)、[DVM-257](../tasks/dexvm/DVM-257.md) |
| Lili 1.06 | macOS | 两段开场影片、CONTINUE 存档菜单，纹理正常 | 音频、按钮退出未验收 | [DVM-247](../tasks/dexvm/DVM-247.md)..249、[BND-54](../tasks/boundary/BND-54.md) |
| Wild Blood 1.0.4 | Win / macOS | 中文菜单/教学；macOS 安装器 Large ProgressBar 已封存，原命令 120 秒 5550 帧退出 0 | 进关 MyVideoView 的 setText(NORMAL) 崩溃已修（DVM-372），手测正常；GAThread `ContentValues$1.<init>` 未实现；CG/Win 待验 | [DVM-257](../tasks/dexvm/DVM-257.md)、[DVM-372](../tasks/dexvm/DVM-372.md) |
| Tales From Deep Space 1.0.0 | macOS | 1920×1080 存档重载，初始场景 80 FPS；2560×1600 菜单爆音已修，20 秒输出与 guest 音乐 PCM 逐样本一致 | **退出读 0x315f0001**；偶发启动黑屏；800×480/1280×720 菜单裁剪 | [DVM-218](../tasks/dexvm/DVM-218.md)、[BND-29](../tasks/boundary/BND-29.md)、[DVM-373](../tasks/dexvm/DVM-373.md) |
| Dead Trigger 1.1.0 | Win / macOS | Win 第一关/存档重载；macOS 场景 1200 帧 | macOS 退出 native futex 超 2 秒预算；隔离 f=4740 receiver 错、默认存档缺 `Dialog(Context)` | [DVM-204](../tasks/dexvm/DVM-204.md) |
| Dungeon Hunter（P1000） | macOS | 1024×600：CG → 标题 → 建档/选职业 → 第一关可移动攻击，SIGTERM 退出 0 | 精灵/光晕白块由 VFS 拒绝纹理父目录路径导致，已修，第一关 MCP 复核；800×480 CG 后 nativeRender 读 0 | [VFS-09](../tasks/vfs/VFS-09.md)、[VFS-11](../tasks/vfs/VFS-11.md)、[DVM-251](../tasks/dexvm/DVM-251.md) |
| Asphalt 6 1.3.2 | macOS | CG → 中文主菜单 → 自由竞速拿索赛道，车辆/HUD/暂停交互正常，SIGINT 退出 0 | 混合链接 glGetString 错报 ES1、JNI 音频回调关闭取消误报已修；全赛程/跨平台待验 | [BND-55](../tasks/boundary/BND-55.md) |
| Asphalt 5 1.1.3 | macOS | 语言选择界面 | 无 Profile 场景 600 帧退出 0；交互未验 | [DVM-19](../tasks/dexvm/DVM-19.md)、[WU5](../tasks/diagnostics/DIAG-6.md) |
| PVZ | macOS | TermsActivity 首帧 | 退出 onDestroy 的 `stopService` 未实现；交互未验 | [DVM-205](../tasks/dexvm/DVM-205.md) |
| Angry Birds 2.3.0 | macOS | 5000 呈现帧，退出 clean | onDestroy 拒绝已修；换卡 WebView 的 setScrollBarStyle/动画缺口已修（DVM-371），手测正常；JIT 整缓存提交与 APK 多副本已修，footprint 932→502 MB（WU-PERF-16）；画面未验收 | [DVM-186](../tasks/dexvm/DVM-186.md)、[DVM-371](../tasks/dexvm/DVM-371.md)、[WU-PERF-16](../tasks/optimization/WU-PERF-16.md)、[WU7-1a](../tasks/diagnostics/DIAG-7.md) |
| GTA3 | macOS | 首次启动 EULA 长消息模态已发布（1280×720、320dpi），原容纳异常消失 | 20 秒有界运行未见新 uncaught 异常，SIGINT 退出 0；EULA 手测正常；影片画面与 NV clock 提示仍待验 | [DVM-370](../tasks/dexvm/DVM-370.md)、[DVM-363](../tasks/dexvm/DVM-363.md) |
| GTASA | macOS | OFFLINE → 标题菜单 → Start Game 进入开场过场，约 2 分钟无 fault，SIGINT 退出 0 | OpenSL 播放器 BUFFERQUEUE/ANDROIDSIMPLEBUFFERQUEUE 未互为别名致 OpenAL 启动失败、随后堆损坏，已修；登录界面图片（guest onDraw）未绘制，游戏内交互未验 | [DVM-366](../tasks/dexvm/DVM-366.md) |
| Monument Valley 2.5.18 | macOS | 720×1280：序章曲柄/行走至终点，进入第二章 The Garden，非零 PCM 已确认 | 默认入口竖屏、触摸复制及长期工作 Looper 已修；退出 IAP 未 bind 却 unbind 抛 IAE，正式 title gate/跨平台未验 | [DVM-369](../tasks/dexvm/DVM-369.md)、[DVM-259](../tasks/dexvm/DVM-259.md) |
| NFS17 | macOS | 下载布局构造通过，进入 GameActivityMain.onCreate | `Window.getWindowManager()` 未实现；尚未进入游戏 | [DVM-367](../tasks/dexvm/DVM-367.md)、[DVM-368](../tasks/dexvm/DVM-368.md) |
| Bad Piggies HD 1.9.1 | macOS | Surface 回调后 `libRCSSDK.so` 加载及 JNI_OnLoad 完成，已越过 `0xec510f1e` 探测 | `f=3951` 时 UnityMain `UnityPlayer.nativeRender()Z` 报 `JNI object is not a string in this store`，本次未出现此前 FMOD 退出 | [DVM-374](../tasks/dexvm/DVM-374.md)、[BND-57](../tasks/boundary/BND-57.md) |
| Sacred Odyssey 1.1.0 | macOS | 800×480：Logo 可见，Release 主菜单实测 24.3 FPS；NEW GAME → Mission 开场对白/Skip，MCP 关闭退出 0 | 非全屏视频黑屏、进关日志非 UTF-8 崩溃已修；开场 3D 区域仍黑；1280×720 Splash 的 `nativeRender` 读 `0x0`；游戏内操作待验 | [DVM-375](../tasks/dexvm/DVM-375.md)、[DVM-353](../tasks/dexvm/DVM-353.md)、[DVM-303](../tasks/dexvm/DVM-303.md)、[BND-5](../tasks/boundary/BND-5.md) |
| 水果忍者 1.8.8 | macOS | 第 110 帧越过原版 `View.isInEditMode` 调用 | 地图手势初始化：ROM `VelocityTracker` 已加载，`nativeInitialize` 未实现 | — |
| 割绳子 2.3.8（广告版） | macOS | ACRA 构造与 Application 生命周期回调注册通过，进入首个 Activity 的 onCreate，未呈现 | FragmentActivity 调用 `getLastNonConfigurationInstance()` 缺失 | [DVM-376](../tasks/dexvm/DVM-376.md) |
| 保卫萝卜 1.1.0 | macOS | Cocos2d 文字位图越过 `Paint.getFontMetricsInt()`，呈现 2 帧 | `nativeRender` 的 JNI `CallStaticObjectMethodV` 返回类型与描述符不符 | [DVM-376](../tasks/dexvm/DVM-376.md) |
| 鳄鱼小顽皮 1.13.1 | macOS | 越过 `ThreadGroup(String)` 和离线 `WifiInfo` 查询，到达 `onResume`，未呈现 | `AlarmManager.setRepeating` 调度未实现 | [DVM-211](../tasks/dexvm/DVM-211.md) |

## 未闭合问题

- [DVM-260](../tasks/dexvm/DVM-260.md) 五阶段 macOS 已验收；数组/String/NIO 共用稳定 guest payload，JNI 双后端 399/399、dalvik 85/91、九场景 6/9，耗时/帧时间门禁通过；Windows 未验证。

- 退出顺序：除上表外，GC 全周期、直接写入 ABA、直接访存卸载 quiescence 未验收；
  Java-only 线程误交 clone join 已修，MCP shutdown 稳定性待验。
- 门禁：[WU7-T](../tasks/diagnostics/DIAG-7.md) 已清理 HEAD
  测试失败，但现已不再全绿：受影响主题 672 用例中 ogplay_tests 仍有 31 个失败/崩溃、app 测试 DIAG-4 失败，9ab2ccd0 上同样存在，见 [DVM-369](../tasks/dexvm/DVM-369.md)；未跑全量。
- 一致性：dalvik/tests 双后端 86/91（94.5%，044-proxy 已通过，见 DVM-259/369）；035/046/070 通过，085 oracle 排除；070 共享数组已修复，见 [DIAG-7](../tasks/diagnostics/DIAG-7.md)；JNI 双后端 399/399（100%，DVM-260 / ADR-0152、0153：pending 按 Dalvik 非 CheckJNI 放行、heap capacity 对齐 4.4；JNI/dalvik 零回归、基础场景 6/9 无新增失败），静态覆盖 233/233（100%），见 [DIAG-8](../tasks/diagnostics/DIAG-8.md)。 Framework A–D=0/521/173/13/2、E=27/0/0；八类撤换，见 [DVM-268](../tasks/dexvm/DVM-268.md)。 BootDex父类准入，场景待外验。
- `--ephemeral-sandbox` 下 Dungeon Hunter 约 106 帧抛 `Invalid path: /sdcard`；持久沙盒不复现。
- 跨平台：近期修复多只在 macOS 实跑；[VFS-07](../tasks/vfs/VFS-07.md) macOS 待验证。

## 支持边界与未验收项

- Profile（[APS-15](../tasks/apk-startup/APS-15.md)）：仅接受 v4 identity；API 19、入口、ABI、VFS 与音频走通用路径。Angry Birds 无 Profile 可启动并退出 0。

- VFS：语义根删除返回 EBUSY、缺失自动重建并记 warn；37 定向用例/436 断言通过，TML 现有 `-3` 持久沙盒自动恢复 obb，连续两次启动越过打开阶段、根保留且既有文件哈希不变，同达 Mono sigaction / Unity EGL 首错（证据 `.local/sandbox-roots-{tests.log,proof.json}`）。多实例需显式 installation-id；PackageManager 仅当前 APK；多 ROM 未验收。
- GUI/Dashboard（[GUI-10](../tasks/gui/GUI-10.md)）：core 改为 C++ 外观与共享游戏库，桌面/CLI
  各自拥有交互；库锁/运行锁、CLI library、参数往返及两条真实 APK 有界路径已在 macOS 验证。
  Windows Release/原生 GUI 链路待复验（GUI-9 证据属旧架构）。macOS 交付 ad-hoc 签名 OGPlay.app
  （[GUI-11](../tasks/gui/GUI-11.md)），冒烟/包/游戏模式与真实导入→启动→移除流程已验；Linux GUI 未接入。
  数据包导入默认复制入库（[GUI-12](../tasks/gui/GUI-12.md)，APFS 克隆），GUI 向导点击未实测；
  游戏退出后按运行时缺失路径提示挂载位置（[GUI-13](../tasks/gui/GUI-13.md)），App 内点击未实测。
  每次库启动记录运行结果（last-run.json），失败游戏归入“待排查”，CLI `library list --filter attention` 同源（[GUI-23](../tasks/gui/GUI-23.md)），App 内点击未实测。
  Windows 启动器单例与托盘/最小化恢复已通过 Debug / Release 专项实测（[GUI-21](../tasks/gui/GUI-21.md)），Release 已重建。
- 图形：P1 范围内的 ROM 图形、软件 Canvas、效果、NinePatch 与 Camera 已接通 Skia。整形与双向重排仍降级；WebP 编码、Parcel blob、排除的图形能力及 P3 UI 投影清理仍有缺口。macOS arm64 已验证，x64 仅交叉链接，Windows/Android 未实跑。实际 context-loss、多窗口/透明 Activity 未验收；GLES1 vertex-array 首错未重达。
  SurfaceTexture 公共队列/Java 与 SwiftShader 合成帧外部采样闭环已验；Metal 无 external，
  MediaPlayer/NDK producer 与启动后端选择未接入（[APS-14](../tasks/apk-startup/APS-14.md)）。
- 音视频（[DVM-189](../tasks/dexvm/DVM-189.md)）：视频改为链接裁剪版 ogplay_ffmpeg
  （[VID-1](../tasks/video/VID-1.md)），macOS arm64 真实 CG 实测正常；预编译包已含 macOS arm64 与 Windows x64，Windows CG 未验；
  raw-resource 完整链、手动音频写入、媒体主时钟与影音同步未验收。
- 输入/显示（[INP-8](../tasks/input/INP-8.md)、ADR-0142）：仅内置直通/Esc + 用户 input.toml，删除 Profile 输入与类型模板。显示宽高（默认 1280×720、1..4096）、密度（默认 320 dpi、72..1280，写入 guest DisplayMetrics）和超采样经游戏 > 全局 > 默认解析；Angry Birds 720p/480p 活跃帧耗时中位数 5.29/1.83→1.30/0.78 ms，macOS 各 4000 帧退出 0（[WU-PERF-17](../tasks/optimization/WU-PERF-17.md)），basic 场景待复验；Angry Birds 1024×600 启动、F2 创建 J 键保存、重启加载及 F1 提示已实测，两次退出 0。input.toml 未知键（含旧 template）读取时跳过；滚轮与触控板捏合内置合成双指捏合（[INP-9](../tasks/input/INP-9.md)），Angry Birds 滚轮与 macOS 触控板缩放已手测；FPS 准星 look/相对鼠标（[INP-10](../tasks/input/INP-10.md)）待实机验；Windows/Linux 未实跑。
- Android（[AND-1](../tasks/android/AND-1.md)）：一加 7 Pro（Adreno 640，ANGLE 仅 ES 2.0）上 Demo 标题画面呈现、触摸进入选关；退出、切后台与屏幕方向未验。
- WebView 仅基础配置与回调（[DVM-187](../tasks/dexvm/DVM-187.md)/[188](../tasks/dexvm/DVM-188.md)），
  页面、JavaScript、TextToSpeech 未实现。
- 不支持 NDK Looper callback 与非 pipe fd；TLS-03、完整 mmap/lock、系统 CA 未验收。
