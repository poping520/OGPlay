# 当前状态

更新：UTIL-1 基础编码与字节工具收敛完成；DVM-92 与 Diagnostics 已闭合

## 当前阶段

- **UTIL-1 已完成代码整合**：canonical UTF-8、策略化 UTF-16→UTF-8、Base64/hex、
  little-endian/range/alignment 与 JNI guest-memory 读取已有唯一共享实现；loader、Android
  flags 与 TOML 域错误边界保留。`windows-msvc` 的 `ogplay`/`ogplay_tests` 编译通过，
  按用户要求未运行测试，见 [UTIL-1](../tasks/maintenance/UTIL-1.md)。
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
- **项目约束已调整**：WU 不再限制触及文件数量，代码文件不再设 800 行上限；代码改动
  默认只构建受影响目标并运行直接相关的单点/定向测试，全量测试仅在用户明确要求时运行。
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

- UTIL-1 Windows Debug `ogplay` 与 `ogplay_tests` 受影响目标编译通过；未运行测试。
- Diagnostics Windows Release `ogplay`/`ogplay_tests` 受影响目标构建通过；ring/drop、busy
  partial、DVM try-safe-point、monitor cycle、OS event/teardown timeout、ACL、目录配额和
  in-flight stop→join 核心新增用例 9/9、97 assertions；连同 futex/syscall/MCP、A32
  watchdog、5 项架构检查和 host_tid 工具共 25/25 定向通过。按约束未跑全量测试。
- DVM-92 Windows Release `ogplay`/`ogplay_tests` 构建通过；Java EGL/native boundary
  退役 2/2、25 assertions，renewable watchdog 与 teardown 回归 6/6、41 assertions。
  用户实跑 PVZ 2.3.12 标题画面点击关闭，确认快速正常退出；按要求未跑全量。
- DVM-89 watchdog 定向覆盖 guest-call/syscall/futex/boundary/call-session/JNI lifecycle；
  首帧握手定向连续 3 轮通过，线程 24/24、monitor/wait 14/14。
- 根上下文 timed park 定向 4 用例、42 assertions 通过；当轮 `dev` 全量
  CTest 1038/1038。Windows `windows-msvc` 全目标与当轮完整 CTest 1034/1034 通过。
- DH Release 越过 license 轮询并稳定到主菜单，240 帧持续 presented，Ctrl-C 干净停止；
  PVZ Release 已进入标题画面、可输入并提交用户名。
- BND-27 Windows Debug/Release 全目标及 3 个 fixed-draw/ANGLE 定向用例通过。

## 下一步

1. 通用闭合 A6 `DT_SONAME` identity；完成 DH 主菜单 Scenario gate 与 profile 长跑复验。
2. 复验 DVM-47/threaded title gate；执行 Linux M9 严格出口复验。
3. 首次出现可复用停滞 fixture 时，补 Diagnostics 外部触发子进程验收。

## 边界

- 根上下文 timed park 会推进确定性 uptime；生命周期内 guest 时间不再严格等于
  16 ms×帧序，不宣称 wall-clock 对齐。
- 键盘字符来自 SDL 当前宿主布局；不宣称完整 Android KeyCharacterMap、dead-key 或 IME。
- `dexvm.api19_capability_stack=complete` 只表示 bounded 设计阶段闭包，不表示完整
  Android framework、联网、完整 SQLite 或任意 title 全流程可玩。
- 未实现能力继续记账并明确失败；长期限制见 [KNOWN-ISSUES.md](KNOWN-ISSUES.md)。

任务索引：[APK Startup](../tasks/apk-startup/README.md) ·
[DexVM](../tasks/dexvm/README.md) · [Layout UI](../tasks/layoutui/README.md) ·
[Diagnostics WU1](../tasks/diagnostics/WU-DIAG-01.md) ·
[Diagnostics WU2](../tasks/diagnostics/WU-DIAG-02.md) · [Playbook](../playbook/README.md)
