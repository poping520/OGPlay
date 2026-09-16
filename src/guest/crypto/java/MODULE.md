# 模块：KeyStore guest Java

本目录拥有 OGPlay 软件 KeyStore 的 Provider、SPI、条目状态、BKS codec 与 KDF/PBE 适配。
代码固定以 API 19 `android.jar` 编译并进入 BootDex，由 DexVM 执行；不得依赖宿主 JDK、
BouncyCastle 运行时或 C++ 条目影子状态。

普通状态只保存在 guest Java 对象图；流由调用方拥有，文件只经既有 Java IO/VFS；时间经
`System.currentTimeMillis()` 的统一 VM Clock，随机数经现有 Provider/CSPRNG。密码原语只能经
既有 `libogplay_jni.so` 使用 guest libcrypto。解析先进入临时表，校验成功后整体发布。

在 BKS v0/v1/v2、密钥保护、外部双向互操作和持久化主链完成前，不得把 Provider 加入
`Security` 默认列表，也不得设置 `keystore.type=BKS`。

DVM-172 已完成上述发布条件并注册默认 BKS。双解释器定向验证覆盖默认回调取密码、
同 store 双线程、磁盘沙盒跨进程重载及不同应用沙盒同路径读写隔离；格式和算法边界
见 `docs/tasks/dexvm/DVM-172.md`，不包含 PKIX、TLS 或 AndroidKeyStore。
