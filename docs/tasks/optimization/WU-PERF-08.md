# WU-PERF-08 · 进程代码缓存一致性与长帧消息泵

目标：让 guest 动态修改代码后执行新指令，并在等待渲染命令期间保持窗口消息处理。
依赖：既有 DynarmicExecutionContext、ARM syscall bridge、DVM-203 renderer 线程模型。
设计：[ADR-0088](../../adr/runtime.md#adr-0088)。

实现：

- ARM cacheflush 校验成功后由 SVC bridge 调用 CPU 范围失效接口，广播到同进程所有
  存活 executor，包括 clone、Java native 与重入实例。后端自身锁保护队列，执行线程
  完成范围失效；不整缓存清空、不在 CPU 层识别 Android syscall。
- 生命周期等待 renderer 时释放 worker/VM 锁后调用宿主消息泵；CLI 复用 SDL owner
  检查与 Clock 节流，不递归分派输入或 guest 生命周期。

验收入口：

- `ARM cacheflush publishes patched ARM and Thumb code to all process CPUs`：真实 SVC、
  A32/T32 指令集、多 CPU、非法/空/不相交范围、CPU 退役与重建。
- `Dynarmic invalidation interrupts an active peer and resumes patched code`：并发请求。
- `DVM-197 renderer EGL policies establish current context before events and callbacks`：
  等待中消息泵的宿主线程归属、VM 锁释放及原有主 Looper 往返。

验证：Windows Release `ogplay` / `ogplay_tests` 构建成功；上述回归及 SVC ABI、TLS、
CPU budget/halt/host hook、JNI 长字符串定向 8 用例/955 断言通过。测试构建同时修正了
既有 JNI 测试的两处 nodiscard 警告（不改变断言含义）。日志在
`.local/perf-dt/{build-formal-tests.log,formal-tests.log,formal-regression-tests.log}`。

2026-09-30 用户实测确认：Loading 未再出现未响应，点击地图打开弹框流畅，FPS 更稳定。
本任务不调整游戏锁帧；该反馈不等于完整游戏兼容验收，也不覆盖已有 teardown 停滞。
