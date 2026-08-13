# 沙盒 · 每游戏持久沙盒（ADR-0020）

设计根节点：[`docs/design/sandbox/`](../../design/sandbox/README.md)。
编号沿用设计 [05 §3](../../design/sandbox/05-verification.md) 的 `SBX-N`，
创建后不移动、不重编号，依赖用文件名声明。

要解决的用户问题：**游戏存档没有保存，每次启动都是全新状态。**

## 任务索引

| WU | 一句话目标 | 依赖 | 状态 |
| --- | --- | --- | --- |
| [SBX-1](SBX-1.md) | `SandboxStore`：布局、装载索引、原子写、tombstone、转义、配额 | — | 完成 |
| [SBX-2](SBX-2.md) | VFS 目录操作 + `Truncate/Flush/FlushAll` 内存语义 + 目录 Stat | — | 完成 |
| [SBX-3](SBX-3.md) | `AttachSandbox`：overlay 解析、脏节点跟踪、flush 点 | SBX-1、SBX-2 | 完成 |
| [SBX-4](SBX-4.md) | syscall 缺口绑定与 ABI 编组 | SBX-2 | 完成 |
| [SBX-5](SBX-5.md) | DexVM File 族改线 VFS、`mkdirs` 去伪成功 | SBX-3 | 完成 |
| [SBX-6](SBX-6.md) | SharedPreferences XML 持久（framework + dexvm 共享） | SBX-3 | 完成 |
| [SBX-7](SBX-7.md) | session/CLI 接线、一次性沙盒、确定性回归 | SBX-3 | 完成 |
| [SBX-8](SBX-8.md) | 覆盖层删除/重建正确性与防复活 | SBX-3 | 完成 |
| [SBX-9](SBX-9.md) | guest 目录 descriptor、fstat 与真分页 | SBX-4 | 完成 |
| [SBX-10](SBX-10.md) | pause/shutdown 落盘与 prefs 错误语义 | SBX-3、6、7 | 完成 |
| [SBX-11](SBX-11.md) | Java File/包装输出流与 float prefs 完整性 | SBX-5、6 | 完成 |
| [SBX-12](SBX-12.md) | store 装载冲突、meta 与合并配额硬化 | SBX-1、3、8 | 完成 |

SBX-7 提前到 SBX-4..6 之前执行：它只依赖 SBX-3，先做完 CLI 接线就能端到端
验证覆盖层，后面三个通道收敛各自落地即刻生效。

## 端到端验收

进入游戏产生存档 → 正常退出 → 重新启动 → 游戏读到旧存档。以 scenario/人工
验收 + 沙盒目录文件断言双口径确认。

**尚未达成。** SBX-1..12 全部交付后，三条 guest 写入通道（native syscall、
DexVM `File`、SharedPreferences）都已收敛到同一个 VFS，机制由机器测试逐条
覆盖，其中跨会话持久有三个独立用例（VFS 层、Java `File` 层、prefs 层）。
但用户级闭环还演示不了：当前最深入的 title（Dungeon Hunter）只到标题画面，
本地有界运行不触发任何存档写入，沙盒里只有 `meta.toml`。等 title 深度推进到
能进游戏后补这一步。

验收补强后的 Windows/MSVC 结果：聚焦沙盒/目录/prefs/DexVM/lifecycle 回归
37/37，全量 CTest 663/663。本轮没有把 exact-title 重跑结果冒充为新证据。
