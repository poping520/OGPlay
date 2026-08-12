# 03 · 持久化语义

## 1. 设计取向

会话内语义**保持现状**：读写作用于 VFS 内存节点，descriptor offset 隔离、
懒物化、errno 契约全部不变。持久化是内存节点之下的**受控落盘**，而不是把
VFS 改造成宿主文件系统的直通代理。理由：

- 现有全部 VFS/syscall/framework 测试语义不动，改造面最小；
- guest 高频小写入（很多老游戏逐字节写存档）不放大为宿主 IO；
- 落盘时机可枚举、可测试，崩溃一致性推理简单。

代价是"进程被强杀时最后未 flush 的写入丢失"，通过 flush 点覆盖生命周期
关键时机（§2）把窗口压到与真机 Android 同量级（Android 的 page cache
同样不保证掉电即刷）。

## 2. flush 点（确定性、可枚举）

| 时机 | 范围 | 触发者 |
| --- | --- | --- |
| `close(fd)` | 该文件（若脏） | syscall / framework / dexvm 经 VFS `Close` |
| `fsync` / `fdatasync` | 该文件 | 新增 syscall 绑定 |
| 元数据操作（`mkdir`/`unlink`/`rename`） | 该目录项，立即 | VFS 目录操作 API |
| lifecycle pause / suspend | 全部脏节点 | session 生命周期模板（guest `onPause` 后、真机存档习惯点） |
| clean shutdown | 全部脏节点 | session teardown，先于 VFS 析构 |
| `SharedPreferences` commit/apply | 对应 `shared_prefs/*.xml` | dexvm / framework prefs handler |

- flush 幂等：非脏节点跳过；同一文件多次 close 只落盘有变更的那次。
- 显式无 timer flush：项目全部时间源经统一 Clock，不为持久化引入后台
  线程或周期任务；flush 点全部由既有确定性事件驱动。

## 3. 原子性与崩溃安全

- **文件写**：写 `<name>.__ogplay_tmp__`（同目录）→ 落盘成功后 rename 覆盖
  目标名。同目录 rename 在三平台为原子替换（Windows 经
  `std::filesystem::rename` 的 ReplaceFile 语义）。任一时刻崩溃，目标文件
  要么旧内容要么新内容，不会半截。
- **tombstone / mkdir**：单个空文件/目录创建本身原子。
- **rename（guest 语义）**：覆盖层内实现为"写新路径 + 删旧路径（含必要
  tombstone）"两步；两步之间崩溃的结果是新旧并存（可接受的最保守残留，
  不丢数据）。不追求跨文件事务。
- **启动清理**：装载沙盒时删除残留 `*.__ogplay_tmp__` 并写结构化警告日志；
  tmp 残留只可能是上次崩溃产物，正式内容从未损坏。
- **meta.toml**：同一 tmp+rename 姿态更新。

## 4. 配额与资源边界

- 每沙盒字节配额，默认 **256 MiB**，Profile `[data]` 可声明更大受检值
  （纯数据，范围受限）。老游戏存档普遍在 KB–MB 级，默认值宽裕一个量级。
- 配额按覆盖层落盘字节 + 脏节点内存字节合并核算；超限时对触发写入返回
  `-ENOSPC` 并记账——与真机 sdcard 写满的表现一致，游戏自身有处理路径。
- 单文件尺寸上限同配额；覆盖底层大文件（如试图改写 OBB 视频）会在物化点
  命中配额，明确失败而非 OOM。
- 沙盒内文件数上限默认 65536，防御病态目录树；超限 `-ENOSPC` 记账。

## 5. 失败模式清单（全部明确失败 + 记账）

| 失败 | guest 所见 | 记账/日志 |
| --- | --- | --- |
| 宿主磁盘满 / IO 错误（flush 时） | 触发操作返回 `-EIO`（close/fsync 处），后续写继续报错 | `ogplay.vfs.sandbox` error，含 guest 路径与宿主 errno |
| 配额超限 | `-ENOSPC` | 同上，含当前用量 |
| 保留后缀路径 | `-EINVAL` | 记账（capability ledger 可查询命中） |
| 宿主路径过长 | `-ENAMETOOLONG` | 记账 |
| 沙盒目录不可创建/不可写（启动时） | 会话装配失败，进程不启动 | 启动错误，提示可执行的修复动作 |
| meta.toml schema 不匹配 / 损坏 | 会话装配失败 | 不猜测迁移；提示用户备份或清空沙盒 |
| 装载时大小写折叠冲突 | 会话装配失败 | 沿用 MountHostDirectory 既有不变量 |

沙盒持久化**从不静默降级为内存模式**：要么装配成功且持久，要么装配失败。
只有显式 `--ephemeral-sandbox` 才是非持久运行。

## 6. SharedPreferences 落盘格式

- 持久为 guest 路径 `/data/data/<pkg>/shared_prefs/<name>.xml`，内容采用
  Android 平台同构 XML（`<map>` + `<string|int|long|float|boolean>` 元素，
  UTF-8）。选择 Android 同构格式的理由：个别游戏会绕过 API 直接读
  shared_prefs 文件，文件视角与 API 视角必须指向同一份事实。
- 支持子集限定为五种标量 + string set；解析器为受检实现，未知元素/属性
  明确失败（不是完整 XML 解析器，拒绝实体、DTD、命名空间）。
- framework HLE 与 DexVM 的 prefs handler 共享同一读写实现，经 VFS 普通
  文件通道读写，flush 点为 commit/apply。
