# 模块：guest ZIP native 边界

API 19 CRC32 的两个私有实例 native 通过 JNI 导出进入 guest `libz.so::crc32`。
源码编入统一 `libogplay_jni.so`，不产生独立共享库，不新增 JNI_OnLoad。

Checksum/CRC32 的公开方法、校验和 crc/tbytes 字段归原版 BootDex；本模块不保存
实例状态。数组经受检 JNI region 分块读取，临时空间有界；空区间保持原校验值，
结果按无符号 32 位扩展为 Java long。访问失败保留 pending exception。

仅支持 CRC32，不声明 Adler32、Inflater/Deflater 或完整 ZIP 流能力。
验证入口：`tests/runtime/native_library_loader_tests.cpp` 的 DVM-201 双解释器用例，
以及 BootDex build/check、guest JNI 可重复构建和 payload 校验。
