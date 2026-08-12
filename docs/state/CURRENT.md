# 当前状态

更新：2026-08-12 · M9 DexVM：Profile v2 entry scope 已交付、Profile v1 已完全删除；
Asphalt 5 逐位回归通过；1:1 宿主 Java 线程已交付（WU-M9-028），Asphalt 6 的
`Object.wait()` 边界已前移到真实子线程，下一步实现 monitor wait-set

## 当前阶段

- M0..M4 已完成并验收；M5 冻结待验收；M6 自动化闭环在用；M8 兼容冲刺继续。
- **M9 DexVM 已启动**（`WU-M9-001..027`，ADR-0017/0022）：
  阶段 0（AOSP 基线/测量/opcode 目录/dexasm）、阶段 1（解释器内核）、
  阶段 2（JNI 双向桥 + java.* P1）、阶段 3（android.* intrinsic +
  dex_activity + profile v2 + pilot 迁移）全部交付；entry override、静态预置和
  v2-only 清理也已完成。
- **阶段 4 线程地基已交付**：WU-M9-027 把 Interpreter 的帧栈、pending
  exception、返回值、tick 与 monitor recursion 拆为显式 execution context；
  WU-M9-028 在其上让 `Thread.start()` 通过 HAL 起一个真实宿主线程执行
  guest `run()`，并接入 interrupt/join/teardown。解释执行由全 VM
  `VmExecutionLock` 串行化——真实线程、真实阻塞，但同一时刻只有一个线程解释
  字节码，这是显式记账的限制而非并发。`dexvm.threads` 为 `partial`：
  wait-set 未实现，`Object.wait()` 仍明确失败；子线程 native 调用仍复用 root
  guest 栈，需要停泊时以 `blocking_in_native` 明确失败。
- **pilot gate（05 §4 gate 1）已通过**：Asphalt 5 删除全部 16 条历史 replay
  调用与 Java handler 映射后，`asphalt5.title_flow` 三轮 + 迁移后复验 passed
  ——468 帧固定预算、主界面 PNG SHA-256 `9ee57323…` 逐位一致、无 fault、
  clean shutdown。`System.loadLibrary`(<clinit>)、onCreate 副作用链、
  GetStaticMethodID 查真实 DEX 方法表、native→解释器第三路由均真实发生。
- **两份设计已落地但未启动**（2026-08-12，capabilities 无变化）：存档持久化
  沙盒（ADR-0020 Proposed + `docs/design/sandbox/`，每 package 一个宿主沙盒
  目录 + 文件粒度 overlay + 确定性 flush，WU 分解 SBX-1..7；根因是设计缺失
  而非缺陷——三条写入通道都止于会话内存）与 GUI 主面板基础版
  （`docs/design/launcher/`，SDL3 + Dear ImGui，WU 分解 GUI-1..7）。

## 已验收基线

M0..M4 验收文档见 `docs/state/M*-ACCEPTANCE.md`；M5 三批索引见
`docs/tasks/m5/README.md`；M9 任务索引见 `docs/tasks/m9/README.md`。
能力现状以 `capabilities.toml` 为准。Windows/x64（windows-msvc）基线全绿；
macOS/arm64 本次 full CTest 为 601/601（含 WU-M9-028 宿主线程用例）。

## 进行中：更多 title 上 dexvm 路线

- **Dungeon Hunter 已到标题画面**（2026-08-12）：交付层级占位、java.* 扩展、
  Bitmap(stb_image)/Canvas、Environment/StatFs、SharedPreferences、协作线程+
  Timer、多 Activity 流转、widget 状态层、布局注入、`VideoView` 完成回调，修复
  precheck k22b 误报。下一步：输入/进游戏与长时游玩。
- **适配流水线（复盘驱动，2026-08-12）**：改为一次收割——`run-apk
  --survey-gaps` 把未声明的平台类/方法合成中性桩并逐次记账，输出按命中排序的
  机读工作单（默认关闭，关闭即明确失败，survey 运行标注为非兼容性结论）；
  `tools/dexvm_stub_gen.py` 由缺口报告生成占位，引用返回值进人工决策清单。
  流程见 [`docs/playbook/NEW-TITLE.md`](../playbook/NEW-TITLE.md)。
- **Asphalt 6（2026-08-12 实测）**：production v2 Profile 在 external `InsTime` 与
  `file000000.dat` 验证后，入口覆盖实际启动 `GLGame`，并在 guest `<clinit>` 后把
  `GameInstaller.sbStarted=true` 作为 provisioned-data 事实写入；安装器/DRM 入口未执行。
  DexVM intrinsic 平台类已发布至同一 native JNI registry，后续通用补齐
  typed Bundle、SAX 构造链（parse 明确未支持）、有界方法反射、Stack 与 Thread
  id/name。删除 `sbStarted` preset 会真实切回 `GameInstaller` 商业外壳，在
  `Intent.setPackage` 处明确失败；未触及 DRM 消费链。
  APK 自带一份改名的 AOSP `GLSurfaceView`，`GLThread.run()` 在
  `GLThreadManager` 上做经典 `synchronized`/`wait`/`notifyAll` 守卫循环。
  WU-M9-028 后该线程已是真实宿主线程，边界前移为
  `Java thread Thread-287 failed: … Ljava/lang/Object;->wait()V`——需要
  wait-set，不用空返回掩盖。尚无首帧/主界面。

开发方式手册（适配/测试/排查的操作步骤）见
**[docs/playbook/README.md](../playbook/README.md)**；title 阻塞点与工作队列见
[`docs/tasks/m9/README.md`](../tasks/m9/README.md)。

## 下一步（按优先级）

1. Asphalt 6 阶段 4（WU-M9-029..030）：按统一 monitor/Clock 实现 wait-set 与
   `Object.wait/notify`；重跑 exact，达到主界面后才宣告 gate。
2. 按命中批次闭合 DexVM 缺口并推进 GC-B；当前 512 MiB GC-A 预算只覆盖
   已验证短流程，不代表长时游玩 ready。
3. 评审 ADR-0020 后启动存档沙盒 SBX-1/2；Linux M9 严格出口复验仍待执行。

## 阻塞

- 无新增阻塞。dexvm 未实现面（反射/finalizer/多 ClassLoader/odex 等
  非目标，及未挂接 intrinsic）全部记账并明确失败，不伪造成功。

长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
