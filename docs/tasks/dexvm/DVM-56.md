# DVM-56 · Threaded 对象指令家族

## 目标（一句话）

把 monitor/type/分配/数组/switch/字段族迁入 FastCode 直达 handler，并将类型、
字段与数组元素类别解析收敛为执行锁内的一次 checked→fast 翻转。

## 依赖

- DVM-55
- `docs/design/dexvm/10-interpreter-threaded.md` V2-4
- `.local/asop/dalvik/vm/mterp/c/OP_{NEW,AGET,APUT,IGET,IPUT,SGET,SPUT}*.cpp`

## 交付

- 对象族按 `object_checked/object_fast` 分类；首执行缓存 `DexClassId`/`VmFieldId`
  与 new-array 元素类别，后续不再走常量池解析。
- 缓存翻转只发生在 `VmExecutionLock` 内且只改 FastCode 派生元数据，不改 u2。
- packed/sparse/fill-array-data 直接读取受检边表，payload 附加 tick 保持原点位。
- 分配、clinit、monitor、数组/字段 tag 与隐式异常仍复用既有 VM 契约。

## 验证

- 双后端夹具覆盖数组、对象、字段、type、clone、clinit、switch，首执行/次执行
  明确断言 checked→fast 且结果一致。
- Windows MSVC Debug 字段循环（`sget/sput`，200 次 × 400，预热后 3 轮）中位数：
  switch 69,911 us，threaded 49,538 us，threaded 快 29.1%；只报告，不设时序断言。

状态：完成。
