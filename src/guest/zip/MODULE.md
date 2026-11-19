# 模块：guest ZIP native 边界

API 19 CRC32、Adler32、Deflater/Inflater 的受审 native 通过 JNI 导出进入 guest `libz.so`。
源码编入统一 `libogplay_jni.so`，不产生独立共享库，不新增 JNI_OnLoad。

Checksum、CRC32 和 Adler32 的公开方法与累加器字段归原版 BootDex；本模块不保存
实例状态。数组经受检 JNI region 分块读取，临时空间有界；空区间保持原校验值，
结果按无符号 32 位扩展为 Java long。访问失败保留 pending exception。

Deflater/Inflater 的公开方法、流头尾、CRC/长度校验和 Java 字段归原版 BootDex；native
只拥有 z_stream、输入快照和逻辑 token。registry/per-stream mutex 与引用计数保护
执行和释放，输入经 JNI region 受检复制，输出 scratch 至多 64 KiB，无累计消息缓存。
原版 inRead/finished/needsDictionary 更新、zlib 错误与 flush/reset/dictionary 行为遵循
API19 边界。end 显式释放；NativeZip.release 是幂等 GC/teardown 释放入口，库析构兜底。
Java streamHandle 使用原版 -1 关闭标记；不得发布宿主或 guest 原始指针。

`Inflater.setFileInputImpl` 按 API19 的文件输入路径复用容量足够的 native 缓冲区，直接对
`FileDescriptor.descriptor` 对应的 guest VFS fd 定位并读取。它返回实际读取长度，I/O 错误
以 errno 对应的消息抛出 IOException。原版 ZipFile 经此路径读取 ZIP32 DEFLATED 条目。
验证入口为 `tests/runtime/guest_zlib_tests.cpp`、guest JNI 可重复构建和 payload 校验。
