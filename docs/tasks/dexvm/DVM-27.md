# DVM-27 · 解释器 per-thread 执行状态拆分

## 目标（一句话）

在不改变单线程语义的前提下，把 DexVM 帧栈、pending exception、monitor
持有与 tick 等执行态从全局 Interpreter 实例拆为显式 per-thread context。

## 依赖

- DVM-26 固化的 A6 `Object.wait()` 边界。
- `docs/design/dexvm/02-architecture.md` §7–§9 与 `04-integration.md` §3–§4。

## 验收

- 每个执行 context 独立持有帧栈、异常、tick 与 monitor recursion，不在
  多线程间共享可变执行态；类链接、对象模型与 intrinsic 目录仍由 VM 共享。
- 现有解释器、Asphalt 5 exact 行为不变，增加两个 context 交错调用的
  隔离测试。
- 本 WU 不启动宿主线程、不实现 wait-set，不宣称 `dexvm.threads` ready。

## 结果（已完成）

- `InterpreterExecutionContext` 现显式选择独立的帧栈、pending exception、
  返回值、tick 与 monitor recursion；默认 context 保持既有 `Call` 语义，linker、
  object model、intrinsic 目录与统计仍由 Interpreter 共享。
- 活跃 context 用每个宿主线程独立的路由保存，native/intrinsic 回入解释器时沿用
  当前 context；在一次活跃调用中切换 context 会明确报内部不变量错误。
- dexasm 夹具增加双 context 交错调用测试，覆盖 tick 保留、异常不泄漏、monitor
  持有隔离和跨 Interpreter context 拒绝。未启动宿主线程、未实现 wait-set，
  `dexvm.threads` 仍不宣称 ready。
- Asphalt 5 exact 回归保持 468 帧/468000 tick 与主界面 SHA-256
  `9ee57323dae576c38d4d29984c067b5bceaa86f77724c8f3b174bcd1a81962b8`；
  Asphalt 6 exact 仍稳定在零首帧的 `Object.wait()V` 明确缺口，边界未被掩盖。
