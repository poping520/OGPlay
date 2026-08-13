# SBX-12 · SandboxStore 装载与配额硬化

## 目标（一句话）

让手工损坏、大小写冲突和未落盘脏数据都经过受检错误与真实配额口径。

## 依赖

- SBX-1、SBX-3、SBX-8。

## 验收

- meta 数字损坏抛 `VfsError`；ASCII 大小写折叠冲突在 attach 前失败。
- 字节配额合并 store 已落盘字节与全部 VFS 脏节点；超限在 write/truncate 点报
  `-ENOSPC`，不推迟到 close。
- tombstone 不占活动文件数配额，达到上限仍可删除底层文件。

## 交付（完成）

- meta 数字改为 `from_chars` 严格解析；装载索引发布前建立 ASCII folded key 冲突表。
- VFS 节点记录已计入 store 的尺寸，写入/截断以“已落盘 - 被替换尺寸 + 全部脏尺寸”
  预检配额。
- SandboxStore 文件数配额只计非 tombstone 活动项，删除永不因 tombstone 新增而
  `-ENOSPC`。
- 宿主布局继续使用 VFS 规范化后的 ASCII 小写 guest 路径；不再承诺保存调用者的
  首见大小写拼写。

## 验证

`tests/runtime/sandbox_store_tests.cpp` 覆盖 malformed meta、跨平台可构造的 `%53ave`/
`save` 冲突与满额删除；`tests/runtime/vfs_sandbox_tests.cpp` 覆盖两个未 flush 文件的
合并字节配额。
