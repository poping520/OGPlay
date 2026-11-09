# 子模块：runtime/raster

CPU 光栅与图像编解码。依赖固定版 Skia SDK，无 DexVM、窗口、GL 或宿主资源探测。
解码入口接收显式字节，校验输入和像素预算。通用图像适配器返回逻辑 ARGB，
供启动器和 UI 投影使用；缩放在预乘空间采样。

每个会话单独持有几何对象表。句柄为分型非零 32 位值，不含宿主指针，
跨会话不复用。对象表由调用方串行访问，RegionIterator 保有独立区域快照。

Bitmap 由调用方显式提供像素跨度和保活对象，Skia pixel ref 持有同一借用。
逻辑 ARGB 通过 Skia 转换，NDK 和 GL 使用存储格式字节。
BitmapFactory 编解码所需的两个头文件取自 SDK 同一 Skia 源码提交，随 SDK 的 BSD 许可分发。
