# 模块：guest ZIP native 边界

API 19 CRC32、Deflater/Inflater 的受审 native 通过 JNI 导出进入 guest `libz.so`。
源码编入统一 `libogplay_jni.so`，不产生独立共享库，不新增 JNI_OnLoad。

Checksum/CRC32 的公开方法、校验和 crc/tbytes 字段归原版 BootDex；本模块不保存
实例状态。数组经受检 JNI region 分块读取，临时空间有界；空区间保持原校验值，
结果按无符号 32 位扩展为 Java long。访问失败保留 pending exception。

Deflater/Inflater 的公开方法、流头尾、CRC/长度校验和 Java 字段归原版 BootDex；native
只拥有 z_stream、输入快照和逻辑 token。registry/per-stream mutex 与引用计数保护
执行和释放，输入经 JNI region 受检复制，输出 scratch 至多 64 KiB，无累计消息缓存。
原版 inRead/finished/needsDictionary 更新、zlib 错误与 flush/reset/dictionary 行为遵循
API19 边界。end 显式释放；NativeZip.release 是幂等 GC/teardown 释放入口，库析构兜底。
Java streamHandle 使用原版 -1 关闭标记；不得发布宿主或 guest 原始指针。

Inflater.setFileInputImpl 不导出，由 core 按能力缺口记账并抛 UnsupportedOperationException。
仅支持字节数组及其 GZIP/DeflaterOutputStream/InflaterInputStream 组合；RAFStream 只作为
原版类型依赖，不声明 ZipFile、Adler32 或完整 ZIP/JAR 能力。
验证入口：`tests/runtime/native_library_loader_tests.cpp` 的 DVM-201/DVM-214 双解释器用例，
以及 BootDex build/check、guest JNI 可重复构建和 payload 校验。
