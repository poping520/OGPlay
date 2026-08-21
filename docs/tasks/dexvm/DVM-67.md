# DVM-67 · Method/Constructor instantiation

## 目标（一句话）

闭合 API-19 `Constructor.newInstance` 与 `Class.newInstance` 的初始化、访问、
分配、参数转换及异常差异。

## 依赖

- DVM-62..66 reflection metadata、wrapper、Class core 与 invoke foundation
- [11 · Class、ClassLoader facade 与有界反射基础栈](../../design/dexvm/11-class-reflection-loader.md)
- 本地 API-19 Dalvik `java_lang_reflect_Constructor.cpp::constructNative` 与
  `java_lang_Class.cpp::Dalvik_java_lang_Class_newInstance`

## 交付

- `Constructor.newInstance` 拒绝 abstract/interface/array/primitive/void，执行
  per-wrapper access、ReflectionCodec 参数转换、class initialization、instance
  分配和精确 direct `<init>`；target throwable 以
  `InvocationTargetException` 包装并保持 identity。
- `Class.newInstance` 查找可访问的 `()V`，无默认构造器或不可实例化类型抛
  `InstantiationException`，构造器 target throwable 原样传播、不包装。
- class initialization failure 在构造调用前直接传播。

## 验证与裁决

- `tests/dexvm/reflection_tests.cpp` 覆盖 widening、private + accessible、clinit、
  target identity、不可实例化类型、无默认构造器以及两种实例化入口的异常差异。
- 只运行 DVM-67 与相邻 reflection 定向测试；最终全量测试由 DVM-69 后统一执行。

状态：完成。
