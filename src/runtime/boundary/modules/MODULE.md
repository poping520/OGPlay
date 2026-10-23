# 子模块：boundary concrete modules

module_catalog 是声明式导出与参数数量的组合目录，具体行为归各 concrete module。
导出名称、thunk 绑定与 proc forwarder 必须一致；不能因存在宿主函数就发布 guest 能力。
API19 GLES 的公开 ELF 入口集合与扩展能力分开：未支持的驱动入口按 AOSP
Loader.cpp/gl_unimplemented 记账、结构化 error 日志、返回确定性零，不读取 guest
输出或修改 GL 状态。入口存在不增加 GL_EXTENSIONS，也不代表扩展已实现。
GLES2 与 GLES3 delta 共用 libGLESv2.so，GLES1 使用独立目录；EGL image target 的 guest
identity 转换复用 EGL 对象表。依赖方向和公共契约见父级 boundary MODULE。

验证：catalog generator gates、boundary integration 和 architecture.boundary_hot_path。
