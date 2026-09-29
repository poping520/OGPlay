# BND-41 · ARM 进程信号与线程暂停恢复

状态：本阶段完成（有界进程内信号，完整范围见下文）。决策：[ADR-0079](../../adr/runtime.md#adr-0079)。

## 范围

- syscall 拥有唯一进程 signal runtime；旧/RT ABI 分开，tgkill 检查进程及活跃线程。
- 目标线程执行真实 guest handler，保存/恢复 ARM、Thumb、VFP、mask 和备用栈；支持嵌套。
- clone 继承 mask；退役清理状态；主 native 调用、clone、headless 在安全边界投递。
- futex 等待可被目标线程信号中断，SA_RESTART 恢复无超时等待；sigsuspend 原子换 mask，
  handler 返回恢复原 mask，退出请求解除等待。
- 标准信号按位合并；不实现实时队列、作业控制、定时器、同步 CPU 异常转信号，
  或中断任意宿主 IO；有超时 futex 中断返回 EINTR。

## 验证

- Windows `windows-msvc` Release `ogplay_tests` 与 `ogplay` 构建通过；沿用本机
  `_CL_=/wd4996 /wd4834` 屏蔽既有测试告警。信号、syscall/bridge、runner/clone、futex
  定向 68 用例/573 断言通过。
- 解释器与 Dynarmic 的真实 guest 载荷覆盖阻塞线程暂停→sigsuspend→嵌套恢复→重启 futex。
- ABI/失败参数、屏蔽/继承/退役、ARM/Thumb/VFP 恢复、备用栈、guest 修改 ucontext、
  非重启 EINTR 与退出唤醒由定向测试验证。
- 原 APK/OBB 无 Profile、无 survey、临时沙盒、3 帧上限启动：原 SIG_SUSPEND 注册失败
  消失，越过 GC 信号初始化；下一首错为 Dynarmic `Should raise coproc exception here`
  断言。GC 还提示 /proc/stat 缺失，但不是本次退出的直接报告。
- 新 CPU 首错不在本工作单修复；尚未触达游戏渲染，也未完成实际 GC 全周期或游戏验收。
- 证据：`.local/signal-{build-verified,tests,startup}.log`。
