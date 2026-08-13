# SBX-8 · 覆盖层删除与重建正确性

## 目标（一句话）

消除覆盖层文件删除后的旧数据复活，以及 tombstone 窗口内“已创建但不可见”的
会话内矛盾。

## 依赖

- SBX-3（覆盖层与落盘点）。

## 验收

- 底层文件经历“改写 → 重启 → 删除 → 重启”后仍为 `-ENOENT`。
- 删除后 `O_CREAT` 的节点在 close/fsync 前即可 Stat、枚举和再次删除。
- 只读 `O_CREAT` 创建的空文件在 close 后跨会话存在。

## 交付（完成）

- 可写命名空间内删除一律保守写 tombstone；冗余 tombstone 不改变语义，但不再
  依赖已经丢失的底层 provenance 猜测。
- 创建节点时立即清内存 tombstone，并把“overlay 归属”与“dirty 内容”分离；
  新建本身置脏，单纯 write-open 不置脏。
- unlink 后仍存活的 descriptor 失去落盘路径，close/fsync 不会复活已删除名字。

## 验证

`tests/runtime/vfs_sandbox_tests.cpp` 的 rewrite/delete/restart、delete/recreate、
read-only create 与 clean write-open 用例均为回退即失败形态。
