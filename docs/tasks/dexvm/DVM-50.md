# DVM-50 · 线程 native 上下文与统一 monitor

## 目标（一句话）

为每个 DexVM Java 线程建立完整、可阻塞的 A32/JNI native 上下文，并让解释器、同步方法与 JNI 对同一 Java 对象使用唯一 monitor 状态机。

## 依赖

- DVM-27..29、DVM-48、DVM-49
- `.local/aosp/dalvik/vm/Thread.cpp`
- `.local/aosp/dalvik/vm/Sync.cpp`
- `.local/aosp/dalvik/vm/oo/Class.cpp`

## 交付

- Java 子线程启动时分配独立 A32 CPU、guest stack、Bionic TLS/thread-info、guest thread id、JNI attach/local-frame；退出时按 monitor → JNI → lifecycle → memory 顺序回收，槽位可复用。
- A32 调用帧显式选择线程上下文，同线程重入继承 suspended CPU 的 SP/TLS；native `J`/`D` 返回读取 `r0:r1`。
- `JniEnvironment` monitor 可委托给 session 唯一后端；DexVM bridge 把 JNI object identity 与 thread id 映射到 `VmObjectRef` 与 execution token，`jclass` 规范化到同一 Class object。
- `ACC_SYNCHRONIZED` 覆盖 interpreted、intrinsic、native 方法；实例方法锁 receiver，静态方法锁 Class object，正常返回、Java 异常、VM 展开和线程停止均释放。
- `<clinit>` 对同线程递归放行，对其他 execution context 释放执行锁等待；成功、失败与 teardown 都发布状态并唤醒等待者。

## 有界差异

- 解释字节码仍由 `VmExecutionLock` 串行，不引入完整 Dalvik Thread/Monitor 内存布局或 thin-lock/fat-lock 优化。
- native 上下文池限制并发 32 个 DexVM 子线程；槽位在线程退出后复用，耗尽时明确失败。
- JNI 独立使用场景仍可使用 `JniEnvironment` 默认 monitor table；装配 DexVM bridge 后只使用 DexVM 后端。

## 验证

- `tests/dexvm/interpreter_tests.cpp`
- `tests/dexvm/vm_thread_tests.cpp`
- `tests/runtime/guest_thread_runner_tests.cpp`
- `tests/runtime/android_guest_call_session_tests.cpp`
- `tests/runtime/jni_environment_tests.cpp`

状态：完成。Windows/x64 `windows-msvc` 完整 CTest 通过；证据同步于 `docs/state/CURRENT.md`。
