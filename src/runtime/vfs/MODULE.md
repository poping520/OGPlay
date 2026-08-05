# 子模块：runtime/vfs

## 职责

提供 Android 绝对路径规范化、APK/OBB/外置来源挂载、文件元数据，以及隔离 descriptor 的
open/read/write/seek/close 核心。

## 依赖

只依赖标准库；不得依赖 syscall、JNI、framework、Bionic、execution 或 integration。
syscall 与 framework Asset 只能单向调用本模块。

## 不变量

- 路径索引按 ASCII 大小写不敏感，拒绝逃逸和歧义。
- 挂载事务化；来源与可写性事实不可丢失。
- descriptor offset 隔离，错误携带稳定 Linux errno。
- pipe 返回隔离的只读/只写 descriptor，共享同一有序字节流；创建和 guest descriptor
  数组发布必须是事务性的。

## 测试

对应 `tests/runtime/vfs_tests.cpp`，并由 syscall 与 Asset 契约累计覆盖。
