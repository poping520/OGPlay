# SBX-9 · guest 目录 descriptor 全链路

## 目标（一句话）

让 guest 通过 `open(O_DIRECTORY)` 获得真实目录 descriptor，并使 fstat/fsync/
getdents64 分页形成可用闭环。

## 依赖

- SBX-4（文件元数据 syscall）。

## 验收

- guest `open(O_DIRECTORY)` → `getdents64`，不允许测试绕过 syscall 直开目录。
- 一个记录能装入而全部记录装不入时返回当前页，下一次继续且不丢条目。
- `fstat64` 不改变文件 offset；目录 fd 返回目录/权限事实，`fsync(dirfd)` 成功。

## 交付（完成）

- Android ARM `O_DIRECTORY` 加入受检 flags 并路由 `OpenDirectory`；目录路径不会被
  `O_CREAT` 错建成文件。
- VFS descriptor 提供不改变 offset 的元数据查询；目录 seek 操作快照 cursor，
  Flush 为已立即落盘元数据的幂等屏障，Truncate 明确 `-EISDIR`。
- `getdents64` 在完整记录放不下时回退一条 cursor 并返回已编组页。

## 验证

`tests/runtime/syscall_tests.cpp` 从 guest `open` 起跑目录枚举，32 字节缓冲分两页
读出两个条目；另锁定 fstat64 后下一字节仍来自原 offset。
