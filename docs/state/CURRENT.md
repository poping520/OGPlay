# 当前状态

更新：2026-08-16 · Layout UI 已正式验收 complete，Asphalt 6 启动视频 skip UI 与
Asphalt 5 title-flow 均三轮 exact gate 闭合；macOS SDL 事件泵已隔离 GLThread；DVM-31 EGL façade 实现完成；MSVC
工程内/工程间并行编译已启用；DexVM 阶段 4 与 intrinsic 声明迁移、SBX、GUI 已交付

## 当前阶段

- M0..M4 已完成并验收；M5 冻结待验收；M6 自动化闭环在用；M8 兼容冲刺继续。
- **M9 DexVM**（DVM-1..39，ADR-0017/0022）：阶段 0..3、entry override、
  静态预置和 v2-only 清理均已交付。
- **Layout UI 已正式验收 complete**：LUI-1..15 的 typed AXML、唯一 UiTree、layout/
  render/composition/input、Linear/RelativeLayout、动态 hierarchy、文本、include/resources
  与 ImageView 已闭合；未命中的 style/selector 不扩张。旧并行 geometry facts 已删除，
  hit-test 只读 UiTree，architecture test 禁止 title-specific runtime identity。LUI-16 将
  layout/draw dirty 分相消费，确保 layout mutation 在 raster 后才清 draw dirty；gesture
  ownership 与 click eligibility 已分离，touch-only false 回退 Activity，四种 touch/click
  组合及 hidden/removed/outside 取消均有机器测试。A5/A6 exact 各三轮复验通过。
- **阶段 4 线程地基已交付**（DVM-27..29）：真实宿主线程、独立 execution
  context、wait-set 与统一 Clock 超时已接入；解释仍由 `VmExecutionLock`
  串行。`threads`/`monitors` 保持 `partial`：子线程 native 调用复用 root
  guest 栈，JNI/DexVM monitor 尚未合一。
- **intrinsic/热路径迁移已交付**（DVM-32..38）：地址稳定绑定与 execution
  传递完成；平台类由同址 `Declare_*()` 直接绑定，旧 registry/字符串 id 已删除。
- **pilot gate（05 §4 gate 1）已通过**：Asphalt 5 删除 16 条历史 replay 调用后
  `asphalt5.title_flow` 三轮 passed——468 帧、主界面 SHA-256 `9ee57323…` 逐位
  一致、无 fault、clean shutdown。
- **存档持久沙盒 SBX-1..12 已交付**（ADR-0020）：native/DexVM/prefs 统一
  VFS 及完整性/配额回归已闭合；**用户级闭环仍未演示**。
- **GUI GUI-1..16 已收口**：库根存档、导入、诊断、bundled data 与
  catalog fail-closed 均已闭合。

## 已验收基线

M0..M4 验收文档见 `docs/state/M*-ACCEPTANCE.md`；M5 三批索引见
`docs/tasks/m5/README.md`；DexVM 任务索引见 `docs/tasks/dexvm/README.md`；
Layout UI 任务索引见 `docs/tasks/layoutui/README.md`。
能力现状以 `capabilities.toml` 为准。macOS/arm64 最近 full CTest 765/765；
Windows/x64（windows-msvc）本次 728/728。
Windows 预设以原生核数并行构建工程，OGPlay 自有 MSVC target 同时启用 `/MP`；
第三方 target 不被全局注入该选项。

## 进行中：更多 title 上 dexvm 路线

- **Dungeon Hunter 已到标题画面**（2026-08-12）：层级占位、java.* 扩展、
  Bitmap/Canvas、Environment/StatFs、SharedPreferences、Timer、多 Activity
  流转、widget 状态层、布局注入、`VideoView` 完成回调。下一步：输入与进游戏。
- **Dungeon Hunter 启动回归已修复**（2026-08-14）：Activity 切换不再误置
  session exit，`stat64` 改为 ARM 104 字节自然对齐；现跑满 300 帧进入音频加载。
- **适配流水线**：`run-apk --survey-gaps` 一次收割整条缺口队列（默认关闭，
  关闭即明确失败，survey 运行标注为非兼容性结论），`tools/dexvm_stub_gen.py`
  由报告生成占位。流程见
  [`docs/playbook/NEW-TITLE.md`](../playbook/NEW-TITLE.md)。
- **DVM-31 EGL façade（2026-08-14）**：host-owned surface 状态机、currency
  接力、managed present 与条件 swap pacer 已实现；driver 可运行时一帧一 swap，
  guest 阻塞时放行，N=2 monitor 握手与反向节拍测试通过。10 个 EGL/GL Java
  handle 与 façade 实现已聚合为单一翻译单元。DVM-39 继续闭合 Android/JNI/VFS
  启动边界；A6 可见启动检查点 sequence 6、`4f8e4bf1…` 三轮一致、无 fault、clean
  shutdown，长运行已进入车辆开场动画。profile 因主界面/可游玩未验收仍为
  `partial`。LUI-15 在当前完整 dev 环境复跑 A5，三轮均得到历史 Main Menu exact
  `9ee57323…`、无 fault、clean shutdown；此前漂移未复现，golden 未改写。
- **macOS GLThread 宿主事件泵修复（2026-08-14）**：A6 的真实 DexVM Java
  GLThread 进入 native 长调用时会复用 guest slice observer，旧实现因此在
  工作线程调用 `SDL_PumpEvents`，AppKit 以非主线程异常终止。现以创建时
  宿主线程门禁隔离窗口事件泵与非线程安全进度状态，工作线程回归已补；
  exact A6 自由运行经用户复验已不再触发该崩溃。

开发方式手册见 **[docs/playbook/README.md](../playbook/README.md)**；title
阻塞点与工作队列见 [`docs/tasks/dexvm/README.md`](../tasks/dexvm/README.md)。

## 下一步（按优先级）

1. 继续推进 A6 开场后的主界面/输入 gate。
2. 按命中批次闭合 DexVM 缺口并启动 GC-B-1..6（设计见 `09-gc.md`）；
   当前 512 MiB GC-A 只覆盖已验证短流程。
3. 启动解释器 v2 threaded 分批（设计见 `10-interpreter-threaded.md`）；
   args-shorty 预计算并入 V2-5，String 只读路径优化仍独立。
4. 阶段 4 收口：子线程 native 调用仍复用 root guest 栈与 thread id，native
   侧 JNI monitor 表与 DexVM monitor 表尚未合一。
5. Linux M9 严格出口复验待执行；五个生产源文件仍超 800 行
   （boundary hle/gles/gles1、guest_call_session、run_apk）。

## 阻塞

- A6 启动 gate 已通过，主界面/可游玩 gate 尚未执行。其余未实现面均记账并明确失败。

长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
