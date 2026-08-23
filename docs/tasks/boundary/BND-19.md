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

- [x] fixed conversion、vector length、矩阵后乘、invalid enum/range 有机器测试；
- [x] state 被 fixed draw 消费，失败不部分提交；
- [x] focused GLES1 module/integration tests 通过。

## 结果

35 项由独立 completion binder 显式绑定；16.16 fixed 向量在完整 guest-memory
读取后提交，frustum/ortho/scale 与 fixed matrix 均按列主序后乘。每 texture unit 的
current coordinate 与 point distance attenuation 已进入 fixed draw shader，而非仅保存状态。
