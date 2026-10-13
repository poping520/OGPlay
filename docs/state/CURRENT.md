# 当前状态

更新：2026-10-13。

本文件是项目当前状态的快照：只写“现在怎样”，不写过程。修改时覆盖对应行，不追加流水；
修复过程、验证明细及证据写入任务单。能力见 [capabilities.toml](../../capabilities.toml)，
契约见各模块 MODULE，任务见 [任务总览](../tasks/README.md)。上限 6 KB，由
`architecture.documentation_layout` 检查。

## 游戏进度

“到达点”指最近一次原 APK 实测；以下游戏均未通过正式 title gate、全关卡或跨平台验收。

| 游戏 | 平台 | 到达点 | 阻塞 / 待办 | 任务 |
| --- | --- | --- | --- | --- |
| Lili 1.06 | macOS | 两段开场影片、CONTINUE 存档菜单，纹理正常 | 音频、按钮退出未验收 | [DVM-247](../tasks/dexvm/DVM-247.md)..249、[BND-54](../tasks/boundary/BND-54.md) |
| Wild Blood 1.0.4 | Win / macOS | 中文菜单、角色教学，CG 完成/跳过均返回 | CG 首图约 4 秒待用户复测；用户报告的点击崩溃未复现 | [DVM-236](../tasks/dexvm/DVM-236.md)、[WU-PERF-15](../tasks/optimization/WU-PERF-15.md) |
| Tales From Deep Space 1.0.0 | macOS | 1920×1080 存档重载，初始场景 80 FPS | **退出读 0x315f0001**；800×480/1280×720 菜单裁剪 | [DVM-218](../tasks/dexvm/DVM-218.md)、[BND-29](../tasks/boundary/BND-29.md) |
| Dead Trigger 1.1.0 | Windows | 第一关持续出帧，存档重载 | **隔离沙盒 f=4740 string 构造 receiver 错**；默认存档缺 `Dialog(Context)`；macOS 退出卡在 FMOD 等待 AudioTrack | [DVM-204](../tasks/dexvm/DVM-204.md) |
| Dungeon Hunter（P1000） | macOS | 安装器→主游戏→开场视频 | launchMode 回归已修 | [DVM-250](../tasks/dexvm/DVM-250.md) |
| Asphalt 5 1.1.3 | macOS | 语言选择界面 | Surface 延迟创建引起的 EGL 回归已修 | [DVM-19](../tasks/dexvm/DVM-19.md) |
| PVZ | macOS | TermsActivity 首帧 | 后续帧与交互未验收 | [DVM-205](../tasks/dexvm/DVM-205.md) |
| Angry Birds 2.3.0 | — | 5000 帧，背景音乐正常 | 绘制未验收 | [DVM-186](../tasks/dexvm/DVM-186.md) |

## 未闭合问题

- 退出顺序：除上表外，GC 全周期、直接写入 ABA、直接访存卸载 quiescence 未验收；
  MCP shutdown 路径的 `unknown guest thread id` 曾复现，未修复。
- 门禁：boundary 架构违规与 `architecture.dexvm_intrinsic_layout` 媒体清单缺项未闭合
  （[DVM-190](../tasks/dexvm/DVM-190.md)）；全 BootDex 类链接因 MediaPlayer overlay/类数断言
  失败，未整体复验。
- `--ephemeral-sandbox` 下 Dungeon Hunter 约 106 帧抛 `Invalid path: /sdcard`；持久沙盒不复现。
- 跨平台：近期修复多只在 macOS 实跑；[VFS-07](../tasks/vfs/VFS-07.md) macOS 待验证；
  Dead Trigger 第 0 帧停滞修复 Windows/Linux 未实跑。

## 支持边界与未验收项

- VFS：多实例需显式 installation-id；PackageManager 仅当前 APK；多 ROM 未验收。
- GUI/Dashboard（[GUI](../tasks/gui/README.md)）：Windows 定向通过；导入→启动→移除全链路
  按用户安排延后；Linux/macOS GUI 未完成，移动端仅预留接口。
- 图形：实际 context-loss、多窗口/透明 Activity 未验收；GLES1 vertex-array 首错未重达。
- 音视频（[DVM-189](../tasks/dexvm/DVM-189.md)）：raw-resource 完整链、手动音频写入、custom
  AVIO 真实解码、媒体主时钟与影音同步未验收。
- PC 操控映射（[Input 设计](../design/input/README.md)）：P0 的 ControlMapper、配置合并与 run-apk/MCP/GUI
  接线已完成（[INP-1..3](../tasks/input/README.md)）；提示叠加层、游戏键位数据与实机按键验收待 INP-4。
- WebView 仅基础配置与回调（[DVM-187](../tasks/dexvm/DVM-187.md)/[188](../tasks/dexvm/DVM-188.md)），
  页面、JavaScript、TextToSpeech 未实现。
- 不支持 NDK Looper callback 与非 pipe fd；TLS-03、完整 mmap/lock、系统 CA 未验收。
