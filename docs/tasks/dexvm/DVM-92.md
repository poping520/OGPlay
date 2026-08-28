# DVM-92 · teardown 图形退役与 renewable native 快速取消

目标：关闭窗口后不再让 guest 渲染/挂起握手阻塞进程 teardown，并保持运行期 watchdog
语义不变。

## 范围

- lifecycle Stop 在任何 guest 回调前退役 Java EGL 与 native/managed GLES 边界。
- process `BeginTeardown()` 幂等发布取消、封闭图形并中断 futex/monitor；join 前再次中断。
- renewable JNI native frame 在既有 slice/boundary 安全点观察 teardown 取消；非 renewable
  guest finalizer 与普通运行期预算不变。
- 退役后的 swap 返回 false，`eglGetError` 锁存 `EGL_BAD_NATIVE_WINDOW`；GLES 返回 0、
  分类 idle，不进入 ANGLE。

## 验证

- [x] Windows Release `ogplay`、`ogplay_tests` 构建通过。
- [x] Java EGL/native boundary 两条退役定向用例 2/2、25 assertions 通过。
- [x] renewable watchdog 与 DexVM teardown 定向回归 6/6、41 assertions 通过。
- [x] PVZ 2.3.12 标题画面点击关闭，用户确认可快速正常退出。

状态：已完成。
