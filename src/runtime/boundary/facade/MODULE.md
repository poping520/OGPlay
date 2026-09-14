# 子模块：boundary facade

组合 AndroidBoundaryHle 的 concrete module、服务和 sealed thunk，不实现第二份 GL/EGL
状态机。所有跨层调用经显式服务接口。GLES1/2/3 与 EGL image thunk 在 catalog/绑定处一致
发布；guest Context 状态切换、share group 退役由 EGL registry 驱动。

managed surface 关闭仅退役自身资源；清空活动 shadow 不能清空其他 Context 的映射。
遵守父级 boundary MODULE 的依赖方向、日志和 guest 指针约束。验证：boundary integration
图形/线程回归及 architecture.boundary_hot_path。
