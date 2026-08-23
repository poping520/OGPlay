# BND-18 · GLES1 Android Bounds exports

## 目标

按 KTU84P `libGLESv1_CM.so` 发布并直接实现 7 个 Android Bounds symbol。

## 闭集

`glColorPointerBounds`、`glNormalPointerBounds`、`glTexCoordPointerBounds`、
`glVertexPointerBounds`、`glPointSizePointerOESBounds`、`glMatrixIndexPointerOESBounds`、
`glWeightPointerOESBounds`。

## 验收

- [x] symbol metadata/参数数目与 ROM/AOSP 一致，不改变 core ID；
- [x] 每项 direct binding 到独立 handler，无 name/local-id runtime dispatch；
- [x] count、guest range、fast/slow fault 和 shared client-array state 测试通过。
