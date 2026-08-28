# ADR-0023 · native watchdog 只按可观测进展续期

- 状态：Accepted
- 日期：2026-08-28
- 关联：[ADR-0017](0017-bounded-dex-interpreter.md)、
  [DVM-89](../tasks/dexvm/DVM-89.md)

## 背景

DexVM 出向 JNI native 帧可能长期拥有进程循环，因此旧实现允许每次已处理的 syscall、
HLE 或 JNI 边界把 tick watchdog 清零。纯 `getpid`/`clock_gettime` 等查询循环也因此可无限
续期；经过边界并不等于业务进展。

## 决定

supervisor 分发统一返回三类结果：`not_handled`、`handled_idle`、
`handled_advanced`。只有标记为 `renewable_native_frame` 的 JNI native 帧遇到
`handled_advanced` 才把本帧 watchdog 消耗清零。普通 guest thread runner 不续期，行为不变；
exit request 仍在每个已处理边界后优先检查，预算耗尽继续抛带 consumed/PC/LR 的既有诊断。

分类表集中在 syscall dispatcher 与 Android boundary 分发层：

- advanced：返回正字节数的 read/pread/write/pwrite；真实进入 wait queue 后唤醒或非零 timeout
  到期的 futex wait；成功 `eglSwapBuffers`；成功 OpenSL ES BufferQueue Enqueue；JNI 重入。
- idle：身份/时间/stat 等查询，零字节或 EOF read，零字节 write，futex value mismatch、
  零 timeout、进入 wait 前已存在的 interrupt 与 futex wake，sched_yield，内存映射/保护等
  其余已处理调用。进入 wait 后才收到 interrupt 仍证明真实驻留，归 advanced。
- 未列出的已处理 syscall/HLE 默认 idle。新增 family 必须显式进入可审计表并由测试锁定，
  不得因“已处理”自动获得续期。

nanosleep 只有经受检 timespec 请求并确实发生非零 host sleep 才标 advanced；错误或零时长
为 idle。

## 取舍与已知限制

JNI 回调会执行任意 guest Java 代码，是 JNI 边界可提供的最强进展信号，故无条件标
advanced。反复调用琐碎 JNI 的 native 死循环仍可能续期；边界无法可靠区分琐碎调用与实质
工作，这是接受的 bounded 限制。本决定修复纯 syscall 空转无限续期，不声称建立业务语义
追踪。

这对应审计建议中的预算类型区分：普通调用保留总 tick 预算；只有显式参与进程级续期的
native 帧拥有可续期 watchdog，且续期由可观测进展触发。取消/退出预算不并入进展预算，
继续由独立 exit request 安全边界抢占。

## 后果

纯查询或 EOF 空转会稳定耗尽既有预算；park、数据偏移推进、present/audio enqueue 与 JNI
重入可维持长期 native 循环。保守默认可能使尚未分类但合法的长期 HLE 更早暴露既有预算
诊断，这是有意的 fail-closed 行为。
