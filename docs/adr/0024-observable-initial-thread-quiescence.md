# ADR-0024 · 生命周期首帧使用可观测线程静默握手

- 状态：Accepted
- 日期：2026-08-28
- 关联：[ADR-0017](0017-bounded-dex-interpreter.md)、
  [DVM-89](../tasks/dexvm/DVM-89.md)

## 背景

DexVM 的 Java 线程各自运行在真实宿主线程上，但字节码由一个 `VmExecutionLock`
串行解释。旧生命周期在 `onStart`/`onResume` 后、第一次 Surface traversal 前调用一次
`VmThreadRuntime::Yield()`，希望新建 worker 已进入 wait-for-surface。这个顺序只描述了
一次调度机会，没有观测 worker 是否真正 sleep、join 或等待 monitor；worker 在 park 前
多次 yield 时，Surface 回调可能先发生。

Android 的 `ViewRootImpl` 不等待任意应用 worker。OGPlay 需要的只是一个有界兼容握手，
不能把首帧变成“所有线程必须停下”的系统级屏障。

## 决定

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

## 后果

- 首帧 Surface 顺序依赖可查询状态，不再依赖“一次 yield 足够”的宿主调度偶然性；晚 park、
  永不 park 和无 worker 都有确定出口。
- 该握手是 OGPlay 的 bounded 兼容时序，不宣称复制 Android Handler/Looper、ViewRootImpl
  消息队列或任意 app worker 的 happens-before 关系。超限继续正是与完整 Android 调度模型
  的显式差异。
- `VmThreadRuntime::Yield()` 从未持锁的 lifecycle host 调用时，会先取得执行锁再执行一次
  可观测 handoff；guest `Thread.yield()` 仍只让出当前执行锁，不建立长期公平调度承诺。
