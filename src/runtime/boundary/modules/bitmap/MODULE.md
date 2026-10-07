# 模块：API19 libjnigraphics

三个 AndroidBitmap 导出以同一 sealed catalog 发布和直接绑定。module 仅拥有 ABI/
guest 输出校验、20 字节 little-endian info 与显式 hooks，不依赖 JNI/DexVM。
空 env/object 返回 BAD_PARAMETER；info/addrPtr 可空，非空输出先校验再操作。
失败不写输出，缺 backend 明确返回 JNI_EXCEPTION。引用、异常与 lease 由注入 owner
处理；hooks 必须在 guest 运行前安装、停止后解除。无 Skia、系统 Java 对象或假 SO。
