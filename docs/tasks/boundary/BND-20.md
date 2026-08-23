# BND-20 · GLES1 query、texture 与剩余 core 补齐

## 目标

完成余下 27 项并证明本轮 62 个已发布缺口全部有 handler。

## 闭集

`glCompressedTexSubImage2D`、`glCopyTexSubImage2D`、`glGetBufferParameteriv`、
`glGetClipPlanef`、`glGetClipPlanex`、`glGetFixedv`、`glGetLightfv`、`glGetLightxv`、
`glGetMaterialfv`、`glGetMaterialxv`、`glGetTexEnvfv`、`glGetTexEnviv`、
`glGetTexEnvxv`、`glGetTexParameterfv`、`glGetTexParameteriv`、`glGetTexParameterxv`、
`glIsBuffer`、`glIsTexture`、`glLogicOp`、`glPointSizePointerOES`、`glTexEnviv`、
`glTexEnvx`、`glTexEnvxv`、`glTexParameterfv`、`glTexParameteriv`、`glTexParameterx`、
`glTexParameterxv`。

## 验收

- [ ] query shape/guest output、texture upload与 object predicate 使用真实 ANGLE/state；
- [ ] point-size array 不被静默忽略；
- [ ] catalog 148 项中本轮 62 项全部 `IsBound`，focused tests 通过。
