# 当前状态

更新：2026-08-12 · M9 DexVM：Profile v2 entry scope 已交付、Profile v1 已完全删除；
Asphalt 5 逐位回归通过；解释器 per-thread 执行态已拆分，Asphalt 6 下一步进入
1:1 宿主 Java 线程实现

## 当前阶段

- M0..M4 已完成并验收；M5 冻结待验收；M6 自动化闭环在用；M8 兼容冲刺继续。
- **M9 DexVM 已启动**（`WU-M9-001..027`，ADR-0017/0022）：
  阶段 0（AOSP 基线/测量/opcode 目录/dexasm）、阶段 1（解释器内核）、
  阶段 2（JNI 双向桥 + java.* P1）、阶段 3（android.* intrinsic +
  dex_activity + profile v2 + pilot 迁移）全部交付；entry override、静态预置和
  v2-only 清理也已完成。
- **阶段 4 线程地基已启动**：WU-M9-027 把 Interpreter 的帧栈、pending
  exception、返回值、tick 与 monitor recursion 拆为显式 execution context；
  两个 context 交错调用的隔离测试通过。尚未启动宿主线程、未实现 wait-set，
  不宣称 `dexvm.threads` ready。
- **pilot gate（05 §4 gate 1）已通过**：Asphalt 5 删除全部 16 条
  历史 replay 调用与 Java handler 映射，
  `asphalt5.title_flow` 三轮 + 迁移后复验 passed——468 帧固定预算、主界面
  PNG SHA-256 `9ee57323…` 保持逐位一致、无 fault、clean shutdown。本次删除旧实现后
  又以 production v2 目录复验一次，结论不变。
  `System.loadLibrary`(<clinit>)、onCreate 副作用链、GetStaticMethodID 查
  真实 DEX 方法表、native→解释器第三路由均真实发生。
- **存档持久化沙盒设计已落地**（2026-08-12）：ADR-0020（Proposed）+
  `docs/design/sandbox/`。根因确认为设计而非缺陷——VFS external、DexVM
  `memory_files`、SharedPreferences 三条写入通道均止于会话内存。方案：
  每 package 一个宿主沙盒目录，可写命名空间以文件粒度 overlay 覆盖只读底层，
  确定性 flush 点原子落盘；WU 分解 SBX-1..7。未启动，capabilities 无变化。
- **GUI 主面板基础版设计已落地**（2026-08-12）：roadmap GUI 选型改为
  SDL3 + Dear ImGui（与游戏窗口同渲染栈），`docs/design/launcher/` 交付
  完整方案——可双击的 `ogplay-gui` 独立进程（Windows 免控制台/macOS
  bundle；`ogplay gui` 供开发测试）+ 每 package 一目录的游戏库 +
  导入（APK 复制入库、数据包原地引用）/删除/点击 spawn `run-apk` 子进程，
  图标与名称走 manifest icon/label 增量 + `loader.arsc` + stb 提取链；
  WU 分解 GUI-1..7。未启动，capabilities 无变化。

## 已验收基线

M0..M4 验收文档见 `docs/state/M*-ACCEPTANCE.md`；M5 三批索引见
`docs/tasks/m5/README.md`；M9 任务索引见 `docs/tasks/m9/README.md`。
能力现状以 `capabilities.toml` 为准。Windows/x64（windows-msvc）基线全绿；
macOS/arm64 本次 full CTest 为 591/591（含 WU-M9-027 双 context 隔离用例）。

## 进行中：更多 title 上 dexvm 路线

- **Dungeon Hunter 已到标题画面**（2026-08-12）：交付层级占位、java.* 扩展、
  Bitmap(stb_image)/Canvas、Environment/StatFs、SharedPreferences、协作线程+
  Timer、多 Activity 流转、widget 状态层、布局注入、`VideoView` 完成回调，修复
  precheck k22b 误报。下一步：输入/进游戏与长时游玩。
- **适配流水线（复盘驱动，2026-08-12）**：上一款靠「跑一轮发现一个缺口」花了
  几十轮，现改为一次收割——`run-apk --survey-gaps` 诊断模式把未声明的平台类/
  方法合成中性桩并逐次记账，输出按命中排序的机读工作单（默认关闭，关闭即
  明确失败，survey 运行显式标注非兼容性结论）；`tools/dexvm_stub_gen.py` 由
  缺口报告生成占位与中性方法行，引用返回值一律进人工决策清单；空接收者诊断
  补上声明类与调用点。流程见 [`docs/playbook/NEW-TITLE.md`](../playbook/NEW-TITLE.md)。
- **Asphalt 6（2026-08-12 实测）**：production v2 Profile 在 external `InsTime` 与
  `file000000.dat` 验证后，入口覆盖实际启动 `GLGame`，并在 guest `<clinit>` 后把
  `GameInstaller.sbStarted=true` 作为 provisioned-data 事实写入；安装器/DRM 入口未执行。
  DexVM intrinsic 平台类已发布至同一 native JNI registry，后续通用补齐
  typed Bundle、SAX 构造链（parse 明确未支持）、有界方法反射、Stack 与 Thread
  id/name。exact 已启动 `GLThread`，`dev19..21` 三轮一致停在无超时
  `Object.wait()`；这需要 1:1 宿主 Java 线程 + monitor wait-set，不用空返回掩盖。
  删除 `sbStarted` preset 会真实切回 `GameInstaller` 商业外壳，在
  `Intent.setPackage` 处明确失败；未触及 DRM 消费链。WU-M9-027 后 exact 复验仍
  是同一零首帧 `Object.wait()V` 边界。尚无首帧/主界面。

开发方式手册（适配/测试/排查的操作步骤）见
**[docs/playbook/README.md](../playbook/README.md)**；title 阻塞点与工作队列见
[`docs/tasks/m9/README.md`](../tasks/m9/README.md)。

## 下一步（按优先级）

1. Asphalt 6 阶段 4（WU-M9-028..030）：以 1:1 宿主线程执行 Java
   `Thread.run()`，再按统一 monitor/Clock 实现
   wait-set 与 `Object.wait/notify`；重跑 exact，达到主界面后才宣告 gate。
2. 按命中批次闭合 DexVM 缺口并推进 GC-B；当前 512 MiB GC-A 预算只覆盖
   已验证短流程，不代表长时游玩 ready。
3. 评审 ADR-0020 后启动存档沙盒 SBX-1/2；Linux M9 严格出口复验仍待执行。

## 阻塞

- 无新增阻塞。dexvm 未实现面（反射/finalizer/多 ClassLoader/odex 等
  非目标，及未挂接 intrinsic）全部记账并明确失败，不伪造成功。

长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
