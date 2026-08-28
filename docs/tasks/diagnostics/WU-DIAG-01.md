# WU-DIAG-01 · 进程内停滞诊断闭环

目标：让主循环停滞、guest 静默 park 或 teardown 卡住时，无需重新插桩和重编译即可从
进程外触发一份有界、允许局部失败且不误报死锁的跨层现场。

依赖：[Diagnostics 设计](../../design/diagnostics/README.md)、
[ADR-0026](../../adr/0026-bounded-stall-snapshots.md)；复用 DVM-52 trace/Java 栈、
NativeActivity GLES trace、futex/monitor 状态和 ADR-0025 teardown cancellation。

## 范围

- schema-1 文本/JSON 共用不可变 snapshot DTO，逐 section 发布采样时间、generation、
  `complete|partial|unavailable` 和失败原因。
- 汇总 lifecycle/Suspend/teardown、guest↔host execution/ARM PC、DVM/Java 栈、GLES、
  futex、monitor、EGL pacer、syscall 和 native bridge 事实。
- syscall/native 热路径只写构造期定容整数环；溢出丢最旧并累计 dropped，方法文本仅在
  查询时由稳定 id 解析。
- 所有运行时来源使用 try-snapshot；单个来源 busy 或抛错只降级对应 section，不阻塞或
  丢弃其他证据。
- monitor 分开等待取得 owner 的 entry waiter 与 `Object.wait()` notify set；只有显式
  entry→owner 边形成环时才发布 `confirmed_cycle`。
- 支持 teardown steady timeout、Windows 当前用户 named event、POSIX signal/self-pipe、
  CLI `diag snapshot` 和 MCP `diag.snapshot`。
- 输出采用临时文件 + 原子 rename，限制单文件、目录总字节和保留数量；宿主绝对路径与
  URL 脱敏，Windows 使用当前用户保护 DACL，POSIX 使用 owner read/write。
- coordinator 析构先唤醒 OS wait，再丢弃未开始请求并 join；禁止 detach，provider owner
  析构前必须清除回调。

## 验收

- [x] Windows Release `ogplay`、`ogplay_tests` 受影响目标构建通过。
- [x] ring 关闭态、enter/return/throw、覆盖顺序与 dropped count 可判定；零容量不分配或
  保留热路径事件。
- [x] futex 只报告 wait/wake 事实；monitor entry/notify waiters 分离，只有显式 owner 环
  产生 `confirmed_cycle`。
- [x] DVM execution lock 被占用时 `TryTrace`/`TryStackSnapshot` 立即返回 busy；其他
  section 和 JSON 仍完整生成。
- [x] guest-call/clone 登记 host/guest/context、ARM PC、entered/last progress，并保留
  有界退出 tombstone。
- [x] Windows OS event、teardown timeout、CLI/MCP 请求进入同一协调器；快照进行中退出
  和排队请求不产生 detach、UAF 或无限等待。
- [x] 路径/URL 脱敏、单文件/目录配额、保留数量和 Windows 当前用户 ACL 可判定。
- [x] 新增核心用例 9/9、97 assertions；连同 futex/syscall/MCP、A32 watchdog、5 项架构
  检查和 host_tid 工具共 25/25 定向通过；按项目约束未运行全量测试。
- [ ] 可杀死的真实停滞子进程中，从另一进程触发 OS 路径并限时落盘。当前没有合适的
  可复用场景，经用户明确决定延期到后续首次遇到时补验；该延期不表示实现缺失，不阻塞
  本 WU 完成。

## 非目标

- 不在进程内暂停任意宿主线程或展开完整 native stack。
- 不把 futex waiter 集合、notify wait set 或未闭合 native enter 自动断言为死锁。
- 诊断预算只报警取证，不改变 ADR-0025 的取消、硬杀或 guest teardown 语义。
- 不实现 title、包名、厂商或游戏专属分支。

状态：已完成；外部停滞子进程场景按用户决定延期补验。
