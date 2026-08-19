# DVM-46 · GC-B 水位触发、OOM 最后一搏与 Profile 参数

## 目标（一句话）

只在解释器安全分配点按确定性水位触发 GC，并把开关接入 Profile v2/v3 与统计日志。

## 依赖

- DVM-44
- DVM-45
- `docs/design/dexvm/09-gc.md` §3、§7、§9、§10

## 交付

- `runtime.dexvm.gc_watermark_percent` 范围 0..100，默认 75；0 精确关闭 GC-B。
- 只在 `new-instance`、`new-array`、`filled-new-array(/range)`、
  `const-string(/jumbo)` 分配前检查 `allocated + request > budget × watermark%`。
- 安全点回收后仍超硬预算时沿用真实 `heap_budget_exhausted` → OOME 路径；
  intrinsic/JNI/异常内部不触发回收，保留 watermark 与 budget 间余量。
- `InterpreterStats` 增加 cycle、累计回收字节、峰值、pause ticks、host destructor 次数。
- `runtime.dexvm.gc` 每轮记录 trigger/live/freed/object/destructor/pause 字段；pause 只用
  确定性对象访问 tick，不读取 wall clock。
- Profile C++ parser、Python validator、v2 schema（v3 引用它）、CLI bridge 与三条现有
  DexVM title profile 同步参数；`System.gc()` 保持合法 no-op。

## AOSP 对照

- `Heap.cpp`：`tryMalloc`/`gcForMalloc`/`GC_FOR_MALLOC`/`GC_BEFORE_OOM` 的先回收后失败次序。
- `dvmMalloc` 的 `ALLOC_DONT_TRACK` 注释用于确认临时根风险；本实现以安全点封闭集替代。

## 机器验收

- 小预算真实 DEX 循环在开启时完成、GC count/freed bytes 非零且水位有界。
- 同一夹具 `gc_watermark_percent = 0` 回到 GC-A 并真实 OOM，GC count 为零。
- schema/validator 拒绝范围外参数，Profile 目录 current gate 通过。

## 不做

`System.gc()` 不强制回收；不在 intrinsic、JNI 入向或异常物化期间触发。
