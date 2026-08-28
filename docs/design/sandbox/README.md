# Sandbox · 每游戏持久沙盒方案设计

本目录是"每个游戏一个持久宿主沙盒目录，guest 可写命名空间跨会话持久化"方案的
完整设计，写给两类读者：**决策者**（评审 [ADR-0020](../../adr/0020-per-title-persistent-sandbox.md)）
和**实施阶段的 AI**（作为该能力开发的长期上下文根节点）。

要解决的用户问题只有一句话：**游戏存档没有保存，每次启动都是全新状态。**

## 效力声明

- 本方案与项目现有设计冲突时，以本方案为准；全部已识别冲突及裁决见
  [04 · §5 契约修订清单](04-integration.md)。未在清单中的新冲突，实施时按
  "本方案优先"补记。
- 本方案**不改变**项目元规范：AGENTS.md 的工作流约束（WU 有界、机器可判定
  测试、能力记账、明确失败、结构化日志、`src/` 零游戏名分支）
  对沙盒全部适用。
- ADR-0020 处于 Proposed；评审通过前不启动实施。实施启动后，代码与
  `MODULE.md` 契约逐步接管，本目录转为设计溯源。

## 一句话架构

> 每个 package 一个宿主沙盒目录；guest 可写命名空间（`/data/data/<pkg>/`、
> `/sdcard/`）以文件粒度 overlay 覆盖在只读 APK/OBB/external 底层之上；
> 写入在确定性 flush 点原子落盘，删除用 tombstone；syscall、framework HLE、
> DexVM 三条文件通道全部收敛到同一个 VFS，不各自造持久化。

由此，存档、`SharedPreferences`、游戏自建目录树都真实落在宿主磁盘上，
跨会话保留，可整体备份迁移；而 OBB 等只读大包仍原地懒挂载不复制。

## 阅读顺序

| 篇 | 内容 |
| --- | --- |
| [01 · 根因、目标与非目标](01-scope.md) | 为什么存档会丢、做什么、明确不做什么 |
| [02 · 核心架构](02-architecture.md) | 命名空间分层、宿主布局、路径与命名规则 |
| [03 · 持久化语义](03-persistence.md) | flush 点、原子性、崩溃安全、配额、失败模式 |
| [04 · 运行时集成](04-integration.md) | VFS API 扩展、syscall 缺口、DexVM/prefs 收敛、CLI 接线、契约修订 |
| [05 · 验证与实施](05-verification.md) | 测试策略、能力条目、WU 分解、风险 |

## 术语

| 术语 | 含义 |
| --- | --- |
| 沙盒（sandbox） | 一个游戏的宿主持久目录，键为 APK package name |
| 可写命名空间 | guest 视角允许写入的路径根集合（`/data/data/<pkg>/`、`/sdcard/` 等） |
| 底层（base layer） | 只读来源：APK assets、OBB、`--external-dir` 宿主目录 |
| 覆盖层（overlay layer） | 沙盒背衬的可写层，文件粒度覆盖底层同路径文件 |
| tombstone | 覆盖层中表示"底层同路径文件已被 guest 删除"的标记 |
| SandboxStore | `runtime/vfs` 内新增的宿主持久存储组件，唯一接触沙盒目录的代码 |
| flush 点 | 把脏内存节点原子写入沙盒目录的确定性时机（close/fsync/pause/shutdown） |
| 一次性沙盒 | 自动化用临时目录沙盒，会话结束即弃，保证 golden gate 确定性 |
