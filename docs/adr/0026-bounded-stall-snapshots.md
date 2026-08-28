# ADR-0026 · 停滞诊断使用有界部分快照与外部宿主栈

- 状态：Accepted
- 日期：2026-08-28
- 关联：[Diagnostics](../design/diagnostics/README.md)、
  [ADR-0025](0025-teardown-cancellation-and-graphics-retirement.md)

## 背景

局部 DVM/GLES/fault trace 无法在主循环停滞后统一回答 guest 线程、native 边界、syscall、
futex 和 teardown 所处位置。进程内暂停任意宿主线程并展开完整栈又可能占用 loader、heap
或符号锁，使诊断成为新的死锁源。

## 决定

frontend 为一次进程运行创建 `DiagnosticState` 与 `DiagCoordinator`，显式把窄状态对象注入
session/runtime。事实源只记录定容事件或提供 `try_lock` 快照；任一 section 繁忙时输出
`unavailable`，不得阻止其余 section 落盘。协调器只由内部请求、teardown 宿主稳态超时、
Windows 当前会话命名 event 或 POSIX signal self-pipe 唤醒，析构固定 stop→join，禁止
detach。

Futex 只能报告等待集合和 wake 历史；没有 owner edge 时不得生成或暗示确认死锁环。完整
宿主原生栈由 procdump/WinDbg/lldb 在进程外采集，再按快照 `host_tid` 对齐；无符号帧只保留
`module+offset`。

## 后果

停滞现场可以在不重新插桩、不依赖 SDL 主循环的条件下取得，但各 section 不是全局原子
时刻。输出必须携带 schema、采样时间、generation 和 section 状态；自动预算只报警取证，
不改变 ADR-0025 的退出取消语义。
