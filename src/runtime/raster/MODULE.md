# 子模块：runtime/raster

CPU 光栅与图像编解码。依赖固定版 Skia SDK，无 DexVM、窗口、GL 或宿主资源探测。
解码入口接收显式字节，校验输入和像素预算。通用图像适配器返回逻辑 ARGB，
供启动器图像和 ROM 图形 native 使用；缩放在预乘空间采样。

每个会话单独持有图形对象表。句柄为分型非零 32 位值，不含宿主指针，
跨会话不复用。对象表由调用方串行访问，RegionIterator 保有独立区域快照。

Bitmap 由调用方显式提供像素跨度和保活对象，Skia pixel ref 持有同一借用。
逻辑 ARGB 通过 Skia 转换，NDK 和 GL 使用存储格式字节。
BitmapFactory 编解码所需的两个头文件取自 SDK 同一 Skia 源码提交，随 SDK 的 BSD 许可分发。


Canvas 的根设备直接使用 Bitmap 的像素借用或软件 Surface 的格式缓冲。矩阵、硬裁切、图层和
保存标志独立于 SkCanvas 的栈。API19 的 Region.Op 在设备空间用 SkRegion 运算，矩形和路径
先按当前矩阵转换，Region 本身是设备空间。裁切记录可在替换 Bitmap 时重放。每次绘制把当前
裁切交给 SkCanvas，图层外可见像素按保存标志路由至下层，restore 用 Skia 合成图层。

Paint 持有 SkPaint、SkFont 及独立的 API19 文字状态。字体集合只消费显式注入的字节，
系统字体按需加载并在进程内共享不可变 SkTypeface；应用字体按字节创建独立字体对象。
文字按 UTF-16 码点映射，family 缺字仅查 DroidSansFallback，按逻辑顺序计算 advance、bounds、
轮廓和软件栅格。调用方负责报告需要整形或双向重排的输入。drawPosText 按字形消费位置并逐字形对齐；沿路径文字按 API19 映射轮廓控制点。
Paint 共享不可变 Skia 效果，绘制副本应用 DrawFilter 标志、Shader 当前局部矩阵及阴影。
BitmapShader 保留初始 pixel ref，并按像素版本更新 SkImage，原像素写入对后续绘制可见，
回收或重新配置 Bitmap 不销毁 Shader 已保留的像素。调用方负责维持对应 Java 强边与 NDK 写锁互斥。

NinePatch 消费 API19 编译 PNG 的分区和颜色提示，固定区、拉伸区及透明区域共用布局算法，
由调用方应用源/目标 density。Camera 使用 Sk3DView 的变换；SDK 未导出 Android 专用位置接口，
非默认位置通过公开 SkM44、SkPatch3D 与 SkCamera3D 按同一投影算法计算。
下层模块不持有 Java 对象，不加载宿主字体。
