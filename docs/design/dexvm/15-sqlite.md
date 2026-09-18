# API 19 SQLite 与真实数据库开发规划

日期：2026-09-18。状态：完成；SQL-01..03 与最终验收矩阵已闭合。
任务：[DVM-186](../../tasks/dexvm/DVM-186.md)。决定：[ADR-0068](../../adr/dexvm.md#adr-0068)。

## 1. 目标与当前事实

采用 **API 19 BootDex Java 数据库栈 + host SQLite amalgamation + OGPlay VFS**，
替换现有 OGDB1 数据存储。应用获得真实 SQL、约束、事务和 SQLite 文件；普通 Java
语义复用原版，不在 C++ 中另写一套 SQLiteDatabase/SQLiteOpenHelper 状态机。

- 子模块位于 `third_party/sqlite-amalgamation`，源自项目自有镜像，当前固定到
  `ed6f533374f8340dee587f714115e75d6d8d6aa6`，头文件声明版本 3.53.4。
- 已核对 Git blob 的 `sqlite3.c` SHA3-256 与上游发布值一致；其余三份源码与子模块
  README 的摘要一致。Windows 工作区为 CRLF，来源校验应读取 Git blob，不能直接
  将工作区摘要差异判为源码改动。发布证据以子模块 README/上游记录为准。
- 子模块由 OGPlay CMake 静态构建；数据库、rollback journal 与临时文件通过 OGPlay VFS。
- production catalog 已切换到 API 19 原版数据库 Java 栈；host 仅保留 SQLiteConnection、
  CursorWindow、SQLiteGlobal/Debug 等固定 native ABI 和受审平台 overlay。
- `dexvm.sqlite_vfs` 已由 DVM-186 的真实 SQLite/VFS 范围取代 DVM-88 有界旧实现。

当前症状来自 Cookie 初始化线程；源码还触达参数查询、Cursor 关闭、64 位过期时间、
增删改和事务。此样本只确定需求，不得成为 SQL 字符串、表名或包名分支。

## 2. 支持范围

首期必须闭合进程内文件数据库与内存数据库：

- open/close、execSQL、query/rawQuery、insert/update/delete，以及 SQLiteStatement
  的编译、绑定、执行和释放；保留各 Android 入口不同的错误/返回值约定。
- NULL、INTEGER/int64、REAL、TEXT、BLOB；列名/顺序、参数计数、空结果和 Cursor 生命周期。
- 引擎执行普通 SQL、索引、约束与触发器，不另写受限 SELECT 解析器。
- rollback-journal 模式的提交/回滚、Java 嵌套事务、线程归属、连接竞争、取消与 teardown。
- Helper 建库、升级、版本更新及异常回滚；真实 SQLite 文件往返与跨会话持久化。

首期不承诺 WAL/shared-memory、跨进程数据库共享、跨库原子提交、ContentProvider/Binder
游标传输、动态加载扩展、完整 FTS/空间扩展或应用自带 SQLite SO 与本后端同时写同库。
这些入口须有明确拒绝/合法不可用结果及账本，不能静默忽略设置或回退到另一文件系统。
只读底层挂载的普通库可验证读取；需写入才能恢复的 hot journal 不得伪装恢复成功。

## 3. 目标分层与唯一所有权

```text
guest 应用
  ↓
BootDex：SQLiteDatabase / SQLiteOpenHelper / SQLiteProgram / Statement / Query
         SQLiteSession / SQLiteConnectionPool / SQLiteConnection
         ContentValues / Cursor 家族 / CursorWindow Java 层
  ↓ 固定 API 19 native 签名及少量受审平台 overlay
Android integration：类型转换、Java 异常、回调、句柄准入
  ↓ 显式接口
host DatabaseRuntime：connection / statement / window 资源及生命周期
  ↓
原版 SQLite 静态库 → SQLite VFS adapter → 唯一 VFS → SandboxStore
```

- 拟新增 `src/runtime/database/`，只依赖下层 VFS、Clock 与显式注入服务，不反向依赖
  DexVM、JNI、integration。新目录同步建立 MODULE；Android 方法仍归既有 API family。
- Java 配置、绑定参数、引用计数、事务栈和连接池状态归原版字段；host 只拥有真正的
  SQLite/window 资源，不复制 Java 事务状态。SQLite 内部锁与 pager 状态由引擎维护。
- API 19 的 connection/statement/window native 参数包含 32 位 int；使用带类型校验和
  防陈旧重用策略的逻辑 token，不将宿主指针截断。token 限于所属 VM/session。
- dispose/close/finalizer/GC/teardown 汇入唯一资源释放机制；先终止使用者，再释放资源。
  回调临时 guest 引用进入 GC root；原版 finalizer 不作为进程退出的唯一清理保证。
- 同一路径按 VFS canonical identity 共享锁；Java 与 native 资源各自只有一份权威状态。

## 4. BootDex 迁移边界

从现有固定 API 19 framework JAR 按闭包选类，源码目录用于语义对照。最终类清单只维护
在 `tools/bootdex/api19.json`，不将整个 android.database 包无条件导入。

| 类族 | 处理方向 |
| --- | --- |
| ContentValues、Cursor 接口、SQLException/SQLite 异常族、SQLiteClosable | 原版 Java，连同必要内部类/接口 |
| SQLiteDatabase/OpenHelper、Program/Statement/Query、Cursor/driver | 原版控制流程和数据状态 |
| SQLiteSession/ConnectionPool/Connection、configuration/cancellation | 保留线程会话、嵌套事务、等待、取消及连接管理 |
| AbstractCursor/AbstractWindowedCursor、CursorWindow | Java 层原版；窗口 native 使用有容量上限的进程内存 |
| SQLiteGlobal、系统资源/配置、诊断与 BlockGuard 依赖 | 逐项审计；平台事实经窄 overlay 注入，不机械引入系统服务 |

先产出依赖/native/overlay 清单，再迁移。必须覆盖默认 open 路径：SQLiteGlobal 配置、
locale/collation 注册、android_metadata、原版调用的 PRAGMA 与框架 SQL 函数。
LOCALIZED/UNICODE 排序不能悄悄替换为 BINARY；先评估现有 ICU 能力，缺少可复用后端时
在 SQL-01 明确依赖和成本。不能把“默认打开即失败”的实现宣称为可用数据库。

CursorWindow 的行容量、start position、NULL/type conversion 与 refill 语义需要验证；
不能用无限复制全部结果代替窗口预算。保留逃逸到进程外的 Parcel/FD 入口为明确边界。
类型迁入 BootDex 的同一批次删除同名 intrinsic 声明，避免两个类身份或两套状态。

## 5. 引擎与 VFS 契约

### 引擎构建及 API 19 差异

- 在 OGPlay CMake 依赖层为固定 `sqlite3.c` 建静态目标；不修改子模块源码，不依赖
  系统 SQLite DLL，不编译 shell.c 进入运行时，不要求开发者重新生成 amalgamation。
- 初始配置评估 `SQLITE_OS_OTHER=1`、`SQLITE_THREADSAFE=1`、禁用动态扩展、禁用 mmap。
  OS_OTHER 对应初始化与 VFS 注册要完整；目标选项显式固定，不套用第三方推荐选项包。
- 每次 open 显式选 OGPlay VFS；URI、ATTACH、VACUUM INTO、临时文件等也不能选择宿主
  VFS 或逃逸沙盒。首期 ATTACH 明确拒绝；其他写文件入口按范围验证或明确拒绝。
- 当前 authorizer 明确拒绝 ATTACH/DETACH 与 WAL 切换；`:memory:` 由 SQLite 内存 pager
  承载且不创建 VFS 文件。生产 xRandomness 使用 HAL OS CSPRNG 注入。
- 新引擎与 API 19 不完全等价。审计双引号兼容、LIKE/排序、PRAGMA、外键默认值及浮点
  文本转换；3.53.4 的 FP_DIGITS=15 可作为旧精度候选，不等于复刻全部旧舍入行为。
  版本查询返回真实版本。AOSP/device 语义对照与独立 SQLite 文件互操作分别验收。

### 文件、锁和持久性

- adapter 实现 xOpen/read/write/truncate/fileSize/sync/delete/access/fullPathname、锁和
  file-control 必要子集；零填充 short read、磁盘满、只读、损坏与 I/O 失败映射准确。
- 数据库、journal、临时文件统一进入 VFS；用受检路径和逻辑 FD，不调用 HostPathFor 绕过沙盒。
- SHARED/RESERVED/PENDING/EXCLUSIVE 锁按实际文件身份管理，同进程不同连接亦不可省略。
  不允许 xLock/xUnlock 成功空操作。外部进程共享写入不在本期能力内。
- 墙钟与单调等待使用统一 Clock；随机源显式注入既有服务，宿主等待不另建 guest 线程。
- 等待连接/锁、长查询和取消须遵循 VM 解锁/重入约定；禁止持有 VM 全局锁等待另一个
  guest 线程提交。定义 VM、pool、database、VFS 锁顺序并验证 teardown 可唤醒等待者。
- 先限定受测 rollback-journal 模式；WAL 返回可观察的失败或 API 允许的 false。
  不发布空的 xShm 实现，不谎报设备的原子写能力。
- SandboxStore 的单文件 tmp+rename 不能替代 SQLite 多文件提交协议。逐项核对 journal
  先落盘、主库同步、journal 删除及目录元数据屏障。缺失能力在 VFS 层补齐后再承诺
  对应耐久性，数据库模块不另建宿主存储。进程崩溃恢复与机器断电耐久性分别记录证据。

## 6. 真实 SQLite 与损坏处理

生产代码只保留真实 SQLite：旧 parser、旧格式 reader/writer、影子数据库状态表和文件头
预检均已删除。不存在旧格式迁移、识别或执行失败后的回退路径。

SQLite 引擎负责判断文件是否合法；`SQLITE_CORRUPT`/`SQLITE_NOTADB` 映射为
`SQLiteDatabaseCorruptException`。API 19 原版 `SQLiteDatabase` 随后写 EventLog，调用默认
损坏处理器经同一 VFS 删除主库及辅助文件，并在同一次 open 中重建。EventLog 读取依赖
Android 日志服务，保持明确不支持。

## 7. 三阶段连续交付

DVM-186 是唯一任务单，原 8 个单元合并为下面 3 个阶段，现均已完成。阶段用于记录进度，
不是审批或会话停止点；收到实施指令后默认按顺序连续推进至最终验收，不逐阶段等待确认。
依赖审计随实现完成，不单独交付调查报告；不按类、方法或测试组另建工作单。

| 阶段 | 依赖 | 实现范围 | 阶段出口 |
| --- | --- | --- | --- |
| SQL-01 真实数据库底座（完成） | 子模块 | 简短闭包/差异审计并确定 locale、配置与平台契约；静态引擎、资源令牌、VFS IO/锁、journal 和恢复 | 内存/文件库读写、空 BLOB、零字节初始库、打开标志、错误映射、令牌隔离、规范路径锁竞争、路径拒绝、short-read/ENOSPC、提交回滚与故障后重开通过 |
| SQL-02 Java 全链路接入（完成） | 01 | BootDex 类闭包、native/overlay、CursorWindow、Helper、线程事务与生命周期；同步删除被接管的 intrinsic/影子状态并切换唯一后端 | build/check、类链接与 native 清单、异常子类、窗口类型/容量/requiredPos/countAllRows 流式 refill、建库升级/rawQuery、嵌套事务/取消/回调/GC/关闭竞争的双解释器定向验证通过 |
| SQL-03 互操作与运行验收（完成） | 02 | 清理旧实现残留、独立 SQLite 互读、跨会话/崩溃恢复、损坏库删除重建及真实 APK 复现；按事实更新文档 | 第 8 节矩阵全部闭合，记录原首错消失、下一独立首错和支持边界 |

执行时复用同一构建目录与夹具，相关实现合并构建；每项验证证据只登记一次，代码变化或
新疑点才重跑。验收矩阵作为检查表，不再拆成工作单。确有阻塞时记录精确缺口和续接点，
恢复后接着执行，不重新审计已确认且未变化的内容。

SQL-02 可先在隔离 fixture 使用候选 BootDex；依赖闭合后再切换生产 catalog，不能发布
半套 Java/native 组合。事务恢复、并发和损坏处理仍是必要验收，不以“已越过 rawQuery”替代。

## 8. 最终验收矩阵

| 必验事实 | 证据 |
| --- | --- |
| 原版类与正确 ABI | BootDex build/check、类链接、native 注册与双解释器实际调用 |
| 通用数据读写 | 参数绑定/计数错误、NULL/Unicode/BLOB/int64、空结果、列顺序、主键冲突、真实查询 |
| 原子状态转换 | 成功提交、未标成功回滚、嵌套事务、Helper callback 抛错后 schema/version 不变 |
| 线程与生命周期 | 两个真实 guest 线程竞争/取消、重复释放、陈旧 token、会话隔离、teardown 无悬挂 |
| 文件兼容 | 独立 SQLite 工具生成→OGPlay 读改→工具查询与 integrity_check；反向亦覆盖 |
| 持久化恢复 | 独立 session 重开；journal 故障注入和进程崩溃检查，断电保证另列验证边界 |
| 损坏处理 | 非 SQLite 文件由引擎报损坏；EventLog 记录后默认处理器删除并重建真实 SQLite 文件 |
| 真实触发路径 | 关闭 survey；独立空库与含有效/过期记录的真实库分别运行，记录下一首错 |

真实 APK 类级验收直接加载 Angry Birds 2.3.0 原始 `classes.dex` 并执行其
`SQLiteCookieStorage`/`CookiePersistanceManager`：空库完成建表，过期记录经事务删除，
有效记录由原始 `rawQuery` 读回。有效记录随后在 JSON 反序列化前触发独立的
`NativeDecimalFormat.open` 缺口；该故障发生在 SQLite 查询和对象装载之后，不属于本专项。
完整进程关闭 survey 的运行也已越过原 `SQLiteDatabase.rawQuery` 首错，下一首错同属
Jackson/日期格式化初始化。

复用 `tests/dexvm/network_sqlite_tests.cpp` 及 BootDex/GC/线程夹具；新增 runtime/VFS
测试仅覆盖新风险。只构建受影响目标，Windows 使用 windows-msvc，不运行无关全量测试。
真实 APK 首错复现按 [排查手册](../../playbook/TROUBLESHOOTING.md)与
[新游戏手册](../../playbook/NEW-TITLE.md)；本专项不要求正式 title gate 的三轮场景。

## 9. 文档与完成判定

ADR、CURRENT、capabilities 与受影响 MODULE 已按最终实现同步；DVM-88 保留为历史证据。
SQL-01..03 与验收矩阵闭合后本期标记完成。WAL 与跨进程共享仍独立未支持。
CURRENT 只记录真实运行变化。SQL 成功、Cookie 存储正确与游戏持续可玩分开报告。

## 参考

- [开发设计参考](../../playbook/DEV-REFERENCE.md)、[DVM-88](../../tasks/dexvm/DVM-88.md)
- [Android integration 契约](../../../src/runtime/integration/dexvm_android/MODULE.md)、
  [VFS 契约](../../../src/runtime/vfs/MODULE.md)
- 本地 AOSP：`.local/aosp/framework/base/core/java/android/database/` 与
  `core/jni/android_database_SQLiteConnection.cpp`、`android_database_CursorWindow.cpp`
- [子模块来源记录](../../../third_party/sqlite-amalgamation/README.md)
- [SQLite VFS](https://www.sqlite.org/vfs.html)、
  [原子提交](https://www.sqlite.org/atomiccommit.html)、
  [3.53.4 发布说明](https://www.sqlite.org/releaselog/3_53_4.html)
