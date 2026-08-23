# BND-21 · GLES2 raster、state 与 object predicates

## 目标

实现 GLES2 已发布缺口中的无/低搬运状态入口及对象判断。

## 闭集

`glBlendEquationSeparate`、`glBlendFuncSeparate`、`glClearDepthf`、`glClearStencil`、
`glColorMask`、`glCullFace`、`glDepthFunc`、`glDepthMask`、`glDepthRangef`、`glFinish`、
`glFrontFace`、`glHint`、`glLineWidth`、`glPolygonOffset`、`glStencilFunc`、
`glStencilFuncSeparate`、`glStencilMask`、`glStencilMaskSeparate`、`glStencilOp`、
`glStencilOpSeparate`、`glIsBuffer`、`glIsFramebuffer`、`glIsProgram`、
`glIsRenderbuffer`、`glIsShader`、`glIsTexture`。

## 验收

- [x] 所有 mutation 调用真实 ANGLE，shadow 只在成功后提交；
- [x] separate front/back stencil 与 object lifecycle 测试通过；
- [x] focused AngleFrame/GLES2 boundary tests 通过。

## 结果

26 个 export 由 `Gles2Module` 的 compile-time direct binding 进入窄化 `AngleFrame` API；
clear depth/stencil 只在 ANGLE 成功后提交 shared shadow。front/back stencil query 与六类
对象创建、绑定、删除后的 predicate 均受检，GLES2 back-stencil 七项 query shape 已补齐。
