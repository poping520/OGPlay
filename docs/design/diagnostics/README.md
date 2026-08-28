# Diagnostics · 通用停滞诊断能力设计

本目录定义 OGPlay 的通用运行时诊断能力，供架构评审和后续实施使用。目标不是建设完整
调试器，而是让挂死、互等、静默 park 和退出停滞在**不重新插桩、不重编译**的前提下，
能取得足够缩小搜索面的现场。

2026-08 的 teardown 卡死定位需要反复执行“加探针→编译→复现”，单轮约 5 分钟，最终
耗时约 3 小时。本设计把那次定位中有效的临时探针固化为跨 title、跨运行路径的能力。

## 现有基础与缺口

OGPlay 不是完全没有可观测性，实施不得另造一套平行设施：

| 已有能力 | 可复用契约 | 本设计仍需补齐 |
| --- | --- | --- |
| DVM-52 diagnostics | 默认关闭、构造期定容的整数事件环，查询时格式化，schema-1 JSON | 跨 lifecycle/native/syscall 的统一快照 |
| DVM-52 Java 栈快照 | 在 `VmExecutionLock` 安全点查询全部 execution context | 锁被占用时必须明确降级，不能阻塞停滞快照 |
| DVM-90 guest fault diagnostics | A32 fault、寄存器、指令窗口与 Java/JNI 上下文 | 非 fault 的挂死/退出停滞现场 |
| NativeActivity GLES trace | 固定 raw ring、独立 trace mutex、最近 2048 次 EGL/GLES 调用 | 与 guest 线程和 teardown 阶段关联 |
| `VmThreadSnapshot.wait_state` | sleeping/joining/monitor 等 Java 等待事实 | futex、pacer、native boundary 等跨层事实 |
| ADR-0023/0025 | 运行期进展预算、teardown cancellation 与图形退役 | 只报警和取证的停滞预算，不重复定义取消策略 |

因此，准确的问题定义是：**已有局部 trace/fault 能力，但缺少不依赖主循环的、跨层且
有界的停滞快照。**

## 范围与术语

- 核心交付物称为**停滞快照**（stall snapshot），不默认声称“已证明死锁”。只有存在
  明确 owner→waiter 边且形成环时，才允许标注 `confirmed_cycle`；其他情况只报告等待
  集合、最近动作和停滞候选。
- 覆盖 lifecycle/teardown、OGPlay 管理的 guest↔host 执行路径、futex/monitor/pacer、
  syscall/native bridge 及已有 DVM/GLES trace。
- 不实现完整调试器、IDE、Binder/system_server 或重型常驻采样器；宿主完整原生栈由
  procdump/WinDbg/lldb 等外部工具负责。
- 所有探针零游戏分支，不识别 title、包名或厂商。
- 诊断开销必须有界。运行期开关关闭时允许一次可测的廉价分支/原子读取，不宣称无法
  证明的“绝对零开销”；需要绝对零热路径成本的构建可使用编译期裁剪。

## 架构与生命周期

```text
Windows Local named event / POSIX self-pipe ─┐
MCP / CLI / teardown timeout ────────────────┤
                                             ↓
session ─owns→ debug::DiagCoordinator ──→ versioned txt/json snapshot
                    │
                    ├─ atomic/seqlock facts: lifecycle、thread execution state
                    ├─ fixed POD rings: syscall、native bridge、DVM、GLES
                    └─ bounded try-snapshot: futex、monitor、thread registry
```

`DiagCoordinator` 位于 `src/runtime/debug/`（新增 `MODULE.md`），由 session 唯一拥有。
runtime 下层只暴露窄查询接口或稳定 POD 记录，不反向依赖 coordinator，不从下层回调
session。所有 section 使用同一 schema、宿主 `steady_clock` 时间基准和容量策略。

协调器可以有独立等待线程，但必须满足：

1. 线程只等待 stop/trigger 事件，不轮询，不参与 guest 调度；主循环挂死时仍可响应。
2. session 析构前先发 stop，使 OS wait 立即返回，再做**有界且可证明完成的 join**；禁止
   `detach`，禁止线程在 session 或数据源销毁后继续访问它们。
3. 每个数据源查询都有独立预算；不得获取 `VmExecutionLock`，不得无限等待互斥锁。
4. 数据源繁忙时输出 `unavailable` 与原因，继续生成其他 section。部分快照优于诊断线程
   自身挂死。
5. 正常退出不等待正在进行的符号解析或大文件写入；快照使用预分配有界缓冲，超限明确
   `truncated`。

### 数据源并发契约

| 数据源 | 允许的读取方式 | 失败语义 |
| --- | --- | --- |
| lifecycle 阶段/teardown 进展 | 原子发布阶段 id、generation、开始时间 | 从未进入则 `not_started` |
| syscall/native/GLES/DVM ring | 预分配 POD ring；atomic sequence 或短 `try_lock` 拷贝 | 竞争时 `unavailable: busy` |
| Futex/monitor/pacer | `try_lock` 后复制有界记录，立即释放再格式化 | 不等待锁，标记对应 section busy |
| Java 栈 | 仅复用现有 `VmExecutionLock` 安全点查询 | 锁不可立即获得则省略，绝不阻塞 |
| 线程注册表 | 短 `try_lock` 或 copy-on-write；保留有界退出 tombstone | 区分 `active`/`recently_exited`/`busy` |

快照不追求跨所有 section 的全局瞬时一致性。每节必须带
`captured_at_steady_ns`、`generation`、`status=complete|partial|unavailable`，读者不得把
不同采样时刻拼成确定的因果链。

## 核心能力

### D1 `GuestStallSnapshot`

按稳定顺序输出：

1. 摘要：触发原因、teardown 阶段、持续时间、静止线程数、完整/部分 section 数；只有
   具备显式 owner 边时才输出确认的等待环。
2. lifecycle：`Stop`/`Suspend` 阶段、generation、开始时间和最近进展时间。
3. guest 执行表：`{guest_tid, name, host_tid, context_token, path, state, ARM PC,
   wait primitive, wait argument, last progress}`。
4. 最近边界动作：关联 DVM/GLES 既有记录及 D3/D4 新记录。
5. 同步原语等待集合：等待者、预期值、超时和累计 wake 事实；有 owner 的 monitor/mutex
   可额外形成 wait-for edge。
6. section 状态、截断计数、schema 版本和安全提示。

`path` 至少区分 Java interpreter、`RunAndroidArmGuestThread`、`InvokeA32GuestCall`、
A32 syscall 和 park。线程退出后保留固定数量的 tombstone，避免最后一个关键线程在快照前
从注册表消失。

文本首行供人快速判读；JSON 供测试和工具消费。两者来自同一不可变 snapshot DTO，禁止
分别拉取运行时状态而产生互相矛盾的结果。文件名为
`<workdir>/diag-<pid>-<seq>.txt|.json`，写临时文件后原子 rename；JSON 顶层必须有
`schema_version`。文本可按节 flush，但不得在持有数据源锁时格式化或执行 I/O。

### D2 同步等待集合

`FutexTable::CollectWaiters()` 输出：

```text
futex address → [{guest_tid, expected_value, timeout_kind, wait_since}], wake_count
```

这只是等待集合，不提供 futex owner，也不能仅凭两个地址/两个等待者证明环或“无人可能
唤醒”。允许的提示是 `no_observed_wake_progress`，并必须带观察窗口。`VmMonitorTable` 等
确有 owner TID 的原语可输出 owner→waiter 边并参与环检测。

### D3 syscall 固定事件环

在 `A32SyscallDispatcher::DispatchOutcome` 单一咽喉点记录固定整数事实：

```text
{sequence, steady_ns, guest_tid, syscall_nr, result, progress_class}
```

每 guest thread 固定容量，溢出丢最旧并累计 dropped count。热路径不保存字符串、宿主
指针或动态容器；复用 DVM-52 的“构造期定容、查询时格式化”契约。

### D4 native bridge 固定事件环

RegisterNatives 与 `Java_` 导出共用的调用边界记录：

```text
{sequence, steady_ns, context_token, guest_tid, method_id, phase=enter|return|throw}
```

方法文本查询时由稳定 id 解析。最后一条只有 `enter` 而没有匹配终态时，快照可陈述
“observed enter without terminal event”，不能直接断言该方法本身死锁。

### D5 teardown 阶段与诊断预算

- `DexActivityLifecycle::Stop` 和 session `Stop` 用稳定阶段 id 原子发布进入/完成与最近
  进展时间，并继续写 `session.teardown` 结构化日志。
- 诊断预算使用宿主 `steady_clock`，不使用可能暂停、快进或被 guest 控制的统一 Clock。
- 超过显式配置的预算时触发 D1；后续按有界频率记录“仍处于 X、已持续 Y”。
- 预算只负责报警取证。退出取消、图形退役和 wait interrupt 继续遵循 ADR-0025；本设计
  不重新决定硬杀、EINTR 或 guest teardown 语义。
- 基础阶段事实常驻且固定大小；自动快照初期由 `--diag-on-teardown-timeout=<seconds>`
  显式启用。取得开销数据后，是否默认启用宽松阈值另行决策。

### D6 guest↔host 执行注册表

在 `RunAndroidArmGuestThread` 与 `InvokeA32GuestCall` 两条 OGPlay 管理的路径登记
`{host_tid, guest_tid/context_token, path, name, entered_at, last_progress}`。只承诺覆盖
OGPlay 创建或直接承载 guest 的线程；ANGLE、SDL、驱动等自行创建的第三方线程不伪称
已注册，需要宿主栈时由 OS/外部 dump 枚举。

### D7 宿主原生栈：外部 dump 优先

核心运行时不通过任意 `SuspendThread` + DbgHelp 或 POSIX `backtrace()`尝试展开所有
线程：暂停线程可能持有 loader/heap/DbgHelp 锁，POSIX `backtrace()`也不能直接展开任意
pthread，这条路径会把诊断本身变成新死锁源。

标准流程是：

1. D1 提供 guest 语义、host_tid、原始 ARM PC 和等待事实；
2. procdump/WinDbg/lldb 获取全部宿主线程的完整栈；
3. 手册按 host_tid 对齐两份证据，并把无 PDB 的 ANGLE/驱动帧标为
   `module+offset`，不猜函数名。

未来如需内置原始 host context，只能作为独立 ADR/WU：限定平台、最大暂停窗口、排除自身、
所有路径恢复线程、DbgHelp 全局串行且预初始化；不得阻塞 D1–D6 的交付。

### D8 触发与输出安全

- 自动：显式启用的 teardown 预算；guest fault 可附带轻量快照，但 fault 原报告仍是主证据。
- Windows：使用当前用户会话的 `Local\ogplay-diag-<pid>-<nonce>` 命名事件和阻塞 wait；
  nonce 通过结构化启动日志发布，避免 PID 复用与跨用户误触发，安全描述符只授权当前用户。
- POSIX：`SIGUSR1` handler 只能设置 lock-free 标志或写 self-pipe/eventfd；分配、日志和
  快照均由诊断线程执行。
- MCP `diag.snapshot` 只是便捷入口。它由主循环泵送，主循环挂死时可能失效，不能作为
  唯一触发路径。

输出默认进入 workdir 的诊断目录，采用仅当前用户可读权限、单文件和总目录容量上限、
有界保留数量。schema 禁止宿主绝对用户路径和原始 guest 字符串；方法名、guest 路径、
URL 等按字段白名单输出或脱敏。截断、脱敏和写盘失败都必须明确记账，不能静默成功。

### D9 符号与调试手册

`docs/playbook/TROUBLESHOOTING.md` 固化 Windows procdump/WinDbg 与 POSIX lldb 的抓取、
`host_tid` 对齐、`~*k`/模块偏移判读及清理策略。`ci`/RelWithDebInfo 作为诊断构建；
Release 不要求常态携带大体积 PDB。ANGLE/驱动无符号时只报告模块归属和偏移。

## 两个 WU 的实施切分

事实源、聚合器与外部触发共同构成“停滞后无需重新插桩即可取现场”的最小闭环。只交付
等待集合或事件环虽然便于单元测试，却仍要求排障者临时编写消费代码，因此不单独算作
可验收 WU。进程内闭环与宿主外部调试工具链的风险和依赖不同，保留为两个 WU；不再按
触及文件数做机械拆分。

| WU | 范围 | 独立收益 | 机器验收 |
| --- | --- | --- | --- |
| WU1 · 进程内停滞诊断闭环 | 公共 snapshot DTO/schema/steady time；D2–D6 事实源；复用 DVM/GLES trace；D1 coordinator；D8 OS/MCP/CLI 触发、部分快照、隐私与容量限制；同步 ADR-0026、MODULE 和 capabilities | 主循环停滞后无需重编译或临时消费代码，可从进程外限时取得 guest 语义现场 | ring 关闭态/溢出/并发快照；wait-set 不误判 cycle；双执行路径与 tombstone；子进程停滞后 OS 触发限时落盘；busy section 部分成功；stop→join 无 detach/UAF；schema、隐私和配额可判定 |
| WU2 · 宿主栈与排障工作流 | D7/D9 外部 dump、host_tid 对齐、符号构建和无符号模块判读；跨文档与能力账本最终一致性收口 | 从 WU1 的 guest 语义现场继续定位到宿主原生模块/偏移，形成可复用的完整排障流程 | fixture dump/文本验证 host_tid 对齐；procdump/WinDbg/lldb 手册可执行；无符号模块不猜函数；诊断构建说明、文档链接和能力账本一致 |

ADR-0025 已用于 teardown cancellation，本设计使用 **ADR-0026**。WU1 必须随代码同步
对应 MODULE.md、capabilities 和 schema 契约；WU2 只做宿主工具链及跨文档一致性收口，
不能用文档工作未完成阻塞已经通过机器验收的进程内诊断闭环。

## 验证策略

所有停滞用例必须放在**可杀死的测试子进程**中，父测试设置短 wall-time 预算并负责回收；
禁止让 CTest 进程本身进入不可恢复死锁。优先使用 barrier、fake source 和可取消 park，
不使用 30 秒真实等待。

最低验收矩阵：

1. 诊断关闭：不分配 ring，事件计数不变；benchmark 约束运行期开关分支开销，不声称
   “绝对零”除非编译期裁剪。
2. ring：enter/return/throw、溢出丢最旧、dropped count、稳定排序和 schema golden。
3. wait-set：两个 futex 等待者只报告事实，不产生虚假 cycle；显式 monitor owner 环才标
   `confirmed_cycle`。
4. partial snapshot：人为占用一个数据源锁，该节限时变成 `unavailable: busy`，其他节和
   JSON 仍完整落盘。
5. trigger：主循环人为停滞后，OS 外部触发仍能在预算内生成快照；MCP 失败不影响该路径。
6. lifecycle：正常退出 stop→join；快照中途退出也无 detach、UAF、泄漏或无限等待。
7. privacy/quota：宿主用户路径、原始 URL 不进入输出；截断、权限和目录配额可判定。

真实游戏只用于验证诊断信息是否足以缩小搜索面，不作为能力测试依赖；开启诊断取得的
结果不直接充当兼容性 gate。

## 风险与明确边界

| 风险 | 设计约束 |
| --- | --- |
| 数据源锁正被挂死线程持有 | 只用 atomic/seqlock/短 try-snapshot；失败节明确 unavailable |
| 快照跨节不一致 | 每节时间戳、generation 和状态；不宣称全局瞬时一致 |
| 诊断线程拖慢或阻塞退出 | stop event 唤醒 + 有界工作 + join；禁止 detach |
| 热路径开销 | 预分配固定 POD、查询时解析、benchmark 阈值、可选编译期裁剪 |
| Futex 误诊 | 没有 owner 就不画 wait-for edge，不使用“无人可能唤醒”结论 |
| 原生栈采集自死锁 | 完整栈外部 dump 优先；进程内任意线程 unwind 不进核心范围 |
| 输出泄密或写满磁盘 | 字段白名单/脱敏、用户权限、单文件/目录配额和有界保留 |

## 本次事故与能力映射

| 当时的临时探针 | 固化能力 | 快照允许陈述的证据 |
| --- | --- | --- |
| lifecycle Stop 阶段日志 | D5 | `teardown phase=X, unchanged_for=92s` |
| native bridge 方法探针 | D4 | `enter LoaderThread.suspendAppThreads, no terminal event observed for 92s` |
| futex wait/wake 探针 | D2 | `address A has waiter root; no wake progress observed in window` |
| syscall 263 后静默 | D3 | 最近 syscall 序列及最后记录时间 |
| host↔guest 双路径探针 | D6 | host_tid、guest_tid/context、执行路径 |
| 手写 StackWalk64/PDB 调整 | D7/D9 | 外部 dump 中 host_tid 对应线程位于某模块/偏移 |
| 无人干预的挂死复验 | D1/D8 | 超时自动或 OS 外部触发后生成有界的部分/完整快照 |

D1–D6 的目标不是自动给出根因，而是把一次“数小时插桩搜索”压缩为“先读取一份可信、
有边界标注的现场，再决定是否抓外部 dump”。
