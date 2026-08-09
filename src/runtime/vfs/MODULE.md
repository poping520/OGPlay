# 子模块：runtime/vfs

## 职责

提供 Android 绝对路径规范化、APK/OBB/外置来源挂载、文件元数据，以及隔离 descriptor 的
open/read/write/seek/close 核心。

只读 APK/OBB backing 可仅挂载路径、尺寸和显式全量读取回调；`Stat`、`Open`、`Seek` 不触发
读取，首次 `Read` 才物化内容，严格核对声明尺寸并缓存成功结果。

## 依赖

只依赖标准库；不得依赖 syscall、JNI、framework、Bionic、execution 或 integration。
syscall 与 framework Asset 只能单向调用本模块。

## 不变量

- 路径索引按 ASCII 大小写不敏感，拒绝逃逸和歧义。
- 挂载事务化；来源与可写性事实不可丢失。
- 懒加载 backing 只允许 APK/OBB 只读来源；失败不得缓存为成功，返回尺寸必须与挂载元数据
  完全一致。
- descriptor offset 隔离，错误携带稳定 Linux errno。
- pipe 返回隔离的只读/只写 descriptor，共享同一有序字节流；创建和 guest descriptor
  数组发布必须是事务性的。

## 测试

对应 `tests/runtime/vfs_tests.cpp`，并由 syscall 与 Asset 契约累计覆盖。
