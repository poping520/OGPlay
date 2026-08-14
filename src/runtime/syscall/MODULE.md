# 子模块：runtime/syscall

## 职责

实现 Android ARM Linux syscall 目录、SVC bridge、ARM kernel helper、VFS syscall 适配，以及
exit/exit_group/clear-child-tid 所需的 guest 线程生命周期状态。

## 依赖

依赖 `cpu`、`memory`、`hal` 与 `runtime/vfs`；不得依赖 Bionic、JNI、framework、execution
或 integration。

## 不变量

- 未实现和未知 syscall 统一返回 `-ENOSYS` 并可观测。
- guest 地址必须经受检内存访问；时间源只使用统一 Clock。
- `pipe` 必须先验证完整两元素输出数组，再原子创建 VFS descriptor pair；发布失败回收
  两端，不泄漏半完成状态。
- 线程状态只能按 running → exit-requested → exited → reap 前进。

## 测试

对应 `tests/runtime/syscall_tests.cpp`、guest thread lifecycle 与 SVC bridge 测试。

## 文件元数据与目录（ADR-0020）

`BindAndroidFileMetadataSyscalls`（`syscall_file_metadata.cpp`）把
mkdir/rmdir/unlink/rename 及其 `*at` 变体、stat64 家族、`getdents64`、
`access`、`ftruncate`、`fsync`/`fdatasync` 与 `pread64`/`pwrite64` 绑到与
`BindAndroidFileSyscalls` 同一个 VFS。`struct stat64` 按 Android ARM 自然对齐
布局（104 字节，非 x86 的打包布局）编组：`st_mode` 在 16、`st_size` 在 48、
`st_blksize` 在 56、`st_blocks` 在 64、`st_ino` 在 96，44 处的填充必须保持 0。
guest libc 的 `__swhatbuf` 就是从 104 字节栈帧的偏移 56 读 `st_blksize`；若按
96 字节打包布局编组，guest 读到的 64 位 `st_size` 高位字会是我们的
`st_blksize`，`fopen`+`fread` 会把每个文件都看成 TB 级并 malloc 失败。
`linux_dirent64` 记录 8 字节对齐且只发完整记录；两者偏移由机器测试锁定。guest `open(O_DIRECTORY)` 取得快照目录 fd；open flags 按
ARM EABI 布局解码（`arch/arm` 的 `O_DIRECTORY=040000`、`O_NOFOLLOW=0100000`、
`O_LARGEFILE=0400000`，不是 asm-generic 值），bionic `opendir()` 的真实组合由
机器测试锁定。记录装不下时回退 cursor 并返回当前页，下一次继续。`fstat64` 直接查询 descriptor 元数据，
不改变文件 offset，目录 fd 的 fsync/fstat 也有明确语义。`st_mode` 的权限位来自 VFS 真实 writable 事实，时间戳
保持 0（唯一时间源是统一 Clock）。`*at` 的相对路径没有真实 per-process cwd，
明确 `-ENOTSUP`。`flock`、`*xattr`、`inotify*` 等维持 `-ENOSYS` 记账。
