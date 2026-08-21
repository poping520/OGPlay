# DVM-68 · Field/Array

## 目标（一句话）

以统一 ReflectionCodec 与真实 VM slot/array store 闭合 API-19 Field 和
`java.lang.reflect.Array` 的 object/primitive 读写。

## 依赖

- DVM-66 ReflectionCodec/caller access
- DVM-67 reflection instantiation
- 本地 API-19 Dalvik `java_lang_reflect_Field.cpp` 与 libcore `Array.java`

## 交付

- Field object get/set 与八种 primitive getter/setter；static 先初始化且忽略
  receiver，instance 校验 null/type，reference 做 assignability，primitive 只允许
  widening，final 在未 bypass access 时拒写，volatile modifier 如实暴露。
- Array `getLength`、object/八种 primitive get/set、单维/多维 `newInstance`；
  object array 保留引用 identity 和 element assignability，primitive 共用 widening。
- null/non-array/index/negative size/void element 等失败均转为规定 Java 异常。

## 验证与裁决

- `tests/dexvm/reflection_tests.cpp` 覆盖 static/instance/ref/primitive/final/volatile/
  clinit/access，以及 object/primitive/multidimensional Array 与全部主要反例。
- 只运行 DVM-68 与相邻 reflection 定向测试；最终全量测试由 DVM-69 后统一执行。

## Chapter 11 收尾复验

- Field fresh wrapper 补齐 API19 name/declaring-name xor hash 与 exact declaration
  string；object/primitive slot 和 Array 行为保持由 ReflectionRuntime/Codec 驱动。

状态：完成（含 Chapter 11 closure 复验）。
