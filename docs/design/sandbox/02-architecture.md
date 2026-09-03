# 02 · 核心架构

## 1. guest 命名空间分层

guest 看到的单一路径树由两类层组成，解析优先级自上而下：

```
┌─ 覆盖层（可写，沙盒持久背衬）────────────────────────────┐
│  /data/data/<pkg>/**   /sdcard/**（含 /mnt/sdcard 别名）   │
├─ 底层（只读，懒挂载，零拷贝）────────────────────────────┤
│  /apk/**（APK assets） /sdcard/...（--external-dir、OBB）  │
└──────────────────────────────────────────────────────────┘
```

- **可写命名空间**默认为 `/data/data/<package>/` 与 `/sdcard/`（`/mnt/sdcard`
  规范化为 `/sdcard` 别名）。该集合是通用机制内置默认，Profile `[data]`
  不需要为持久化新增声明；guest 对可写命名空间之外路径的写入依旧
  `-EACCES`/`-ENOENT` 明确失败。
- **底层**即现有挂载：APK/OBB 懒只读挂载、`--external-dir` 宿主目录挂载。
  底层永远不被写入，语义与现状完全一致。
- **解析规则**（`Stat/Open/getdents` 共用）：
  1. 覆盖层存在同路径文件 → 用覆盖层（文件粒度整体覆盖，非字节级 COW）；
  2. 覆盖层存在同路径 tombstone → 视为不存在（`-ENOENT`）；
  3. 否则落到底层；
  4. 目录枚举 = 覆盖层 ∪ 底层 − tombstone，按路径规范序输出，保证确定性。
- **写入规则**：对可写命名空间内路径的 `O_CREAT`/写/truncate 一律进覆盖层；
  写覆盖底层文件时按声明尺寸物化底层内容到内存节点后修改（复用现有懒加载
  路径），文件自此归属覆盖层。`unlink` 底层文件 = 在覆盖层放 tombstone；
  `unlink` 覆盖层文件 = 删除覆盖层文件（若底层同路径仍有文件，则同时放
  tombstone，避免"删除后旧文件复活"）。

## 2. 宿主沙盒布局

```
<sandbox-root>/                      # 默认见 §4，CLI 可覆盖
  <package>/                         # 沙盒键 = APK package name，仅此一级
    meta.toml                        # schema 版本、package、最近 versionCode、更新时间
    fs/                              # 覆盖层 1:1 镜像（guest 绝对路径去掉首个 '/'）
      data/data/<pkg>/files/save0.dat
      data/data/<pkg>/shared_prefs/<name>.xml
      sdcard/gameloft/games/.../profile.sav
      sdcard/.../old.dat.__ogplay_tombstone__   # tombstone 标记文件（空文件）
```

- **沙盒键只用 package name**：与 Android 一致，版本升级共享存档；
  `meta.toml` 记录最近一次运行的 versionCode 仅作诊断事实。package name
  来自 `loader.apk_manifest_identity` 的受检事实，不来自用户输入。
- **`fs/` 与 guest 路径 1:1**：无路径数据库、无内容寻址。用户能直接看懂、
  手工备份、删除单个存档文件；这是把"沙盒目录=游戏可变状态"做成用户
  可操作事实的前提（roadmap 06 §2）。
- **meta.toml 为受限纯数据**：复用项目 TOML 受检读取姿态；未知字段、
  schema 版本不匹配明确失败，禁止猜测迁移。
- 空目录持久化：guest `mkdir` 产生的空目录在 `fs/` 下真实创建目录；
  装载时空目录照常进入 VFS 目录索引（消除 `MountHostDirectory`
  "至少一个文件"限制对沙盒的适用）。

## 3. 路径与文件名规则

沙盒装载/落盘是 guest 路径与宿主文件名的翻译边界，规则必须双向确定：

1. **合法字符直通**：guest 路径分段中 `[A-Za-z0-9._-]` 及其他宿主三平台
   均合法的字符原样使用。
2. **确定性转义**：Windows 非法字符（`<>:"|?*`、控制字符）、结尾句点/空格、
   保留设备名（`CON`、`NUL`、`COM1`…）以 `%XX` 百分号转义为宿主文件名；
   装载时反向解码。转义只看字节，不看语义，双向无损。
3. **保留后缀**：`.__ogplay_tombstone__` 与 `.__ogplay_tmp__` 为实现保留；
   guest 创建以保留后缀结尾的路径直接 `-EINVAL` 并记账（真实游戏不会命中，
   命中即值得记录）。
4. **大小写**：VFS 索引维持 ASCII 大小写不敏感，并把 guest 路径规范化为
   ASCII 小写；沙盒按该规范路径落盘，不保存调用者的首见大小写。装载时若
   宿主目录内出现大小写折叠冲突，发布挂载前明确失败。
5. **逃逸拒绝**：`SandboxStore` 对每个相对路径做与 VFS 同源的 traversal
   校验（拒绝 `..`、绝对段、驱动器号），guest 路径永远不可能落到 `fs/`
   之外；这是沙盒之为"沙盒"的硬边界。
6. **长路径**：Windows 上宿主访问统一经绝对路径构造，配合
   `std::filesystem` 处理；超出宿主限制的路径在落盘点报 `-ENAMETOOLONG`，
   不静默截断。

## 4. 沙盒根目录选择

| 场景 | 沙盒根 |
| --- | --- |
| CLI 默认 | roadmap 08 用户数据目录 + `sandbox/`：Windows `%APPDATA%\OGPlay\sandbox`、Linux `~/.local/share/ogplay/sandbox`、macOS `~/Library/Application Support/OGPlay/sandbox` |
| CLI 显式 | `--sandbox-dir <host-dir>`（指向 `<sandbox-root>`，内部仍按 package 分子目录） |
| 调试 | `--ephemeral-sandbox`：临时目录沙盒，退出即弃（等价现状行为） |
| 自动化 | scenario runner / CTest **默认一次性沙盒**；需要测持久化的用例显式指定专用临时目录并自行清理 |

默认根目录解析放在 `frontend`（它已负责 `--external-dir` 等宿主路径策略）；
`runtime/vfs` 只接受一个已存在或可创建的绝对目录，不做
平台约定推断，维持依赖方向。

## 5. 组件划分与依赖方向

```
frontend/cli ──选根、开关──► session（装配）──► runtime/vfs
                                              ├─ VirtualFileSystem（内存语义 + overlay 解析，现有）
                                              └─ SandboxStore（新增：装载/flush/tombstone/转义/配额）
runtime/syscall ──单向──► runtime/vfs
runtime/framework、runtime/integration(dexvm) ──单向──► runtime/vfs
```

- **`SandboxStore` 是唯一接触沙盒目录的代码**，与 `VirtualFileSystem` 同处
  `runtime/vfs` 子模块，只依赖标准库（`std::filesystem` 先例已存在），
  不违反 ADR-0013 的子模块边界。
- `VirtualFileSystem` 新增 `AttachSandbox(SandboxStore&)`：attach 后装载
  覆盖层索引（路径+尺寸+tombstone，内容懒读）、后续写操作产生脏节点并在
  flush 点经 store 落盘。不 attach 时行为与现状逐字节一致——持久化是
  纯增量能力，既有测试不受影响。
- 上层（syscall/framework/dexvm）看不到 `SandboxStore`，只看 VFS 既有
  抽象加新增的目录操作 API（[04 · §1](04-integration.md)）。
