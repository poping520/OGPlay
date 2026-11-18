# 模块：guest crypto JNI 源码

本目录与 ICU、ZIP、NativeBN 的适配源码统一编译为 API 19 ARM `libogplay_jni.so`。
本目录拥有 crypto 注册、状态和 OpenSSL 调用，`crypto_jni.c` 提供唯一 JNI_OnLoad。
使用 JNI 1.6 必要槽、API 19 bionic mutex ABI 和 OpenSSL EVP 接口。仅 RSA 使用
OpenSSL 1.0.1 公开 `rsa_st` 前缀，字段偏移以固定 ARM ELF 为准；加载期按字段构造已知密钥并
核对 PKCS#1 DER，失败时拒绝加载。密码学原语在 guest libcrypto 执行。
API 19 payload 同时提供同一 AOSP revision 构建的 `libssl.so`；它是 OpenSSL 运行库的一部分，
当前 Java/TLS 能力边界仍由上层模块约束。

算法与 context 使用逻辑 token；registry mutex、引用计数与 per-context mutex 保护
查找/执行/释放。Java owner GC、teardown 释放资源，库析构兜底。独立 buffer 支持
输入/输出别名，临时明文与 key buffer 使用 volatile 清零。重初始化按原版 Conscrypt 透传
null IV。OpenSSL 1.0.1 在 CBC 中恢复原始 IV，在 CTR 中保留已推进的计数器。

构建：`python tools/bootdex/build_bootdex.py build-guest-jni`。固定使用 NDK r25c 的
ARMv7 API 19 clang/ld.lld，默认位于 `D:\01_software\android-sdk\ndk\r25c`，可用
`OGPLAY_ANDROID_NDK` 指向同版本 NDK。
两次构建必须字节一致；源码、工具链和输出哈希写入同一 payload manifest。
输入仅从 `data/android/19/lib/libcrypto.so` 读取，构建器不从手机提取目录恢复文件。
该输入固定为 AOSP `platform/external/openssl` 的
`android-4.4.4_r2.0.1` commit `dd1da36b0baa39942f0aef42c4712ef0ad628a83`
以 `aosp_arm-user`、`make -B -j8 libcrypto` 构建的 ARM ELF；来源、哈希和许可证由
payload manifest/validator 共同校验。缺库或哈希不符时构建与完整 payload 校验明确失败。

同一源码 revision 的 `libssl.so` 以 `aosp_arm-user`、`make -B -j8 libssl` 构建，
SHA-256 为 `8b1a7d20e405ff73edcaad592cf846f8e28b78bb446874208d65990b16d79734`；
它与 `libcrypto.so` 共用 OpenSSL NOTICE，来源和哈希由 payload manifest/validator 校验。

RSA JNI 覆盖 EVP_PKEY 构造、类型、比较与长度，SPKI/PKCS#8 编解码、RSA 参数与密钥生成、
四个原版 RSA 块操作，以及 EVP 增量签名/验签。编码输入上限 1 MiB，负数 BigInteger 被拒绝，
模数沿用 OpenSSL 1.0.1 的 16384 位上限。带公开指数的私钥启用盲化，只有 n、d 时关闭。
临时私钥编码及参数缓冲区以 volatile 写入清零。

原版 `NativeCrypto` 类和 descriptor 来自固定 JAR。RSA EVP_PKEY 和 HMAC key 使用同一类型化
token registry，`EVP_PKEY_free` 按类型释放；签名上下文与 digest/HMAC 共用
`EVP_MD_CTX_destroy`。VM 统一追踪 `OpenSSLKey.ctx`、`OpenSSLSignature.ctx` 等 owner 字段。
引用计数与 per-context mutex 保护操作和并发释放。初始化安装 OpenSSL API 19 的锁、pthread
identity 回调与摘要，库析构在释放资源后撤销回调。

MD5、SHA-1/224/256/384/512 摘要共用原版查询、初始化、更新、结束、clone 与 destroy ABI；
update 的 scratch 最多 64 KiB，无累计消息长上限。`NativeVerification.verify` 继续负责
SHA224withRSA 与 ECDSA 验签，完整消费 SPKI，单个编码输入最多 1 MiB。

`java/` 随固定 API 19 类路径编译进入 BootDex。BKS RSA 私钥复用 Conscrypt 的 EVP_PKEY；
EC PKCS#8 私钥保留 BKS 的独立 token。标准/历史 3DES PBE 调用 guest libcrypto。
`trust_jni.c` 路径验证走 `X509_verify_cert`，`tls_jni.c` 的客户端 TLS 使用内存 BIO 和
SSL_CTX/SSL token，不传宿主 FD。RSA 随机填充、盲化、密钥生成、OpenSSLRandom 和 TLS 共用
首次随机操作前一次性的进程播种，由 guest `/dev/urandom` 读取宿主 HAL CSPRNG；熵不可用时明确失败。
DT_NEEDED 含 `libssl.so`。SSLEngine、server TLS 与公开互联网 CA 包未纳入。
路径验证时间经 JNI 读取 `System.currentTimeMillis()` 再 `X509_STORE_CTX_set_time`，不写死墙钟。
握手使用 `SSL_VERIFY_PEER`，在 `SSL_do_handshake` 内调用 Java TrustManager。
客户端 session 经 `i2d_SSL_SESSION`/`SSL_set_session` 恢复；`SSL_session_reused` 在
OpenSSL 1.0.1 中不是导出符号，恢复成功以未再次调用 TrustManager 判定。
payload `libssl.so` 仍为 AOSP 4.4.4_r2.0.1 的 OpenSSL 1.0.1，不能据本地握手发布在线 HTTPS。
