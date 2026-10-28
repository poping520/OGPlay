# SBX-16 · 文件 mmap2 快照与共享只读一致性

状态：有界完成；编排方验收发现的两项回归已由编排方返修，原 APK 点击 PLAY 进入第一关。依赖 [SBX-14](SBX-14.md)、[SBX-15](SBX-15.md)。

## 触发链与证据

编排方三次一致复现：主菜单 PLAY 后约两秒，guest=2 在 bionic memcpy
`pc=0x10021e5c` 读取未映射 `0x006d2ddc`。证据
`.local/analysis/island-play/repro-1.log`（编排方提供，本单不重复调查）。
libmain 基址 `0x306d0000`，调用链为 CTerrainMesh::RenderCells → kdMemcpy；
LoadCachedMeshFromFile 的 kdFmmap 使用文件 MAP_SHARED/PROT_READ，旧 syscall 192
仅接受匿名私有映射，失败后留下空指针。退出 Pattern.closeImpl 与 cleanup admission 不在本单范围。

## Linux 对照与实现边界

依据 [mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html) 与
[mmap2(2)](https://man7.org/linux/man-pages/man2/mmap2.2.html)：

- A32 pgoff 以 4096 字节为单位，使用 64 位乘法；映射长度向上取整到 guest 页。
- PRIVATE R/RW 是建立时快照，修改仅影响 guest 副本；POSIX 不规定是否看到后续文件修改。
- SHARED R 仅接受只读打开的普通文件。读取源持有节点读保护，最后区间退役前拒绝 VFS
  写入、定位写、截断、O_TRUNC 及 rename 源/目标替换，避免快照与文件静默分叉。
- 尾页 EOF 后清零；整页超 EOF 无法投递 SIGBUS，明确 ENOSYS。
- 关闭 fd 不影响映射。VFS 在打开状态锁内捕获节点、访问模式与尺寸，读取源独立于 fd
  数字和路径；后续复用 fd 不会切换节点。宿主绕过 VFS 修改 backing 不在保证范围。
- VFS 只提供节点保护/读取源，拒绝通过显式回调记账；syscall 负责区间拆分/覆盖/释放，
  不产生 VFS → syscall 依赖。相同节点的别名和独立 fd 共用保护。
- PRIVATE 快照复制期间也临时持有节点保护，成功发布后解除；SHARED 持有至最后映射
  区间解除或进程 dispatcher owner 析构。unlink 不破坏旧节点身份，同名新节点独立。
- 使用 VFS 统一资源预算预留快照临时缓冲，调用 positioned ReadAt，不移动 fd offset。
  在建立任何 guest 页之前完成读取；AddressSpace 在锁内准备可写 host backing、清零、
  拷入初始化字节、发布最终 guest 权限与世代。这样保证 MAP_FIXED 读取/分配失败保留旧页，
  并避免 RW 临时映射对其他 guest 线程可见。未增加宿主 executable 权限。
- 非固定继续 MapAnywhere first-fit；固定继续 ReplaceAnonymous 的低地址 guard 与原子替换。
  区间账本在内存变更之前准备所有分裂与容量，内存提交后无分配地更新账本。
- 文件映射不允许 mprotect 增加执行权限；SHARED R 不允许增加写权限，防止绕过边界。
  无共享回写、msync、通用 mremap、文件执行或 SIGBUS；无新依赖、游戏专属代码。

## errno 与运行期记账

| 条件 | errno | 记账 |
| --- | --- | --- |
| PRIVATE R/RW；只读 fd 的 SHARED R | 成功 | 无未实现命中 |
| 坏 fd | EBADF | 正常参数错误，无未实现命中 |
| pipe、目录、字符设备 | ENODEV | 正常类型错误，无未实现命中 |
| PRIVATE 使用不可读 fd | EACCES | 正常权限错误，无未实现命中 |
| SHARED WRITE，只读 fd | EACCES | 正常权限错误，无未实现命中 |
| SHARED WRITE，可写 fd | ENOSYS | dispatcher `syscall.mmap2` |
| SHARED R，可写 fd；文件 EXEC/其他未支持 prot；GROWSDOWN/HUGETLB/未知 flag | ENOSYS | dispatcher `syscall.mmap2` |
| 有整页超 EOF（含空文件/最大 pgoff） | ENOSYS | dispatcher `syscall.mmap2` |
| 零长度/固定地址不对齐/非法 prot 位 | EINVAL | 正常参数错误，无未实现命中 |
| 长度或固定范围溢出、backing 分配失败 | ENOMEM | 正常资源错误，无未实现命中 |
| 固定映射低 guard/provider 保护区 | EPERM | provider 拒绝沿用其 owner 记账 |
| VFS 节点写入/截断/rename 受保护 | EBUSY | 显式 `vfs.shared_mapping_mutation`，LR=0 |
| 文件 mprotect EXEC；共享 mprotect WRITE | ENOSYS | dispatcher `syscall.mprotect` |
| madvise DONTNEED 与文件映射区间重叠 | ENOSYS | dispatcher `syscall.madvise`；其余 advice 沿用 WU-0088 |
| VFS backing 读取错误/预算不足 | VFS errno（如 EIO/ENOSPC） | 明确资源/IO 失败，无未实现命中 |

EBUSY 表示活跃读取快照占用节点；不采用暗示可执行正文文件的 ETXTBSY。
VFS 拒绝发生在通用写入口，可能没有 syscall frame，因此 ledger LR 置 0，不猜测调用地址。

## 原断言调整

（已撤销）实施方曾按错误前提移除 WU-0088 的 madvise 并把原用例改验 ENOSYS；编排方返修恢复了
实现与原用例全部断言，见末节。
SBX-15 原用例中两条旧参数拒绝预期随支持边界调整：未绑定 VFS 的文件映射从 EINVAL
改为 EBADF；尚未支持的匿名非零 pgoff 从 EINVAL 改为 ENOSYS，并补记账检查。
该用例的旧页字节、权限/页数、世代不变与最后一页断言均保留。其他匿名 mmap、brk、
mprotect 原断言保持；新增文件映射用例不替代旧用例。

## 自行决定事项

以下均为**实施方按编排方常设原则决定，待编排方复核**：

1. 节点一致性在 VFS 层实现，syscall 保留区间；统一 EBUSY 与上述细分显式账目。
2. 文件字节在提交前预读；扩展现有 memory 原语的可选初始化参数，在一次锁内复制并发布
   最终权限，替代可失败的“先替换、再 Write/Protect”，保证固定旧页不被失败路径破坏。
3. 私有快照复制期间临时冻结节点；共享快照存活期冻结，mprotect 不能升级绕过支持边界。
4. ~~依指令将已有 madvise 收回至 ENOSYS~~：指令前提错误，编排方返修时已撤销，见末节。
5. 快照缓冲计入现有 VFS 预算；普通 errno 不作未实现记账，未支持 mmap 统一由 dispatcher
   记 `syscall.mmap2`，跨写入口的节点拒绝显式记 `vfs.shared_mapping_mutation`。

## 验证与阶段耗时

证据目录：`.local/analysis/sbx-16/`。生产改动前保存
`baseline-bin/{ogplay_tests,ogplay-cli,libogplaycore.dylib}`，未提交 git commit。

基点命令：

```sh
build/release/ogplay_tests --source-file='*syscall_tests.cpp,*address_space_tests.cpp,*vfs*tests.cpp,*file_vfs_tests.cpp,*file_streams_tests.cpp,*file_media_descriptors_tests.cpp'
```

基点：157 用例，156 通过，1 个既有 AtomicFile 用例因缺少 regex JNI bridge 抛异常；
27957 断言全部通过。证据 `baseline-tests.log`。不修无关 JNI/BootDex。

构建仅限：

```sh
cmake --build build/release --target ogplay_tests ogplay-cli -j 6
```

格式化仅改动行：

```sh
git diff -U0 | .local/analysis/dvm260-i2-fix/format-env/bin/python .local/analysis/dvm260-i2-fix/format-env/bin/clang-format-diff.py -p1 -binary /opt/homebrew/bin/clang-format
```

结果：

- 受影响目标构建通过，最终增量构建证据 `build-verified.log`；两轮接口构建记录为
  `build.log`、`build-final.log`，只有既有 duplicate libraries 链接警告。
- 新增 4 用例/98 断言全部通过（`mapping-tests.log`），支持路径无未实现命中。
- 规定相关主题最终 161 用例，160 通过，仍只有基点同一个 AtomicFile 异常；
  28061 断言全部通过，断言失败未增加。原有 157 个用例全部保留，新增 4 个。
  `final-tests.log`、`baseline-cases.txt`、`final-cases.txt` 记录对比。
- 只改动行的格式检查输出为空；UTF-8、相对链接、git diff --check 通过。
  能力账本检查通过（431 条）。CURRENT 总字节 7042 → 7042，只有 Island 行变化。
  证据 `format-check.log`、`static-checks.log`、`capabilities-check.log`。
- 未修改 dexvm 核心、JNI 或 BootDex，未跑 dalvik/JNI/compose，未启动 APK。

阶段耗时按日志首末写入粗估：基点测试 10.54 秒；两轮接口构建分别 137.26、132.65 秒。
后续增量构建与夹具/旧断言审阅修正的日志窗口约 160 秒（含审阅间隔，非纯编译耗时）；
实现、阅读与文档约数分钟，后测未独立精确计时，不以日志窗口冒充 CPU 耗时。

外验步骤保存在 `.local/analysis/sbx-16/external-validation.txt`：从 build/release 运行
`./ogplay-cli library run com.g5e.islandcastaway.full`，关闭原生磁盘空间弹窗、跳过 CG、点击
PLAY，确认原 memcpy memory_fault 消失、关卡载入出画并记录下一首错。本单未启动 APK，
没有宣称 reached-fault 或游戏兼容验收。

## 编排方验收（2026-10-28）：未通过

复建后定向主题 161 项 160 通过（既有 AtomicFile JNI 失败），但原 APK（`.local/analysis/sbx-16/apk-verify-1.log`）
启动即挂起：MCP `frame=0`、无呈现帧；`libc: pthread_create failed: couldn't allocate 1040384-byte stack:
Function not implemented`，随后 guest call 持续运行。须返修：

1. **匿名映射附加 flag 回归**：新 mmap2 对 `flags & ~(MAP_TYPE|MAP_FIXED|MAP_ANONYMOUS)` 一律 ENOSYS。HEAD 对匿名私有
   映射接受其他 flag；bionic pthread 栈使用 `MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE`。须恢复匿名路径原接受面，
   对本模型无语义差异的提示位（NORESERVE、POPULATE、NONBLOCK、STACK、DENYWRITE、EXECUTABLE；内存常驻下 LOCKED
   亦如实）在匿名与文件路径同样接受；有语义且未实现的（如 GROWSDOWN、未知位）明确失败并记账，写明依据。
2. **madvise 被删除（提示词前提错误所致）**：编排方提示词误称 madvise 为 ENOSYS，实际 HEAD 已实现（≤4 与 12–15，
   DONTNEED 清零可写页，非法对齐/区间/advice 的 errno）。本单删除了实现并把原用例改为期待 ENOSYS。须恢复 HEAD
   实现及原用例全部断言；仅在区间与文件映射重叠且 advice 为 DONTNEED 时明确失败并记账（Linux 对私有文件映射
   DONTNEED 会从文件重新填充而非清零），其余 advice 对文件映射如实成功。
3. 补回归：bionic 式 `MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE`（及 STACK）栈映射成功；原 madvise 用例恢复；
   文件映射上 DONTNEED 明确失败、WILLNEED 成功。
4. 观察项（不阻塞）：私有映射复制期间节点临时冻结，并发写入会得到 EBUSY 并记账，属可接受边界，须在 MODULE 写明。

### 编排方返修与复验（2026-10-28）

- `syscall.cpp`：mmap2 在匿名与文件路径接受 `kMapHints`（DENYWRITE 0x800、EXECUTABLE 0x1000、LOCKED 0x2000、
  NORESERVE 0x4000、POPULATE 0x8000、NONBLOCK 0x10000、STACK 0x20000）；GROWSDOWN/HUGETLB/未知位仍 ENOSYS。
  恢复 HEAD 的 madvise（220），仅在 DONTNEED 与文件映射区间重叠时先于写权限校验返回 ENOSYS。
- `syscall_tests.cpp`：原 “Android madvise validates hints and discards writable pages” 断言逐条恢复；新增
  “SBX-16 anonymous hint flags map and madvise never zeroes file snapshots”（NORESERVE/STACK/LOCKED/POPULATE 成功、
  GROWSDOWN 记账失败、文件区间 WILLNEED 成功/DONTNEED 记账失败且字节不变、换回匿名后 DONTNEED 成功）。
  SBX-16 与 madvise 六项 122 断言通过；相关主题 162 项 161 通过（既有 AtomicFile JNI），28073 断言全过；
  改动行 clang-format-diff 无差异。
- 原 APK（`.local/analysis/sbx-16/apk-verify-2.log`）：启动无 `pthread_create failed`；跳过 CG 后点击 PLAY，
  出现 “Part 1 Nightmare.” 章节页（`play-chapter.png`），随后第一关地形与对白 “What a strange place!” 出画
  （`play-level.png`），无 memcpy fault、无 EBUSY 拒绝。会话末尾 57 条 `Surface endpoint budget exhausted` 均在
  SIGINT 停止后（`process_stopping` 拒绝新 Surface），为退出期噪声，非本单问题。
