# 01 · 根因、目标与非目标

## 1. 现状与根因

"每次启动都是全新状态"不是单点 bug，而是三条写入通道在设计上就止于进程内存：

| 通道 | 现落点 | 依据 |
| --- | --- | --- |
| native `open/write` → syscall → VFS | `VirtualFileSystem` 内存节点 | `src/runtime/vfs/MODULE.md`："external 修改只存在于会话内，不反写宿主目录"；`docs/tasks/m5/WU-0301.md` 非目标"不持久化 guest 修改" |
| DexVM `File*` 流 | `DexVmAndroidContext::memory_files` 会话 map | `include/ogplay/runtime/integration/dexvm_android.h` 注释明示 no cross-session persistence |
| `SharedPreferences`（framework HLE 与 DexVM） | 进程内 map | `capabilities.toml` `dexvm.intrinsics_android_core`："SharedPreferences(会话内存)" |

叠加三处结构性缺口，即使想持久也接不住真实存档流程：

1. **文件 syscall 缺口**：`BindAndroidFileSyscalls` 只实现了
   `open/openat/read/write/close/lseek/pipe`；`mkdir*`、`unlink*`、`rename*`、
   `stat64/fstat64/fstatat64`、`getdents64`、`faccessat`、`ftruncate`、`fsync`
   在目录中声明但走默认 `-ENOSYS`。存档代码普遍是
   "stat 探测 → mkdir 建目录 → 写临时文件 → rename 替换"，当前链条走不通。
2. **`MountHostDirectory` 拒绝空目录**：纯存档目录（首次运行为空）无法挂载。
3. **DexVM `File.mkdirs` 恒返回 1 但不建目录**：伪成功，违反项目记账原则，
   且会诱导游戏后续写入失败路径。

另外，guest 新建文件（`O_CREAT`）被标为 `VfsSource::runtime`，与任何宿主
backing 无绑定——即使 `--external-dir` 挂了宿主目录，新文件也从未属于它。

## 2. 目标

1. **存档持久**：guest 对可写命名空间的写入（文件内容、目录结构、删除、
   重命名）跨会话保留；下次启动 `Stat/Open/getdents` 所见与上次退出一致。
2. **每游戏隔离**：沙盒以 package name 为键，游戏之间互不可见、互不干扰；
   一个沙盒目录即一个游戏的全部可变状态，可整体备份/迁移（对齐
   roadmap 06 §2 "每个游戏一个独立的虚拟 sdcard"）。
3. **沙盒内自由读写**：补齐存档流程必经的文件 syscall（目录创建/枚举/删除/
   重命名/stat/truncate/sync），游戏在自己的可写命名空间内不需要 quirk
   即可完成常规文件操作。
4. **三通道收敛**：syscall、framework HLE、DexVM 的文件与 preferences 语义
   全部经由同一 `VirtualFileSystem`；同一路径在三条通道下看到同一份内容。
5. **只读底层零拷贝**：APK/OBB/external 大文件维持只读原地懒挂载，不因
   持久化而复制。
6. **确定性不回退**：既有 golden gate（如 `asphalt5.title_flow`）与全部
   CTest 在一次性沙盒下保持逐位可重复。

## 3. 非目标

| 不做 | 理由 |
| --- | --- |
| Linux 权限位/uid/gid/多用户/SELinux 语义 | 单游戏单用户进程用不到；`Stat` 继续只报尺寸/可写/来源事实 |
| 符号链接、硬链接、特殊文件 | 老游戏存档不用；底层挂载既有"发布前失败"规则不变 |
| SQLite/数据库 HLE | guest 侧 SQLite 是普通 `.so`，走文件 syscall 即可，无需专门模拟 |
| 即时存档（Save State，全 guest 状态快照） | 独立 backlog（roadmap 01 §4.6 / 06 §3.3），与文件沙盒正交 |
| 存档管理 GUI（导入/导出/备份界面） | roadmap 07 M7+ 前端功能；本方案只提供"一个目录=一个游戏"的存储基础 |
| 跨设备/云同步 | 范围外（roadmap 00 §排除项） |
| 宿主目录热更改感知 | 会话运行中外部改动沙盒目录属未定义行为，只在启动装载时读取 |
| Android `Content Provider`/SAF/权限对话框 | 兼容层不是 Android 系统（AGENTS.md 范围边界） |

## 4. 规模量级

- 新增生产代码集中在 `runtime/vfs`（SandboxStore + 目录操作 + overlay 解析）、
  `runtime/syscall`（约 12 个 syscall 绑定）、`runtime/integration`
  （DexVM File/prefs 改线）、`frontend/cli`（默认根目录与开关），预估
  1500–2500 行，分 7 个左右 WU（见 [05 · §3](05-verification.md)）。
- 不新增第三方依赖：宿主 IO 用 `std::filesystem`（`runtime/vfs` 已有先例），
  preferences XML 为受限子集自产自解析。
