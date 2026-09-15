# 子模块：GLES2 共用入口

实现 GLES2 core handler 与 ES3 复用的基础入口。依赖 boundary core/services 和 ANGLE，
复用当前 EGL Context 的执行锁及状态，不创建第二套 EGL 对象。合法 ES3 buffer/PBO/VAO
参数按当前 native Context 校验；guest 指针与 buffer offset 不能混用。

查询在 native 写入前预检准确的输出范围。uniform 查询从真实共享 program 获取宽度；
program/shader 查询保留 GL error，允许可空的 info-log length 指针。测试：integration
`GLES2*`、`BND34 ES3*`。扩展查询采用 graphics service 的统一清单。
超采样 viewport/scissor 只作用于默认 framebuffer；用户 FBO 始终使用 guest 像素尺寸。
ES3 Context 复用的 glVertexAttribPointer 接受 HALF_FLOAT、INT/UNSIGNED_INT 与 packed
2_10_10_10 类型；packed 类型要求 size=4，ES2 Context 仍保持原枚举范围。
