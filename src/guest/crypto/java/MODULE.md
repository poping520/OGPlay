# 模块：KeyStore/JSSE guest Java

本目录拥有 OGPlay 软件 KeyStore 与 JSSE 的 Provider、SPI、条目状态、BKS codec、
只读 AndroidCAStore、路径验证适配、客户端 SSLSocket/HTTPS 和 KDF/PBE 适配。
代码固定以 API 19 `android.jar` 编译并进入 BootDex，由 DexVM 执行；不得依赖宿主 JDK、
BouncyCastle 运行时或 C++ 条目/会话影子状态。

普通状态只保存在 guest Java 对象图；流由调用方拥有，文件只经既有 Java IO/VFS；时间经
`System.currentTimeMillis()` 的统一 VM Clock，随机数经现有 Provider/CSPRNG。密码原语、
路径验证和 TLS 只能经既有 `libogplay_jni.so` 使用 guest libcrypto/libssl。解析先进入临时表，
校验成功后整体发布。

默认 CA 来自 owner 注入的版本化 `OGPLAYCA` 包，路径为 `/system/etc/security/cacerts.ogplay`；
缺失或损坏时 TrustManagerFactory.init(null) 失败，不读取宿主证书库。AndroidCAStore 保持
API19 只读；写/删/store 抛 UnsupportedOperationException。

DVM-172 已完成 KeyStore/BKS 发布条件并注册默认 BKS。DVM-173 增加 OGPlayJSSE：PKIX
TrustManagerFactory、真实 libcrypto 路径验证与只读 AndroidCAStore；SSLSocket/HTTPS
Java 与内存 BIO 已接线，客户端握手尚未闭合。不包含 AndroidKeyStore、通用
CertPathValidator.PKIX、SSLEngine、server TLS 或公开互联网 CA。
