# DVM-44 · GC-B 清扫器与句柄复用

## 目标（一句话）

把 DVM-43 判定的不可达 record 真实释放，使预算记账下降并确定性复用空闲句柄。

## 依赖

- DVM-43
- `docs/design/dexvm/09-gc.md` §6、§10

## 交付

- record、instance storage、object-array storage 三条 LIFO 空闲链。
- string/primitive-array 清扫调用 session store `Delete`；实例/对象数组槽清空后复用。
- 清理 identity 映射、throwable/builder/list/map 侧表、空 monitor 与 JNI weak global。
- record 保存的 `reserved_bytes` 回减 `allocated_bytes`，`ObjectCount` 只报告 live records。
- `At()`/`IsValidRef()` 明确识别已回收 record；复用前悬垂访问携带 handle 失败。
- 回收句柄按固定线性 sweep + LIFO free-list 确定性复用。

## AOSP 对照

- `MarkSweep.cpp`：`dvmHeapSweepSystemWeaks` 与 `dvmHeapSweepUnmarkedObjects` 的
  弱引用/monitor 清理先于对象释放时序。
- 不采用 heap bitmap 差集、dlmalloc footprint 或 `madvise`。

## 机器验收

- store Delete、allocated bytes 精确下降、侧表清空、悬垂失败、句柄复用。
- 连续第二轮 `freed_objects == 0` 且 `freed_bytes == 0`。

## 不做

不执行 guest finalizer，不处理 ReferenceQueue，不触发自动 GC。
