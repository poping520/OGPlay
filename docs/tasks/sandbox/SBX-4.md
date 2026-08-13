# SBX-4 · 文件元数据与目录 syscall 绑定

## 目标（一句话）

把 native 存档流程会碰到的文件元数据与目录 syscall 从 `-ENOSYS` 绑到同一个
VFS 上，并锁定 ABI 布局。

## 依赖

- SBX-2（目录操作）。

## 验收

- 新增各 syscall 的 guest 指针受检、`struct stat64`/`linux_dirent64` ABI
  布局锁定、errno 映射；`getdents64` 小缓冲区分页。

## 交付（完成）

- `src/runtime/syscall/syscall_file_metadata.cpp`：mkdir/mkdirat/rmdir/
  unlink/unlinkat/rename/renameat、stat64/lstat64/fstat64/fstatat64、
  getdents64、access/faccessat、ftruncate/ftruncate64、fsync/fdatasync、
  pread64/pwrite64。目录中补齐 rename/rmdir/fsync/ftruncate64/renameat 五个
  此前未声明的编号。
- `VirtualFileSystem::OpenDirectory`/`ReadDirectory`：目录 descriptor 在 open
  时快照子项，`getdents64` 分页因此稳定；对目录 descriptor 读写字节
  `-EISDIR`。
- `struct stat64` 按 Android ARM 打包布局（96 字节）编组，偏移由测试锁定。
  `st_mode` 的权限位取自 VFS 的真实 `writable` 事实，不凭空编造；时间戳保持
  0——唯一时间源是统一 Clock，VFS 不持有它（设计 04 §2）。
- `pread64`/`pwrite64` 不扰动 descriptor offset。
- `*at` 的相对路径没有真实 per-process cwd，明确 `-ENOTSUP` 而不是拿一个
  目录去猜。
- 顺带对齐：ephemeral 运行也建立可写命名空间根目录，使它与持久运行除持久性
  外行为一致（设备上 `/sdcard` 总是存在）。

## 验证

`tests/runtime/syscall_tests.cpp` 新增 4 个用例（mkdir/stat64/unlink 全链路
与 ABI 偏移、getdents64 记录对齐与分页、access/rename/positional IO、`*at`
相对路径拒绝）；macOS/arm64 CTest 651/651；Asphalt 5 exact 逐位持平。
