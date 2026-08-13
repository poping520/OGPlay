# 当前状态

更新：2026-08-12 · M9 阶段 4（真实宿主 Java 线程、monitor wait-set、managed
surface 回调）已交付，Asphalt 6 边界前移到自带 GLSurfaceView 的 EGL 面；
存档沙盒 SBX-1/2/3/7 已交付，DexVM 与 prefs 两条写入通道尚未收敛

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
- **存档持久沙盒 SBX-1..7 全部交付**（ADR-0020 Accepted，任务单
  [`docs/tasks/sandbox/`](../tasks/sandbox/README.md)）：`SandboxStore` 宿主
  存储、VFS 目录操作、`AttachSandbox` 覆盖层与落盘点、CLI 接线与一次性沙盒、
  DexVM `File` 族改线 VFS（`memory_files` 废除、`mkdirs` 去伪成功）、
  SharedPreferences 落成平台同构 XML、文件元数据与目录 syscall 绑定。三条
  guest 写入通道（native syscall、DexVM File、prefs）现在都收敛到同一个 VFS，
  `run-apk` 默认按 package 持久保存，自动化默认一次性沙盒且 Asphalt 5 golden
  逐位不变。**用户级闭环仍未演示**：当前最深入的 title 只到标题画面，本地
  运行没有触发任何存档写入，"进游戏产生存档→重启读到"要等 title 深度推进。
- **GUI 主面板未启动**：设计见 `docs/design/launcher/`（SDL3 + Dear ImGui，
  WU 分解 GUI-1..7），capabilities 无变化。

## 已验收基线

M0..M4 验收文档见 `docs/state/M*-ACCEPTANCE.md`；M5 三批索引见
`docs/tasks/m5/README.md`；M9 任务索引见 `docs/tasks/m9/README.md`。
能力现状以 `capabilities.toml` 为准。Windows/x64（windows-msvc）基线全绿；
macOS/arm64 本次 full CTest 为 636/636（含线程、wait-set 与沙盒用例）。
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
3. 启动器主面板 GUI-1..7（`docs/design/launcher/`）：目前只有 CLI，非技术
   用户无法导入并启动游戏。
4. 阶段 4 收口：子线程 native 调用仍复用 root guest 栈与 thread id（需要
   停泊时以 `blocking_in_native` 明确失败），native 侧 JNI monitor 表与
   DexVM monitor 表尚未合一。
5. Linux M9 严格出口复验仍待执行；五个生产源文件仍超 800 行上限
   （`android_boundary_hle.cpp` 1001、`android_boundary_gles.cpp` 962、
   `android_guest_call_session.cpp` 907、`run_apk.cpp` 856、
   `android_boundary_gles1.cpp` 820）。

## 阻塞

- 无新增阻塞。dexvm 未实现面（反射/finalizer/多 ClassLoader/odex 等
  非目标，及未挂接 intrinsic）全部记账并明确失败，不伪造成功。

长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
