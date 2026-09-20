# 子模块：runtime/vfs

## 职责

提供唯一 Android 路径索引、节点/backing/打开状态、定位与顺序 IO、目录操作，以及按安装
实例持久化的 `SandboxStore`。VFS 只依赖标准库；ZIP/OBB、媒体、Java 与 syscall 只能从
上层注入或调用，VFS 不反向依赖它们。

## 核心模型与公共边界

- `File` 是稳定节点：拥有进程内唯一 `node_id`、来源、权限、尺寸、`generation` 与
  overlay 归属；路径只是可替换的名字。`OpenFile` 独立拥有 offset、权限和操作锁，FD 表
  保存其 `shared_ptr`。Close 先移除表项，再等待旧打开状态的在途操作；复用同一数字不会
  将旧结果提交到新 FD。
- 内存、小文件、宿主文件和上层注入的 APK/OBB backing 共用 `Read`/`ReadAt` 数据面。
  `ReadAt`/`WriteAt` 不移动 offset；普通 Read/Write/Seek 在同一打开状态串行，同一节点
  的读写/截断由节点锁排序。失败未交付字节时不推进 offset。
- 宿主目录挂载在发布前拒绝 symlink、特殊文件、空目录与大小写歧义；每个文件持有一次
  打开的宿主文件身份并做定位读取，不按路径重复打开。小读不触发全文件物化；宿主外部
  原地修改不承诺快照，截断造成短读时明确 `-EIO`。
- `VfsReadLease` 固定节点来源版本与窗口，独立于 FD offset/Close/unlink/rename。只读
  backing 共享拥有状态，并将每次读取的取消 token 传到上层注入的区间 reader；lease
  不提供无预算的全量复制接口。可写来源捕获时在统一资源预算内复制窗口，超限 `-ENOSPC`。
  `VfsConfig::resource_memory_budget_bytes` 默认 128 MiB，也覆盖归档缓存和可写 lazy
  backing 物化及后续扩容；调用方必须先取得 `ReserveResourceMemory` token，再分配并在
  资源存活期持有。`IoStatistics` 发布 backing 读取、全量物化及总预算/快照当前值与高水位。
- 全局锁只用于路径、FD 表和必要元数据；backing 读取、CRC/解压均在锁外。锁序为
  open-state → node → global metadata；不得持 global 等待 backing/node IO。同步沙盒
  fsync/Close/rename 仍允许阻塞落盘，本模块不创建后台预读或线程池。
- `ListDirectory` 合并显式/隐式目录并返回排序去重快照；`OpenDirectory` 的 cursor 在打开
  时冻结。目录树 rename、mmap、跨进程锁、WAL 与通用 POSIX 扩展不在本模块范围。

## 安装实例沙盒

`SandboxStore::Open(root, installation_id, package)` 只接收安装层已选定的实例 id，不分配
编号；安装层用 `Create` 原子占用尚未使用的 id。宿主布局固定为：

```text
<root>/<installation_id>/
  meta.toml
  internal/
  external/
  obb/
  sdcard/
```

`meta.toml` 只接受 schema 3，并校验 installation id、真实 package、versionCode 诊断值与
每实例 `ANDROID_ID`。旧 `fs/`、旧/缺失 schema、未知或混合布局、不匹配 meta 明确失败；
不迁移、不删除、不以空沙盒降级。

映射唯一集中在 `SandboxStore`：

- `/data/data/<package>/...` → `internal/...`
- `/sdcard/Android/data/<package>/...` → `external/...`
- `/sdcard/Android/obb/<package>/...` → `obb/...`
- 其他 `/sdcard/...` → `sdcard/...`

专用根优先；其他包的 external/obb 路径仍落本实例通用 sdcard。反向枚举若发现两个宿主
位置映射同一 guest 节点或大小写折叠后同一节点则拒绝。文件名转义、ASCII case-fold、越界拒绝、tmp 原子替换、
tombstone、256 MiB/65536 活动项配额继续保留。close/fsync/pause/shutdown 落文件内容；
mkdir/unlink/rmdir/rename 立即落元数据。unlink/覆盖后的存活句柄不得复活旧路径。

安装层从 library 与 sandbox 目录并集按 package 精确匹配，使用最小空缺编号：首份裸名，
后续 `-2`、`-3`。并发导入先原子创建对应 sandbox 目录占位，再发布同名 library 目录；
冲突后重扫，失败只清理本次新建内容。GUI 启动传递选中 id；裸 CLI 在零/一个实例时分配或
复用，多实例要求 `--installation-id` 消歧。SandboxStore 与 guest platform facts 始终传递
同一 id；版本字段不参与身份判定。

## 不变量

- 路径索引按 ASCII 大小写不敏感，拒绝 traversal、别名歧义和事务挂载的部分发布。
- `generation` 只随内容写入/截断变化；路径替换创建不同节点身份。alias 与 canonical
  路径共享同一节点、锁、SQLite identity 和 overlay。
- 底层 APK/OBB/宿主来源永不反写；写入只进入内存可写节点或 attached overlay。
- 未实现能力返回稳定 errno/异常；不得伪造成功或静默返回零。

## 测试

- `tests/runtime/vfs_tests.cpp`：大虚拟/宿主来源的小窗口读取统计、全局锁隔离、FD
  close/reuse、同 FD 顺序、ReadAt/WriteAt、并发截断、lease 身份/快照/预算、宿主物化预留。
- `tests/runtime/sandbox_store_tests.cpp`、`tests/runtime/vfs_sandbox_tests.cpp`：四根映射、
  双实例隔离、旧布局拒绝、ANDROID_ID、配额、tombstone、存活句柄和跨会话落盘。
- syscall/Java 文件与 SQLite 定向回归验证定位 IO、alias 锁身份、空库/空 BLOB、journal
  恢复和 ENOSPC。
