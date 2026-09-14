# 子模块：EGL

实现 EGL core 和明确发布的 KHR 扩展，维护 per-thread error/current、Context、Surface、
share group 与 guest sync/image identity。一个 Context 只拥有一个 native Context，Surface
独立拥有存储；deferred destroy 到最后 current 引用释放后执行。最后 share group 退役通过
GraphicsBoundaryContext 显式回调清理 guest map/sync 记录，禁止依赖 GLES 模块内部实现。

依赖 boundary core/services 和底层 gles。扩展必须按实际 ANGLE 能力发布，guest 指针先受检，
禁止把 guest native-buffer 或 image 数值直接当 host 指针。测试：integration `BND34 EGL*`
与原 EGL lifecycle/thread/proc 用例。完整边界见父级 boundary MODULE 和 ADR-0063。
