# 05 · 验证与实施

## 1. 测试策略（全部机器可判定）

| 类别 | 关键用例 |
| --- | --- |
| SandboxStore 单元 | 布局往返（写→重开→索引一致）；tmp+rename 原子替换；崩溃残留 tmp 清理且正式文件完好；tombstone 写读；转义往返（Windows 非法字符/保留名/结尾句点，字节级无损）；traversal 逃逸拒绝；配额与文件数超限 `-ENOSPC`；meta.toml schema 不匹配失败 |
| VFS 目录操作 | mkdir/rmdir/unlink/rename/ListDirectory 内存语义；`-ENOTEMPTY`/`-EEXIST`/`-ENOENT` errno 契约；枚举稳定序；目录 Stat 事实 |
| overlay 语义 | 覆盖层遮蔽底层同路径；tombstone 后 `-ENOENT` 且枚举不出现；删除覆盖层文件不复活底层文件；写底层文件先物化再归属覆盖层；只读底层拒写不变 |
| **跨会话持久（核心验收）** | 同一临时沙盒目录上先后构造两个 VFS：会话 1 写文件/建目录/删底层文件/写 prefs → 正常关闭；会话 2 的 Stat/Read/getdents/prefs 逐字节等于会话 1 退出时状态 |
| syscall 编组 | 新增各 syscall 的 guest 指针受检、`stat64`/`linux_dirent64` ABI 布局锁定、errno 映射；`getdents64` 小缓冲区分页 |
| DexVM 收敛 | File 流经 VFS 与 native open 同路径互见；`File.mkdirs` 真实建目录且失败返回 false（伪成功消除属行为变更，必须有"回退旧行为即失败"的测试）；prefs XML 往返 + 与文件视角一致 |
| flush 点 | close/fsync/pause/shutdown 各自触发落盘（以 store 观测计数断言）；无脏节点时幂等 |
| 确定性回归 | 全量 CTest 与 `asphalt5.title_flow` 在一次性沙盒下逐位不变（golden SHA 不动） |

持久化用例一律使用测试自建临时目录，不触碰用户数据目录；不依赖真实 APK 的
用例进常规 CTest，exact-title 验证走既有 scenario gate。

## 2. 能力条目（`capabilities.toml`，只前进）

| 条目 | 起始状态 | complete 判据 |
| --- | --- | --- |
| `runtime.vfs_sandbox_persistence`（新增） | unimplemented | SandboxStore + overlay + flush 全部落地且跨会话用例通过 |
| `runtime.vfs_directory_ops`（新增） | unimplemented | 目录操作 API + errno 契约测试 |
| `runtime.syscall_file_metadata`（新增） | unimplemented | §表列 syscall 全部真实绑定 |
| `dexvm.file_io_vfs`（新增） | unimplemented | memory_files 废除，File 族经 VFS |
| `runtime.preferences_persistence`（新增） | unimplemented | 双通道共享 XML 实现且持久 |
| `runtime.vfs` / `dexvm.intrinsics_android_core` 等既有条目 | 保持 | note 更新事实，状态不后退 |

## 3. WU 分解（每个单会话可完成、≤10 文件、依赖显式）

| WU | 内容 | 依赖 |
| --- | --- | --- |
| SBX-1 | `SandboxStore`：布局、装载索引、原子写、tombstone、转义、配额、tmp 清理 + 单元测试 | 无 |
| SBX-2 | VFS 目录操作 + `Truncate/Flush/FlushAll` 内存语义 + 目录 Stat + 测试 | 无 |
| SBX-3 | `AttachSandbox`：overlay 解析、脏节点跟踪、flush 点挂接 + 跨会话持久测试 | SBX-1、SBX-2 |
| SBX-4 | syscall 缺口绑定（mkdir/unlink/rename/stat64/getdents64/access/ftruncate/fsync/pread64/pwrite64）+ ABI 编组测试 | SBX-2 |
| SBX-5 | DexVM File 族改线 VFS、mkdirs 去伪成功、getFilesDir/Environment/StatFs 事实 + 测试 | SBX-3 |
| SBX-6 | SharedPreferences XML 持久（framework + dexvm 共享实现）+ 测试 | SBX-3 |
| SBX-7 | session 装配 + CLI（默认根/`--sandbox-dir`/`--ephemeral-sandbox`/preflight）+ scenario runner 一次性沙盒 + exact-title 存档冒烟 | SBX-3 |

每个 WU 同步更新触及模块的 `MODULE.md` 与 `capabilities.toml`；启动实施时按
ADR-0012 在当期里程碑目录建任务单。SBX-1/2 不触碰现有行为，可并行先行。

**端到端验收（用户问题闭环）**：Dungeon Hunter（或当期最深入的 exact title）
进入游戏产生存档 → 正常退出 → 重新启动 → 游戏读到旧存档（标题画面出现
"继续"一类状态差异，或存档文件级断言）。以 scenario/人工验收 + 沙盒目录
文件断言双口径确认。

## 4. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 持久状态破坏 golden gate 确定性 | 自动化默认一次性沙盒（架构级保证，不靠纪律）；确定性回归列为 SBX-7 出口 |
| `File.mkdirs` 去伪成功改变既有 title 行为 | 属修正而非回退；Dungeon Hunter 全程 scenario 复跑作为 SBX-5 出口 |
| Windows 文件名/路径长度边缘 | 转义与 `-ENAMETOOLONG` 全部有单元测试；CI 双平台跑 SandboxStore 用例 |
| guest 写爆宿主磁盘 | 配额默认 256 MiB，超限 `-ENOSPC` 记账 |
| 用户手工改沙盒目录致装载失败 | 装载错误信息给出可执行修复（备份/清空该 package 子目录），不猜测修复 |
| stat 常量时间戳不满足个别游戏 | 记账可查询；确有需求时按 quirk 立项（"关闭即失败"测试随附） |
| prefs XML 子集不够 | 未知元素明确失败并记账，按命中扩展子集，不预做完整 XML |
