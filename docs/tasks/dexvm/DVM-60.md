# DVM-60 · Threaded dispatch 尾跳、invoke wide 安全与热路径

## 目标（一句话）

闭合 invoke FastCode 的 J/D 参数 pair 证明，把 GCC/Clang 分派改成 handler 尾部
indirect goto，并把 ticks/invoke 热路径成本压到不再慢于 switch；验收只用
dexasm、双后端单测和 Release 微基准，不跑三款游戏。

## 依赖

- DVM-59
- `docs/design/dexvm/10-interpreter-threaded.md`

## 交付

- `invoke_checked` 在解析 descriptor/shorty 之后校验 J/D 的寄存器 pair：
  `lo+1 < registers_size`；k35c 还要求列出的两个 word 连续（`hi == lo+1`）。
  switch 内核走同一 `CheckInvokeWidePair`。畸形 DEX 抛 `DexVmError` /
  `invalid_register`，禁止 GetFastWide OOB。双后端反例对照完整 diagnostic。
- GCC/Clang：每个 opcode handler 尾部 `DISPATCH_*` 直接 `goto *op_labels[next]`，
  不再回到共享 fetch 间接跳。handler 仍全部位于 `StepThreaded`，由 `.inc`
  组织。MSVC 保留 `fetch_at` + 稠密 `FastHandler` switch loop。
- 热路径 `ticks` / `executed` 只驻留局部变量；bridge、异常、EnsureInitialized、
  解释压帧、yield 才回写。stop/tick budget 仍是先加后判、命中在同一条指令。
  同帧热路径不 `frames.back()`、不重复 `IndexForDexPc`。解释 invoke 经
  `Frame::fast_ip` 恢复调用方/被调方。
- invoke 热路径：不再每调用 value-init 256 个 `VmValue`（改 ≤8 槽栈缓冲）；
  invoke-virtual 用缓存的 `vtable_index` 直取 receiver vtable；invoke-interface
  仍按名查找。DVM-59 在 MSVC Release 上 invoke-static −13%、invoke-virtual −5%
  的主因是 256 槽清零与按名 vtable 查找，以及每次压帧后的 dex-pc 索引。
  优化后两类 opcode 均快于 switch，保留 threaded 直达路径，不降级到 bridge。

## 验证与裁决

- 机器门禁：`tests/dexvm/interpreter_tests.cpp`（含 invoke-wide 35c 反例）与
  `tests/dexvm/fast_code_tests.cpp`。微基准 1 轮预热后 5 轮取中位数，只报告、
  不设时序断言。本轮不把 A5/A6/DH title 作为验收。
- Windows Release（MSVC 稠密 switch）微基准中位相对 switch：straight +48%、
  object +53%、invoke-static +41%、array +26%、packed-switch +51%、
  instance +51%、invoke-virtual +51%、wide +52%、instance-of +51%。
- `dexvm.interpreter_threaded` 保持 `partial`，生产默认仍为 switch。

状态：完成（热路径与 wide 安全闭合；能力不推进 complete）。
