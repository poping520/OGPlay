# DVM-65 · java.lang.Class core

## 目标（一句话）

以 linker 与 reflection runtime 的唯一 metadata 为事实源，闭合 API-19
`java.lang.Class` 的结构、类型关系与 declared/public 成员查询，不提前伪造
invoke、实例化、Field/Array 操作或 system metadata。

## 依赖

- DVM-62（reflection linker metadata）
- DVM-63（single ClassLoader facade）
- DVM-64（immutable reflection metadata 与 wrapper factory）
- [11 · Class、ClassLoader facade 与有界反射基础栈](../../design/dexvm/11-class-reflection-loader.md)
- 本地 API-19 libcore `Class.java` 与 Dalvik `java_lang_Class.cpp`

## 交付

- `Class` 结构查询覆盖 name/simple name、defining loader、component、superclass、
  direct interfaces、modifiers 以及 array/interface/primitive/enum/synthetic 分类。
- `isAssignableFrom`/`isInstance` 唯一委托 linker 类型关系；`cast`/
  `asSubclass` 保留 API-19 的 null 与异常语义，`toString` 区分 primitive、
  interface 与 class。
- `ReflectionRuntime` 新增 declared/public Method、Constructor、Field 精确查找与
  数组物化。public Method/Field 按 class → superclass → direct interface 递归
  确定性聚合并去重；Constructor 不继承。
- `getDeclared*`/`get*` 每次返回 fresh wrapper；null parameter array 视为空，
  null name 抛 NPE，未命中按成员类型抛 `NoSuchMethodException`/
  `NoSuchFieldException`。
- nested/enclosing simple-name 规则、generic/annotation/signature 与声明异常由 DVM-69
  system metadata 闭合；invoke/newInstance/Field/Array 操作依次留给 DVM-66..68。

## 验证与裁决

- `tests/dexvm/reflection_tests.cpp` 以专用 hierarchy fixture 覆盖 primitive/array/
  interface/class 结构、可赋值性、cast 异常、declared/public 成员集合、
  精确查找、可见性过滤、继承与 interface 聚合顺序。
- 只运行 DVM-65 新增用例及相邻 reflection 回归，不跑全量测试。
- 新能力记为 `dexvm.class_reflection_core = complete`。

## Chapter 11 收尾复验

- `Class` facade 补齐两个 `forName` overload；public `getClassLoader()` 继续遵循本地
  API19 `Class.java`：primitive 为 null，bootstrap 非 primitive 为 boot facade，
  application/array 按 defining/component loader 返回稳定 facade。

状态：完成（含 Chapter 11 closure 复验）。
