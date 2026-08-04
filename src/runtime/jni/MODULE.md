# 子模块：runtime/jni

## 职责

实现完整 JNI/JavaVM ABI 目录及 M3 常用行为，包括类型、引用、异常、签名、Modified UTF-8、
字符串、数组、对象、类、字段、方法调用、native 注册和线程附着。

## 依赖

只依赖 `core` 与标准库；对象身份可以表示宿主对象或未来 VM 对象，但不得依赖 framework、
Bionic、syscall、execution 或 integration。

## 不变量

- 233 个 JNIEnv 槽与 8 个 JavaVM 槽的索引稳定。
- 未绑定槽记账并 trap；引用类别、线程作用域和 pending exception gate 必须严格检查。
- guest handle 保持固定宽度，不暴露宿主指针。

## 测试

对应 `tests/runtime/jni*_tests.cpp`、字段/数组/对象/JavaVM 契约测试。
