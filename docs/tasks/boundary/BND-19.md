# BND-19 · GLES1 fixed-point、matrix 与 scalar 补齐

## 目标

以规范转换复用现有 float/state 路径，完成 GLES1 fixed-point 与 scalar 缺口。

## 闭集

`glAlphaFuncx`、`glClearColorx`、`glClearDepthx`、`glClipPlanex`、`glColor4x`、
`glDepthRangex`、`glFogx`、`glFogxv`、`glFrustumf`、`glFrustumx`、`glLightModelf`、
`glLightModelx`、`glLightModelxv`、`glLightx`、`glLightxv`、`glLineWidthx`、
`glLoadMatrixx`、`glMaterialx`、`glMaterialxv`、`glMultMatrixx`、`glMultiTexCoord4f`、
`glMultiTexCoord4x`、`glNormal3x`、`glOrthof`、`glOrthox`、`glPointParameterfv`、
`glPointParameterx`、`glPointParameterxv`、`glPointSizex`、`glPolygonOffsetx`、
`glRotatex`、`glSampleCoveragex`、`glScalef`、`glScalex`、`glTranslatex`。

## 验收

- [ ] fixed conversion、vector length、矩阵后乘、invalid enum/range 有机器测试；
- [ ] state 被 fixed draw 消费，失败不部分提交；
- [ ] focused GLES1 module/integration tests 通过。
