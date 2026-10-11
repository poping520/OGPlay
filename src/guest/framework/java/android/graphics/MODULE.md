# 模块：有界 BitmapFactory Java 客户端

BitmapFactory.Options 保留固定 API19 原版字段、默认构造与取消标志；resource/stream/
byte-array 包装按 AOSP 流程处理参数、资源密度、流关闭及已知解码失败。移除 Trace/
Skia transport，使用三个 private native 边界；未支持能力的 UOE 不被 resource catch 吞掉。

普通位图支持 ARGB8888、密度缩放、bounds 和 inMutable；复用/降采样/其他 config/
purge/dither/quality/NinePatch 明确失败，不模拟完整 Skia。inInputShareable 在未启用
purgeable 时按 API19 忽略。调用方提供的 stream 不关闭，decodeResource 打开的 stream
始终关闭。取消通过原 mCancel，串行同步解码没有另一个 Skia 任务，仍为 best effort。

来源：本地 AOSP 4.4.4 BitmapFactory.java，保留 Apache-2.0 头。
