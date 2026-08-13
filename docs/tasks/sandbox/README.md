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
| [SBX-4](SBX-4.md) | syscall 缺口绑定与 ABI 编组 | SBX-2 | 待开始 |
| [SBX-5](SBX-5.md) | DexVM File 族改线 VFS、`mkdirs` 去伪成功 | SBX-3 | 待开始 |
| [SBX-6](SBX-6.md) | SharedPreferences XML 持久（framework + dexvm 共享） | SBX-3 | 待开始 |
| [SBX-7](SBX-7.md) | session/CLI 接线、一次性沙盒、确定性回归 | SBX-3 | 完成 |

SBX-7 提前到 SBX-4..6 之前执行：它只依赖 SBX-3，先做完 CLI 接线就能端到端
验证覆盖层，后面三个通道收敛各自落地即刻生效。

## 端到端验收

进入游戏产生存档 → 正常退出 → 重新启动 → 游戏读到旧存档。以 scenario/人工
验收 + 沙盒目录文件断言双口径确认。
