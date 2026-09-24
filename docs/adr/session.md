# 会话、Profile 与持久沙盒

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0020 · 每游戏持久沙盒（可写命名空间跨会话持久化）](#adr-0020)
- [ADR-0022 · Profile 启动作用域与 v2-only 迁移](#adr-0022)
- [ADR-0071 · VFS backing、资源 lease 与安装实例沙盒](#adr-0071)
- [ADR-0074 · CLI 数据输入独立于 Profile 挂载声明](#adr-0074)

<a id="adr-0020"></a>

## ADR-0020 · 每游戏持久沙盒（可写命名空间跨会话持久化）

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

### 背景

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

### 决定

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

### 后果

- "存档不保存" 结构性消失；存档随沙盒目录可整体备份/迁移，为 roadmap 07 的
  "存档管理" 功能提供存储基础（即时存档/快照仍属独立 backlog，不在本 ADR）。
- `runtime/vfs` 职责从纯内存会话文件系统扩展为 "内存语义 + 受控宿主持久"，
  模块契约、`capabilities.toml` 新增条目与既有测试需按设计文档同步演进；
  `runtime.vfs` 等既有条目状态不后退。
- 文件 syscall 缺口（mkdir/unlink/rename/stat64/getdents64/ftruncate/fsync 等）
  必须补齐为真实语义，DexVM `File.mkdirs` 伪成功被消除。
- 引入宿主文件名兼容面（Windows 保留名、大小写、路径长度）与沙盒配额两类
  新失败模式，全部按 "明确失败 + 记账" 处理，见设计文档 03 章。

<a id="adr-0022"></a>

## ADR-0022 · Profile 启动作用域与 v2-only 迁移

- 状态：Accepted
- 日期：2026-08-12
- 关联：[启动作用域裁剪设计](../design/entry-scope/README.md)、
  [ADR-0017](dexvm.md#adr-0017)
- Supersedes：ADR-0017 中“Profile v1 冻结并与 v2 并行”的过渡决定，以及
  Title Profile v1 的 `native_call` / `[[java.class]]` 人工重放路线。

### 背景

DexVM 已用 Asphalt 5 exact gate 证明：解释执行真实 Activity 生命周期可以完全替代
Profile v1 的手工 JNI 调用序列和 Java handler 映射。继续保留 v1 会形成两套启动语义，
并诱导新 title 通过补商业外壳 Java 面而不是收敛到游戏引擎作用域。

Asphalt 6 的 manifest launcher 会转入首启下载、DRM、推送和分析外壳；在 external
数据已经 provisioned 时，这条 store 路径不是游戏引擎运行所需的作用域。启动作用域
必须由通用机制表达，游戏差异只进入 `data/profiles/`，且数据前提不成立时明确失败。

### 决定

- Title Profile 只接受 schema v2；删除 v1 schema、解析、手工 Java 装配、
  `native_call` 解析/解析符号/生命周期重放代码及其专属测试。
- 所有精确 Profile 使用 `dex_activity`，应用类与方法只来自 APK DEX；平台面只来自
  intrinsic 目录，缺失能力继续记账并明确失败。
- v2 增加可选 `[runtime.entry]`：`launch_activity` 覆盖 manifest launcher；
  `[[runtime.presets]]` 在目标类真实完成初始化后写入受检静态字段。每条 preset 必须
  提供非空 `reason`，且只允许基元或 `java.lang.String`。
- 声明 entry/preset 的 Profile 必须有至少一个 required data manifest 事实；run-apk
  在启动 DexVM 前经统一 VFS 验证这些事实。缺失时不实例化 Activity。
- 方法中性化不是本 ADR 的首批出口；只有 exact 测试证明入口/事实预设仍不足，且方法
  符合设计中的非目标与返回类型红线时，才以独立 WU 引入。

### 后果

- Profile 启动语义只剩一条 DexVM 路线；旧 title 必须迁移到 v2 才能继续进入生产目录。
- 入口覆盖和预设属于结论级配置，必须有 schema 负例、运行时类/字段存在性检查及
  “关闭即失败”exact 证据；gap survey 仍仅是诊断工具。
- `src/` 不出现游戏名、厂商名或包名。Asphalt 6 的入口、字段和理由只存在于其 Profile。

<a id="adr-0071"></a>

## ADR-0071 · VFS backing、资源 lease 与安装实例沙盒

- 状态：Accepted
- 日期：2026-09-20
- 关联：[VFS 资源读取、寿命与安装实例沙盒](../design/vfs/README.md)
- Supersedes：ADR-0020 中“package name 为沙盒键”、`fs/` 镜像布局以及旧 schema
  可迁移的条款；ADR-0020 的 overlay、tombstone、配额、原子替换和 flush 语义继续有效。

### 背景

原 VFS 在首次读取时持全局锁物化整文件，媒体又各自复制编码源；同 package 多份安装
共享一个沙盒和设备身份。大资源、慢 backing、FD 捕获及多安装因此没有统一寿命边界。

### 决定

- 节点、打开状态与 backing 分离；定位读取和耗时校验/解压不持 VFS 全局索引锁。
  普通 FD 保持独立游标，资源 lease 固定节点版本和窗口。
- 宿主文件与 stored 归档按区间读取；压缩来源采用有界增量解压与总预算。完整性校验
  仍是发布归档窗口前的硬门槛，不能用局部读取跳过 CRC。
- 音频和视频只依赖消费者模块定义的窄读取接口，由 integration 适配同一 VFS lease；
  不再以宿主路径或整文件复制作为生产资源协议。
- library 与 sandbox 使用同一个安装 id。首份为 package，后续为 `-2`、`-3`；从两侧
  已占用编号并集取最小空缺，版本号不参与身份。
- 新沙盒 schema 只接受 `internal/`、`external/`、`obb/`、`sdcard/` 四根；旧 `fs/`、
  缺失或冲突 meta、新旧混合布局明确拒绝，不迁移、不删除、不降级为空沙盒。

### 后果

- 小窗口读取、慢来源隔离、关闭后的 lease 寿命和多安装数据隔离成为可测试契约。
- 可写源捕获需要受总预算约束的快照；压缩后退仍允许线性重启，本轮不承诺 mmap、WAL、
  跨进程锁或所有同步宿主 IO 可立即取消。

<a id="adr-0074"></a>

## ADR-0074 · CLI 数据输入独立于 Profile 挂载声明

- 状态：Accepted
- 日期：2026-09-24
- 关联：[VFS-04](../tasks/vfs/VFS-04.md)
- Supersedes：WU-0302 中“无 Profile external 声明时拒绝 `--external-dir`”的条款。

### 背景

`run-apk` 把宿主数据输入和游戏特殊 guest 路径合为 Profile mount。无匹配 Profile 时，
显式 `--external-dir` 和 `--obb` 因缺少声明被拒绝；OBB 还只以 ZIP 条目形式挂载，
guest 看不到标准路径下的原 `.obb` 文件。

### 决定

- `--external-dir` 无 Profile external 声明时挂到 `/sdcard`；有声明时沿用唯一声明路径，
  无声明时可用 `--external-guest-dir` 指定 `/sdcard` 内的 guest 根；required mount 与
  manifest 校验仍生效。
- `--obb` 始终按原文件名只读挂到 `/sdcard/Android/obb/<package>/`，通过宿主文件
  backing 定位读取。Profile OBB 声明额外挂载归档条目，required 规则不变。
- 原始输入挂载在沙盒 overlay 前完成；路径冲突和非法输入明确失败，不静默覆盖底层。

### 后果

无 Profile 的 APK 可使用显式外部目录和标准 OBB 路径。Profile 保留特殊数据布局职责；
本决定不为任意外部目录猜测游戏专属路径，也不扩大到多个 OBB 的 CLI 输入。
