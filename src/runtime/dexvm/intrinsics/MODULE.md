# 模块：runtime/dexvm/intrinsics

## 职责与装配

发布 Java core 的 VM/native 边界；纯 Java 算法执行 pinned API 19 BootDex。
依赖、对象/GC 与线程总契约见 [DexVM](../MODULE.md)，平台装配见
[integration](../../integration/MODULE.md)，能力范围见 [capabilities](../../../../capabilities.toml)。

- 每个 Java class 只有一个正式命名空间内的 `Declare_*()`，声明与 handler 同址。
  物理文件固定为 catalog 加 lang/classloading/reflect/io/util/icu/regex/zip/nio/net/xml/
  concurrent/crypto 13 个 family TU；family 只向 catalog.h 暴露 Append*，catalog 不含行为。
- 禁止迁移转发层、misc/common/all TU、字符串 handler id、静态自注册和 android.* 行为。
  flags 只用 access_flags.h 的 kAcc*；反射 modifier mask 使用对应命名常量。
- java.*、javax.net/xml 与 org.xml.sax 归 core；平台事实只经 CoreIntrinsicServices 注入，
  不读取 DexVmAndroidContext 或宿主 locale、环境、网络。XmlPullParser 仅声明接口。
- 普通 Java 状态只在 guest 字段/数组；资源交给 per-VM runtime，不重建算法或影子侧表。
  跨 nested guest call 的新引用必须用 RootScope；宿主 VmObjectRef 容器不是 GC 根。
  未支持的方法记账并明确失败，不能用默认值伪造能力。

## lang、线程与退出

- String 的 UTF-16 由 VM/JNI 唯一拥有；内部 char[] 构造复制不可变快照，_getChars 只复制区间。
  三个 builder 与 IntegralToString 普通方法归 BootDex，value/count/shared 是唯一可变事实源，
  无 builder map、clone/sweep 或普通方法 overlay。RealToString 仅保留受检 native digit generator；
  scratch 为有界局部值，digits/digitCount/firstK 用绑定字段。Character 现有 wrapper 边界补齐
  五个 UTF-16 array 原语，无 builder 状态；详见 DVM-151 / ADR-0057。
- Math 普通方法/常量/random 归 BootDex，仅保留 24 个 libm native；不宣称 fdlibm 逐位一致。
  Throwable/StackTraceElement/异常家族归 BootDex，仅保留两个栈 native；消息虚派、原异常
  身份、cause/suppressed 与输出格式归 Java。PrintStream 只接结构化输出，空追加不输出。
- Thread 声明字段和 Java 校验；start 虚派 this.run，基类 run 才虚派 target。
  线程、identity、sleep/join/park/interrupt 只用 VmThreadRuntime/monitor；纳秒向上取整到
  统一毫秒 Clock。priority/daemon 仅为 guest fact，不映射宿主调度或自动退出。
- Thread 字段用预绑定 handle/IntrinsicCall，禁止裸槽和逐调用 descriptor 查找。
  contextClassLoader 受 GC 追踪：root 为 application loader，子线程继承，setter 允许 null。
  实例 uncaught handler 优先于默认 handler；handler 自身异常按 API19 忽略，无 handler
  保留进程致命诊断。ThreadGroup 仅 bounded system/main、名称和存活枚举，结束后 group 为 null。
  线程栈来自本 VM safe-point snapshot，不伪造完整 ThreadGroup/State。
- Runtime 的单例、hook List、shuttingDown、add/remove/exit/halt 执行原版 Java；System.exit
  委托 Runtime.exit，nativeExit 进入 Interpreter.Exit，不能返回 guest 或直接退出宿主。
  宿主 Stop 保持取消语义；availableProcessors 保留单执行通道事实 1。
  Runtime gc/内存统计/nativeLoad/runFinalization 尚未支持；runFinalizersOnExit(true) 立即
  记账抛 UnsupportedOperationException，false 保持未启用。见 [DVM-150](../../../../docs/tasks/dexvm/DVM-150.md)。
- System 属性共用 per-VM 表，默认只发布三个 separator，参数按 Java 校验；getenv 只读取
  注入的 guest Bionic environ，无参版本返回原版不可修改快照。getSecurityManager 固定 null，
  不安装宿主权限系统；nanoTime 只用统一 Clock。Class.desiredAssertionStatus 默认 false。
- 平台 enum 用 IntrinsicEnumBuilder 在链接前生成字段、$VALUES、clinit、values/valueOf；
  BootDex enum 复用 Enum.getSharedConstants，禁止 EnumSet 的 C++ 副本。顶层接口只声明
  已登记 shape，Thread.UncaughtExceptionHandler 归 Thread family。

## 类加载、反射、集合与并发

- ClassLoader/BootClassLoader/PathClassLoader 身份、parent、lookup/initiate 只用
  ClassLoaderFacade；自定义 loader 仍映射唯一 application namespace，动态 classpath 拒绝。
  Class.forName 使用真实 caller；三参 null 映射 API19 system loader。CNFE 保留 cause，
  clinit EIIE 保留原 throwable。Class 相对/绝对资源名与 ClassLoader 根资源名经注入的
  bootstrap/APK sealed archive 查询并返回真实 ByteArrayInputStream；缺失返回 null。
  完整约束见 [DexVM](../MODULE.md)。
- Class/Method/Constructor/Field/reflect.Array 只委托 linker、ReflectionRuntime、ReflectionCodec
  和 typed array store；不读写 raw member id。public 聚合按 class→superclass→direct interface；
  nested/enclosing/Throws 只读 Dalvik metadata，禁止名称拆分猜测。Class.getEnumConstants 复用
  `isEnum` 与 `SharedEnumConstants`，返回具体枚举数组类型的浅克隆。Class/Field 运行时注解查询
  委托 `AnnotationRuntime`；Method.getDefaultValue 读取注解声明默认值。禁止 generic 与
  通用 annotation proxy。invoke/实例化/字段读写保留类型转换和原异常引用；Modifier 与对象流普通协议归 BootDex。
- 集合、迭代器、Arrays/Collections、Random、ThreadLocal、普通 atomic/同步器和
  FutureTask/ThreadPoolExecutor/ScheduledThreadPoolExecutor/普通 Executors 都执行 BootDex。
  队列、任务、等待者只存 Java 字段；不恢复宿主集合、任务侧表或独立 executor。
- Unsafe 单例与 caller loader 受检；读写/CAS/数组位置委托 UnsafeRuntime，park/unpark
  复用 Thread，absolute epoch 经注入时间转单调 deadline。无 Clock/溢出明确失败；
  allocateInstance 先 clinit 再跳过构造器。并发 family 只保留 Unsafe 与 AtomicLong CS8。
  Timer/TimerTask 的 deadline、队列、Clock、取消和生命周期交注入 scheduler；不反向读 context。

## IO、NIO 与网络

- 普通 stream/reader/writer、File/FIS/FOS/RAF/FileChannelImpl、内存/过滤/缓冲流与对象流归
  BootDex；资源归 IoRuntime，文件调用经 Posix native 子集进入 VFS。
  FileDescriptor 是逻辑身份；FIS/FOS 通过共享 OpenFileDescription 直连 VFS descriptor，
  同一 FD 的流共享 VFS offset，append 每次写前定位末尾，借用/拥有关闭语义一致，不保存宿主句柄。
  FileChannel 当前受检的 size/position/transferTo/close 复用同一状态；transferTo 以有界缓冲
  复制并恢复源 offset，不用虚假 MappedByteBuffer 冒充文件 mmap。
  RandomAccessFile 的路径/模式、基础字节读写、seek/getFilePointer、length/setLength、FD/channel
  也复用该状态；VFS errno 通过 IoRuntimeError 保真，Java 边界再翻译为对应 IOException。
  File 只消费注入 VFS/工作目录；mkdir/mkdirs 分开，filter 虚派并传播异常，缺 IoFileSystem
  不与 ENOENT 混淆；setWritable 仅在既有可写对象上报告成功，不伪造权限改变。
- InputStreamReader 用 Reader.lock 保护固定 ICU 六标准编码的增量转换，close/GC/teardown
  回收。OutputStreamWriter 使用同一编码集合写入真实 OutputStream，并传播 write/flush/close
  异常；PrintStream 保持 OutputStream 继承并把字节写入结构化 guest 日志。基类 bulk
  read/write 必须虚派子类，不能要求任意 guest 流存在宿主资源状态。
  ObjectStreamClass 仅保留六个受检反射原语；ObjectOutputStream.getFieldL、Proxy 生成和
  VMStack 除 getClasses 外的四个 native 明确失败。宿主资源不因迁入对象流自动可序列化。
- ZIP 的 archive/entry/cursor/close 只用 ZipRuntime，ZIP32/inflate/CRC 复用严格 loader；
  FilterInputStream 持源强引用，close 幂等关闭源，mark/reset 明确不支持。
- Buffer/Charset 的 handler 只做类型/异常边界；cursor/backing/view/字节序交 NioRuntime。
  typed view 共享 backing 并隐藏不匹配 array，保持 concrete class；direct buffer 只用强类型
  guest-memory 接口，不退化为 heap 或保存宿主指针。Memory 仅提供受检 byte[] 整数 codec。
- socket/stream/datagram 交 NetworkRuntime；默认离线，只有注入 policy/allowlist/transport
  才能连接，SSL factory 不扩大权限。form URL codec 用固定 Boost.URL、UTF-8、空格/+ 规则，
  非法百分号和未支持 charset 抛异常。客户端 TLS 由 OGPlayJSSE 经 raw transport 与 guest
  libssl 内存 BIO 执行；loopback 握手与 HTTPS GET 已受检，默认离线，不能静默把未播种 RNG
  或公开 CA 缺失当成成功。SAX 保留构造/handler 身份，未支持 parse 明确失败。
- InetAddress/Inet4Address/Inet6Address、地址缓存、InetSocketAddress 与 NetworkInterface 普通
  行为归 API 19 BootDex；IP 字节、hostName、scope 与 endpoint 字段是唯一状态。Posix 仅保留
  地址解析、受策略 DNS/反向查询及确定性 guest uname 边界；其余原生 OS 调用明确失败。
- URI、内部 encoder、URISyntaxException 与 UrlUtils 普通行为归 BootDex；构造、create、
  normalize/resolve/relativize、比较和对象流协议使用原版字段与算法。URL 仍使用现有有界
  intrinsic，因此共用的 C++ URL 解析辅助函数继续保留。
- ProxySelector 只登记无进程代理的薄 shape；getDefault 返回 null，供 API19 Apache
  RoutePlanner 明确选择直连。setDefault/select/connectFailed 未提供代理服务并明确失败，
  不读取宿主代理，不引入 Proxy 相关 BootDex 类。

## Locale、ICU、正则与密码

- Locale 保持 API19 类型/字段与 22 个静态强根常量，默认值按 VM 注入；大小写和 he/id/yi
  规范化遵循原版。LocaleData 首批只接受 ROOT/en/en_US/zh/zh_CN；ISO native 用固定 ICU51
  的 559 语言/249 国家表，不过滤或越过 NULL，Java 缓存每次返回 clone。
- Date/Calendar/Format/NumberFormat/TimeZone 普通状态和算法归 BootDex；TimeZone default
  只消费注入值且保持 clone/reset。ICU 数字/日期能力走固定 guest ICU，不恢复 java_text TU；
  formatter long 仅存 per-VM token，guest JNI 管 clone/close/错误，GC/teardown 回收。
  Pattern/Matcher 只承诺 String 与登记 regex 语义，groupCount 不要求已有匹配。
- crypto family 只管 provider、熵、JNI 和资源 owner。AES ECB/CBC 的 NoPadding/PKCS5Padding、
  CBC/ZeroBytePadding 及 CTR/NoPadding、128/192/256 KeyGenerator 已登记；ZeroBytePadding
  由 guest Java 适配到 CBC/NoPadding，Cipher 普通方法无 overlay。
  OGPlayOS 用 HAL CSPRNG 且拒绝 setSeed；SHA1PRNG 首次由同一 CSPRNG 播种，后续执行
  guest RAND_seed/RAND_bytes，调用方 seed 仅追加熵。IvParameterSpec 可用。
- API 19 原版 `NativeCrypto` 是 BootDex 唯一定义；catalog 仅按类准入其原版 native 到
  guest JNI，不重列成员。原版 `javacrypto` 加载名映射统一 JNI，字段 token 资源由 VM 装配；
  OGPlay 私有证书验签使用独立 `NativeVerification`，不扩展原版 ABI。
- Certificate/ASN.1/X.509/PKCS7/CertPath、UUID、MessageDigest 普通协议归 BootDex。
  证书 verify 不允许 overlay；SHA1/224/256/384/512 × RSA PKCS#1 v1.5/ECDSA 的 10 个
  验签 SPI 用普通字段保存公钥/最多 1 MiB 消息，真实解码后调用 guest ARM EVP，验后清空。
  摘要仅 MD5/SHA1/SHA256/SHA384/SHA512；NativeBN 只保留 17 个值原语和 per-VM token。
- 未登记能力明确失败：NativeCrypto 的 ENGINE/RSA/EC/X509/TLS 长尾、完整大数/double formatter、具名时区历史、privileged executor、
  完整 ThreadGroup/反射长尾、RSA Cipher/GCM/其他 transformation、签名生成、通用 CertPathValidator.PKIX、
  撤销检查、HMAC/SHA3/独立 SHA224 摘要。TrustManager 路径验证与只读 AndroidCAStore 已由
  OGPlayJSSE 交付；loopback 客户端 TLS/HTTPS 已闭合，SSLEngine/server TLS 明确失败。AES AlgorithmParameters provider 未注册，原版
  engineGetParameters 可返回 null；公钥仅编码 fallback，不宣称数学参数/KeyFactory 能力。
- KeyStore/KeyStoreSpi 及公开嵌套类已进入 BootDex；自有 `OGPlayKeyStore` 已注册 BKS 并设为
  默认类型，BKS v0/v1/v2、标准/历史 key PBE、AES RAW、RSA/EC PKCS#8 与 API19 双向互操作
  已受检。CallbackHandler 实际取密码、同 store 双线程、磁盘跨 session 重载及应用沙盒隔离
  已受检；complete 仅指 DVM-172 约定的软件 KeyStore/BKS 与算法集合。DVM-173 增加
  OGPlayJSSE：PKIX TrustManagerFactory、真实路径验证与只读 AndroidCAStore；`SSLContext`
  归 BootDex；loopback 客户端 TLS/HTTPS 已闭合，公开互联网 CA 与 OpenSSL 1.0.1 维护仍阻塞，
  TLS-03 未完成。

## 验证入口

[DexVM 工作单](../../../../docs/tasks/dexvm/README.md)保存迁移和验收历史；定向测试见
[tests/dexvm](../../../../tests/dexvm/)，架构门禁为 architecture.dexvm_intrinsic_layout。
行为改动覆盖 switch/threaded；只构建受影响目标，不因文档调整运行构建或测试。
