# DVM-48 · `java.lang.Thread` Core 能力栈

## 目标（一句话）

把既有 guest-host 线程、execution context、monitor 与 interrupt 能力按 pinned
Android 4.4.4 Dalvik 语义完整暴露为 dexvm core `java.lang.Thread` intrinsic。

## 依赖

- DVM-27..29
- `.local/asop/libcore/libdvm/src/main/java/java/lang/Thread.java`
- `.local/asop/libcore/libdvm/src/main/java/java/lang/VMThread.java`
- `.local/asop/dalvik/vm/Thread.cpp`
- `.local/asop/dalvik/vm/Sync.cpp`
- `.local/asop/dalvik/vm/native/java_lang_VMThread.cpp`

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
