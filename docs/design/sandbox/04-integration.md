# 04 · 运行时集成

## 1. VFS API 扩展（`include/ogplay/runtime/vfs/vfs.h`）

新增（内存语义，attach 沙盒后自动持久）：

```cpp
// 目录操作：存档流程必经面
void CreateDirectory(std::string_view path);                    // mkdir，父目录必须存在
void RemoveFile(std::string_view path);                         // unlink
void RemoveDirectory(std::string_view path);                    // rmdir，非空 -ENOTEMPTY
void Rename(std::string_view from, std::string_view to);        // 同命名空间内
[[nodiscard]] std::vector<VfsDirectoryEntry> ListDirectory(std::string_view path) const;

// 文件操作补齐
void Truncate(std::int32_t descriptor, std::uint64_t size);     // ftruncate
void Flush(std::int32_t descriptor);                            // fsync/fdatasync 汇合点
void FlushAll();                                                // lifecycle pause / shutdown

// 沙盒挂接
void AttachSandbox(SandboxStore& store,
                   std::span<const std::string> writable_roots);
```

- `VfsDirectoryEntry` 含名字与 `is_directory`，枚举结果按路径规范序排序
  （overlay ∪ base − tombstone），保证 `getdents64` 输出确定。
- `VfsFileInfo` 增加 `is_directory` 事实；`Stat` 对目录返回目录事实而非
  `-ENOENT`（现状目录不可 stat，是 `faccessat`/`File.isDirectory` 缺口）。
- 未 attach 沙盒时以上 API 为纯内存语义，供既有测试与一次性沙盒复用。

`SandboxStore`（同子模块新头文件）：`Open(root, package)`、装载覆盖层索引、
`WriteFileAtomic`、`WriteTombstone`、`CreateDirectory`、`Remove`、`Rename`、
配额核算、tmp 清理。只依赖标准库。

## 2. syscall 缺口补齐（`runtime/syscall`）

`BindAndroidFileSyscalls` 新增绑定（目录中已声明，现走 `-ENOSYS`）：

| syscall | VFS 映射 | 备注 |
| --- | --- | --- |
| `mkdir` / `mkdirat` | `CreateDirectory` | `mode` 忽略（无权限位语义） |
| `unlink` / `unlinkat` | `RemoveFile` / `RemoveDirectory`（`AT_REMOVEDIR`） | |
| `rename` / `renameat` | `Rename` | |
| `stat64` / `lstat64` / `fstatat64` | `Stat` → `struct stat64` 编组 | size/mode(仅文件类型位)/常量时间戳；无符号链接语义，`lstat64`=`stat64` |
| `fstat64` | descriptor → `Stat` | |
| `getdents64` | `ListDirectory` → dirent 编组 | 稳定序、受检缓冲区分页 |
| `access` / `faccessat` | `Stat` 存在性 + 可写事实 | |
| `ftruncate` / `ftruncate64` | `Truncate` | |
| `fsync` / `fdatasync` | `Flush` | |
| `pread64` / `pwrite64` | 现有 Read/Write + 显式 offset（不动 descriptor offset） | |

- 编组遵循既有姿态：guest 指针经 `AddressSpace` 受检拷贝，错误映射稳定
  Linux errno；`struct stat64`/`linux_dirent64` 布局按 Android ARM ABI
  固定并加机器测试。
- 时间戳策略：VFS 不引入真实墙钟（时间源必须经统一 Clock），`stat` 的
  `st_mtime` 等返回常量 0——老游戏用 mtime 做逻辑的个例交给 quirk，
  默认不伪造时间。
- 仍不实现的项（`flock`、`*xattr`、`inotify*` 等）维持 `-ENOSYS` 记账。

## 3. DexVM 与 framework 收敛（`runtime/integration`、`runtime/framework`）

- **废除 `DexVmAndroidContext::memory_files`**：`File`/`FileInputStream`/
  `FileOutputStream`/`FileWriter` intrinsic 全部经 VFS 解析与发布，路径解析与
  native 侧同源（含 working directory 规则）。当前流对象为会话内整文件缓冲，
  flush/close 时短期打开 VFS descriptor 整体发布；尚未实现的 RandomAccessFile
  不在本期完成面内。
  `File.mkdirs` 改为逐级 `CreateDirectory`，真实建目录、失败真实返回 false，
  消除伪成功。
- **`Context.getFilesDir`/`getCacheDir`/`getDir`** 返回
  `/data/data/<pkg>/files` 等可写命名空间路径事实（package 来自 manifest
  受检身份，不硬编码）。
- **`Environment.getExternalStorageDirectory`** 返回 `/sdcard`；
  `StatFs` 对可写命名空间报告配额剩余，对只读底层报告 0 可用——诚实事实
  而非猜测宿主磁盘。
- **SharedPreferences**：framework HLE（`FrameworkPreferencesHle`）与 DexVM
  handler 改为共享同一 XML 读写实现（[03 · §6](03-persistence.md)），
  经 VFS 文件通道。两条线的进程内 map 变为写通缓存。
- framework Asset（`/apk/assets/`）只读路径不受影响。

## 4. 装配与 CLI 接线（`session`、`frontend`）

- `AssembleProfileVfs` 增加沙盒装配步骤：external/APK/OBB 底层挂载完成后
  `AttachSandbox`；required mount 校验语义不变。装配失败整体回滚，维持
  事务性。
- `run_apk` 新增 `--sandbox-dir <dir>` 与 `--ephemeral-sandbox`（互斥）；
  缺省用 roadmap 08 用户数据目录。preflight 输出沙盒根、package 键与
  当前用量事实。
- `--external-dir` 语义微调为纯底层：宿主目录仍只读索引；此前"external
  会话内可写"的用户可见行为由覆盖层取代（写入现在真实保存，属严格增强）。
- scenario runner 默认一次性沙盒；Scenario schema v1 不改（沙盒不是场景
  可声明面），持久化专项测试走 CTest 而非 scenario。

## 5. 契约修订清单（实施时逐条落实）

| 现契约 | 修订 |
| --- | --- |
| `src/runtime/vfs/MODULE.md`："external 修改只存在于会话内，不反写宿主目录" | 改为：底层只读不反写；可写命名空间经 SandboxStore 在 flush 点持久化 |
| 同上："空目录…必须在发布挂载前失败" | 仅约束底层挂载；沙盒覆盖层允许空目录 |
| `docs/tasks/m5/WU-0301.md` 非目标"不持久化 guest 修改" | 由 ADR-0020 supersede，历史文档不改 |
| `include/ogplay/runtime/integration/dexvm_android.h` "no cross-session persistence" 注释 | 随 memory_files 废除删除 |
| `docs/design/dexvm/03-platform-intrinsics.md` prefs 表述与实现不一致 | 以本方案为准，dexvm 设计文档补记指向 ADR-0020 |
| `capabilities.toml` `dexvm.intrinsics_android_core` note "SharedPreferences(会话内存)" | 实施后更新为持久化事实（状态只前进） |
| `src/runtime/MODULE.md`、`src/runtime/syscall/MODULE.md`、`src/session/MODULE.md`、`src/frontend/MODULE.md` | 随各 WU 同步职责与不变量 |
