# WU-DIAG-03 · Guest 退出根因诊断

目标：让 A32 native 调用中的 guest 退出不再退化为固定文本，而是直接保留退出来源、调用边界和 CPU 现场。

依赖：[Diagnostics 设计](../../design/diagnostics/README.md)、[WU-DIAG-01](WU-DIAG-01.md)。

## 范围

- guest 生命周期状态持久保存 host request、`exit`、`exit_group` 来源，以及 requester、退出码、syscall PC/LR。
- A32 调用异常统一输出目标地址、累计 tick、受影响/请求线程、最后 stop、寄存器和有界指令窗口。
- DexVM native 桥补充 class、method、descriptor、guest thread、context token，保留底层多行原因。
- 不识别 title、包名或第三方库，不改变退出语义。

## 验收

- [x] 单线程及进程组退出来源可机器判定并跨 lifecycle 状态转换保留。
- [x] host request 与真实 A32 `exit` syscall 的即时错误包含调用、退出和 CPU 现场。
- [x] Windows Release 受影响目标构建及定向测试通过。
- [x] 使用真实 APK 复现时，错误可定位到 Java native 方法、syscall 和 guest 地址。

状态：已完成。
