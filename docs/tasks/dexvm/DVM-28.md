# DVM-28 · Java Thread 1:1 宿主线程执行

## 目标（一句话）

使 `Thread.start()` 通过 HAL 启动一个对等宿主线程，在独立 DexVM 执行
context 中运行 guest `run()`，并接入 session interrupt/join/teardown。

## 依赖

- DVM-27。

## 验收

- 一个 guest Java 线程对应一个宿主线程；`start` 二次调用、`isAlive`、
  `join`、uncaught exception 与 teardown 均有机器测试。
- 子线程与 root 共享同一 JavaObjectModel/JNI 对象身份，不复制对象世界。
- 未接入 wait-set 时 `Object.wait()` 仍明确失败。

## 交付（完成）

- `dexvm::VmExecutionLock`：全 VM 执行锁，`Call`/`EnsureClassInitialized`
  入口获取，同线程可重入，阻塞原语整体释放再按原深度恢复。同一时刻只有一个
  线程解释字节码——显式记账的限制，换来 linker 缓存、object model arena 与
  intrinsic 侧表单写者。
- `dexvm::VmThreadRuntime`（`vm_threads.h/.cpp`）：`hal::StartHostThread`
  每线程一个宿主线程 + 一个 execution context；`Start`/`Join`/`Interrupt`/
  `IsAlive`/`Yield`/`Shutdown`/`TakeFailure`/`Snapshot`。
- `RequestStop` + 每指令停止检查：teardown 让 guest 死循环真实展开
  （`thread_stopped`），不强杀线程；`DexActivityLifecycle::Stop` 与
  `DexVmGuestBridge` 析构都在 finalize guest 之前 interrupt→join。
- `android.thread.*` 接线：`start`/`join`/`isAlive` 改走真实线程，新增
  `currentThread`/`interrupt`/`isInterrupted`/`interrupted`/`yield`。
  `java.util.Timer` 仍是帧边界协作队列（确定性不变）。
- 持有 guest native 帧时拒绝停泊：`blocking_in_native` +
  `dexvm.threads.block_in_native` 记账。A32 执行器只有一条 root guest 栈。

## 验证

- `tests/dexvm/vm_thread_tests.cpp` 10 个用例；macOS/arm64 CTest 601/601。
- Asphalt 5 exact 回归逐位持平：468 帧 / 468000 tick、主界面 SHA-256
  `9ee57323…`、无 guest fault、suspend/resume 与 clean shutdown 通过。
- Asphalt 6 exact 边界按预期前移：`Object.wait()V` 现在失败在真实子线程
  （`Java thread Thread-287 failed: method cannot be resolved:
  Ljava/lang/Object;->wait()V`），证明 `GLThread` 已在自己的宿主线程上运行。
  证据：`.local/evidence/a6-wu028/`。
