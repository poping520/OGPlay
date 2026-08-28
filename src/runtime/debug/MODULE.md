# runtime/debug

## 责任

- 汇总 lifecycle、同步原语、syscall/native boundary 与 guest↔host 执行事实，输出
  versioned、允许部分失败的停滞快照。
- 独立等待 teardown 预算、MCP/CLI 请求和 OS 外部触发；不依赖 SDL 主循环存活。
- 完整宿主原生栈不在进程内展开，由 playbook 规定的外部 dump 工具获取。

## 契约

- 下层只接收显式注入的 `DiagnosticState` 并发布固定容量事实，不反向依赖 session。
- 数据源只允许 atomic、固定 ring 或有界 try-snapshot；busy section 标为 unavailable，
  禁止等待 `VmExecutionLock` 或执行持锁 I/O。
- coordinator 由会话拥有，stop 唤醒后 join，禁止 detach；数据源必须比 coordinator
  活得更久。
- futex wait-set 没有 owner 边，不得标为 confirmed deadlock。
- JSON schema、steady-clock 时间戳、截断和写盘失败必须明确；输出不得包含宿主用户
  绝对路径或未受检 guest 字符串。
- syscall/native 热路径只写进程级定容整数环；方法文本、DVM/GLES 和 Java 栈在查询时
  通过不等待的 provider 解析。monitor 的 entry waiter 与 Object.wait notify set 必须分开；
  仅 entry→owner 等待边形成环时发布 confirmed cycle。
- 输出同时受单文件、总目录字节和快照数量约束；Windows 文件和命名 event 使用仅当前
  用户的保护 DACL，POSIX 文件替换为 owner read/write。

## 非目标

- 不实现任意线程 `SuspendThread`/DbgHelp 或 POSIX `backtrace()` 展开。
- 不替代 procdump、WinDbg、lldb，也不改变 teardown cancellation 语义。
