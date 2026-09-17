# API 19 信任验证与 TLS 开发规划

日期：2026-09-17。状态：TLS-01/02 已验收；TLS-03 未完成，公开 CA / OpenSSL 1.0.1 维护仍阻塞在线发布。
任务：[DVM-173](../../tasks/dexvm/DVM-173.md)。决策：[ADR-0066](../../adr/dexvm.md#adr-0066)。

## 1. 目标与兼容边界

交付应用可直接使用的 TrustManagerFactory、默认 CA、SSLContext、SSLSocket 和 HTTPS，
公开 API 与约定的可观察行为对齐 Android 4.4.4；内部使用 OGPlay 自有 Provider、
进程内服务和现有 VFS/网络边界，不复制 Android 系统服务。

“最小化”约束实现范围、依赖和状态数量，不能省略证书验证、回调、错误或生命周期语义。
不引入生产 BouncyCastle、宿主 JVM、Binder/system_server、keystore daemon 或硬件密钥服务。

AndroidCAStore、AndroidKeyStore 是应用可通过 KeyStore.getInstance 访问的类型，并非完全
不可见。可替换的是磁盘布局、服务协议和 native 实现；类型、条目操作、异常、应用隔离、
密钥可导出性等属于应用契约。Provider 名称也可被显式请求，不能随意冒充已有 Provider。

| 层次 | 保持的契约 | 可自定义的内部实现 |
| --- | --- | --- |
| javax.net.ssl / java.security | 原版类形状、重载、异常、初始化及委托规则 | SPI 实现与内部类布局 |
| TrustManagerFactory | PKIX 默认算法、X509 别名、显式/默认信任库、真实回调 | 信任锚索引、缓存和验证适配 |
| AndroidCAStore | API19 只读证书视图、alias 查询、日期及非法操作行为 | CA 包、索引格式、版本管理，无系统目录服务 |
| AndroidKeyStore | 若发布则兑现软件密钥条目、隔离和不导出等约定 | 应用沙盒中的软件密钥服务，无 Binder/TEE |
| TLS | 实际协议、协商、peer/session、错误、关闭与超时 | 自有 Java Provider、BIO 驱动和逻辑 token |
| 网络 | 唯一 policy/transport、默认关闭、取消与会话隔离 | 宿主传输适配，不承担第二套 TLS |

API19 签名兼容不代表复制过时密码默认值。协议/算法支持集和安全限制独立记录；
被策略禁止的旧协议必须明确失败，不能暗中降级或发布不能协商的套件。

## 2. 已核实现状

- 基线提交 b71e9b10：同一 AOSP android-4.4.4_r2.0.1 构建的 libcrypto.so/libssl.so
  已进入 payload；libssl SHA-256 为 8b1a7d20e405ff73edcaad592cf846f8e28b78bb446874208d65990b16d79734。
- DVM-172 已完成 BKS、AES/RSA/EC 密钥恢复及持久化；DVM-106 已完成证书解析和验签。
- BootDex 为 1572 类。javax.net.ssl 普通状态仍有 C++ 类壳；SSLContext.init 明确失败。
- AOSP TrustManagerFactoryImpl 的 init(null) 加载 AndroidCAStore；TrustManagerImpl 依赖
  CertPathValidator.PKIX、证书索引和 pinning。直接选入这些类不会自动得到真实信任能力。
- NetworkTransport 当前 Connect(host, port, bool tls) 与 Send/Receive 接口尚不能充分表达
  分层 socket、部分写、EOF/暂不可读、deadline 和取消；TLS 接入前须收敛该契约。
- 原版 NativeCrypto 类身份已迁入 BootDex；本专项不恢复 NativeCryptoBoundary 类壳。

## 3. 目标架构与所有权

```text
应用 / Apache HTTP / HttpsURLConnection
                  ↓
AOSP javax.net.ssl 公开 API（BootDex，唯一类定义）
                  ↓
OGPlayJSSE Provider（guest Java）
  TrustManagerFactory / KeyManagerFactory / SSLContext SPI
  X509TrustManager / SSLSocket / SSLSession / SessionContext
       ↓                         ↓
只读 CA 快照 / 调用方 KeyStore   NativeTls / NativeTrust（私有 JNI）
       ↓                         ↓
现有 VFS                  guest libssl + libcrypto
                                 ↕ 密文 BIO
                 NetworkRuntime → 注入的原始字节 transport
```

- guest Java 拥有 API 状态、manager、listener、证书对象、参数与 session 元数据。
- native 只拥有 SSL_CTX/SSL/BIO/X509_STORE/EVP 等必需资源；强类型逻辑 token，不能暴露指针。
- JNI 继续放在唯一 libogplay_jni.so 内，源码可按 tls/trust 拆分，不新增平行 JNI 运行库。
- OpenSSL 注册/锁/CSPRNG 初始化复用现有路径。新增资源沿用引用计数、GC 和 teardown。
- 自有 SPI 使用私有 org.ogplay.security 边界；原版 NativeCrypto ABI 保留，不能添加私有方法。
  原版 Conscrypt 若有可独立复用的纯 Java 辅助类，须先审核闭包，不为复用而引入系统服务。
- 不同时维护宿主 TLS 和 guest TLS。既有 tls 标记只能代表请求分类/授权，不能令 transport
  再加密一次；改为显式 raw-channel 与 TLS 会话关联，保留所有调用方的策略校验。

## 4. 信任验证

### 4.1 Factory 和 manager

原版 TrustManagerFactory/Spi、TrustManager、X509TrustManager、ManagerFactoryParameters 等
精确闭包进入 BootDex，删除重复类壳。OGPlayJSSE 发布 PKIX 及 API19 X509 别名，配置
ssl.TrustManagerFactory.algorithm；默认查找、显式 Provider 对象和未知 Provider 异常受检。
不注册 AndroidOpenSSL/AndroidKeyStore 等未兑现的完整 Provider 身份。

init(non-null KeyStore) 只使用该库的信任材料；空库允许初始化，但不能信任任意远端。
init(null) 获取已配置的默认 CA 快照；默认库缺失/损坏必须失败，不自动读取宿主证书库。
getTrustManagers 在未初始化时失败；getAcceptedIssuers 返回真实快照且不得泄露可修改内部数组。
按本地 API19 取证信任条目选择规则（包括私钥条目的证书）、重复 init、密码和异常边界。
ManagerFactoryParameters 按 API19 provider 不支持路径明确抛异常，不能无视参数成功。

checkServerTrusted/checkClientTrusted 调用统一验证器，检查 chain/authType、用途和资源上限；
authType 必须与证书/协商认证一致。客户端与服务端用途分别配置。
不将 Java 自定义 TrustManager 替换成系统默认：握手必须调用实际传入对象，传播其拒绝。
自定义 manager 的接受决定也不能跳过 TLS 对私钥持有的协议证明、网络策略或独立主机名校验。

### 4.2 验证器

使用 libcrypto 的路径构建/验证能力，guest Java 映射结果与异常。至少覆盖：信任锚选择、
链签名、有效期、BasicConstraints、pathLen、KeyUsage/EKU、NameConstraints、未知 critical
extension、弱算法限制、乱序/重复/交叉签名链。不能以逐证书 verify 代替路径验证。

验证时间显式来自统一 Clock；不得让 OpenSSL 默读宿主墙钟。证书链和信任库按版本快照传入，
区分“不受信任”和“格式错误/不支持算法/资源不足”。错误带阶段、证书索引和 native reason。
缓存仅保存已验证材料，按应用、信任版本、验证用途与策略隔离；更新后失效。

第一阶段不发布通用 CertPathValidator.PKIX 服务，只交付 TrustManager 所需验证契约。
将来若发布通用 PKIX，必须另验 PKIXParameters、checker、policy、结果和失败索引，不能把
内部 native 验证接口冒充完整 JCA 服务。首批无联网 AIA/OCSP/CRL 获取；默认策略明确记录
撤销未检查，显式要求的撤销能力不支持时失败，不能报告已检查。

## 5. 默认 CA：自有存储，API19 只读视图

默认信任源采用发行时固定版本的 CA 包（DER + 索引 + 来源/许可证/哈希），由 session owner
注入只读 VFS。CA 集合更新独立于程序升级；加载验证总大小、条目数、格式及重复身份。
测试仅使用测试 CA；发行根证书来源和更新责任在发布前固定，不能把 API19 历史根包视为永久集合。

自有 AndroidCAStore SPI 暴露 aliases、containsAlias、getCertificate、getCertificateAlias、
getCreationDate、size、证书条目判断及 load 行为。对照原版确定 alias system:/user: 格式和
碰撞处理；内部存储不需要复刻 /system/etc/security/cacerts。日期来自稳定包元数据。
API19 TrustedCertificateKeyStoreSpi 的写证书/删条目/写密钥均抛 UnsupportedOperationException，
遵循该只读契约，不提供虚假写成功。

若需要用户 CA，由显式配置导入应用隔离的附加信任源，合成只读视图；普通 app 无权修改
全局发行根。发布新快照时已建 manager/session 使用旧快照还是强制失效须固定：本规划采用
新连接取新版本，吊销/紧急策略更新主动失效旧 session cache。运行中的连接处置由 owner 策略决定。

## 6. TLS 与网络

### 6.1 Java 面

迁入 SSLContext/Spi、SSLParameters、SSLSocket/Factory、SSLSession/Context、listener/event、
KeyManager/X509KeyManager/Factory/Spi 等所需公开闭包；保留 AOSP 默认对象和 setter/getter
语义。逐类替换 java_net.cpp 临时类壳，BootDex 精确类集与 payload validator 同步。

自有 SSLContextSpi 首批支持客户端 TLS；发布前固定 TLS/TLSv1/TLSv1.1/TLSv1.2 的别名、
可用协议与默认启用集合。默认仅启用受审协议/套件，旧协议须显式兼容策略；不发布 SSLv3。
SSLEngine、SSLServerSocket/server mode、DTLS、ALPN/现代扩展为独立后续能力，未支持入口明确失败。
这意味着首批 complete 只指客户端 socket TLS，不宣称完整 JSSE。

KeyManagerFactory 从显式 BKS 恢复 RSA/EC 私钥和链，执行真实客户端证书 alias 选择及回调；
无需 AndroidKeyStore 即可完成双向 TLS。自定义非导出 PrivateKey 若当前后端不能使用，明确失败。

### 6.2 BIO 与 transport

用内存 BIO 分离加密和网络：guest SSL 产生密文，经唯一 NetworkRuntime 原始通道发送；
收到密文再喂入 SSL。不把宿主 FD 传进 guest SSL，不让 SSL 自行解析 DNS/连接网络。

先扩充 transport 明确进展字节数、EOF、暂不可读写、错误、deadline、取消及 channel 所有权。
SSL WANT_READ/WANT_WRITE 进入可取消等待，允许读操作需要写、写操作需要读；禁止忙循环。
设置握手预算、单连接密文/明文缓冲上限、证书总量与链深度上限，TLS-01 固定具体值。

分层 createSocket(existing, host, port, autoClose) 复用已有连接，保留原 host 用于 SNI/校验；
不能重连或绕过 allowed_hosts/allow_tls。autoClose=false 不关闭调用方底层连接，但 TLS wrapper
进入关闭态；shutdown/close_notify、EOF 截断、读写中 close 的行为必须可测试。
等待释放 VM 执行锁并遵循现有线程规则，取消唤醒所有等待；不能在 native registry 锁内回调 Java。

### 6.3 握手与验证顺序

状态至少包含 NEW、HANDSHAKING、VERIFYING、OPEN、CLOSING、CLOSED、FAILED。
在合适的 libssl 验证回调阶段执行真实 Java TrustManager；采用显式 JNI 回调桥，固定线程、
RootScope/JNI 引用、异常保存和锁释放规则。若用暂停/恢复方案，须先证明当前库支持，不能
默认存在新版 OpenSSL 异步验证 API。验证拒绝则使握手失败并清理，应用数据不得提前发布。

SNI、链验证和主机名校验是三件事。HttpsURLConnection/Apache 保留原版 HostnameVerifier；
原始 SSLSocket 按 API19/SSLParameters 的 endpoint identification 约定执行，不能一律偷偷
增加或移除主机名检查。测试 DNS SAN、IP SAN、通配符、CN fallback 的 API19 边界。
HandshakeCompletedListener 只在真正成功后触发；peer certificates、cipher、protocol、session
必须来自真实结果，不得返回占位值。

Session cache 以应用、目标 host/port、SNI、manager/context、CA/策略版本隔离；支持 invalidate、
timeout 和大小限制。复用不得绕过更新后的信任策略；具体回调次数与恢复行为做 API19 对照。

## 7. AndroidKeyStore 的位置

AndroidKeyStore 不是 TrustManagerFactory、默认 CA 或客户端 TLS 的前置条件。本轮不为了
“系统完整”实现它；保持独立后续工作，不将 BKS 冒充 AndroidKeyStore。

应用确实调用时，使用相同公开 API + 应用私有软件密钥底座：alias 元数据、事务写入、
重启恢复、删除与应用数据清理；private key 对象通过 opaque token 引用，getEncoded 按
该类型约定不可导出，签名通过统一密码后端。算法生成入口按 API19 KeyPairGeneratorSpec
审计，不套用后世 KeyGenParameterSpec。
软件方案不宣称 TEE/硬件保护或设备认证。磁盘密钥保护依赖 owner 注入的保护材料及既有
沙盒权限，禁止内置固定主密钥。需要设备解锁/用户认证而无法兑现的请求必须失败。
这是单独范围和验收，不计入本轮 TLS 完成条件。

## 8. 后端版本与部署

当前同源 libssl/libcrypto 可用于 API19 ABI 和本地互操作开发，不因文件已入库就开放公网。
长期在线发布前须完成该源码修订的漏洞/补丁审计、可复现构建和维护责任确定；若不能维护，
迁移到受维护后端。迁移保持自有 JNI 稳定，不把 API19 Java 版本绑定到旧密码实现。
更换库必须同步评估 guest 原生应用对旧 libssl/libcrypto 的 ABI、导出和结构依赖，不能直接
覆盖 SONAME；优先维持唯一密码后端，若兼容要求必须隔离双版本，另立 ADR，禁止意外符号混用。

协议策略、根证书更新和 backend 修订各有版本，诊断可以查询。完整性清单继续记录源码、
工具链、导出、DT_NEEDED、许可证和 SHA-256；JNI 链接 libssl 后要校验加载依赖和初始化顺序。

## 9. 开发单元与发布顺序

按三个开发单元连续推进，每个单元交付可独立验收的完整能力。API 取证、Java/native
接入、测试和文档均作为单元内检查项，不再单独编号或拆成额外开发任务。

| 单元 | 一句话目标 | 依赖 | 实施范围与验收出口 |
| --- | --- | --- | --- |
| TLS-01 | 完成交给应用使用的真实证书信任服务 | DVM-172、b71e9b10 | 固定 API19/Provider/异常、密码策略和资源上限；迁入 TrustManager 公开闭包；实现路径验证、显式 KeyStore/default init、CA 包及只读 AndroidCAStore。验收正反证书矩阵、空库与默认库、只读异常、时间、隔离和资源释放。 |
| TLS-02 | 完成可真实通信的客户端 TLS/HTTPS | TLS-01 | 一并迁移 SSL API/Context/Session，收敛 raw transport，接入 BIO/token、信任回调、SNI、SSLSocket、session cache、BKS KeyManager/mTLS 和两套 HTTPS 调用链。验收本地独立服务器、主机名、RSA/EC mTLS、部分 I/O、超时取消、autoClose、双线程、GC/Stop 和失败清理。 |
| TLS-03 | 完成受控在线发布与最终验收 | TLS-02 | 从 TLS-01 开始取证后端维护与 CA 来源，本单元闭合补丁/版本、可复现产物、声明平台和双解释器证据；按 playbook 验收受控网络场景，更新能力、模块契约和 CURRENT。无法闭合的发布条件保持明确阻塞，不能据本地握手成功发布在线能力。 |

TLS-02 内按“状态和通道 → 握手和验证 → 客户端认证和 HTTPS”实施并做定向检查，
中间进度仍记在同一个开发单元中，不将只可链接的 Provider 作为完成出口。
三个单元保留全部既定范围和第 10 节验收矩阵；网络始终默认关闭，TLS-03 完成后才允许
按显式策略使用受控在线能力。单游戏越过异常不构成专项验收。

## 10. 验证与能力记账

- Java/API：默认与显式 provider、错误参数、未初始化、重复 init、空信任库、null 默认库、
  自定义 manager/hostname verifier、异常 cause、数组快照、原版 API19 行为对照。
- 信任：可信/不可信、过期/未生效、非 CA、pathLen、用途错误、NameConstraints、关键扩展、
  乱序/缺链/交叉签名、受信任叶证书、同主题不同 key、弱算法、超大链/损坏 DER。
- TLS：本地独立 server oracle，握手/应用数据/mTLS/SNI、协商失败、TCP 分片、短写、截断、
  close_notify、超时取消、session 恢复、验证失败前无应用数据外发。
- 生命周期：两个 guest 线程、读写并发 close、Stop 中断、GC、两应用/两 session 隔离；
  callback 不能被跳过，失败不能残留 native token、线程、channel 或 pending exception。
- 运行：只构建受影响目标、只跑相关定向测试；真实游戏结论遵循既有 playbook/scenario。
  独立测试服务器/参考库只作为测试依赖，不加入生产发行包。
- 日志：统一结构化事件含连接逻辑 ID、阶段、协议、suite、验证原因和 CA/策略版本；
  不输出私钥、密码、明文应用数据或宿主指针。

实施时分别登记 trust_manager_factory、x509_trust_validation、android_ca_store、tls_client、
tls_client_auth、https_transport 的能力状态；按可工作的阶段新增，不因本规划调整现有能力。
完整通用 PKIX、AndroidKeyStore、server TLS、SSLEngine 单独记账，不能挂靠本轮 complete。

## 11. 参考依据

- 本地 AOSP：libcore/luni 的 javax/net/ssl；crypto 的 TrustManagerFactoryImpl、
  TrustManagerImpl、TrustedCertificateKeyStoreSpi；framework/base/keystore 的 AndroidKeyStore。
- [NetworkRuntime 契约](../../../include/ogplay/runtime/dexvm/network_runtime.h)。
- [现有 crypto 模块](../../../src/guest/crypto/MODULE.md)、[DVM-164](../../tasks/dexvm/DVM-164.md)、
  [KeyStore 规划](13-keystore.md)。
- [OpenSSL SSL_set_bio](https://docs.openssl.org/1.0.2/man3/SSL_set_bio/)：BIO 分离网络和 SSL，
  具体 ABI 仍以当前固定源码为准，不假定 1.0.2 或新版方法存在。
- [OpenSSL 1.0.1 维护状态](https://www.openssl-library.org/news/vulnerabilities-1.0.1/)：旧系列已停止维护；
  当前 AOSP 修订的补丁覆盖必须单独审计。
