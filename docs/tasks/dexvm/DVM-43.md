# DVM-43 · GC-B 精确标记器

## 目标（一句话）

从 DVM-42 的统一根集出发完成非移动精确标记，只统计 live/garbage，不清扫对象。

## 依赖

- DVM-42
- `docs/design/dexvm/09-gc.md` §5、§8、§10

## 交付

- 每轮按 record 下标新建 mark bitmap 与灰栈。
- `vm_instance` 只扫描 `SlotTag::ref`，对象数组扫描元素，各对象按需连接已物化 class object。
- string、primitive array、class object、external 为叶子。
- host-backed owner 扫描时进入四张 intrinsic 侧表的注册 trace hook；builder 无 VM ref。
- 每条 record 保存原始 `reserved_bytes`，标记结果公开 live/garbage 对象数与字节数。

## AOSP 对照

- `MarkSweep.cpp`：`markObjectNonNull` → 灰栈、`processMarkStack`、`scanObject`、
  `scanFields` 的置位与逐类型遍历时序。
- 不采用 immune region/finger、并发重标记、Reference processing、card table。

## 机器验收

- `tests/dexvm/gc_tests.cpp` 的活/死矩阵覆盖实例 ref 槽、对象数组和 list 侧表唯一引用。
- 死对象只进入 garbage 统计；对象与 allocated bytes 均不改变。

## 不做

不清扫、不复用、不触发 GC，不改变任何运行行为。
