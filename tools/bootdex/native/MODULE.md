# 模块：BootDex guest Cipher JNI

cipher.c 仅作为 API 19 ARM guest 共享库编译。使用 JNI 1.6 必要槽、API 19 bionic
mutex ABI 与 OpenSSL EVP 不透明接口，无 OpenSSL 结构布局依赖；算法在 libcrypto 执行。

算法与 context 使用逻辑 token；registry mutex、引用计数与 per-context mutex 保护
查找/执行/释放。Java owner GC、teardown 释放资源，库析构兜底。独立 buffer 支持
输入/输出别名，临时明文与 key buffer 使用 volatile 清零。reset 明确恢复原始 IV，
修复 OpenSSL CTR 传 null IV 时不复位的行为。

构建：`python3 tools/bootdex/build_bootdex.py build-cipher`。需要支持 ARM 的 clang
和 ELF ld.lld；无 NDK 的 macOS 可使用 Xcode clang 与 Rust bundled ld.lld。
两次构建必须字节一致；源码、工具链和输出哈希写入同一 payload manifest。
输入为用户授权临时提取的 API 19 ARM libcrypto.so；替换为自行构建版本时必须更新
来源、哈希并重跑 AES 定向测试。
