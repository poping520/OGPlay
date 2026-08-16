# LUI-7 · Bitmap UI renderer

## 目标（一句话）

把 UiTree resolved state rasterize 成 deterministic 透明 RGBA overlay。

## 依赖

- LUI-5、LUI-6。

## 范围与验收

- render list：solid rect、bitmap、push/pop clip，document order 决定 Z-order。
- CPU RGBA8 raster：初始透明、整数定点 source-over、node alpha。
- INVISIBLE/GONE 不产生命令；invalid bitmap/clip stack 明确失败。
- drawable 按 resource id 缓存，draw dirty 未变化时 overlay 不重建。
- exact pixel tests 覆盖透明、bitmap、alpha overlap、Z-order、visibility 与 cache。

## 实施结果

renderer 位于 `runtime/ui`，不依赖 guest、APK、SDL、ANGLE 或 video；DexVM inflater 仅把
解码后的 immutable RGBA bitmap cache 与 resource handle 交给 UI 层。
