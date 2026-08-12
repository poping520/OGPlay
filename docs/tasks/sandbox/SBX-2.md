# SBX-2 · VFS 目录操作与文件元数据补齐

## 目标（一句话）

给 `VirtualFileSystem` 补上存档流程必经的目录操作、`Truncate`/`Flush` 与目录
`Stat` 事实，全部为内存语义，attach 沙盒后自动持久。

## 依赖

- 无（不改变既有挂载与读写语义，可与 SBX-1 并行）。

## 验收

- mkdir/rmdir/unlink/rename/ListDirectory 内存语义正确。
- errno 契约：`-ENOENT`/`-EEXIST`/`-EISDIR`/`-ENOTDIR`/`-ENOTEMPTY`/`-EACCES`。
- 枚举稳定序；目录 `Stat` 返回目录事实。

## 交付（完成）

- `VfsFileInfo::is_directory` 与新类型 `VfsDirectoryEntry`；`ListDirectory`
  改为返回名字 + 是否目录，显式目录与隐式目录合并为单一稳定序。
- `CreateDirectory`（父目录必须存在，不做隐式 mkdir -p）、`RemoveFile`、
  `RemoveDirectory`、`Rename`、`Truncate`、`Flush`、`FlushAll`。
- 目录整棵子树的 rename 尚无调用方，明确 `-EINVAL`；未 attach 沙盒时
  `Flush`/`FlushAll` 只校验 descriptor，不伪装落盘。
- 唯一既有调用方 `dexvm_android_io.cpp` 的目录列举随新返回类型更新。

## 验证

`tests/runtime/vfs_tests.cpp` 新增 7 个用例；macOS/arm64 CTest 629/629。
