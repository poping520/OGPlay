# ADR-0025 · teardown 使用单向图形退役与独立取消事实

- 状态：Accepted
- 日期：2026-08-28
- 关联：[ADR-0023](0023-native-watchdog-observable-progress.md)、
  [DVM-92](../tasks/dexvm/DVM-92.md)

## 背景

OGPlay 的 Surface、ANGLE、音频与统一 Clock 由进程内主循环驱动。主循环收到退出后，
仍在长期 JNI native 帧中的 guest 渲染线程可能继续进入 GPU 或 futex；Activity teardown
又同步等待该线程的挂起确认，形成退出期互等。运行期 100 亿 tick watchdog 不适合作为
取消预算，烧完它会把关闭延迟放大到数十秒。

Android 4.4.4 的 `surfaceDestroyed` 在 Surface 实际销毁前回调；本决定不把“提前失效”
伪称为 AOSP 行为，而是定义 OGPlay 进程退出时的 bounded compatibility 策略。

## 决定

process 发布幂等、不可逆的 `BeginTeardown()`：原子封闭 Java EGL、native/managed
GLES 与 EGL swap，唤醒 swap pacer 和 blocking waits，并发布独立 teardown cancellation。
renewable JNI frame 只在既有 CPU slice 或 boundary 安全点观察取消并失败展开；普通运行期
watchdog 的续期分类、预算，以及非 renewable guest finalizer 均不改变。

lifecycle 在首个 guest teardown 回调前调用该入口，并在 Java thread join 前再次中断等待，
覆盖回调期间新建的 futex。退役后 GLES 中性返回 0/idle，swap 返回 false 并锁存
`EGL_BAD_NATIVE_WINDOW`，不再进入 ANGLE。

## 后果

退出期 guest 回调可能因 native frame 取消而提前失败；lifecycle 已按既有 best-effort
契约继续线程 join、持久化 flush、guest fini 与 surface close。取消不靠 title/profile
分支，也不把完整运行预算全局调小。
