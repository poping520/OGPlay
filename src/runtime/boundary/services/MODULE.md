# 子模块：boundary services

向 concrete module 提供显式依赖接口与共享状态，不依赖 facade 或上层 session。
GraphicsBoundaryContext 使用显式 owner/callback 连接组合层；GuestGlContext 保存单个
Context 的 GL shadow，可共享的对象元数据用独立共享所有权保存。FrameService 发布统一
拥有型帧；graphics dispatch 搬运 guest 数组与输出，native 调用由底层 ANGLE 执行。

GLES2 混合 client 属性与 element Buffer 绘制按实际 Buffer 的索引区间回读并计算最大索引，
再预检/暂存 guest 属性；索引 offset 不解释为 guest 地址。临时 array binding 在成功与
异常路径均恢复；实际 Buffer 是权威数据源，不维护另一套可能过期的索引内容镜像。

图形状态不能以进程全局变量替代 Context/share-group 生命周期。内部固定管线临时状态必须
恢复 guest 的 framebuffer、VAO、整数属性与 buffer binding。测试：boundary integration
图形用例、BND34 回归及 architecture.boundary_hot_path。其余服务遵守父级 MODULE 契约。

FrameService::TryStats 只 try-lock 复制 GPU 标量统计，不复制 draw_targets，不执行 GL 或读取渲染对象；忙时 nullopt。
