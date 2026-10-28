# 子模块：EGL

实现 EGL core 和明确发布的 KHR 扩展，维护 per-thread error/current、Context、Surface、
share group 与 guest sync/image identity。一个 Context 只拥有一个 native Context，Surface
独立拥有存储；deferred destroy 到最后 current 引用释放后执行。最后 share group 退役通过
GraphicsBoundaryContext 显式回调清理 guest map/sync 记录，禁止依赖 GLES 模块内部实现。
Display terminate 不隐式解绑任何线程；current 资源保留到后续解绑，重复 terminate 成功，
重新 initialize 后可创建新对象。
Java EGL10/EGL14 查询与 native 共用实际扩展字符串；wrapper 参数校验失败也写入同一
per-thread native error 槽，禁止 Java 层返回失败却让 eglGetError 仍报告成功。

依赖 boundary core/services 和底层 gles。扩展必须按实际 ANGLE 能力发布，guest 指针先受检，
禁止把 guest native-buffer 或 image 数值直接当 host 指针。测试：integration `BND34 EGL*`
与原 EGL lifecycle/thread/proc 用例。完整边界见父级 boundary MODULE 和 ADR-0063。

配置身份为有界 RGBA、实际支持的 RGB888/RGB565；choose 最低条件不改写属性。context/surface
使用选中配置，draw/read 与 context 格式失配返回 EGL_BAD_MATCH。Java EGL10/14 共用事实；
RGB565 仅在真实存储探测通过后发布；Metal fallback 为 5/6/5/0，depth/stencil 经真实
Surface 图像导入探测可为 D24S8（否则 D0/S0），GLES1 compatibility/GLES2；
纹理 binding/mipmap 与 fallback GLES3 失败，完整驱动配置集合未发布。
依据 [ADR-0106](../../../../../docs/adr/media.md#adr-0106)；Java/native 共用格式事实。实现依据 [ADR-0090](../../../../../docs/adr/media.md#adr-0090)。

每个 native/managed Context 的 GL_DITHER 初始启用；创建时继承进程已冻结的压缩纹理策略；share 参数不覆盖策略。
首次合法绑定可生成并记录能力快照，切换线程、Surface 或重建 Context 不改变策略。
窗口表面属性最多读取 64 words，仅支持 EGL_RENDER_BUFFER=EGL_BACK_BUFFER；NULL/空表沿用默认双缓冲，single buffer、其他属性/值或未终止表返回 EGL_BAD_ATTRIBUTE 并记账。

API19 lock/unlock surface 校验 display、初始化和 surface；无 ANGLE lock backing 时
返回 FALSE/EGL_BAD_DISPLAY 并记账，不读取属性指针。NV system-time 两入口无驱动
支持时按 4.4 返回 64 位零并设置 EGL_BAD_DISPLAY，不用宿主时间伪装 NV 时钟。
presentation-time 校验后保存 surface 的有符号 64 位生产者时间戳并返回 TRUE，
与 4.4 wrapper 忽略 native-window setter 返回码一致；不改变时钟/调度，未发布该扩展。
这些入口不适用 GL gl_unimplemented 的错误模型。

托管 window surface 通过显式 native-window geometry hook 获取独立 buffer 尺寸；创建、
EGL_WIDTH/HEIGHT 和实际 ANGLE backing 一致。尺寸变化在下一次 swap 发布旧 buffer 后，
重新分配并绑定下一 buffer；成功后 query 返回新尺寸，GL viewport 保持 guest 自己的状态。
分配/绑定失败传播真实 EGL 错误，不冒充 resize 成功。NDK getter 可先于 EGL dequeue 变化。
托管 holder 的读回按 supersample 解析到 buffer 尺寸，经显式纯像素 hook 交 owner 快照，
合成缩放至 View frame；普通 standalone/NativeActivity 无 holder 时保持原 frame service。
