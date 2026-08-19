# DVM-55 · Threaded 直线指令家族

## 目标（一句话）

把 move/const/return/goto/if/cmp/算术族迁入 FastCode 直达 handler，同时保持
tag、异常、tick、trace 与 switch 后端逐位一致。

## 依赖

- DVM-54
- `docs/design/dexvm/10-interpreter-threaded.md` V2-3
- `.local/asop/dalvik/vm/mterp/c/OP_*.cpp`

## 交付

- FastCode 构建期把直线族分类为 `FastHandler::straight`，预拼装寄存器、literal
  与内部 branch target。
- 直达 handler 不再读 u2 或重复检查寄存器边界；tag/wide-pair/zero-as-null 校验保留。
- cmp 与全部算术族复用唯一 `ExecuteArithmetic` 语义体，避免双内核语义分叉。
- 未迁移的 object/invoke/switch 继续 bridge，默认后端仍为 switch。

## 验证

- FastCode + 双后端直线/算术/控制流聚焦门禁通过。
- Windows MSVC Debug tight-loop（`loopSum(200)` × 400，预热后 3 轮）中位数：
  switch 44,712 us，threaded 41,988 us，threaded 快 6.1%；基准只报告，不设时序断言。

状态：完成。
