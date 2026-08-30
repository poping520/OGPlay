# DVM-48 · `java.lang.Thread` Core 能力栈

## 目标（一句话）

把既有 guest-host 线程、execution context、monitor 与 interrupt 能力按 pinned
Android 4.4.4 Dalvik 语义完整暴露为 dexvm core `java.lang.Thread` intrinsic。

## 依赖

- DVM-27..29
- `.local/aosp/libcore/libdvm/src/main/java/java/lang/Thread.java`
- `.local/aosp/libcore/libdvm/src/main/java/java/lang/VMThread.java`
- `.local/aosp/dalvik/vm/Thread.cpp`
- `.local/aosp/dalvik/vm/Sync.cpp`
- `.local/aosp/dalvik/vm/native/java_lang_VMThread.cpp`

## 交付

- core catalog 独占声明 `java.lang.Thread`；Java-visible target/name/priority/
  daemon/stackSize/id/hasBeenStarted 为声明式 instance field，不再依赖 Android
  集成侧表。
- root context 获得 id=1、name=`main`、priority=5、非 daemon 的稳定强根 Thread
  对象；child `currentThread()` 返回启动它的同一对象。
- 构造器、per-VM ID、virtual `this.run()`、start-once、isAlive、name、priority、
  interrupt 三 API、join/timed join、sleep、yield、holdsLock 与有界 daemon flag
  语义完成。
- `Object.wait`、timed join 与 sleep 共用 `VmMonitorTable::SetTimeSource` 注入的
  monotonic clock；所有真实停泊释放 `VmExecutionLock`。
- active Thread 为 runtime 强根，target 由 Thread field 的普通对象图追踪；finished
  diagnostic record 不继续强根化 Thread。

## 有界差异

- 纳秒精度在毫秒 Clock 上向上取整；host condition variable 仅调度重查。
- priority 不映射 host scheduler；daemon flag 不驱动 session 自动退出。
- ThreadGroup、ThreadLocal/Thread.State/stack trace、完整 uncaught-handler 分发、
  park/Unsafe/LockSupport 与 deprecated stop/suspend/resume/destroy 不在本 WU；
  deprecated API 显式未实现。
- 子线程 native guest 栈/thread id 与 JNI/DexVM monitor 合一仍是既有 runtime
  边界，因此总能力 `dexvm.threads` / `dexvm.monitors` 保持 `partial`。

## 验证

- `tests/dexvm/thread_intrinsic_tests.cpp`
- `tests/dexvm/vm_thread_tests.cpp`
- `tests/dexvm/vm_monitor_tests.cpp`
- `tests/dexvm/intrinsics_p1_tests.cpp`
- `tests/dexvm/interpreter_tests.cpp`

状态：完成。Windows/x64 `windows-msvc` focused suites 全通过，完整 CTest
830/830；证据同步于 `docs/state/CURRENT.md`。

## 后续补充（2026-08-30）

- 对照 API 19 `Thread.java` 与 `.local/aosp/core.jar`，补齐私有
  `contextClassLoader` 字段及 `getContextClassLoader/setContextClassLoader` 两个公共 API。
- root `main` Thread 默认绑定进程稳定 application ClassLoader；新 Thread 在构造时继承
  创建者的当前值，setter 接受 API 19 允许的 null，不创建第二套 class namespace。
- API shape 与 switch/threaded 双后端默认值、继承、隔离、null 语义由
  `thread_intrinsic_tests` 锁定；既有 Thread、ClassLoader 和 core catalog 定向回归通过。
- Tales Release 关闭 survey 实跑中 `libAmazonGamesJni.so` 与 `libTales.final.so` 均完成
  JNI 初始化，下一首错推进到独立的 `android.location.LocationListener` 类层级缺口。

## 第二优先级补充（2026-08-30）

- 补齐 API 19 `Thread.UncaughtExceptionHandler` interface、线程级与默认级 handler
  字段，以及四个 `get/setUncaughtExceptionHandler` API；引用均由 guest object graph/
  static storage 强根追踪，默认 handler 按 VM 隔离，setter 接受 null。
- child `run()` 未捕获 Java 异常时先选择线程显式 handler，再选择默认 handler；回调自身
  的异常按 AOSP 规则忽略。两者均不存在时保留既有进程致命 `TakeFailure()` 诊断。
- 补齐 `getStackTrace/getAllStackTraces`，把既有 DexVM safe-point snapshot 转成 guest
  `StackTraceElement[]`；全量查询只枚举当前 VM 的真实存活 Thread，并返回真实 HashMap。
- 发布稳定 system/main `ThreadGroup`、`getName/activeCount/enumerate`，Thread 新建时继承
  main group、终止后 `getThreadGroup` 返回 null；`Thread.toString` 使用 API 19 格式。
- ThreadGroup handler fallback 尚未纳入异常分发，因此本批直接从显式 handler 落到默认
  handler；Thread.State、park 与完整 ThreadGroup family 继续 deferred。
- API shape、switch/threaded 双后端的 VM 隔离/null/显式优先/默认 fallback，以及无
  handler 的旧失败路径均由 Thread 定向测试锁定。

## 第三优先级补充（2026-08-30）

- 补齐四个带显式 `ThreadGroup` 参数的 `Thread` 构造器、`activeCount/enumerate/checkAccess/dumpStack/
  countStackFrames`，沿用 bounded system/main ThreadGroup 与当前 VM 的真实线程集合。
- 发布 API 19 `Thread.State` 六个枚举值及 `values/valueOf`；`getState` 投影 runtime 的
  NEW/RUNNABLE/WAITING/TIMED_WAITING/TERMINATED，可观测 wait state 无法区分 monitor
  竞争与 `Object.wait`，因此不伪造 BLOCKED。
- `parkFor/parkUntil/unpark` 复用 Thread 对象 monitor、单许可 `parkState` 与统一 Clock；
  预发许可只消费一次，纳秒向毫秒上取整，interrupt 唤醒后恢复中断标记。
- `pushInterruptAction$/popInterruptAction$` 保持 API 19 的 LIFO 配对和已中断立即回调；
  `interrupt()` 逆序执行当前 action。deprecated `stop(Throwable)` 仅补齐 API shape，继续
  明确未实现，不注入异步异常。
- API shape、显式组继承、状态迁移、枚举、park 边界/许可和 interrupt action 由
  switch/threaded 双后端定向测试锁定；未新建 WU。
- 最终检查补齐 intrinsic execution-local 临时强根：调用中的 receiver/ref 参数自动保活，
  stack trace 数组、全量 trace map 与 dump throwable 在跨 nested guest call 时显式扎根，
  防止低 GC watermark 下回收仍由宿主 handler 使用的 guest 引用。
