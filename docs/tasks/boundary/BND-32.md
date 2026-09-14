# BND-32 · EGL 1.4 core 导出闭合

## 目标

补齐 2026-09-14 GLES 审计报告列出的 9 个 EGL 1.4 core 函数，使 API 19
`libEGL.so` core 名称完整，并让范围外能力明确返回规范 EGL 错误。

## 交付

- 发布 `eglCreatePixmapSurface`、`eglCopyBuffers`、`eglSurfaceAttrib`、
  `eglBindTexImage`、`eglReleaseTexImage`、`eglWaitGL`、`eglWaitNative`、
  `eglWaitClient`、`eglCreatePbufferFromClientBuffer`。
- `eglWaitGL`/`eglWaitClient` 对当前 ANGLE context 执行真实完成同步；
  `eglWaitNative(EGL_CORE_NATIVE_ENGINE)` 在没有第二套 native renderer 时成功。
- 当前不支持 pixmap、OpenVG client buffer 和 texture-capable pbuffer；相关入口先校验
  display/config/surface/buffer/attribute，再返回 `EGL_BAD_NATIVE_PIXMAP`、
  `EGL_BAD_MATCH`、`EGL_BAD_PARAMETER` 等明确错误，不伪造对象或成功。
- `eglSurfaceAttrib` 接受当前 surface 已有的 destroyed swap/default resolve 事实；
  请求未宣告的 preserved swap/box resolve 返回 `EGL_BAD_MATCH`。

## 验证

- Windows `windows-msvc` Debug `ogplay_tests` 构建通过。
- `ogplay_tests --test-case='*EGL*'`：21/21 用例、397/397 断言通过。
- 未运行全量测试、游戏 gate 或跨平台测试。

状态：完成。
