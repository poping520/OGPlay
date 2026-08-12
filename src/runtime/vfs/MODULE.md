# 子模块：runtime/vfs

## 职责

提供 Android 绝对路径规范化、APK/OBB/外置来源挂载、文件元数据，以及隔离 descriptor 的
open/read/write/seek/close 核心；并提供每游戏持久沙盒的宿主存储
`SandboxStore`（ADR-0020）。

只读 APK/OBB backing 与宿主 external 目录可仅挂载路径、尺寸和显式全量读取回调；
`Stat`、`Open`、`Seek` 不触发读取，首次 `Read` 或非截断 `Write` 才物化内容，严格核对
声明尺寸并缓存成功结果。external 修改只存在于会话内，不反写宿主目录。
`ListDirectory` 返回某目录路径的直接子项名（文件与隐式目录名，排序去重），目录通过
其下挂载的文件隐式存在；不触发读取。

`SandboxStore`（`sandbox_store.h`）是**唯一接触沙盒目录的代码**：
`<root>/<package>/` 下 `meta.toml` 记 schema/package/versionCode，`fs/` 与 guest
绝对路径 1:1 镜像，用户可直接看懂、备份、手工删除单个存档。文件写一律
tmp + 同目录 rename（崩溃只会留旧内容或新内容，不会半截）；装载时清理残留
`*.__ogplay_tmp__` 并计数上报。删除底层文件用 `.__ogplay_tombstone__` 空文件
标记。宿主文件名对 Windows 非法字符、`%` 本身、结尾句点/空格与保留设备名做
`%XX` 百分号转义，只看字节、双向无损。配额默认 256 MiB / 65536 文件，超限
`-ENOSPC`。attach 到 VFS 由后续 WU 承接；本组件本身不改变任何既有会话行为。

## 依赖

只依赖标准库；不得依赖 syscall、JNI、framework、Bionic、execution 或 integration。
syscall 与 framework Asset 只能单向调用本模块。

## 不变量

- 路径索引按 ASCII 大小写不敏感，拒绝逃逸和歧义。
- 挂载事务化；来源与可写性事实不可丢失。
- 懒加载 backing 允许 APK/OBB 只读来源与受检宿主 external 目录；external 保持会话内
  可写，符号链接、特殊文件、空目录与大小写歧义必须在发布挂载前失败。读取失败不得缓存
  为成功，返回尺寸必须与挂载元数据完全一致。
- descriptor offset 隔离，错误携带稳定 Linux errno。
- relative guest 路径只有在调用方显式设置受检绝对工作目录后才解析；解析复用相同的
  ASCII 大小写折叠与 traversal 拒绝规则，未配置时不得猜测目录。
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
`SandboxStore` 对应 `tests/runtime/sandbox_store_tests.cpp`（布局跨开启往返、
原子替换与崩溃残留清理、转义双向无损、逃逸与保留后缀拒绝、字节/文件数配额
`-ENOSPC`、tombstone 遮蔽与解除、非空目录 `-ENOTEMPTY`、rename、meta.toml
拒绝），全部在测试自建临时目录内进行，不触碰用户数据目录。
