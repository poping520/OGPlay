# 子模块：boundary services

向 concrete module 提供显式依赖接口与共享状态，不依赖 facade 或上层 session。
GraphicsBoundaryContext 使用显式 owner/callback 连接组合层；GuestGlContext 保存单个
Context 的 GL shadow，可共享的对象元数据用独立共享所有权保存。FrameService 发布统一
拥有型帧；graphics dispatch 搬运 guest 数组与输出，native 调用由底层 ANGLE 执行。

GLES2/3 浮点纹理扩展按当前 ANGLE 上下文的完整扩展 token 条件发布；线性过滤依赖
基础浮点支持。字符串、已支持的索引与数量查询共用同一清单。浮点纹理采样不声明
浮点颜色附件支持；GLES1 保持独立固定管线扩展范围。
`GL_OES_packed_depth_stencil` 也按当前原生完整 token 发布，D24S8 storage/双附件
与 framebuffer status 仍直接使用 ANGLE 的真实结果。

GLES2 混合 client 属性与 element Buffer 绘制按实际 Buffer 的索引区间回读并计算最大索引，
再预检/暂存 guest 属性；索引 offset 不解释为 guest 地址。临时 array binding 在成功与
异常路径均恢复；实际 Buffer 是权威数据源，不维护另一套可能过期的索引内容镜像。

图形状态不能以进程全局变量替代 Context/share-group 生命周期。内部固定管线临时状态必须
恢复 guest 的 framebuffer、VAO、整数属性与 buffer binding。测试：boundary integration
图形用例、BND34 回归及 architecture.boundary_hot_path。其余服务遵守父级 MODULE 契约。

FrameService::TryStats 只 try-lock 复制 GPU 标量统计，不复制 draw_targets，不执行 GL 或读取渲染对象；忙时 nullopt。

FrameService 接受显式上层 readback filter，在不持有帧存储锁时查询。认证覆盖时仅省略
宿主 present 的 ANGLE 读回；guest 绘制、readPixels 与统计仍执行。未认证立即恢复读回。

GL 错误诊断由每次 A32CallFrame 的独立结果传递给 FrameService；一次边界返回错误的
调用计数一次，trace 保留精确 GLenum。它不取代 Context 的首错锁存，不额外调用
glGetError，也不覆盖未在边界捕获的原生错误、宿主异常或完整 GLES 验证。
