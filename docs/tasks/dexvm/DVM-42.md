# DVM-42 · GC-B 根枚举与侧表闭集

## 目标（一句话）

在不回收任何对象的前提下，把 DexVM 全部强根收口为可测试的统一访问面。

## 依赖

- DVM-29
- DVM-41
- `docs/design/dexvm/09-gc.md` §3、§4、§8、§10

## 交付

- `JniReferenceTable::VisitRoots` 只枚举全部线程 local 与 process global，明确排除 weak global。
- 类链接期按字段 descriptor 预计算 `LinkedClass::static_ref_slots`。
- `Interpreter::VisitRoots` 枚举全部 context/帧 tagged ref、`last_result`、`caught`、
  pending/exit ref、静态 ref 槽、intern/class 不朽根、JNI/Thread/session 外部根。
- intrinsic 侧表固定为 throwable/builder/list/map 四张注册闭集；`host_state` 不解释为 VM ref。
- 核实现状额外发现 `DexVmAndroidContext` 保存受生命周期管理的裸句柄；桥接层显式枚举这些
  session 根，避免 GC-B 把仍被 Activity/UI/media/stream 状态持有的对象清掉。

## AOSP 对照

- `.local/asop/dalvik/vm/alloc/MarkSweep.cpp`：`dvmHeapMarkRootSet` 根清单。
- `.local/asop/dalvik/vm/alloc/Visit.cpp`：`visitThread`/`dvmVisitRoots` 的 frame、Thread、
  JNI local/global、intern 与 VM internal roots。
- 静态字段按 `scanStaticFields` 的 descriptor 首字符（`L`/`[`）判引用。

## 机器验收

- `tests/dexvm/gc_tests.cpp` 锁定 permanent/static/JNI/session 根与四张侧表闭集。
- JNI local/global 被枚举，weak global 不被枚举。
- full CTest 无回归。

## 不做

本 WU 不标记、不清扫、不复用句柄，不改变分配或 OOM 行为。
