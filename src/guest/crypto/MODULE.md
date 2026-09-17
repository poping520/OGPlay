# 模块：BootDex guest crypto JNI 源码

crypto_jni.c 与 `src/guest/icu/icu_jni.c` 统一编译为 API 19 ARM
`libogplay_jni.so`。本目录只拥有 crypto 注册、状态和 OpenSSL 调用；唯一 JNI_OnLoad
在本文件协调两个源码模块。使用 JNI 1.6 必要槽、API 19 bionic
mutex ABI 与 OpenSSL EVP 不透明接口，无 OpenSSL 结构布局依赖；算法在 libcrypto 执行。
API 19 payload 同时提供同一 AOSP revision 构建的 `libssl.so`；它是 OpenSSL 运行库的一部分，
当前 Java/TLS 能力边界仍由上层模块约束。

算法与 context 使用逻辑 token；registry mutex、引用计数与 per-context mutex 保护
查找/执行/释放。Java owner GC、teardown 释放资源，库析构兜底。独立 buffer 支持
输入/输出别名，临时明文与 key buffer 使用 volatile 清零。reset 明确恢复原始 IV，
修复 OpenSSL CTR 传 null IV 时不复位的行为。

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

DVM-106 在同一库增加验签 JNI：SPKI 公钥经 d2i_PUBKEY 完整消费，EVP_Digest/Verify
执行 RSA PKCS#1 v1.5 或 ECDSA，摘要为 SHA1/224/256/384/512。Java 拥有证书解析与
Signature 状态；native 调用内创建的 key/digest context 在所有出口释放，不发布指针。
错误公钥、算法、签名分别明确处理；每个编码输入最多 1 MiB。JNI_OnLoad 安装 OpenSSL
API 19 锁与 pthread identity 回调，析构在 context 清理后撤销回调并释放锁。
Cipher、Digest、Signature 与 ICU 共用一个 manifest library 条目，不允许恢复独立 crypto SO。

DVM-108 增加 7 个 MessageDigest JNI 入口：MD5/SHA1/SHA256/SHA384/SHA512 的
algorithm lookup/size、init/update/final、ctx copy/destroy。算法和 context 使用逻辑 token，
摘要长度为相应 EVP 输出大小；无效/过期 token 明确失败，clone 使用 EVP_MD_CTX_copy_ex。
每次 update 分块读取 Java byte[]，scratch 最多 64 KiB，无累计消息长度上限。
final 消费 context，Java 清零 ctx 字段；GC/teardown 通过同一 destroy 释放。
registry/per-context mutex、引用计数与库析构兜底沿用 Cipher 约定；MD5 不在宿主实现。

DVM-171 使用固定 JAR 的原版 `NativeCrypto` 类和全部 native descriptor；本模块只导出当前
26 个已实现后端及受限 `clinit`。`clinit` 验证 `JNI_OnLoad` 已完成线程回调和摘要注册，
不初始化或发布 TLS。OGPlay 私有证书验签导出归 `NativeVerification.verify`；digest/HMAC/
OpenSSLKey 的字段 token 清理由 VM 统一登记，registry、锁、GC/teardown 语义不变。

DVM-172 的 `java/` 目录以固定 API 19 类路径编译进入 BootDex，拥有自有 KeyStore
Provider、SPI、条目、BKS codec 与 PKCS#12 KDF；Provider 以 `OGPlayKeyStore` 独立身份发布
标准 BKS v2 写出和 v0/v1/v2 读取。BC 仅为隔离 oracle，生产 DEX 拒绝其类型引用。
标准/历史 3DES PBE 调用 guest libcrypto；RSA/EC PKCS#8 使用独立 EVP_PKEY token registry，
引用、GC、显式释放与 teardown 一致。生产 C++ 不保存条目或 KeyStore 影子状态。
DVM-173 在同一 `libogplay_jni.so` 增加 `trust_jni.c` 与 `tls_jni.c`：路径验证走
`X509_verify_cert`，客户端 TLS 使用内存 BIO 与逻辑 SSL_CTX/SSL token；JNI_OnLoad 调用
`ogplay_tls_on_load` 初始化 libssl。`NativeTls.seed` 在 `SSL_CTX_new` 前执行 `RAND_seed`，
未播种时 `createContext` 明确失败。不扩展原版 NativeCrypto ABI，不把宿主 FD 传入 guest SSL。
DT_NEEDED 含 `libssl.so` 并写入 manifest。SSLEngine、server TLS 与公开互联网 CA 包不在本模块范围。
路径验证时间经 JNI 读取 `System.currentTimeMillis()` 再 `X509_STORE_CTX_set_time`，不写死墙钟。
握手使用 `SSL_VERIFY_PEER`，在 `SSL_do_handshake` 内调用 Java TrustManager。
payload `libssl.so` 仍为 AOSP 4.4.4_r2.0.1 的 OpenSSL 1.0.1，不能据本地握手发布在线 HTTPS。
