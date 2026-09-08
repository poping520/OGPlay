# 当前状态

更新（2026-09-08）：[DVM-114](../tasks/dexvm/DVM-114.md) 补齐 Throwable 消息虚分派；
DVM-112/113 已提交 `c7859ed5`/`f78c893d`。

## 当前能力

- **运行与发行**：按 exact Profile API 选择 bundled data；API 19 内置 pinned AOSP
  五库、845 类 BootDex 与 ICU4C 51.1。ROM libcrypto 已按用户再次授权恢复到本地临时使用，
  未纳入 Git，哈希与清单一致。JNI 桥源码为 src/guest/crypto/crypto_jni.c；构建器仍不自动恢复设备库。
  自行构建替换后须更新来源/哈希并复验。制品身份见
  [payload manifest](../../data/android/19/manifest.json)，bootdex.jar 继续不提交。
- **Cipher**：AES 128/192/256；ECB/CBC NoPadding/PKCS5Padding、CTR/NoPadding，
  裸 AES 默认 ECB/PKCS5Padding。Java 算法归 BootDex，11 个 native 进入 guest OpenSSL；
  分段/原位/异常、IV reset、OS 随机源、真实线程、GC/teardown 受检。
- **UUID/摘要**：UUID、MessageDigest/Spi、DigestInputStream/DigestOutputStream 与 Conscrypt
  MD5/SHA-1/SHA-256/SHA-384/SHA-512 执行 BootDex；7 个 JNI 入口调用 guest EVP。
  分块 scratch 最多 64 KiB，无消息累计上限；别名/OID、增量/reset/clone/ByteBuffer/摘要流受检。
  GC/teardown 聚合共享 token，失败可重试。UUID v3 用 MD5、
  v4 用 OS CSPRNG；旧固定 JNI UUID 已删除；原版 readObject 已恢复 UUID transient 缓存。
- **Certificate**：Certificate/X509Certificate/CertificateFactory、Harmony ASN.1/X.509 与
  PkiPath/PKCS7 执行 Java；10 个 RSA/ECDSA 摘要验签组合经 ARM EVP，单消息最多 1 MiB。
  解析、属性、公钥编码、链签名、异常与 GC 受检。不含 PKIX 信任、系统 CA/撤销、TLS 或签名生成。
- **集合与流**：List/Collection/Map 家族、普通 atomic/AQS、工具/事件/beans、内存/包装
  IO、Reader/Writer、X500 和 key spec 来自 API 19 DEX；普通字段/数组为唯一状态。
  JNI 普通类/接口与嵌套数组使用VM 类型关系。ObjectInputStream/ObjectOutputStream、描述符与辅助类
  执行原版 Java，删除 C++ 协议及 handle 副本；私有读写/替换回调、GetField/PutField、默认 UID、
  循环引用、八种基本数组/对象数组和 Externalizable 协议 1/2 受检。六个 native 只处理构造
  token/元数据；SoftReference 普通 GC 保留、压力下清除入队，String.intern 保持弱 canonical 身份。
  静态初始化非 Error 包装 EIIE/cause，Error 保持身份，后续访问 NCDFE；反射 wide get 已修正。
  Throwable 本地化消息及 toString 保留子类虚分派与原异常身份。
- **日期与值边界**：Format/DateFormat/SimpleDateFormat、NumberFormat/DecimalFormat、
  Date/Calendar/TimeZone 执行 BootDex。标准六字符集
  与 Locale 大小写、ISO 语言/国家代码枚举复用 ICU。BigInt/NativeBN 扩展至 17 个值原语，支持长整数编码转换，
  单次输入最多 1 MiB；其余 18 个 native 明确失败。
- **执行器/VM**：ScheduledThreadPoolExecutor/FutureTask、接口/异常与普通工厂包装类
  执行 BootDex；删除旧 FutureTask/串行 executor handler。单次/周期/FIFO、取消/中断、超时/
  异常、关闭策略、两个 worker 与队列 GC 受检；invokeAny/invokeAll 已接通。任务只存 Java 字段，
  复用 AQS/Unsafe/统一 Clock；一个 guest 线程对应一个宿主线程，解释执行由 VmExecutionLock
  串行，threaded 默认关闭。接口数组协变和 threaded 原异常身份已修正。
  文件/VFS、资源 XML、Locale、URL/form codec、Intent/Context 与平台 enum 保持。
- **framework 值类**：Pair、五种 SparseArray、ComponentName/CREATOR、Parcelable 接口、
  ContainerHelpers/ArrayUtils 和 PrintWriter 执行 Java；旧 Sparse 侧表和迁移类声明已删除。
  ArrayUtils 从固定原始 Java 源用本地 AOSP dx 编译，其余来自 pinned jar；来源单独记账。
- **Activity 身份**：getLocalClassName/getComponentName/getPreferences 与每实例 Intent
  已接通，保留组件身份；setIntent 不改组件身份。
  UTF-16 append 依赖已补。一般组件解析和非根 alias 切换仍未扩展。
- **服务查询**：ServiceConnection 来自 BootDex。resolveService 的 action-only/flags=0
  查询依据当前 APK 服务信息，无候选返回 null，未知或潜在匹配明确失败；无 Binder/支付。
- **Title**：PvZ 已越过本地化消息查询，当前首错 Throwable.printStackTrace(PrintWriter)；
  尚未通过游戏 gate；Tales 首错 LocationListener。

## 最近验证

- DVM-114：消息/继承 9 用例、P1 18 用例、近期 7 用例回归及 4 项门禁通过。
  证据 `.local/review/dvm114/`；历史验收见任务单。

- MoKee API 19 ARMv7 临时文件（哈希已核对）：
  `.local/android-device/20260906-cipher/`。手机已断开；发行前
  须自行构建替换临时制品。macOS Release 使用 WARNINGS_AS_ERRORS=OFF，
  仅构建 ogplay_tests 及 ogplay 依赖；未跑全量测试或 Windows/Linux 验收。
- 已知门禁遗留：architecture.platform_boundaries 在既有 GUI process_manager.cpp:131
  平台分支失败，本轮未修改、未重跑该门禁。ADR 继续按 6 个主题维护，追加
  [0042](../adr/dexvm.md#adr-0042)，不新增独立 ADR 文件。

## 下一步与边界

1. 处理 Throwable.printStackTrace(PrintWriter) 的真实栈/原因链输出。
2. 自建 ARM libcrypto 替换临时制品；Windows/Linux、既有 GUI 门禁、DH 与 Diagnostics 验收。

OGPlay 是老游戏兼容层；complete 只覆盖登记范围。具名时区/历史 DST、完整大数与
formatter、宿主侧表对象完整持久化/对象流长尾、Proxy 生成、privileged 执行器工厂/安全上下文、高争用集合、RSA Cipher、HMAC/Mac、SHA-3、完整 JCA/TLS/系统服务仍未交付。
长期限制见 [KNOWN-ISSUES](KNOWN-ISSUES.md)。

索引：[DexVM](../tasks/dexvm/README.md) · [APK Startup](../tasks/apk-startup/README.md) ·
[Layout UI](../tasks/layoutui/README.md) · [Playbook](../playbook/README.md)
