# 模块：BootDex guest crypto JNI

crypto_jni.c 仅作为 API 19 ARM guest 共享库编译。使用 JNI 1.6 必要槽、API 19 bionic
mutex ABI 与 OpenSSL EVP 不透明接口，无 OpenSSL 结构布局依赖；算法在 libcrypto 执行。

算法与 context 使用逻辑 token；registry mutex、引用计数与 per-context mutex 保护
查找/执行/释放。Java owner GC、teardown 释放资源，库析构兜底。独立 buffer 支持
输入/输出别名，临时明文与 key buffer 使用 volatile 清零。reset 明确恢复原始 IV，
修复 OpenSSL CTR 传 null IV 时不复位的行为。

构建：`python3 tools/bootdex/build_bootdex.py build-cipher`。需要支持 ARM 的 clang
和 ELF ld.lld；无 NDK 的 macOS 可使用 Xcode clang 与 Rust bundled ld.lld。
两次构建必须字节一致；源码、工具链和输出哈希写入同一 payload manifest。
输入仅从 data/android/19/lib/libcrypto.so 读取，构建器不从手机提取目录恢复文件。
用户已撤回 ROM 版库；后续自行构建后必须更新来源、哈希和校验器的来源约束，并重跑
AES/证书定向测试。缺库时构建与完整 payload 校验明确失败。

DVM-106 在同一库增加验签 JNI：SPKI 公钥经 d2i_PUBKEY 完整消费，EVP_Digest/Verify
执行 RSA PKCS#1 v1.5 或 ECDSA，摘要为 SHA1/224/256/384/512。Java 拥有证书解析与
Signature 状态；native 调用内创建的 key/digest context 在所有出口释放，不发布指针。
错误公钥、算法、签名分别明确处理；每个编码输入最多 1 MiB。JNI_OnLoad 安装 OpenSSL
API 19 锁与 pthread identity 回调，析构在 context 清理后撤销回调并释放锁。
保留 build-cipher 名称、crypto_jni.c 和 manifest.cipher_native，避免另建配置/工具链。
