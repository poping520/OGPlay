# 子模块：boundary services

向 concrete module 提供显式依赖接口与共享状态，不依赖 facade 或上层 session。
GraphicsBoundaryContext 使用显式 owner/callback 连接组合层；GuestGlContext 保存单个
Context 的 GL shadow，可共享的对象元数据用独立共享所有权保存。FrameService 发布统一
拥有型帧；graphics dispatch 搬运 guest 数组与输出，native 调用由底层 ANGLE 执行。

图形状态不能以进程全局变量替代 Context/share-group 生命周期。内部固定管线临时状态必须
恢复 guest 的 framebuffer、VAO、整数属性与 buffer binding。测试：boundary integration
图形用例、BND34 回归及 architecture.boundary_hot_path。其余服务遵守父级 MODULE 契约。
