# 当前状态

更新：2026-08-13 · M9 阶段 4（真实宿主 Java 线程、monitor wait-set、managed
surface 回调）已交付，Asphalt 6 边界前移到自带 GLSurfaceView 的 EGL 面；
存档沙盒 SBX-1..12 已交付；主面板 GUI-1..10 基础版闭环并完成验收补强

## 当前阶段

- M0..M4 已完成并验收；M5 冻结待验收；M6 自动化闭环在用；M8 兼容冲刺继续。
- **M9 DexVM 已启动**（`WU-M9-001..030`，ADR-0017/0022）：
  阶段 0（AOSP 基线/测量/opcode 目录/dexasm）、阶段 1（解释器内核）、
  阶段 2（JNI 双向桥 + java.* P1）、阶段 3（android.* intrinsic +
  dex_activity + profile v2 + pilot 迁移）全部交付；entry override、静态预置和
  v2-only 清理也已完成。
- **阶段 4 线程地基已交付**：WU-M9-027 把 Interpreter 的帧栈、pending
  exception、返回值、tick 与 monitor recursion 拆为显式 execution context；
  WU-M9-028 在其上让 `Thread.start()` 通过 HAL 起一个真实宿主线程执行
  guest `run()`，并接入 interrupt/join/teardown；WU-M9-029 按 AOSP
  `vm/Sync.cpp` 实现 owner/recursion/wait-set 与真实
  `Object.wait/notify/notifyAll`，超时只走统一 Clock。解释执行由全 VM
  `VmExecutionLock` 串行化——真实线程、真实阻塞，但同一时刻只有一个线程解释
  字节码，这是显式记账的限制而非并发。`dexvm.threads`/`dexvm.monitors` 均为
  `partial`：子线程 native 调用仍复用 root guest 栈（需要停泊时以
  `blocking_in_native` 明确失败），native 侧 JNI monitor 表尚未与之合一。
- **pilot gate（05 §4 gate 1）已通过**：Asphalt 5 删除 16 条历史 replay 调用
  与 Java handler 映射后，`asphalt5.title_flow` 三轮 passed——468 帧、主界面
  SHA-256 `9ee57323…` 逐位一致、无 fault、clean shutdown。
- **存档持久沙盒 SBX-1..12 全部交付**（ADR-0020 Accepted，任务单
  [`docs/tasks/sandbox/`](../tasks/sandbox/README.md)）：native/DexVM/prefs 已统一
  VFS；ARM EABI open flags、目录分页、删除/rename 防复活、生命周期 flush、
  Java/prefs 完整性、装载与 O(1) 配额均有回归。Windows/MSVC 663/663；本轮未
  重跑 exact-title。**用户级闭环仍未演示**：title 尚未进入会产生存档的流程。
- **GUI 主面板基础版已交付**（任务单 [`docs/tasks/launcher/`](../tasks/launcher/README.md)）：
  GUI-1..10 交付双入口、严格库、visuals、网格、导入、设置、删除与点击启动；
  同目录 CLI 启动显式绑定库根 sandbox，删除保留 external/存档；空库按钮 ID 冲突
  已修复并纳入真实冒烟审计，宿主选择器未决时导入模态不再先行关闭。

## 已验收基线

M0..M4 验收文档见 `docs/state/M*-ACCEPTANCE.md`；M5 三批索引见
`docs/tasks/m5/README.md`；M9 任务索引见 `docs/tasks/m9/README.md`。
能力现状以 `capabilities.toml` 为准。Windows/x64（windows-msvc）本次 693/693；
macOS/arm64 此前 full CTest 为 636/636（含线程、wait-set 与沙盒用例）。
沙盒任务单见 [`docs/tasks/sandbox/`](../tasks/sandbox/README.md)。

## 进行中：更多 title 上 dexvm 路线

- **Dungeon Hunter 已到标题画面**（2026-08-12）：层级占位、java.* 扩展、
  Bitmap/Canvas、Environment/StatFs、SharedPreferences、Timer、多 Activity
  流转、widget 状态层、布局注入、`VideoView` 完成回调。下一步：输入与进游戏。
- **适配流水线**：`run-apk --survey-gaps` 一次收割整条缺口队列（默认关闭，
  关闭即明确失败，survey 运行标注为非兼容性结论），`tools/dexvm_stub_gen.py`
  由报告生成占位。流程见
  [`docs/playbook/NEW-TITLE.md`](../playbook/NEW-TITLE.md)。
- **Asphalt 6（2026-08-12 实测）**：production v2 Profile 入口覆盖启动
  `GLGame`，并在 guest `<clinit>` 后写入 `GameInstaller.sbStarted=true` 这一
  provisioned-data 事实；删除该 preset 会真实切回商业外壳并在
  `Intent.setPackage` 明确失败，未触及 DRM 消费链。
  APK 自带一份改名的 AOSP `GLSurfaceView`，`GLThread.run()` 在
  `GLThreadManager` 上做经典 `synchronized`/`wait`/`notifyAll` 守卫循环。
  WU-M9-028..030 后该线程已是真实宿主线程、越过 `Object.wait()`、并收到
  managed surface 的 `surfaceCreated`/`surfaceChanged`。三轮 exact 逐字一致
  停在 `class is not available: Ljavax/microedition/khronos/egl/EGLContext;`
  ——自带 `GLSurfaceView` 要自己驱动 EGL。**未达首帧**，profile 保持
  `partial`；前向缺口已静态枚举（EGL10 18 个方法 + 4 常量、
  `EGLContext.getEGL/getGL`、`GL10.glGetString`），由 WU-M9-031 承接。
  证据：`.local/evidence/a6-gate-r1..r3/`。

开发方式手册（适配/测试/排查的操作步骤）见
**[docs/playbook/README.md](../playbook/README.md)**；title 阻塞点与工作队列见
[`docs/tasks/m9/README.md`](../tasks/m9/README.md)。

## 下一步（按优先级）

1. WU-M9-031：解释执行的 EGL10/GL10 façade（自带 `GLSurfaceView` 的通用形态，
   非 title 特判），让 A6 取得首帧；达到主界面后才宣告 gate。
2. 按命中批次闭合 DexVM 缺口并推进 GC-B；当前 512 MiB GC-A 预算只覆盖
   已验证短流程，不代表长时游玩 ready。
3. 主面板基础版已闭环；后续只按 M7 任务扩展输入映射、存档管理等完整体验。
4. 阶段 4 收口：子线程 native 调用仍复用 root guest 栈与 thread id（需要
   停泊时以 `blocking_in_native` 明确失败），native 侧 JNI monitor 表与
   DexVM monitor 表尚未合一。
5. Linux M9 严格出口复验待执行；五个生产源文件仍超 800 行
   （boundary hle/gles/gles1、guest_call_session、run_apk）。

## 阻塞

- 无新增阻塞。dexvm 未实现面（反射/finalizer/多 ClassLoader/odex 等
  非目标，及未挂接 intrinsic）全部记账并明确失败，不伪造成功。

长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
