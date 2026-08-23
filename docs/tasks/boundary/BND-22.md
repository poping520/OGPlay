# BND-22 · GLES2 buffer、texture 与 query

## 目标

实现 GLES2 pointer transfer、texture/buffer 与通用 query 缺口。

## 闭集

`glBufferSubData`、`glCompressedTexImage2D`、`glCompressedTexSubImage2D`、
`glCopyTexImage2D`、`glCopyTexSubImage2D`、`glGetBooleanv`、`glGetBufferParameteriv`、
`glGetFloatv`、`glGetFramebufferAttachmentParameteriv`、`glGetRenderbufferParameteriv`、
`glGetTexParameterfv`、`glGetTexParameteriv`、`glTexParameterf`、`glTexParameterfv`、
`glTexParameteriv`。

## 验收

- [x] IDL shape 驱动完整 guest preflight，fault 前不调用 ANGLE；
- [x] query 结果来自真实 ANGLE或唯一 shared shadow，不伪造；
- [x] compressed/copy texture 与 pack/unpack状态测试通过。

## 结果

15 个 export 以 compile-time direct binding 进入 `Gles2Module` 命名 handler；所有 pointer
先由生成 IDL 与 shared transfer state 完整搬运。buffer/texture/framebuffer/renderbuffer
query 调用真实 ANGLE，ETC1 fallback、copy、pack/unpack 保持与唯一 context 一致；非法
texture-parameter guest pointer 在 native mutation 前失败且原状态不变。
