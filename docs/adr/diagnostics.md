# 可观测性、线程握手与退出

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0006 · 结构化可观测性是一等接口](#adr-0006)
- [ADR-0023 · native watchdog 只按可观测进展续期](#adr-0023)
- [ADR-0024 · 生命周期首帧使用可观测线程静默握手](#adr-0024)
- [ADR-0025 · teardown 使用单向图形退役与独立取消事实](#adr-0025)
- [ADR-0026 · 停滞诊断使用有界部分快照与外部宿主栈](#adr-0026)

<a id="adr-0006"></a>

## ADR-0006 · 结构化可观测性是一等接口

- 状态：Accepted
- 日期：2026-08-03

### 背景

DEMO 的裸文本输出、静默桩和人工地址解析导致调试循环漫长且无法自动断言。

### 决定

日志内部保存固定消息与结构化字段，以 frame 为主时间轴；能力账本和运行时命中计数分别
描述覆盖与实际需求；Agent Control 用同一结构化状态服务调试和 CI。

### 后果

禁止裸输出、字符串拼接消息和以日志 grep 代替指标。未实现调用必须记账并明确失败。

<a id="adr-0023"></a>

## ADR-0023 · native watchdog 只按可观测进展续期

- 状态：Accepted
- 日期：2026-08-28
- 关联：[ADR-0017](dexvm.md#adr-0017)、
  [DVM-89](../tasks/dexvm/DVM-89.md)

### 背景

DexVM 出向 JNI native 帧可能长期拥有进程循环，因此旧实现允许每次已处理的 syscall、
HLE 或 JNI 边界把 tick watchdog 清零。纯 `getpid`/`clock_gettime` 等查询循环也因此可无限
续期；经过边界并不等于业务进展。

### 决定

supervisor 分发统一返回三类结果：`not_handled`、`handled_idle`、
`handled_advanced`。只有标记为 `renewable_native_frame` 的 JNI native 帧遇到
`handled_advanced` 才把本帧 watchdog 消耗清零。普通 guest thread runner 不续期，行为不变；
exit request 仍在每个已处理边界后优先检查，预算耗尽继续抛带 consumed/PC/LR 的既有诊断。

分类表集中在 syscall dispatcher 与 Android boundary 分发层：

- advanced：返回正字节数的 read/pread/write/pwrite；真实进入 wait queue 后唤醒或非零 timeout
  到期的 futex wait；成功 `eglSwapBuffers`；成功 OpenSL ES BufferQueue Enqueue；JNI 重入。
- idle：身份/时间/stat 等查询，零字节或 EOF read，零字节 write，futex value mismatch、
  零 timeout、进入 wait 前已存在的 interrupt 与 futex wake，sched_yield，内存映射/保护等
  其余已处理调用。进入 wait 后才收到 interrupt 仍证明真实驻留，归 advanced。
- 未列出的已处理 syscall/HLE 默认 idle。新增 family 必须显式进入可审计表并由测试锁定，
  不得因“已处理”自动获得续期。

nanosleep 只有经受检 timespec 请求并确实发生非零 host sleep 才标 advanced；错误或零时长
为 idle。

### 取舍与已知限制

JNI 回调会执行任意 guest Java 代码，是 JNI 边界可提供的最强进展信号，故无条件标
advanced。反复调用琐碎 JNI 的 native 死循环仍可能续期；边界无法可靠区分琐碎调用与实质
工作，这是接受的 bounded 限制。本决定修复纯 syscall 空转无限续期，不声称建立业务语义
追踪。

这对应审计建议中的预算类型区分：普通调用保留总 tick 预算；只有显式参与进程级续期的
native 帧拥有可续期 watchdog，且续期由可观测进展触发。取消/退出预算不并入进展预算，
继续由独立 exit request 安全边界抢占。

### 后果

纯查询或 EOF 空转会稳定耗尽既有预算；park、数据偏移推进、present/audio enqueue 与 JNI
重入可维持长期 native 循环。保守默认可能使尚未分类但合法的长期 HLE 更早暴露既有预算
诊断，这是有意的 fail-closed 行为。

<a id="adr-0024"></a>

## ADR-0024 · 生命周期首帧使用可观测线程静默握手

- 状态：Accepted
- 日期：2026-08-28
- 关联：[ADR-0017](dexvm.md#adr-0017)、
  [DVM-89](../tasks/dexvm/DVM-89.md)

### 背景

DexVM 的 Java 线程各自运行在真实宿主线程上，但字节码由一个 `VmExecutionLock`
串行解释。旧生命周期在 `onStart`/`onResume` 后、第一次 Surface traversal 前调用一次
`VmThreadRuntime::Yield()`，希望新建 worker 已进入 wait-for-surface。这个顺序只描述了
一次调度机会，没有观测 worker 是否真正 sleep、join 或等待 monitor；worker 在 park 前
多次 yield 时，Surface 回调可能先发生。

Android 的 `ViewRootImpl` 不等待任意应用 worker。OGPlay 需要的只是一个有界兼容握手，
不能把首帧变成“所有线程必须停下”的系统级屏障。

### 决定

- `VmThreadSnapshot` 发布按 execution `context_token` 标识的 `wait_state`：`none`、
  `sleeping`、`joining`、`monitor`。`Thread.sleep`、join、`Object.wait` 与 monitor 争用在
  真正释放执行锁的 park 区间维护该状态；终态线程清回 `none`。
- launcher 的 `onStart`/`onResume` 返回后冻结当时已存在的 worker context 集合；根生命周期
  context 不在集合中，握手期间新建的线程不追加入集合。
- 首次 Surface traversal 前，以有限轮 host/guest yield 推进该集合。每个初始 worker 必须
  被观测到至少一次非 `none` wait state；`finished`、`stopped`、`failed` 同样满足要求。
- yield 轮数由命名常量限制。达到上限时写一条 `session.dex_lifecycle` warn，包含上限和
  尚未静默的线程数，然后照常 traversal；不得等待墙钟或死锁会话。
- 初始 window focus 仍由下一次 frame 独立投递，不并入握手或首次 traversal。

### 后果

- 首帧 Surface 顺序依赖可查询状态，不再依赖“一次 yield 足够”的宿主调度偶然性；晚 park、
  永不 park 和无 worker 都有确定出口。
- 该握手是 OGPlay 的 bounded 兼容时序，不宣称复制 Android Handler/Looper、ViewRootImpl
  消息队列或任意 app worker 的 happens-before 关系。超限继续正是与完整 Android 调度模型
  的显式差异。
- `VmThreadRuntime::Yield()` 从未持锁的 lifecycle host 调用时，会先取得执行锁再执行一次
  可观测 handoff；guest `Thread.yield()` 仍只让出当前执行锁，不建立长期公平调度承诺。

<a id="adr-0025"></a>

## ADR-0025 · teardown 使用单向图形退役与独立取消事实

- 状态：Accepted
- 日期：2026-08-28
- 关联：[ADR-0023](diagnostics.md#adr-0023)、
  [DVM-92](../tasks/dexvm/DVM-92.md)

### 背景

OGPlay 的 Surface、ANGLE、音频与统一 Clock 由进程内主循环驱动。主循环收到退出后，
仍在长期 JNI native 帧中的 guest 渲染线程可能继续进入 GPU 或 futex；Activity teardown
又同步等待该线程的挂起确认，形成退出期互等。运行期 100 亿 tick watchdog 不适合作为
取消预算，烧完它会把关闭延迟放大到数十秒。

Android 4.4.4 的 `surfaceDestroyed` 在 Surface 实际销毁前回调；本决定不把“提前失效”
伪称为 AOSP 行为，而是定义 OGPlay 进程退出时的 bounded compatibility 策略。

### 决定

process 发布幂等、不可逆的 `BeginTeardown()`：原子封闭 Java EGL、native/managed
GLES 与 EGL swap，唤醒 swap pacer 和 blocking waits，并发布独立 teardown cancellation。
renewable JNI frame 只在既有 CPU slice 或 boundary 安全点观察取消并失败展开；普通运行期
watchdog 的续期分类、预算，以及非 renewable guest finalizer 均不改变。

lifecycle 在首个 guest teardown 回调前调用该入口，并在 Java thread join 前再次中断等待，
覆盖回调期间新建的 futex。退役后 GLES 中性返回 0/idle，swap 返回 false 并锁存
`EGL_BAD_NATIVE_WINDOW`，不再进入 ANGLE。

### 后果

退出期 guest 回调可能因 native frame 取消而提前失败；lifecycle 已按既有 best-effort
契约继续线程 join、持久化 flush、guest fini 与 surface close。取消不靠 title/profile
分支，也不把完整运行预算全局调小。

<a id="adr-0026"></a>

## ADR-0026 · 停滞诊断使用有界部分快照与外部宿主栈

- 状态：Accepted
- 日期：2026-08-28
- 关联：[Diagnostics](../design/diagnostics/README.md)、
  [ADR-0025](diagnostics.md#adr-0025)

### 背景

局部 DVM/GLES/fault trace 无法在主循环停滞后统一回答 guest 线程、native 边界、syscall、
futex 和 teardown 所处位置。进程内暂停任意宿主线程并展开完整栈又可能占用 loader、heap
或符号锁，使诊断成为新的死锁源。

### 决定

frontend 为一次进程运行创建 `DiagnosticState` 与 `DiagCoordinator`，显式把窄状态对象注入
session/runtime。事实源只记录定容事件或提供 `try_lock` 快照；任一 section 繁忙时输出
`unavailable`，不得阻止其余 section 落盘。协调器只由内部请求、teardown 宿主稳态超时、
Windows 当前会话命名 event 或 POSIX signal self-pipe 唤醒，析构固定 stop→join，禁止
detach。

Futex 只能报告等待集合和 wake 历史；没有 owner edge 时不得生成或暗示确认死锁环。完整
宿主原生栈由 procdump/WinDbg/lldb 在进程外采集，再按快照 `host_tid` 对齐；无符号帧只保留
`module+offset`。

### 后果

停滞现场可以在不重新插桩、不依赖 SDL 主循环的条件下取得，但各 section 不是全局原子
时刻。输出必须携带 schema、采样时间、generation 和 section 状态；自动预算只报警取证，
不改变 ADR-0025 的退出取消语义。
