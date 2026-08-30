# 子模块：runtime/vfs

## 职责

提供 Android 绝对路径规范化、APK/OBB/外置来源挂载、文件元数据，以及隔离 descriptor 的
open/read/write/seek/close 核心；并提供每游戏持久沙盒的宿主存储
`SandboxStore`（ADR-0020）。

只读 APK/OBB backing 与宿主 external 目录可仅挂载路径、尺寸和显式全量读取回调；
`Stat`、`Open`、`Seek` 不触发读取，首次 `Read` 或非截断 `Write` 才物化内容，严格核对
声明尺寸并缓存成功结果。external 修改只存在于会话内，不反写宿主目录。
`ListDirectory` 返回某目录路径的直接子项（名字 + `is_directory`，排序去重），
显式目录（`CreateDirectory`）与隐式目录（其下挂载了文件）合并为同一份稳定序，
`getdents64` 由此确定；不触发读取。目录操作 `CreateDirectory`/`RemoveFile`/
`RemoveDirectory`/`Rename` 与 `Truncate`/`Flush`/`FlushAll` 均为内存语义，
errno 契约与平台一致（父目录缺失 `-ENOENT`、已存在 `-EEXIST`、对目录 unlink
`-EISDIR`、对文件 rmdir `-ENOTDIR`、非空 `-ENOTEMPTY`、只读来源 `-EACCES`）。
目录 `Stat` 返回目录事实而非 `-ENOENT`。文件 rename 要求源和目标父目录存在，可覆盖
目标文件且同路径只在源真实存在时成功；目录整棵子树的 rename 尚无调用方，明确
`-EINVAL` 而不是猜测。未 attach 沙盒时 `Flush`/`FlushAll` 只校验
descriptor，不伪装落盘。
`OpenDirectory` 产生 guest 可用的目录 descriptor；目录快照 cursor 支持分页与
受检 seek，`DescriptorInfo` 不移动 offset，目录 `Flush` 是元数据已立即落盘后的
幂等屏障，目录 `Truncate` 明确 `-EISDIR`。

`SandboxStore`（`sandbox_store.h`）是**唯一接触沙盒目录的代码**：
`<root>/<package>/` 下 `meta.toml` 记 schema/package/versionCode，`fs/` 与 guest
绝对路径 1:1 镜像，用户可直接看懂、备份、手工删除单个存档。文件写一律
tmp + 同目录 rename（崩溃只会留旧内容或新内容，不会半截）；装载时清理残留
`*.__ogplay_tmp__` 并计数上报。删除底层文件用 `.__ogplay_tombstone__` 空文件
标记。宿主文件名对 Windows 非法字符、`%` 本身、结尾句点/空格与保留设备名做
`%XX` 百分号转义，只看字节、双向无损。guest 路径按 VFS ASCII 小写规范落盘；
装载时 folded key 冲突明确失败。配额默认 256 MiB / 65536 活动项，字节口径合并
已落盘与未 flush 脏节点，tombstone 不占活动项配额，超限 `-ENOSPC`。
`AttachSandbox(store, writable_roots)` 把它挂成 VFS 的覆盖层：解析顺序为
覆盖层文件 → 覆盖层 tombstone（视为 `-ENOENT`，枚举也不出现）→ 只读底层；
写入只允许落在可写命名空间内（默认 `/data/data/<pkg>`、`/sdcard`，这些根本身
成为目录），越界 `-EACCES`。落盘点按设计 03 §2 枚举：`close`、`Flush`(fsync)、
`FlushAll`(pause/shutdown) 落文件内容，`mkdir`/`unlink`/`rmdir`/`rename` 立即落
元数据。可写命名空间内删除保守写 tombstone；新建立即解除会话内遮蔽，unlink
或 rename 覆盖后的存活 descriptor 不得在 close 时复活或覆盖该名字。write-open
本身不置脏，只有新建、写入、截断与 rename 才落盘，避免无变化重写。脏字节
以聚合增量维护（size/dirty 只经 `SetNodeSizeDirtyLocked` 变更），配额检查
O(1)，不随文件数扫描。guest rename 由覆盖层组合完成（写新名 + 旧名
tombstone），`SandboxStore` 自身不提供 rename。
未 attach 时行为与既有逐字节一致——持久化是纯增量能力。

## 依赖

只依赖标准库；不得依赖 syscall、JNI、framework、Bionic、execution 或 integration。
文件分工：`vfs.cpp` 挂载与 descriptor IO，`vfs_sandbox.cpp` 目录/元数据操作与
沙盒覆盖层，`sandbox_store.cpp` 宿主存储，`vfs_internal.h` 为三者共享内部状态。
syscall 与 framework Asset 只能单向调用本模块。

## 不变量

- 路径索引按 ASCII 大小写不敏感，拒绝逃逸和歧义。
- 挂载事务化；来源与可写性事实不可丢失。
- 懒加载 backing 允许 APK/OBB 只读来源与受检宿主 external 目录；符号链接、特殊文件、
  空目录与大小写歧义必须在发布挂载前失败（此约束只针对底层挂载，沙盒覆盖层允许空目录）。
  读取失败不得缓存为成功，返回尺寸必须与挂载元数据完全一致。
- 底层（APK/OBB/external 宿主目录）永远只读、永不反写；guest 对 external 文件的修改
  经覆盖层持久化（ADR-0020 取代了旧契约"external 修改只存在于会话内"）。
- descriptor offset 隔离，错误携带稳定 Linux errno。
- relative guest 路径只有在调用方显式设置受检绝对工作目录后才解析；解析复用相同的
  ASCII 大小写折叠与 traversal 拒绝规则，未配置时不得猜测目录。
- 显式 path alias 在规范化后把别名前缀映射到同一 canonical 节点；alias 与 canonical
  路径必须共享 descriptor、host backing 与 sandbox overlay，禁止复制两套文件状态。
- pipe 返回隔离的只读/只写 descriptor，共享同一有序字节流；创建和 guest descriptor
  数组发布必须是事务性的。
- `HostPathFor` 只对宿主目录挂载的文件返回其 backing 宿主路径(供需要直接读宿主文件
  的解码器使用);APK/OBB 条目、会话内新建文件与不存在的路径一律返回空,不猜测路径。
- `SandboxStore` 的 guest 路径永远落不到 `fs/` 之外：traversal、绝对段与保留
  后缀在写入前拒绝。沙盒键只用 manifest 受检的 package name，版本升级共享
  存档。meta.toml 的 schema 不匹配、package 不符或出现未知键一律明确失败，
  不猜测迁移。沙盒从不静默降级为内存模式。

## 测试

对应 `tests/runtime/vfs_tests.cpp`，并由 syscall 与 Asset 契约累计覆盖。宿主目录事务测试
使用预存 guest 路径的 ASCII 大小写折叠冲突，不依赖宿主文件系统能否创建反斜杠文件名。
目录操作与 `Truncate`/`Flush` 的内存语义、errno 契约与枚举稳定序在同一文件内
按用例覆盖。沙盒覆盖层对应 `tests/runtime/vfs_sandbox_tests.cpp`，核心用例是
**跨会话持久**：同一宿主目录上先后两个 VFS，会话 2 逐字读到会话 1 留下的内容；
另覆盖 overlay 遮蔽与 tombstone 不复活、可写命名空间越界 `-EACCES`、落盘点
（写入不落、fsync 落、close 落、幂等）、元数据立即落盘、未 attach 时行为不变。
`SandboxStore` 对应 `tests/runtime/sandbox_store_tests.cpp`（布局跨开启往返、
原子替换与崩溃残留清理、转义双向无损、逃逸与保留后缀拒绝、字节/文件数配额
`-ENOSPC`、tombstone 遮蔽与解除、非空目录 `-ENOTEMPTY`、meta.toml
拒绝），全部在测试自建临时目录内进行，不触碰用户数据目录。
