# 当前状态

更新：A6/Tales 两项 GLES 扩展缺口已闭合；title gate 继续

## 当前阶段

- **A6/Tales GLES 缺口已修复**：uniform reflection 接受 API19
  `GL_SAMPLER_3D_OES` 单值 shape；GLES1 extension 目录和 boundary 真实实现
  `GL_OES_mapbuffer` 三入口，既有 thunk ID 不漂移。A6 已持续渲染 2.5 万余 draw；Tales
  已完成 native load，新首错为 JNI `getPackageCodePath()`。见
  [DVM-47](../tasks/dexvm/DVM-47.md)、[WU-0231](../tasks/m5/WU-0231.md)。
- **UTIL-3 已完成**：收敛 diagnostics snapshot 投影、ELF 地址映射、JNI guest 返回编码/
  string lease/FieldID lookup、StringBuffer/Builder、简单 throwable、reflection member、
  数值 binop、NIO bulk 校验、Profile exact keys 与 internal class-name predicate；锁策略、
  错误文本、异常类别和 ABI 不变。见 [UTIL-3](../tasks/maintenance/UTIL-3.md)。
- **UTIL-2/1 已完成**：guest 小端搬运、DEX LEB128、syscall 路径、package/id、ArrayKind、
  UTF/Base64/hex/range/alignment 等重复实现已接唯一公共入口；见
  [UTIL-2](../tasks/maintenance/UTIL-2.md)、[UTIL-1](../tasks/maintenance/UTIL-1.md)。
- **DVM-92 已完成并通过 title 验收**：退出首个 guest 回调前单向退役 Java EGL、
  native/managed GLES 与 EGL swap；process `BeginTeardown` 发布独立取消并中断
  blocking wait，join 前再次中断覆盖回调中新建 futex。renewable JNI native frame
  在既有 slice/boundary 安全点失败展开，运行期 ADR-0023 预算与 non-renewable
  finalizer 不变。契约见 [DVM-92](../tasks/dexvm/DVM-92.md) 与
  [ADR-0025](../adr/0025-teardown-cancellation-and-graphics-retirement.md)。
- **Diagnostics WU1 已完成**：schema-1 统一 lifecycle、execution/PC、DVM/Java 栈、GLES、
  futex/monitor/pacer 与 syscall/native 定容环；busy 独立降级，monitor 分开 entry/notify
  waiter 且只有 entry→owner 环才确认 cycle。Windows event/POSIX self-pipe、CLI/MCP、
  teardown timeout、脱敏、当前用户 ACL、单文件/目录配额和 stop→join 已闭合。真实停滞
  子进程的外部触发 fixture 按用户决定延期，不阻塞 WU1。
- **Diagnostics WU2 已完成**：排查手册固化 procdump/WinDbg/lldb、host_tid 十进制/十六进制
  对齐、符号构建与无符号 `module+offset` 边界；对齐工具只合并事实，不猜函数名。
  见 [Diagnostics](../design/diagnostics/README.md) 与 [ADR-0026](../adr/0026-bounded-stall-snapshots.md)。
- **DVM-89 能力栈已闭合**：native watchdog 仅按真实阻塞/I/O/GPU/音频/JNI 重入进展
  续期；首帧握手可观测 worker park/终态；AudioTrack marker/periodic 与输出采样率接入
  真实 mixer；GuestProcFacts、根上下文 timed park 快进均已交付。watchdog 与首帧契约
  见 ADR-0023/0024。
- **近期兼容性闭合**：Android View fallback 已支持 reverse-Z、deepest-first 触摸路由；
  BND-27 修复 GLES1 coordinate array 来源；DVM-91 完成 FileDescriptor/PFD/AFD 媒体
  区间能力；DVM-90 完成动态 SurfaceView holder generation。
- **仍未闭合**：DVM-47 的 A6/DH exact/长运行 gate 与 threaded 默认裁决；解释执行仍由
  `VmExecutionLock` 串行，threaded 生产默认关闭。

## 最近验证

- 2026-08-29 macOS dev/release 受影响目标通过；GLES 定向测试通过。A6 带 FFmpeg exact
  完成 GLGame/GameRenderer nativeInit 并持续渲染；Tales 越过 mapbuffer 强符号拒载。
- 2026-08-28 UTIL-3 macOS `dev` 受影响目标编译通过；算术/异常/builder/diagnostics/
  reflection/NIO/ELF/JNI guest/Profile 定向 31/31 及 architecture 6/6 通过。
- 2026-08-28 UTIL-2 macOS `dev` 受影响目标编译通过；定向测试 loader/session/input
  17/17、boundary GLES 41/41（10512 assertions）、syscall/dexvm/core 10/10
  （14637 assertions）、architecture 6/6。按约束未跑全量测试。
- 2026-08-28 macOS `dev` 全量 CTest 1066/1066 通过（约 136 s，unit 1032 + tools 25 等）。
- UTIL-1 Windows Debug `ogplay` 与 `ogplay_tests` 受影响目标编译通过；未运行测试。
- Diagnostics 与 DVM-92 Windows Release 受影响目标及定向回归通过；PVZ 2.3.12 标题画面
  点击关闭实跑确认快速正常退出。
- DVM-89 watchdog 定向覆盖 guest-call/syscall/futex/boundary/call-session/JNI lifecycle；
  首帧握手定向连续 3 轮通过，线程 24/24、monitor/wait 14/14。
- 根上下文 timed park 定向 4 用例、42 assertions 通过；当轮 `dev` 全量
  CTest 1038/1038。Windows `windows-msvc` 全目标与当轮完整 CTest 1034/1034 通过。
- DH Release 越过 license 轮询并稳定到主菜单，240 帧持续 presented，Ctrl-C 干净停止；
  PVZ Release 已进入标题画面、可输入并提交用户名。
- BND-27 Windows Debug/Release 全目标及 3 个 fixed-draw/ANGLE 定向用例通过。

## 下一步

1. 补 Tales `getPackageCodePath()` JNI 能力；完成 DH 主菜单 Scenario gate 与 profile 长跑复验。
2. 执行 A6 bootstrap 三轮、gc_long 与 threaded title gate；执行 Linux M9 严格出口复验。
3. 首次出现可复用停滞 fixture 时，补 Diagnostics 外部触发子进程验收。

## 边界

- 根上下文 timed park 可推进确定性 uptime；worker 仅在 clock driver 已阻塞时补到
  deadline。生命周期内 guest 时间不再严格等于 16 ms×帧序，不宣称 wall-clock 对齐。
- 键盘字符来自 SDL 当前宿主布局；不宣称完整 Android KeyCharacterMap、dead-key 或 IME。
- `dexvm.api19_capability_stack=complete` 只表示 bounded 设计阶段闭包，不表示完整
  Android framework、联网、完整 SQLite 或任意 title 全流程可玩。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md) ·
[Diagnostics WU1](../tasks/diagnostics/WU-DIAG-01.md) ·
[Diagnostics WU2](../tasks/diagnostics/WU-DIAG-02.md) · [Playbook](../playbook/README.md)
