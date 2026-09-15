# DIAG-3 · Guest 退出根因诊断

目标：让 A32 native 调用中的 guest 退出不再退化为固定文本，而是完整保留退出来源、fatal 原文、调用边界和 CPU 调用链。

依赖：[Diagnostics 设计](../../design/diagnostics/README.md)、[DIAG-1](DIAG-1.md)。

## 范围

- guest 生命周期状态持久保存 host request、`exit`、`exit_group` 来源，以及 requester、退出码、syscall PC/LR。
- `tgkill(SIGABRT)` 按默认 fatal action 请求进程组退出，保存 signal、target、PC/LR 和退出码 134；信号 0 只探测线程，其他信号仍明确 `-ENOSYS`。
- syscall `write` 捕获 fd 1/2；`open("/dev/log/*")` 使用进程内诊断 descriptor，`write`/`writev` 把 payload 送入结构化 logger，不创建宿主设备。
- A32 调用异常统一输出目标地址、累计 tick、受影响/请求线程、最后 stop、寄存器和有界指令窗口。
- native 调用异常从 r11 按 API 19 GCC frame record 有界展开 16 帧，逐帧输出归一化 PC、原始 LR、FP 和停止原因。
- DexVM native 桥补充 class、method、descriptor、guest thread、context token，保留底层多行原因。
- 不识别 title、包名或第三方库；不实现一般信号 handler 投递、完整 Linux logger 驱动或无 frame pointer 的启发式扫描。

## 验收

- [x] 单线程及进程组退出来源可机器判定并跨 lifecycle 状态转换保留。
- [x] host request 与真实 A32 `exit` syscall 的即时错误包含调用、退出和 CPU 现场。
- [x] SIGABRT 对全部 live guest 线程保存相同终止事实。
- [x] stderr 与 `/dev/log/main` vectored payload 可机器判定。
- [x] FP chain 正常、不可读、终止和非单调边界均有界。
- [x] Windows Release 受影响目标构建及定向测试通过。
- [x] 使用真实 APK 复现时，错误可定位到 Java native 方法、`io::IOException`、recursive terminate、SIGABRT syscall 和 6 层有效 guest 调用链。

状态：已完成。
