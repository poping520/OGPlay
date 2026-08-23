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

- [ ] IDL shape 驱动完整 guest preflight，fault 前不调用 ANGLE；
- [ ] query 结果来自真实 ANGLE或唯一 shared shadow，不伪造；
- [ ] compressed/copy texture 与 pack/unpack状态测试通过。
