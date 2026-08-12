# 当前状态

更新：2026-08-12 · M9 DexVM：Profile v2 entry scope 已交付、Profile v1 已完全删除；
Asphalt 5 逐位回归通过；Asphalt 6 已直达引擎 Activity，当前停在 native→DexVM
intrinsic 的 JNI 类可见性（`android/content/Intent`）

## 当前阶段

- M0..M4 已完成并验收；M5 冻结待验收；M6 自动化闭环在用；M8 兼容冲刺继续。
- **M9 DexVM 已启动**（`WU-M9-001..025`，ADR-0017/0022）：
  阶段 0（AOSP 基线/测量/opcode 目录/dexasm）、阶段 1（解释器内核）、
  阶段 2（JNI 双向桥 + java.* P1）、阶段 3（android.* intrinsic +
  dex_activity + profile v2 + pilot 迁移）全部交付；entry override、静态预置和
  v2-only 清理也已完成。
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

## 已验收基线

M0..M4 验收文档见 `docs/state/M*-ACCEPTANCE.md`；M5 三批索引见
`docs/tasks/m5/README.md`；M9 任务索引见 `docs/tasks/m9/README.md`。
能力现状以 `capabilities.toml` 为准。Windows/x64（windows-msvc）与本次
macOS/arm64 本次 full CTest 为 585/585（删除旧 Profile 专属用例后重新发现）。

## M9 交付摘要

- 基线与工具：`third_party/aosp-dalvik` 锚点哈希门禁、opcode 目录 218 项机器
  派生、dexasm 确定性汇编器、Scenario runner 易用性批次（WU-M9-020/021）。
- 解析层：`loader.dex_code`、`loader.arsc`、Manifest launcher activity 事实。
- `runtime/dexvm`：类链接、JavaObjectModel（与 native 同对象）、GC-A 预算
  arena、tagged 帧解释器全 dex 035 家族、异常展开、`<clinit>` 状态机。
- `DexVmGuestBridge` 双向 JNI（233 槽 ABI 不变的第三路由）、
  `DexActivityLifecycle` 生命周期反转、Title Profile v2。
- ADR-0022 entry scope：Profile 可在 required data manifest 已验证后选择引擎 Activity，
  并于真实 `<clinit>` 后写受检 Z/B/C/S/I/J/F/D/String static field；schema 1、旧 Java
  映射和调用重放源文件/头/CTest/数据 schema 均已删除。

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
  通用补齐 AudioManager、离线 Telephony、SurfaceHolder/ViewTreeObserver、Thread priority
  与 UTF-8 URLEncoder 后，当前首个边界是 native `FindClass` 只能看 session registry，
  看不到 DexVM intrinsic `android/content/Intent`。这是桥接架构缺口，保持明确失败、无首帧，
  不用假 Intent 掩盖。证据 `.local/automation/asphalt6-entry-scope-dev10-20260812`（不入库）。

开发方式手册（适配/测试/排查的操作步骤）见
**[docs/playbook/README.md](../playbook/README.md)**；title 阻塞点与工作队列见
[`docs/tasks/m9/README.md`](../tasks/m9/README.md)。

## 下一步（按优先级）

1. Asphalt 6（WU-M9-026）：统一 DexVM intrinsic 对 native JNI `FindClass`/method/field 的
   可见性与调用路由，先闭合 `Intent` 的真实类契约，再重跑 exact；达到主界面后才宣告 gate。
2. dexvm 记账缺口按命中批次闭合：J/D 出向返回解码、string 资源、
   MediaPlayer 完成回调、`dexvm.stats/stack` Agent 查询面（04 §8）。
   GC-B 优先级上调：pilot 试玩实证 GC-A 记账在资源重载路径线性增长
   （已用 512 MiB 预算诚实覆盖换语言/进赛道，长时游玩需 GC-B）。
3. 阶段 4（线程/wait-notify/GC-B/java.* P2P3）在厚层 title（libGDX 类）
   立项时启动；05 §4 gate 3。
4. Linux M9 严格出口复验（macOS/arm64 与 Windows/x64 已全绿；Windows 侧
   修复了 dexvm 的 MSVC 可移植性：`__builtin_memcpy`→`std::bit_cast`、
   遮蔽警告，aosp-dalvik 子模块需 LF 检出——autocrlf 会破坏锚点哈希）。
5. 存档持久化沙盒：评审 ADR-0020 后按 `docs/design/sandbox/05` 的
   SBX-1/2（SandboxStore、VFS 目录操作，均不触碰现有行为）先行启动。
6. 存量性能 backlog 不变（ADR-0019 呈现管线方向等）。

## 阻塞

- 无新增阻塞。dexvm 未实现面（反射/finalizer/多 ClassLoader/odex 等
  非目标，及未挂接 intrinsic）全部记账并明确失败，不伪造成功。

长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。
