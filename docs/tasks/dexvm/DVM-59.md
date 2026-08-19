# DVM-59 · Threaded 稳态循环与 Precheck 闭合

## 目标（一句话）

把 v2 做成设计文档要求的单函数 threaded 循环，并补上 FastCode 去 bounds check
所依赖的 Precheck 证明；验收用 dexvm 微基准与双后端夹具，不跑三款游戏。

## 依赖

- DVM-58
- `docs/design/dexvm/10-interpreter-threaded.md`
- `.local/docs/优化验收建议.md`

## 交付

- `PrecheckMethod` 校验 `k35c`/`k3rc` 参数寄存器表、`k22c` wide pair，以及
  long/double 算术与转换的 wide 源/目的寄存器。畸形 DEX 在 switch 与 threaded
  上都抛 `DexVmError`/`invalid_register`，不走 `vector[]` 越界。
- `StepThreaded` 是同帧稳态循环：FastCode 下标为 ip，`Slot* regs` 与 tick 驻留
  局部变量；handler 经 `.inc` 拼入同一函数。同帧下一条走 `fetch`，不调用
  `frames.back()`。GCC/Clang 按 opcode computed goto（每 opcode 独立 stub），
  MSVC 循环内稠密 `FastHandler` switch。压帧/弹帧/pending 才回到 `Run()`。
  `force_all_bridge` 永久保留为 `Step()` 桥回归锚点。
- packed-switch 按 first-key O(1) 索引；非算术 straight 不再探测
  `ExecuteArithmetic`；invoke 参数封送改栈上 `array`，去掉热路径 `vector`。
- `<clinit>` 在 `ResolveDescriptor`/`AddClass` 之后按 id 重取 `LinkedClass`，
  避免合成 primitive `TYPE` 时把悬挂引用读成垃圾 method id。
- dexasm interpreter 夹具按 switch/threaded 双后端跑；FastCode 结构反例诊断
  与 Precheck 同 reason/同 what()；直达 handler 对照 switch 的 instruction
  tick 与 opcode。Windows Release（MSVC 稠密 switch）微基准三轮中位相对
  switch：straight +34%、object +39%、invoke −13%、array +22%、
  packed-switch +38%、instance +39%、virtual −5%、wide +37%、
  instance-of +34%。Debug 同机对照仍有更大百分比，因 switch 内核未优化。
  只报告、不设时序断言。

## 验证与裁决

- 机器门禁：`tests/dexvm/interpreter_tests.cpp` 与
  `tests/dexvm/fast_code_tests.cpp`；微基准 `*microbenchmark*` 只报告、不设
  时序断言。本轮不把 A5/A6/DH title wall-time 作为验收。
- `dexvm.interpreter_threaded` 保持 `partial`，生产默认仍为 switch。

状态：完成（结构与 Precheck 闭合；能力不推进 complete）。
