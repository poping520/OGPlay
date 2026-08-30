# DVM-96 · Switch/Threaded 共享对象与 Invoke 语义

## 目标

保留两种 dispatch/operand source，但让调用目标选择和 intrinsic 调用边界只保留一份语义。

## 交付

- switch 从 u2、threaded 从 FastCode 取得操作数后，共用 `SelectInvokeTarget` 处理
  virtual/interface/super/static/direct/constructor 和 Survey fallback。
- 两后端共用 DVM-94 `ResolvedCallSite` 与 `MethodShape`；FastCode 仍是只读派生缓存。
- `InvokeIntrinsic` 在 handler 前验证 receiver、参数数量/类别，在返回后立即验证
  cat1/wide/ref/void，不再把错误拖延到 `move-result*`。
- threaded 固定小栈参数缓冲、tick glue、computed-goto/MSVC switch 与默认 profile 均不变。

## 验收

- [x] 错误 receiver、参数和返回类别在统一边界失败。
- [x] force-all-bridge、reflection invoke、array exception 与 invoke 双后端定向测试通过。
- [x] switch 保持默认，threaded 能力状态仍为 partial。

状态：已完成。
