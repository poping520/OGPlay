# DVM-57 · Threaded invoke 与轮询裁决

## 目标（一句话）

把五种 invoke 及 range 变体迁入 FastCode 直达 handler，缓存寄存器 word 列表、
解析方法与参数 shorty，并依据现有证据裁决 stop 双表机制。

## 依赖

- DVM-56
- `docs/design/dexvm/10-interpreter-threaded.md` V2-5
- `.local/asop/dalvik/vm/mterp/README.txt`
- `.local/asop/dalvik/vm/mterp/c/OP_INVOKE_*.cpp`

## 交付

- FastCode 构建期预拼装 35c/3rc register words 与基础 invoke 类型。
- 首执行解析常量池方法并把 descriptor 归一为参数 shorty；缓存后
  `invoke_checked` 翻为 `invoke_fast`，virtual/interface/super 动态派发仍逐次执行。
- interpreted/intrinsic/native、同步方法、clinit、异常与 trace 路径保持原契约。
- stop 双表不实施：现有 benchmark 已在保留 `Tick()` 每指令 relaxed stop 检查下
  获得收益，没有证据值得把“每指令检查”改为有界延迟；teardown 契约不变。

## 验证

- 双后端覆盖 direct/static/virtual/interface/super、解释/intrinsic 与跨帧异常；
  checked→fast、register words 与 `{'I'}` shorty 均有机器断言。
- Windows MSVC Debug invoke 循环（200 次调用 × 400，预热后 3 轮）中位数：
  switch 274,968 us，threaded 220,322 us，threaded 快 19.9%；只报告，不设时序断言。

状态：完成。
