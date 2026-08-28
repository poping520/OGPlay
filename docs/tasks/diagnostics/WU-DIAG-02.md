# WU-DIAG-02 · 宿主栈与排障工作流

目标：把 WU-DIAG-01 的 guest 语义现场与进程外宿主 native dump 按 `host_tid` 对齐，形成
Windows、Linux 和 macOS 可重复执行的排障流程，同时在缺少符号时保持事实边界。

依赖：[WU-DIAG-01](WU-DIAG-01.md)、
[Diagnostics 设计](../../design/diagnostics/README.md)、
[排查手册](../../playbook/TROUBLESHOOTING.md) 和 [ADR-0026](../../adr/0026-bounded-stall-snapshots.md)。

## 范围

- Windows 使用 procdump/WinDbg 获取进程外完整 dump 与全部线程栈；Linux/macOS 使用
  lldb attach 和 `thread backtrace all`。
- `tools/diagnostics/align_host_threads.py` 读取 schema-1 snapshot 与 WinDbg/lldb 文本，
  规范十进制/十六进制 TID 并只按稳定 `host_tid` 合并事实。
- 记录 Windows `windows-msvc` 诊断构建与 PDB、POSIX DWARF 的使用边界。
- 缺少符号时只保留工具实际提供的 `module+offset`，禁止依据相邻符号猜函数名。
- 手册规定快照、dump、线程对齐、判读和敏感现场清理顺序。

## 验收

- [x] fixture snapshot 与 WinDbg 十六进制线程 id 可对齐到正确 guest execution。
- [x] fixture snapshot 与 lldb 十六进制/十进制 tid 可对齐。
- [x] 未匹配宿主线程与缺失符号保持明确，不生成猜测函数名。
- [x] `tests/tools/test_align_host_threads.py` 定向通过。
- [x] `docs/playbook/TROUBLESHOOTING.md` 包含 procdump、WinDbg、lldb、PDB/DWARF、
  `module+offset` 和现场清理步骤。
- [x] `debug.host_stack_workflow` 能力账本、设计文档和模块契约一致。

## 非目标

- 不在 OGPlay 核心中引入 `SuspendThread` + DbgHelp、任意 pthread unwind 或常驻采样器。
- 不解析、修复或伪造第三方 ANGLE、SDL、驱动内部符号。
- 不把宿主栈替代 WU-DIAG-01 的 guest wait、lifecycle 和 boundary 事实。

状态：已完成。
