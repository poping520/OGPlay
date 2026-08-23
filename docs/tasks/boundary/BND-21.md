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

- [ ] 所有 mutation 调用真实 ANGLE，shadow 只在成功后提交；
- [ ] separate front/back stencil 与 object lifecycle 测试通过；
- [ ] focused AngleFrame/GLES2 boundary tests 通过。
