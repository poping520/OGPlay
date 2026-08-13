# 当前状态

更新：2026-08-13 · DexVM 阶段 4（真实宿主 Java 线程、monitor wait-set、managed
surface 回调）与 intrinsic 声明迁移（DVM-32..37）已交付；存档沙盒 SBX-1..12、
主面板 GUI-1..16 与验收报告均已收口

## 当前阶段

- M0..M4 已完成并验收；M5 冻结待验收；M6 自动化闭环在用；M8 兼容冲刺继续。
- **M9 DexVM**（任务已编号至 `DVM-38`，ADR-0017/0022）：阶段 0..3（AOSP 基线/解释器
  内核/JNI 双向桥/java.*+android.* intrinsic/dex_activity/profile v2/pilot
  迁移）全部交付；entry override、静态预置和 v2-only 清理完成。
- **阶段 4 线程地基已交付**（DVM-27..29）：显式 per-thread execution
  context；`Thread.start()` 起真实宿主线程执行 guest `run()`，接入
  interrupt/join/teardown；按 AOSP `vm/Sync.cpp` 实现 owner/recursion/
  wait-set 与真实 `Object.wait/notify/notifyAll`，超时只走统一 Clock。解释
  执行由 `VmExecutionLock` 串行化——真实线程、真实阻塞，同一时刻仅一个线程
  解释字节码。`dexvm.threads`/`dexvm.monitors` 均 `partial`：子线程 native
  调用仍复用 root guest 栈（需停泊时以 `blocking_in_native` 明确失败），
  native 侧 JNI monitor 表尚未与之合一。
- **intrinsic/热路径优化与声明迁移已交付**：DVM-32..34 完成地址稳定绑定、
  execution 传递和受检 builder 迁移基础；DVM-35 将 68 个 java.*/javax.* 类拆为
  一类一文件并直接持有 162 个 core handler；DVM-36 将 165 个 android/platform
  类迁入 `integration/dexvm_android/` 的逐类 `Declare_*()`，frontend/bridge 只传
  合并 catalog，生成器改产逐类 builder 骨架；DVM-37 已删除兼容 registry、字符串
  handler id 与懒绑定缓存，声明内嵌实现成为唯一 intrinsic 分发通道。
- **DVM-38 已交付**：android handler 全部与逐类声明同址；41 个跨类/platform
  handler 工厂化，5 个死成员及 `AndroidHandlers`/Populate 脚手架删除。保留的
  support 文件只负责 surface/video/widget 派发子系统。
- **pilot gate（05 §4 gate 1）已通过**：Asphalt 5 删除 16 条历史 replay 调用
  与 Java handler 映射后，`asphalt5.title_flow` 三轮 passed——468 帧、主界面
  SHA-256 `9ee57323…` 逐位一致、无 fault、clean shutdown。
- **存档持久沙盒 SBX-1..12 已交付**（ADR-0020 Accepted，任务单
  [`docs/tasks/sandbox/`](../tasks/sandbox/README.md)）：native/DexVM/prefs 统一
  VFS；ARM EABI open flags、目录分页、删除/rename 防复活、生命周期 flush、
  完整性与 O(1) 配额均有回归。**用户级闭环仍未演示**：title 尚未进入会产生
  存档的流程。
- **GUI 主面板基础版已收口**（[`docs/tasks/launcher/`](../tasks/launcher/README.md)）：
  GUI-1..16 闭环；按钮 ID 冒烟审计、库根存档绑定、宿主选择器期间保持导入
  模态、诊断 FIFO、bundled Profile/quirk 数据（不依赖编译机源码树）、
  catalog 失效 fail closed、视觉/空闲/持久恢复/边缘元数据全部收口。

## 已验收基线

M0..M4 验收文档见 `docs/state/M*-ACCEPTANCE.md`；M5 三批索引见
`docs/tasks/m5/README.md`；DexVM 任务索引见 `docs/tasks/dexvm/README.md`。
能力现状以 `capabilities.toml` 为准。Windows/x64（windows-msvc）本次 709/709；
macOS/arm64 此前 full CTest 为 636/636（含线程、wait-set 与沙盒用例）。

## 进行中：更多 title 上 dexvm 路线

- **Dungeon Hunter 已到标题画面**（2026-08-12）：层级占位、java.* 扩展、
  Bitmap/Canvas、Environment/StatFs、SharedPreferences、Timer、多 Activity
  流转、widget 状态层、布局注入、`VideoView` 完成回调。下一步：输入与进游戏。
- **适配流水线**：`run-apk --survey-gaps` 一次收割整条缺口队列（默认关闭，
  关闭即明确失败，survey 运行标注为非兼容性结论），`tools/dexvm_stub_gen.py`
  由报告生成占位。流程见
  [`docs/playbook/NEW-TITLE.md`](../playbook/NEW-TITLE.md)。
- **Asphalt 6（2026-08-12 实测）**：production v2 Profile 入口覆盖启动
  `GLGame` 并写入 provisioned-data 事实；删除该 preset 真实切回商业外壳并在
  `Intent.setPackage` 明确失败，未触及 DRM 消费链。APK 自带改名的 AOSP
  `GLSurfaceView`；DVM-28..30 后 `GLThread` 已是真实宿主线程、越过
  `Object.wait()`、收到 managed surface 回调。三轮 exact 逐字一致停在
  `class is not available: Ljavax/microedition/khronos/egl/EGLContext;`——
  自带 `GLSurfaceView` 要自己驱动 EGL。**未达首帧**，profile 保持 `partial`；
  前向缺口已静态枚举（EGL10 18 方法 + 4 常量、`EGLContext.getEGL/getGL`、
  `GL10.glGetString`），由 DVM-31 承接。证据：`.local/evidence/a6-gate-r1..r3/`。

开发方式手册见 **[docs/playbook/README.md](../playbook/README.md)**；title
阻塞点与工作队列见 [`docs/tasks/dexvm/README.md`](../tasks/dexvm/README.md)。

## 下一步（按优先级）

1. DVM-31：解释执行的 EGL10/GL10 façade（自带 `GLSurfaceView` 的通用形态，
   非 title 特判），让 A6 取得首帧；达到主界面后才宣告 gate。
2. 按命中批次闭合 DexVM 缺口并推进 GC-B；当前 512 MiB GC-A 预算只覆盖
   已验证短流程，不代表长时游玩 ready。
3. 解释器性能余项：invoke 参数封送 args-shorty 预计算、String intrinsic
   只读路径去整串拷贝（分析结论见 DVM-32/33 任务单）。
4. 阶段 4 收口：子线程 native 调用仍复用 root guest 栈与 thread id，native
   侧 JNI monitor 表与 DexVM monitor 表尚未合一。
5. Linux M9 严格出口复验待执行；五个生产源文件仍超 800 行
   （boundary hle/gles/gles1、guest_call_session、run_apk）。

## 阻塞

- 无新增阻塞。dexvm 未实现面（反射/finalizer/多 ClassLoader/odex 等
  非目标，及未挂接 intrinsic）全部记账并明确失败，不伪造成功。

长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
