# API 19 KeyStore 通用能力开发规划

日期：2026-09-16。状态：方向确认，尚未实施。
架构决定：[ADR-0064](../../adr/dexvm.md#adr-0064)。

前置依赖：[DVM-171](../../tasks/dexvm/DVM-171.md) 已将固定 API 19 原版 `NativeCrypto`
迁入 BootDex，并保留统一 guest JNI/资源生命周期边界。该迁移只稳定 Conscrypt ABI，未实现
本规划要求的 KeyStore/BKS、密钥编码、PKIX 或 TLS；下述完整验收要求不变。

## 1. 目标与完成边界

补齐游戏进程可调用的通用 Java KeyStore 能力：通过 AOSP JCA API 管理真实证书和
密钥条目，读取、修改、保存可与 Android 4.4.4 互操作的 BKS 文件；不引入 BouncyCastle
运行时库，密码原语复用现有 guest libcrypto。

游戏调用只是发现缺口的证据，不定义实现范围。禁止按调用栈、包名或某个 SDK 特判，
禁止以 `load(null, null)`、空枚举或构造成功作为整个能力的验收出口。

本专项完成必须同时满足：

- AOSP KeyStore/KeyStoreSpi、Entry、ProtectionParameter、Builder 相关公开语义和异常闭合。
- 证书、私钥及证书链、对称密钥条目可增删查、保护、恢复、保存、跨实例和跨会话重新加载。
- 标准 BKS v2 写出、API 19 可接受的 v0/v1/v2 读取及相关历史密钥保护差异有独立样本验证。
- 错误密码、损坏、资源超限和未支持算法明确失败，不能丢弃条目后报告加载成功。
- 双解释器、外部互操作、VFS 隔离和 native 生命周期定向验证通过，能力账本如实发布。

这里的完成指本文件定义的 BKS 软件 KeyStore 能力，不等于所有 JCA 算法或所有 store 格式。
PKCS12/JKS、UBER/BouncyCastle 加密 store 类型另行规划，不为规避 BKS 而修改默认类型。
系统 AndroidKeyStore、Binder 服务、硬件密钥、TEE 和系统解锁不在范围内。

## 2. 已核对的实现基础

| 层次 | 当前事实 | 本专项处理 |
| --- | --- | --- |
| BootDex | 配方尚无 KeyStore/KeyStoreSpi；已有 JCA 和证书闭包 | 复用原版 API，审计增量依赖 |
| Security 配置 | 只登记 AndroidOpenSSL、DRLCertFactory；未设置 keystore.type | 发布 BKS 时同步配置默认类型 |
| 默认类型 | AOSP Security 属性为 BKS，KeyStore 缺属性时回退 jks | 不仅加入类而漏掉属性 |
| crypto | 已有摘要、HmacSHA1、AES、随机数和部分证书验签 | 补 KDF、3DES PBE 和密钥编码所需边界 |
| 构建 | 当前 BootDex 从固定 JAR 的 DEX 精选并重组 | 新增可重复的自有 Java 源码编译接入 |
| 持久化 | Java 流经 Posix 接入唯一 VFS | KeyStore 只消费流，不自建宿主文件入口 |
| 网络 | SSLContext.init 明确失败；真实 TLS/PKIX 尚未实现 | 独立后续能力，不能据 KeyStore 完成宣称在线可用 |

依据：[CURRENT](../../state/CURRENT.md)、[DVM-169](../../tasks/dexvm/DVM-169.md)、
[DVM-164](../../tasks/dexvm/DVM-164.md)、[BootDex 配方](../../../tools/bootdex/api19.json)、
[provider 配置](../../../src/runtime/dexvm/intrinsics/java_crypto.cpp)、
[SSL 边界](../../../src/runtime/dexvm/intrinsics/java_net.cpp)、
[crypto 契约](../../../src/guest/crypto/MODULE.md)、
[intrinsics 契约](../../../src/runtime/dexvm/intrinsics/MODULE.md)。
模块文档中的历史算法清单与后续 DVM-169 有差异；实施时同步修正，不能据旧文字撤销已有能力。

本地参考：`.local/aosp/libcore/luni/src/main/java/java/security/` 的 KeyStore、KeyStoreSpi、
Security 和 security.properties；`.local/aosp/libcore/crypto/` 的 Conscrypt；固定 core.jar、
conscrypt.jar、ext.jar。实际文件名为 conscrypt.jar。
本地 bouncycastle.jar 仅作 API 19 BKS 行为取证，不进入生产配方或发行依赖。
参考制品哈希、来源、样本生成步骤须在实施基线中固化；不能以现代 JDK 默认格式替代 API 19。

## 3. 架构与所有权

```text
AOSP KeyStore / KeyStoreSpi / JCA（BootDex）
                    |
OGPlayKeyStore Provider → BksKeyStoreSpi（自有 Java，BootDex）
                    |
Java 条目与 BKS codec / 现有 CertificateFactory、Mac、MessageDigest
                    |
必要的私有 guest JNI → 既有 libogplay_jni.so → libcrypto.so

调用方 InputStream/OutputStream → 既有 Java IO / Posix / VFS
```

- 建议 Provider 名 `OGPlayKeyStore`，服务名 `KeyStore.BKS`；类置于 `org.ogplay.security`。
  不把自有 store 注册成原版 AndroidOpenSSL 实现，不冒充完整 BC。
  显式请求 `getInstance("BKS", "BC")` 在 BC 未安装时保留 NoSuchProviderException；
  这是已知 provider 身份边界，不影响默认 provider 查找。
- AOSP API、初始化状态、参数检查与 JCA 分派继续执行原版字节码。
  自有 Java 只拥有 provider/SPI、BKS codec 和必须的适配代码，不重写一套 KeyStore API。
- 条目、alias、日期、证书链、保护后的 key 数据均在 guest Java 对象图中保存；
  C++ 不另建条目侧表。证书沿用现有解析；日期经统一 Clock；盐和 IV 使用现有 CSPRNG。
- native 只提供密码原语和确有必要的编码边界；guest 指针强类型封装，Java 仅持逻辑 token。
  复用既有锁、GC/teardown 和敏感临时缓冲清零机制，不新增私有 crypto SO。
- 新 Java 源码建议放入 `src/guest/crypto/java/`，实施时为新增生产目录补 MODULE.md，
  同步父模块契约；编译编排留在现有 build_bootdex.py，不能把生产算法写进生成脚本。
- 固定 Java 编译器、API 19 编译类路径及 DEX 转换工具和哈希，避免链接宿主 JDK 实现；
  自有 DEX 与原版精选类做重复类检查、闭包审计、制品身份校验和两次构建一致性验证。
  构建工具不是新的运行时依赖；不新增运行时 JVM。

## 4. 语义与格式契约

### 4.1 API 与条目

实施基线须逐项记录 API 19 方法、依赖、参考行为、实现位置和测试入口，覆盖：

- 三种 getInstance、getDefaultType、provider 身份、未知算法和未初始化访问。
- load/store 的流和参数重载；重复 load、load 失败后的可观察状态与流所有权。
- aliases、size、containsAlias、deleteEntry、creationDate、证书/链/alias 查询及条目类型判断。
- TrustedCertificateEntry、PrivateKeyEntry、SecretKeyEntry，setEntry/getEntry/entryInstanceOf。
- 两个 setKeyEntry 重载；密码保护、保护对象销毁、回调取密码及 Builder 的相关语义。
- null 参数、alias 覆盖、证书链缺失/类型不匹配、对象/数组可变性与原版异常类型、cause。

`load(null, password)` 创建空库；空库没有默认信任锚。存在条目时查询返回真实内容。
受保护密钥不能因在同一进程中就绕过口令检查；store 密码与单条 key 密码分别处理。
byte[] key 重载按参考实现单独定义，不自行假定输入一定为明文或 PKCS#8。

并发不额外承诺超出 API 19 的枚举/复合调用原子性，但 SPI 必须避免内部数据竞争和损坏。
通过 guest Java 同步保护条目；测试并发读写、保存快照和失败状态，禁止引入宿主影子锁状态。

### 4.2 BKS 与密码原语

| 内容 | 实施要求 |
| --- | --- |
| 文件结构 | 版本、salt、迭代次数、条目类型、modified UTF alias、日期、证书链、结束标记、MAC |
| store 完整性 | PKCS#12 KDF + HMAC-SHA1，明确版本相关派生长度；不能用 PBKDF2 替换 |
| key 保护 | SHA/3-key TripleDES PBE；参考历史 Broken/Old 变体并用外部样本验证 |
| 密码编码 | null、空 char[]、非 ASCII、UTF-16 边界逐项比对；不能简单转 UTF-8 |
| key 编解码 | RAW secret key、PKCS#8 私钥、X.509 公钥及算法分派；未知算法明确失败 |
| 格式范围 | 标准 BKS 不要求 Twofish；不把 UBER 别名注册为 BKS |

先核对固定 libcrypto 的真实导出和行为，再决定 JNI 绑定；有同名符号不等于语义已兼容。
KDF/3DES 优先使用既有库实现；不得重写密码算法或为 store 内部需要发布未验收的完整 JCA 服务。
RSA、EC 私钥编码/恢复为首批必达；DSA 和其他 API 19 可见 key 算法必须在基线明确支持或延期，
不能静默忽略。恢复密钥不等于已支持签名生成或 TLS 客户端认证。

加载先在临时对象中解析和校验，成功后发布条目，避免损坏输入暴露半成品。
原版 load 失败后保留旧数据还是清空、null/空密码是否跳过完整性校验，必须先取证并在基线
明确选择；若为了安全偏离参考行为，记入兼容差异和 ADR 补充，不能暗中改变。
存在兼容性跳过 MAC 的情况时，不得宣称该次加载已经过完整性认证。

为文件大小、字段长度、条目数、链长度和 KDF 迭代次数设置检查与可查询的上限。
具体阈值在基线阶段固定并说明依据；读取分配前检查负数、溢出和剩余输入。
错误必须区分损坏、错误口令、超限和未支持能力；日志不输出密钥、密码或明文。

### 4.3 持久化与发布

KeyStore.store 写调用方提供的流，不保证任意流上的原子替换，不擅自关闭流；关闭行为以
API 19 取证为准。路径、访问隔离和持久性由既有 VFS 负责。跨会话重新加载必须读取真实文件，
不能依赖 static cache；应用之间不得共享 store 对象或绕过沙盒访问。

内部开发阶段可以分别验收 API、codec 和 key 保护，但不能提前对默认 JCA 发布不完整的 BKS。
完成条目与持久化互操作主链后，在同一发布步骤注册服务、设置 `keystore.type=BKS`，
更新能力项及明确的版本/算法边界。不能把仅内存实现当作已交付的 BKS。

## 5. 开发单元与依赖

下列 KS 编号是本规划内的工作单元，不占用 DVM 全局编号。每行目标可独立验收；
开始实现时在 `docs/tasks/dexvm/` 建立对应 DVM 工作单并反向链接，记录实际状态。
全部初始状态为未开始；单元若超过一次会话，按明确格式/算法拆分，不能删除验收范围。

| 单元 | 一句话目标 | 依赖 | 机器验收出口 |
| --- | --- | --- | --- |
| KS-01 | 固定 API/格式兼容矩阵、资源上限和外部样本基线 | 无 | 样本有来源/哈希/预期；版本、密码及失败状态可重复提取 |
| KS-02 | 接入自有 Java 编译并纳入原版 KeyStore 闭包 | KS-01 | 两次构建一致，闭包可链接，无宿主 JDK/BC 泄漏或重复类 |
| KS-03 | 实现完整条目模型和 SPI API 状态 | KS-02 | 双后端 API/异常/并发测试，内部实例验收而不提前默认发布 |
| KS-04 | 接通 PKCS#12 KDF 和 store 完整性验证原语 | KS-01、KS-02 | 独立派生/MAC 向量，版本差异和密码编码向量通过 |
| KS-05 | 实现证书及受保护数据条目的 BKS codec | KS-03、KS-04 | 外部 v0/v1/v2 读取、v2 双向证书库互读及损坏输入测试 |
| KS-06 | 接通标准及历史 key PBE 保护/恢复 | KS-04 | 3DES/PBE 独立向量、错 key 密码、历史兼容样本通过 |
| KS-07 | 完成 RAW/RSA/EC key 编解码及 Entry 主链 | KS-03、KS-06 | 外部密钥样本恢复、证书链匹配、未知算法及两种 key 重载受检 |
| KS-08 | 验收真实持久化、隔离及资源生命周期 | KS-05、KS-07 | 外部完整库双向互读、VFS 跨会话加载、GC/teardown/失败清理通过 |
| KS-09 | 正式发布默认 BKS 服务与能力记录 | KS-08 | JCA 默认/显式 provider 路径通过，能力查询与受检矩阵一致 |

Builder/回调/ProtectionParameter 的 Java 闭包与行为分别纳入 KS-02/03，不能漏到验收之后。
KS-07 中 RSA 与 EC 若需要独立的 native 工作，拆为两个有依赖的工作单；不得用编码占位对象
宣称数学接口或实际密码操作已完成。

## 6. 验证与交付要求

- 核心测试与任何游戏无关：空库、混合条目、覆盖/删除、密码、异常、重复初始化和格式版本。
- 外部 oracle 使用固定 API 19 环境生成/读取；BC 只允许在隔离的测试工具或参考环境中运行，
  不进入生产构建、运行时 classpath 或发行物。无法运行 oracle 时，保留未验收状态。
- 两个方向都验证：Android 写 → OGPlay 读；OGPlay 写 → Android 读；比较 alias、条目类型、
  日期、证书 DER、链、密钥编码及口令行为。随机盐不同，不能要求文件字节完全一致。
- 负例覆盖错 store/key 密码、MAC 篡改、负长度、截断、未知类型/版本、超大迭代次数、
  输入/输出流中途异常及不支持算法。不能只做自写自读测试。
- 生命周期覆盖两个 guest 线程、多实例隔离、GC 后恢复、反复 load/store、失败资源回收、
  session teardown；私钥材料不得出现在日志或诊断快照。
- 仅构建受影响目标，Windows 使用 `windows-msvc` 预设；只运行新增用例与直接相关 crypto/
  BootDex/架构定向检查，不运行全量测试。每个实施工作单保存精确命令和结果。
- 如追加实际游戏验证，按 [playbook](../../playbook/README.md) 执行；它只作为集成证据，
  不取代通用互操作测试，也不以绕过异常或回退默认 HttpClient 作为成功。
- 每单元同步相关 MODULE.md、CURRENT 和能力清单；新增能力不能靠提高旧 AES/HMAC 状态表达。
  建议独立记录 KeyStore API、BKS 持久化、key 保护/算法集合，未完成部分仍可查询并明确失败。

## 7. TLS 与后续格式的衔接

KeyStore 提供证书/密钥容器，不拥有网络权限，也不负责信任策略。
Apache `SSLSocketFactory(KeyStore)` 仍会初始化 TrustManagerFactory 与 SSLContext；
完成本专项不能承诺该 HTTPS 路径已可用。

后续另立专项，依次交付 SSLContext/Factory 原版 Java 状态、TrustManagerFactory 与 PKIX、
可追溯 CA 来源及 AndroidCAStore 只读视图、policy-gated TLS transport 和回调。
自定义 TrustManager 必须执行真实 guest 回调；主机名校验、证书链验证和网络授权分开验证。
空 truststore 不能自动获得系统 CA，缺少 PKIX 时不得默认信任。已有证书 verify 不替代路径验证。

TLS 后端版本和维护策略须单独评审；不能因为当前离线 crypto 复用 API 19 libcrypto，
就默认把同年代 TLS 栈直接用于长期在线连接。新增 PKCS12 时复用条目与密码边界，但使用
独立格式 SPI、算法矩阵和外部互操作测试，不修改 BKS 默认语义来绕过缺口。

## 8. 本次规划交付状态

仅建立规划和架构决定，未改变运行时能力；`capabilities.toml` 保持现状。
实施完成后以工作单证据和滚动快照为准，不能把本规划中的目标表当作已实现能力。
