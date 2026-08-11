# 子模块：runtime/jni

## 职责

实现完整 JNI/JavaVM ABI 目录及 M3 常用行为，包括类型、引用、异常、签名、Modified UTF-8、
字符串、数组、对象、类、字段、方法调用、native 注册、标准 native 导出名和线程附着。

## 依赖

只依赖 `core` 与标准库；对象身份可以表示宿主对象或未来 VM 对象，但不得依赖 framework、
Bionic、syscall、execution 或 integration。

## 不变量

- 233 个 JNIEnv 槽与 8 个 JavaVM 槽的索引稳定。
- 未绑定槽记账并 trap；引用类别、线程作用域和 pending exception gate 必须严格检查。
- 已解析方法缺少 implementation handler 时，错误必须携带规范 implementation ID，禁止
  丢失定位所需的注册表身份。
- guest handle 保持固定宽度，不暴露宿主指针。
- JNI monitor 按强类型 object identity 隔离 owner guest thread、recursion 与 waiters；同线程
  可重入，非 owner exit 明确失败。thread detach 释放其全部 ownership，sticky shutdown
  interrupt 唤醒全部当前 waiter 并拒绝后续 enter，避免 session teardown 永久阻塞。
- `BuildJniNativeExportNames` 严格验证 class/method/descriptor，并按 JNI 规范同时产出
  short 与含参数 descriptor 的 long 名；Unicode 必须先转为 UTF-16 code unit 再转义。

## 测试

对应 `tests/runtime/jni*_tests.cpp`、字段/数组/对象/JavaVM 契约测试。
