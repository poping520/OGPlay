# SBX-1 · SandboxStore 宿主持久存储

## 目标（一句话）

在 `runtime/vfs` 内实现唯一接触沙盒目录的组件：布局、装载索引、原子写、
tombstone、宿主文件名转义、配额与崩溃残留清理。

## 依赖

- 无（不触碰任何既有会话行为，可与 SBX-2 并行）。

## 验收

- 布局往返：写 → 重开 → 索引、尺寸、内容一致。
- tmp + 同目录 rename 原子替换；装载清理残留 tmp 且正式文件完好。
- tombstone 写读；转义往返对 Windows 非法字符、`%`、结尾句点/空格、保留设备
  名字节级无损。
- traversal 与保留后缀逃逸拒绝；配额与文件数超限 `-ENOSPC`。
- meta.toml schema/package/未知键不匹配明确失败。

## 交付（完成）

- `include/ogplay/runtime/vfs/sandbox_store.h` + `src/runtime/vfs/sandbox_store.cpp`。
- 布局 `<root>/<package>/{meta.toml, fs/}`，`fs/` 与 guest 绝对路径 1:1，
  用户可直接备份或手工删除单个存档。
- `WriteFileAtomic`/`WriteTombstone`/`CreateDirectory`/`Remove`/`Rename`/
  `ReadFile`/`Entries`，条目按 guest 路径有序，供后续 overlay 枚举保持确定。
- 配额默认 256 MiB / 65536 文件；`RecordVersionCode` 只作诊断事实。
- `tests/runtime/sandbox_store_tests.cpp` 12 个用例，全部在测试自建临时目录
  内运行，不触碰用户数据目录。

## 验证

macOS/arm64 CTest 622/622；既有 VFS 与会话行为零变化（本组件尚未 attach）。
