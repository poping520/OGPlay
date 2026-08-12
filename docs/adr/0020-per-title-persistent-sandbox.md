# ADR-0020 · 每游戏持久沙盒（可写命名空间跨会话持久化）

- 状态：Accepted（2026-08-12 评审通过并启动实施；任务单见
  [`docs/tasks/sandbox/`](../tasks/sandbox/README.md)）
- 日期：2026-08-12
- 关联：[docs/design/sandbox/](../design/sandbox/README.md)（方案设计全文）、
  [roadmap 06 · §2](../roadmap/06-user-experience.md)（"每个游戏一个独立的虚拟
  sdcard，存档互不干扰"）、[roadmap 01 · §4.6](../roadmap/01-architecture.md)
- Supersedes：`src/runtime/vfs/MODULE.md` 中 "external 修改只存在于会话内，
  不反写宿主目录" 的契约条款，以及 `docs/tasks/m5/WU-0301.md` 的
  "不持久化 guest 修改" 非目标。二者在当时是正确的范围裁剪，本 ADR 将其
  升级为受控持久化；实施时同步修订 MODULE.md。

## 背景

游戏存档不保存，每次启动都是全新状态。根因不是缺陷而是既有设计：guest 的三条
写入通道全部止于进程内存——

1. native `open/write` 经 syscall 进入 `VirtualFileSystem`，写入内存节点，
   契约明确 "external 修改只存在于会话内，不反写宿主目录"；
2. DexVM 的 `FileOutputStream`/`FileWriter` 落在 `DexVmAndroidContext::memory_files`
   会话 map；`File.mkdirs` 恒返回成功但不建目录，属伪成功，违反记账原则；
3. `SharedPreferences`（framework HLE 与 DexVM 两条线）均为进程内 map，
   从不落盘。

同时 `mkdir/unlink/stat64/getdents64/rename` 等存档流程必经的文件 syscall
仍走默认 `-ENOSYS`，真实游戏的存档代码路径尚未被完整接住。

## 决定

- 每个游戏获得一个以 **package name 为键**的宿主沙盒目录（默认位于
  [roadmap 08](../roadmap/08-naming.md) 约定的用户数据目录下，CLI 可覆盖）。
  guest 的**可写命名空间**（`/data/data/<package>/`、`/sdcard/` 及其别名）
  由该目录持久背衬：写入跨会话保留，下次启动原样可读。
- 采用**文件粒度 overlay**：APK/OBB/external 保持只读原地懒挂载（不复制大文件，
  维持 roadmap 06 §2 与现有 lazy backing 架构不变），guest 写入进入沙盒覆盖层；
  读取按 覆盖层 → 只读底层 顺序解析，删除以 tombstone 表达。
- 持久化实现在 **`runtime/vfs` 一层**（新增 `SandboxStore` 宿主存储 +
  VFS 挂接），syscall/framework/dexvm 三条写入通道全部收敛到同一 VFS，
  不在上层各自造持久化：DexVM `memory_files` 废除改走 VFS descriptor，
  `SharedPreferences` 持久化为 guest 可见的 `shared_prefs/*.xml` 文件。
- 落盘语义为**确定性 flush 点 + 同目录临时文件原子替换**（close/fsync/
  lifecycle pause/clean shutdown）；宿主 IO 失败向 guest 返回真实 errno
  并结构化记账，禁止伪造成功。
- 沙盒内容属用户数据，**不入库**；自动化（scenario runner、CTest）默认使用
  一次性临时沙盒，保证既有 golden gate 的确定性不被持久状态破坏。
- `src/` 保持零游戏名分支：沙盒键、可写根、配额均为通用机制或 Profile 纯数据。

## 后果

- "存档不保存" 结构性消失；存档随沙盒目录可整体备份/迁移，为 roadmap 07 的
  "存档管理" 功能提供存储基础（即时存档/快照仍属独立 backlog，不在本 ADR）。
- `runtime/vfs` 职责从纯内存会话文件系统扩展为 "内存语义 + 受控宿主持久"，
  模块契约、`capabilities.toml` 新增条目与既有测试需按设计文档同步演进；
  `runtime.vfs` 等既有条目状态不后退。
- 文件 syscall 缺口（mkdir/unlink/rename/stat64/getdents64/ftruncate/fsync 等）
  必须补齐为真实语义，DexVM `File.mkdirs` 伪成功被消除。
- 引入宿主文件名兼容面（Windows 保留名、大小写、路径长度）与沙盒配额两类
  新失败模式，全部按 "明确失败 + 记账" 处理，见设计文档 03 章。
