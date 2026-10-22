# 当前状态

更新：2026-10-21。

本文件是项目当前状态的快照：只写“现在怎样”，不写过程。修改时覆盖对应行，不追加流水；
修复过程、验证明细及证据写入任务单。能力见 [capabilities.toml](../../capabilities.toml)，
契约见各模块 MODULE，任务见 [任务总览](../tasks/README.md)。上限 6 KB，由
`architecture.documentation_layout` 检查。

## 游戏进度

“到达点”指最近一次原 APK 实测；以下游戏均未通过正式 title gate、全关卡或跨平台验收。

| 游戏 | 平台 | 到达点 | 阻塞 / 待办 | 任务 |
| --- | --- | --- | --- | --- |
| TML 1.0 | macOS | 标题；三点击 survey 零命中；基础场景退出 0 | 正式交互/稳定性待验 | [LUI-18](../tasks/layoutui/LUI-18.md)、[DVM-257](../tasks/dexvm/DVM-257.md) |
| Lili 1.06 | macOS | 两段开场影片、CONTINUE 存档菜单，纹理正常 | 音频、按钮退出未验收 | [DVM-247](../tasks/dexvm/DVM-247.md)..249、[BND-54](../tasks/boundary/BND-54.md) |
| Wild Blood 1.0.4 | Win / macOS | 中文菜单/教学；macOS 基础场景 1200 帧退出 0 | GAThread `ContentValues$1.<init>` 未实现；CG/Win 待验 | [DVM-257](../tasks/dexvm/DVM-257.md) |
| Tales From Deep Space 1.0.0 | macOS | 1920×1080 存档重载，初始场景 80 FPS | **退出读 0x315f0001**；800×480/1280×720 菜单裁剪 | [DVM-218](../tasks/dexvm/DVM-218.md)、[BND-29](../tasks/boundary/BND-29.md) |
| Dead Trigger 1.1.0 | Win / macOS | Win 第一关/存档重载；macOS 场景 1200 帧 | macOS 退出 native futex 超 2 秒预算；隔离 f=4740 receiver 错、默认存档缺 `Dialog(Context)` | [DVM-204](../tasks/dexvm/DVM-204.md) |
| Dungeon Hunter（P1000） | macOS | 无 Profile、用户挂载；1024×600 呈现 900 帧，CLI/GUI 退出 0 | 默认 800×480 恢复游戏后 nativeRender 读 0（PC 0x30885b08）；未改默认尺寸 | [VFS-09](../tasks/vfs/VFS-09.md)、[DVM-251](../tasks/dexvm/DVM-251.md) |
| Asphalt 5 1.1.3 | macOS | 语言选择界面 | 无 Profile 场景 600 帧退出 0；交互未验 | [DVM-19](../tasks/dexvm/DVM-19.md)、[WU5](../tasks/diagnostics/DIAG-6.md) |
| PVZ | macOS | TermsActivity 首帧 | 退出 onDestroy 的 `stopService` 未实现；交互未验 | [DVM-205](../tasks/dexvm/DVM-205.md) |
| Angry Birds 2.3.0 | macOS | 5000 呈现帧，退出 clean | onDestroy 拒绝已修；无下一失败，画面未验收 | [DVM-186](../tasks/dexvm/DVM-186.md)、[WU7-1a](../tasks/diagnostics/DIAG-7.md) |

## 未闭合问题

- 退出顺序：除上表外，GC 全周期、直接写入 ABA、直接访存卸载 quiescence 未验收；
  Java-only 线程误交 clone join 已修，MCP shutdown 稳定性待验。
- 门禁：boundary 架构违规与 `architecture.dexvm_intrinsic_layout` 媒体清单缺项未闭合
  （[DVM-190](../tasks/dexvm/DVM-190.md)）。[WU7-T](../tasks/diagnostics/DIAG-7.md) 已清理 HEAD
  测试失败：ogplay_tests 1903/1903、ogplay_app_tests 89/89 通过，0 失败，保留既有 7 skip。
- 一致性：dalvik/tests 双后端 84/91（92.3%）；035/046 通过，085 oracle 排除；070 缺共享数组，见 [DIAG-7](../tasks/diagnostics/DIAG-7.md)；JNI 双后端 359/399（90.0%，WU8-2b：JNI/dalvik 零回归、基础场景 6/9 无新增失败），静态覆盖 233/233（100%），见 [DIAG-8](../tasks/diagnostics/DIAG-8.md)。
- `--ephemeral-sandbox` 下 Dungeon Hunter 约 106 帧抛 `Invalid path: /sdcard`；持久沙盒不复现。
- 跨平台：近期修复多只在 macOS 实跑；[VFS-07](../tasks/vfs/VFS-07.md) macOS 待验证。

## 支持边界与未验收项

- Profile（[APS-15](../tasks/apk-startup/APS-15.md)）：仅接受 v4 identity/quirks；API 19、入口、ABI、VFS 与音频走通用路径。Angry Birds 无 Profile 可启动并退出 0。

- VFS：语义根删除返回 EBUSY、缺失自动重建并记 warn；37 定向用例/436 断言通过，TML 现有 `-3` 持久沙盒自动恢复 obb，连续两次启动越过打开阶段、根保留且既有文件哈希不变，同达 Mono sigaction / Unity EGL 首错（证据 `.local/sandbox-roots-{tests.log,proof.json}`）。多实例需显式 installation-id；PackageManager 仅当前 APK；多 ROM 未验收。
- GUI/Dashboard（[GUI-10](../tasks/gui/GUI-10.md)）：core 改为 C++ 外观与共享游戏库，桌面/CLI
  各自拥有交互；库锁/运行锁、CLI library、参数往返及两条真实 APK 有界路径已在 macOS 验证。
  Windows Release/原生 GUI 链路待复验（GUI-9 证据属旧架构）。macOS 交付 ad-hoc 签名 OGPlay.app
  （[GUI-11](../tasks/gui/GUI-11.md)），冒烟/包/游戏模式与真实导入→启动→移除流程已验；Linux GUI 未接入。
  数据包导入默认复制入库（[GUI-12](../tasks/gui/GUI-12.md)，APFS 克隆），GUI 向导点击未实测；
  游戏退出后按运行时缺失路径提示挂载位置（[GUI-13](../tasks/gui/GUI-13.md)），App 内点击未实测。
- 图形：实际 context-loss、多窗口/透明 Activity 未验收；GLES1 vertex-array 首错未重达。
  SurfaceTexture 公共队列/Java 与 SwiftShader 合成帧外部采样闭环已验；Metal 无 external，
  MediaPlayer/NDK producer 与启动后端选择未接入（[APS-14](../tasks/apk-startup/APS-14.md)）。
- 音视频（[DVM-189](../tasks/dexvm/DVM-189.md)）：视频改为链接裁剪版 ogplay_ffmpeg
  （[VID-1](../tasks/video/VID-1.md)），macOS arm64 真实 CG 实测正常；预编译包已含 macOS arm64 与 Windows x64，Windows CG 未验；
  raw-resource 完整链、手动音频写入、媒体主时钟与影音同步未验收。
- 输入/显示（[INP-8](../tasks/input/INP-8.md)、ADR-0142）：仅内置直通/Esc + 用户 input.toml，删除 Profile 输入与类型模板。显示宽高（默认 800×480、1..4096）和超采样经游戏 > 全局 > 默认解析；Angry Birds 1024×600 启动、F2 创建 J 键保存、重启加载及 F1 提示已实测，两次退出 0。旧 template/input_template 明确失败、不迁移；Windows/Linux 未实跑。
- WebView 仅基础配置与回调（[DVM-187](../tasks/dexvm/DVM-187.md)/[188](../tasks/dexvm/DVM-188.md)），
  页面、JavaScript、TextToSpeech 未实现。
- 不支持 NDK Looper callback 与非 pipe fd；TLS-03、完整 mmap/lock、系统 CA 未验收。
