# 模块：KeyStore/JSSE guest Java

本目录拥有 OGPlay 软件 KeyStore 与 JSSE 的 Provider、SPI、条目状态、BKS codec、
只读 AndroidCAStore、路径验证适配、客户端 SSLSocket/HTTPS 及软件 crypto SPI。
代码固定以 API 19 `android.jar` 编译并进入 BootDex，由 DexVM 执行；不得依赖宿主 JDK、
BouncyCastle 运行时或 C++ 条目/会话影子状态。

普通状态只保存在 guest Java 对象图；流由调用方拥有，文件只经既有 Java IO/VFS；时间经
`System.currentTimeMillis()` 的统一 VM Clock，随机数经现有 Provider/CSPRNG。密码原语、
路径验证和 TLS 只能经既有 `libogplay_jni.so` 使用 guest libcrypto/libssl。解析先进入临时表，
校验成功后整体发布。

`AES/CBC/ZeroBytePadding` 由本目录的 CipherSpi 完成分段缓冲、零填充和解密去尾零，
实际分组运算复用 API 19 Conscrypt 的 `AES/CBC/NoPadding`，仍进入 guest libcrypto。

`OGPlayCrypto` 发布 `KeyGenerator.AES` 和 `AlgorithmParameters.AES`。密钥生成遵循 API 19 BC，
默认 192 位，显式位数按字节向上取整，拒绝 `AlgorithmParameterSpec` 初始化。AES IV 参数支持
`IvParameterSpec` 与 DER OCTET STRING 往返，`ASN.1`（含 null）和 `RAW` 格式名区分大小写，
未知编码格式返回 null。Conscrypt 的 CBC 参数查询使用该服务。

`OGPlayCrypto` 的 `Cipher.RSA` 接受 ECB/NONE，裸 RSA 默认 NoPadding。精确命中的
NoPadding/PKCS1Padding 由 AndroidOpenSSL 提供，通用 SPI 也复用这两个原版 transformation。
OAEP 在原始 RSA 上执行 Java EME-OAEP 与 MGF1，接受 MD5、SHA-1/224/256/384/512、独立摘要和
`OAEPParameterSpec` 的 PSpecified label。解码以固定次数扫描摘要与分隔符，格式错误统一抛
BadPaddingException；去掉前导零的短密文按右对齐解密。`AlgorithmParameters.OAEP` 提供规格及 PKCS#1 DER 往返。SHA-224 摘要
复用已有 Conscrypt native ABI，为固定 API 19 Provider 缺少的 SHA-224 OAEP 提供支撑。

BKS RSA PKCS#8 在本目录解析为 RSAPrivateCrtKeySpec，经原版 KeyFactory 构造 CRT 私钥，EC 仍使用 BksPrivateKey。
同一 native key 继续提供 PKCS#8 编码用于 BKS 写出和 TLS 客户端认证。

`OGPlayCrypto` 独立发布 `PBEWITHSHAAND256BITAES-CBC-BC` SecretKeyFactory 及 SHA1/SHA-1
名称别名。公开 API/规格来自原版 BootDex，SPI 与 PBEKey 状态归 guest Java；复用 PKCS12
SHA-1 KDF，固定派生 32-byte key，不使用 PBKDF2。无盐规格返回延迟密码编码；有盐密钥
保留 PBE 元数据，getKeySpec/translateKey 对齐 API19 的原始字节转换。密码最多 65536
UTF-16 单元、盐最多 65536 bytes、迭代 1..1000000，超限明确失败。此服务不提供 PBE Cipher，
不冒充 BC Provider；DES/RC/GCM 等公开规格可用不代表对应算法已注册。

默认 CA 来自 owner 注入的版本化 `OGPLAYCA` 包，路径为 `/system/etc/security/cacerts.ogplay`；
缺失或损坏时 TrustManagerFactory.init(null) 失败，不读取宿主证书库。AndroidCAStore 保持
API19 只读；写/删/store 抛 UnsupportedOperationException。

已完成 KeyStore/BKS 发布条件并注册默认 BKS。提供 OGPlayJSSE：PKIX
TrustManagerFactory、真实 libcrypto 路径验证与只读 AndroidCAStore；客户端 SSLSocket/
HTTPS 经 raw NetworkRuntime 完成 loopback TLS 1.2 握手与 GET。校验时间走统一 Clock；
`SSLContext.init` 与 `DefaultContext` 都不覆盖调用方已设置的全局 HTTPS 工厂；协议/SNI
作用于 native SSL；TrustManager 在握手内执行；session 以二进制 id 缓存并经
`SSL_set_session` 恢复，超时在查询/枚举/恢复路径上检查。请求头在 `connect()` 前快照，
因为 `URLConnection.getRequestProperties()` 在已连接后抛 Already connected。
不包含 AndroidKeyStore、通用 CertPathValidator.PKIX、SSLEngine、server TLS 或公开互联网 CA。

Session 超时按统一 Clock 与原创建时间计算；恢复复用原 Session 对象，查询不续期。
缓存容量 0 表示无限，只有超过容量才淘汰；二进制 ID 使用不可变字节键。
