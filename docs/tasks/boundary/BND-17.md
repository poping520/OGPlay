# BND-17 · EGL 1.4 基础状态与 proc-address

## 目标

实现指定 13 个 EGL 基础入口及规范 thread/error/current/query 语义。

## 闭集

`eglGetError`、`eglQueryString`、`eglGetProcAddress`、`eglGetConfigs`、
`eglGetCurrentContext`、`eglGetCurrentSurface`、`eglGetCurrentDisplay`、`eglQueryContext`、
`eglBindAPI`、`eglQueryAPI`、`eglReleaseThread`、`eglSwapInterval`、`eglCreatePbufferSurface`。

## 验收

- [ ] 每个 export seal 为 direct `{fn,EglModule*}`；
- [ ] sticky per-thread error/current、nullable outputs、attribute-list 和 invalid handle 测试通过；
- [ ] `eglGetProcAddress` 只返回 sealed public callable thunk，未知项返回 0；
- [ ] metadata-only preflight 与 late dlopen focused tests 通过。
