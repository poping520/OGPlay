# 模块：BootDex guest crypto JNI 源码

crypto_jni.c 与 `src/guest/icu/icu_jni.c` 统一编译为 API 19 ARM
`libogplay_jni.so`。本目录只拥有 crypto 注册、状态和 OpenSSL 调用；唯一 JNI_OnLoad
在本文件协调两个源码模块。使用 JNI 1.6 必要槽、API 19 bionic
mutex ABI 与 OpenSSL EVP 不透明接口，无 OpenSSL 结构布局依赖；算法在 libcrypto 执行。

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
