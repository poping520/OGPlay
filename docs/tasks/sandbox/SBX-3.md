# SBX-3 · AttachSandbox 覆盖层与落盘点

## 目标（一句话）

把 `SandboxStore` 挂成 `VirtualFileSystem` 的覆盖层：解析顺序、脏节点跟踪与
确定性落盘点，使 guest 写入跨会话保留。

## 依赖

- SBX-1（宿主存储）、SBX-2（目录操作）。

## 验收

- 覆盖层遮蔽底层同路径；tombstone 后 `-ENOENT` 且枚举不出现；删除不复活底层。
- 写底层文件先物化再归属覆盖层；只读底层拒写不变。
- **跨会话持久（核心）**：同一沙盒目录上先后两个 VFS，会话 2 的 Stat/Read/
  枚举逐字等于会话 1 退出时状态。
- flush 点 close/fsync/FlushAll 各自触发落盘；无脏节点时幂等。

## 交付（完成）

- `VirtualFileSystem::AttachSandbox(store, writable_roots)` 与
  `SandboxAttached()`；新增 `VfsSource::sandbox` 事实。
- 覆盖层装载：文件以懒读回调指向 store（内容不预读），目录进目录索引，
  tombstone 进遮蔽集合并把底层同路径条目移出可见集。
- 可写命名空间：写入/建目录/rename 目标越界一律 `-EACCES`；命名空间根自身
  成为目录（Android 上 `/sdcard` 就是目录）。
- 落盘点：`close` 与 `Flush`(fsync) 落该文件、`FlushAll` 落全部脏节点、
  `mkdir`/`unlink`/`rmdir`/`rename` 立即落元数据；干净节点跳过。
- 删除仍由只读底层提供的路径写 tombstone；覆盖层自有文件直接删除。
- 文件拆分：`vfs_internal.h` + `vfs.cpp`（挂载/IO）+ `vfs_sandbox.cpp`
  （目录/元数据/覆盖层），均在 800 行以内。

## 验证

`tests/runtime/vfs_sandbox_tests.cpp` 7 个用例（含跨会话持久核心用例）；
macOS/arm64 CTest 636/636；未 attach 路径行为不变，既有 VFS 用例全绿。
