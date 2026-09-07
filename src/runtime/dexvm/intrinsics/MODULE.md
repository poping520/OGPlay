# 模块：runtime/dexvm/intrinsics

intrinsic 的逻辑单位仍然是 Java class：每个 class 恰好一个直接位于正式
`ogplay::runtime::dexvm::intrinsics` 命名空间的 `Declare_*()`，handler 与对应声明同址；
物理文件只按 API family 聚合，不保留 `dvm80_*` 迁移命名空间或同名转发函数。
目录固定为 `catalog.cpp` 加 `java_lang/classloading/reflect/io/util/icu/regex/zip/nio/net/xml/
concurrent/crypto.cpp` 13 个 family TU。family 文件只向 `catalog.h` 暴露 `Append*()`，
`catalog.cpp` 不感知 family 内具体 class，也不得包含行为。

family TU 按 API 语义与共享状态聚合，以控制翻译单元数量。
禁止新增 `misc`/`common`/`all` 等无语义聚合文件、字符串 core handler id、
全局静态自注册，以及 android.* 声明和行为顺手修改。
class/member access flag 必须使用 `access_flags.h` 的共享 `kAcc*` 常量组合，禁止在
family TU 中写裸十六进制访问标志；反射过滤使用同一头文件中单独命名的 Java modifier mask。
平台 enum 必须通过 `IntrinsicEnumBuilder` 在链接前生成常量字段、`$VALUES`、类初始化器
及精确类型的 `values/valueOf`；普通扩展只声明常量，payload 使用显式回调。
curated Boot DEX 内的纯 Java enum 消费 `java.lang.Enum.getSharedConstants` 通用 VM
overlay；不得为 `EnumSet/MiniEnumSet/HugeEnumSet` 增加 C++ 行为副本。

`java.*`、`javax.net.*`、`javax.xml.*` 与 `org.xml.sax.*` 均由 core 发布；需要平台事实的
Locale、Timer、SSL singleton 与 SAX handler 通过 `CoreIntrinsicServices` 窄接口注入；
Locale 保持 API 19 的 final class 及 Cloneable/Serializable 直接接口关系；ROOT、US、
ENGLISH、CHINESE 等 22 个预定义对象由类初始化器创建并保存在静态强根中。language/country/
variant/script 使用 API 19 transient 字段，默认 Locale 从会话注入且按 VM 隔离，不读取宿主
locale；首批 LocaleData 只接受 ROOT、en、en_US、zh、zh_CN，其余明确失败。
`org.xmlpull.v1.XmlPullParser` 只发布 API 19 接口 shape，资源事件实现归 Android integration，
core 不依赖 `DexVmAndroidContext`。
Timer/TimerTask 仅保留 Java 参数、重复调度与取消入口；deadline、执行队列、Clock 和
生命周期由注入的 scheduler 窄接口拥有。core 禁止反向读取 Android context 或恢复
cooperative next-frame task queue。

`java_util.cpp` 只保留 Locale 与 Timer/TimerTask 的宿主边界。Collection/List/Map、所有
选入的容器/视图/迭代器、Arrays/Collections、Observable/Observer 与 Random 由 BootDex
执行，不允许重建 C++ 算法或集合侧表。WeakReference 的弱边/入队属于通用 VM GC 原语。
ThreadLocal 消费 Thread.localValues 和 wrapping atomic getAndAdd；System.nanoTime 只读
统一 Clock，Runtime.availableProcessors 发布单 guest 执行通道事实 1。
`java_icu.cpp` 只发布 DVM-102 审计固定的 ICU native、NativeDecimalFormat 与 TimeZone 数据
边界；Format/DateFormat/SimpleDateFormat、NumberFormat/DecimalFormat、Date/Calendar 及
SimpleTimeZone 的类、字段和 Java 算法均来自 BootDex。formatter 的 Java long 只保存 per-VM
逻辑令牌，clone/close/非法令牌、GC sweep 与 VM teardown 由 IcuFormatterRuntime 管理；
未交付的具名时区数据库、大数/double formatter 和 ICU 查询明确失败。不得恢复 java_text.cpp
或日期/日历 C++ 行为副本。区域字段、货币和时区名称必须调用固定 ICU 数据，数字 native
必须委托真实 ICU formatter。TimeZone.getDefault 使用 DEX defaultTimeZone，与 Java
setDefault 保持 clone/reset 语义；首次/重置只使用 CoreIntrinsicServices.default_timezone。
Locale 构造规范化语言小写、区域大写与 he/id/yi 旧码；Matcher.groupCount 来自 pattern，
不要求已有匹配。ICU 分配失败、非法输入和范围外操作分别映射为明确 Java 异常。

`java_regex.cpp` 的 Pattern/Matcher 只承诺 String 输入与已登记 API 的 bounded regex 语义；
非法语法/flag 必须抛 Java 异常，不伪造匹配。`java_concurrent.cpp` 的 FutureTask、串行 executor
和 atomic family 复用 `VmThreadRuntime` 的真实 Thread identity；不创建第二套 scheduler，
不扩展到并行解释执行或 scheduled executor；concurrent 容器算法由 BootDex 拥有。

`java_concurrent.cpp` 同时声明 API 19 libdvm 的 `sun.misc.Unsafe`：同一静态强根单例、
真实 caller loader 检查、受检字段/数组位置、int/long/reference 读写和 CAS 委托
`UnsafeRuntime`；handler 不操作裸槽。`park/unpark` 复用 Thread，absolute epoch 毫秒
经 `CoreIntrinsicServices.current_time_millis` 与 monitor Clock 转换为单调 deadline。
无时间源/溢出明确失败，permit/interrupt/teardown 不另建状态。Thread 补齐 API 19 private
`parkBlocker` 对象字段，由 BootDex LockSupport 通过 Unsafe 读写并形成普通 GC 强边。`allocateInstance` 完成
clinit 后跳过构造器，拒绝不可实例化类。此能力不代表 BootDex Executors 已闭合。

`java_io.cpp` 保留文件/VFS、有界 Object streams 与 InputStreamReader 字符解码边界。
DVM-104 的 Input/OutputStream、Reader/Writer、内存/Buffer/Filter/Data 等普通流来自
BootDex，状态只在 guest 字段/数组中；禁止恢复相关 handler 或 wrapper-adoption 侧表。
FileDescriptor、对象协议状态与 ICU decoder 委托 per-VM IoRuntime，不回读 Android context。
FileReader 经真实 FileInputStream 构造后委托 InputStreamReader；后者以 Reader.lock 的
source monitor 保护增量转换器，六个标准编码来自固定 ICU 51，close/GC/teardown 释放资源。
逻辑描述符不保存宿主句柄；`FileInputStream.getFD()` 与借用描述符构造
共享输入状态和读游标，`FileOutputStream.getFD()` 同样共享输出状态，两者关闭时都必须
区分拥有与借用关系。`File` 路径/对象、访问、类型、过滤列表与文件 rename 语义以
pinned API 19 libcore 为准；相对绝对化只消费 `IoRuntime` 注入的 guest 工作目录，`mkdir`
与 `mkdirs` 必须分别保持单级和递归语义。`FilenameFilter`/`FileFilter` 通过 guest virtual
`accept` 过滤直接子项并传播回调异常。VFS 尚无权限位修改能力，`setWritable(true)` 只在
目标已存在且本来可写时成功，其他请求明确失败，不得伪造权限变更。
未注入 `IoFileSystem` 与普通 ENOENT 不得合并：前者必须抛明确 Java 异常。filter、排序、
比较等 handler 若把新 guest 引用带过 nested guest call，必须使用 interpreter 的
execution-local `RootScope` 保活，宿主 `VmObjectRef` 容器本身不是 GC 根。
`OutputStream` 的默认 bulk write 必须经 receiver vtable 派发到子类 override；基类
`flush/close` 是 no-op，禁止直接假设任意 guest 子类都在 `IoRuntime` 中拥有输出状态。
`InputStream` 的默认 bulk read/skip 同样必须经 receiver vtable 派发；基类
`available/close/mark` 使用 API 19 默认语义，`reset` 明确抛 `IOException`，不得要求自定义
子类注册 `IoRuntime` 输入状态。
`ObjectInputStream/ObjectOutputStream` 按 API 19 分别继承 `InputStream/OutputStream`，
实现 `ObjectInput/ObjectOutput` 与 `ObjectStreamConstants`；`ObjectInput/ObjectOutput` 再继承
`DataInput/DataOutput` 与 `AutoCloseable`。公开包装构造器保留 source/sink 的 guest 身份，
经虚方法读取/写出而不接管其游标；IoRuntime 只存对象协议所需缓冲，校验/写入 stream header，并按 block-data wire format 实现继承的
原始字节、基本类型、modified UTF、available/skip/flush/close 契约。对象协议支持 `null`、
`String`、默认 `Serializable` 类层级、字段、循环/重复引用、枚举及 API 19 `Date` 自定义段；
Serializable/Externalizable 及上述数据/对象 IO 接口均来自 BootDex。
Externalizable 协议 2 使用显式 UID、公共无参构造器及虚派 writeExternal/readExternal，
注册 handle 后才调用回调；同一流共享递归预算，未消费的 block/object 数据跳过到 end-block，
回调异常原样传播。stream handle 中的 guest ref 必须经 `IoRuntime` trace。
数组、Externalizable 协议 1、任意私有 `writeObject/readObject` hooks 和默认 UID 计算仍明确失败。

`java_zip.cpp` 聚合 `ZipEntry`/`ZipInputStream`。构造时经 guest read 读取源数据，
archive/entry/cursor/close 状态只委托 per-VM `ZipRuntime`；ZIP32 结构校验、inflate
与 CRC 继续复用 loader 的严格实现。该 family 不得在 Android context 恢复 ZIP side map。

`java_nio.cpp`（DVM-82）聚合 API 19 Buffer family、ByteOrder、Buffer exception 与 Charset。
handler 只做 descriptor/array/异常边界，cursor、heap/direct/view backing、字节序与 GC 生命周期
统一委托 `NioRuntime`。typed view 必须共享 backing 且隐藏不匹配类型的原始 array；direct
buffer 不保存宿主指针，只消费 integration 注入的强类型 guest-memory 窄接口。同类型 view
沿用 receiver concrete class，direct buffer 不得退化成 heap class。

`java_net.cpp`（DVM-88）聚合 URL/SSL 既有 shape 与 InetAddress、Socket、Datagram、
SocketFactory family。socket/stream/packet 状态只委托 per-VM `NetworkRuntime`；默认 policy
离线，只有显式 allowlist 与注入 transport 才能发起连接。handler 不调用宿主 socket/DNS，
不读取 Android network service，也不以 no-op 伪造连接、TLS 或 datagram 成功。
`SocketFactory.getDefault/createSocket` 与 SSL factory 只创建受同一 policy 管理的 socket。
`URLEncoder/URLDecoder` 是无网络副作用的 API 19 form codec：UTF-8 字节的百分号编解码使用
仓库固定的 `third_party/ext-boost` Boost.URL，空格与 `+` 保持
`application/x-www-form-urlencoded` 语义；非法 `%` 与未交付 charset 必须抛 Java 异常。

`java_lang.cpp` 中的 interface 段覆盖 pinned libcore `java.lang` 顶层 8 个
interface；方法表按 Luni 源码建模。已有 `CharSequence.length` handler 保持
不变，其余接口方法（含 `Readable.read(CharBuffer)`）为显式
`UnimplementedVirtual`，不伪造成功。嵌套的 `Thread.UncaughtExceptionHandler`
由 Thread family 独占，`java.lang.annotation` 不纳入。

`java_lang.cpp` 中的 Thread 段是 pinned libcore `Thread.java`/`VMThread.java` 的 core
façade：声明 Java-visible fields 并负责参数校验/Java exception，生命周期、
execution context、parking、interrupt、sleep/join 与 identity mapping 全部委托
`VmThreadRuntime`/`VmMonitorTable`。`start()` 必须 virtual-dispatch `this.run()`；
基类 `run()` 才 virtual-dispatch target Runnable。纳秒在统一毫秒 Clock 上向上取整；
priority 与 daemon 仅是明确有界的 guest fact。字段通过 builder 的预绑定 handle
访问，handler 参数与字段值统一走 `IntrinsicCall`，不得恢复逐调用 descriptor 查找和
裸 instance slot 编解码。`contextClassLoader` 同样是受 GC 追踪的声明式字段：root 默认
指向唯一 application loader，新 Thread 继承创建者，显式 setter 可保存 null；不得因此
创建新的 class directory 或定义权限。uncaught handler 的 instance/static 引用均由对象图
追踪并按 VM 隔离；异常退出时显式 handler 优先于默认 handler，回调异常按 API 19 忽略，
均为空才保留进程致命诊断。ThreadGroup fallback 在其 family 发布前不伪造。
线程诊断把既有 safe-point stack snapshot 投影为 guest `StackTraceElement[]`，全量查询只
枚举本 VM 存活 Thread 并写入真实 HashMap。bounded ThreadGroup 提供稳定 system/main
身份、名称与存活线程枚举；Thread 继承 main group，终止后 getter 返回 null，`toString`
严格输出 `Thread[name,priority,groupName]`。不借此宣称完整 ThreadGroup/Thread.State。

`java.lang.System` 的 `getProperty`/`setProperty` 与 primitive wrapper property
API 共用每 VM 属性表；默认只发布
API 19 guest 可确定的 `/`、`:`、`\n` 三个 separator 属性，不读取宿主系统属性。
未知 key 返回 null，null/空 key 与 null value 按 Java 异常语义失败。
`System.getSecurityManager()` 按 pinned API 19 libcore 固定返回 null；
`SecurityManager` 自身由 curated BootDex 提供 class shape，OGPlay 不安装 security
manager、执行 permission 检查或接入宿主安全机制。
`String.toLowerCase(Locale)` 对齐 API 19 的 null 检查和“内容未变则返回 receiver”语义；
当前只接受英语 ASCII 映射，其他 Locale 或需要 ICU 的非 ASCII 输入明确
抛 `UnsupportedOperationException`，不得读取宿主 locale 或伪造完整 Unicode case mapping。

`java.lang.ClassLoader`、`java.lang.BootClassLoader` 与
`dalvik.system.PathClassLoader` 分别保持一类一文件；对象身份、parent 与 lookup/
initiate 状态委托 `ClassLoaderFacade`。动态 classpath 构造器显式未实现，自定义
ClassLoader 仅映射到唯一 application namespace，不获得第二套 class directory。
`Class.forName(String)` 从 interpreter 活跃 frame 取得真实 caller loader 并初始化；
三参数版本将 null 映射到 API19 system loader，并只接受 boot/application/custom 的
bounded role。lookup/link failure 保留为 CNFE cause；init EIIE 复用现有 guest
throwable identity，不由 intrinsic 重新物化。

reflection shape 按一类一文件声明 `AnnotatedElement`、`GenericDeclaration`、`Type`、
`Member`、`AccessibleObject`、`Modifier`、`Method`、`Constructor` 与 `Field`。
`Class` 的结构、类型关系和 declared/public Method/Constructor/Field 查询只能调用
linker/`ReflectionRuntime`，禁止读写 raw member id；public 聚合顺序固定为
class → superclass → direct interface 递归。nested/enclosing 与 Throws 只消费 loader
输出的 Dalvik system metadata；generic 与 annotation proxy 明确不实现。
OGPlay 不提供 Dalvik assertion-control 启动参数，`Class.desiredAssertionStatus()` 固定返回
API 19 无匹配规则时的默认值 `false`。

DVM-66 将 `Method.invoke` 降为 `ReflectionRuntime` 的薄入口：handler 不手写
unbox/boxing 或 target id，统一使用 `ReflectionCodec`、真实 interpreted caller
和 declared invoke category。`InvocationTargetException.target` 是普通 guest reference field，
`getTargetException/getCause` 保留原 throwable identity 并由 GC 普通对象图追踪。

DVM-67/68 的 Constructor/Class 实例化与 Field object/primitive 操作同样是
`ReflectionRuntime` 薄入口；`reflect.Array` 的单维/多维创建和 object/primitive
访问使用真实 typed array store 与同一 ReflectionCodec。DVM-69 的 Class nested/
enclosing/member-local-anonymous 和 Method/Constructor Throws 只读 linker system
metadata，禁止 `$` split 或 guest Annotation proxy。
Method/Constructor/Field 的 API19 hashCode 与 exact toString 读取同一 immutable
metadata；最小 `Modifier.toString(int)` 只提供 JLS 顺序格式化，不开启 generic surface。

DVM-104：java_concurrent 删除普通 atomic 宿主算法，只保留 AtomicLong.VMSupportsCS8
native；同步器/原子数组经 DEX 调用既有 Unsafe、Thread、monitor 与统一 Clock。
Charset 的六种标准编码具有 canonical name、相等/排序与真实编解码语义；String 具名和
Charset 重载使用同一固定 ICU 转换，Locale 大小写同样由 ICU 提供。Memory 仅保留受检
byte[] 的 short/int/long 大小端 codec，不开放 raw address。java_lang 的 NativeBN
绑定 17 个值原语（DVM-106 扩展长整数编解码），18 个其余 native 显式未实现；令牌由
BigIntRuntime 按 VM 管理，owner GC/teardown 释放，不实现完整大数计算。

DVM-104 的 ZIP 适配器调用 BootDex FilterInputStream 构造以保持源强引用；read/skip/available
使用解压后 entry 游标，mark/reset 明确不支持，close 经父类关闭原始源且幂等。

## DVM-105 Java crypto

java_crypto.cpp 仅提供 provider 配置、OS entropy service、JNI 声明和 context owner 绑定。
AES/ECB、CBC 的 NoPadding/PKCS5Padding 与 CTR/NoPadding 注册为 AndroidOpenSSL，AES
默认别名指向 ECB/PKCS5Padding。普通 Cipher 方法无 intrinsic 副本。
SecureRandom 的 OGPlayOS 服务调用 HAL CSPRNG，engineSetSeed 明确失败，不伪装 SHA1PRNG。
RSA Cipher、TLS、AES-GCM 和其他 transformation 未注册；AES AlgorithmParameters 编码
provider 未注册，AOSP engineGetParameters 按原代码返回 null。IvParameterSpec 可用。

## DVM-106 Certificate

Certificate/X509Certificate（含 javax 旧 API）、CertificateFactory、Harmony ASN.1/X.509/
PKCS7/CertPath 的普通方法均执行 BootDex，verify 不允许 intrinsic overlay。
Security 加入原版 DRLCertFactory；AndroidOpenSSL 注册 10 种仅验签的 SignatureSpi：
SHA1/SHA224/SHA256/SHA384/SHA512 与 RSA PKCS#1 v1.5/ECDSA 的组合及标准 OID 别名。
SPI 只保存普通 guest byte[] 公钥快照和 ByteArrayOutputStream，累计消息最多 1 MiB；
engineInitVerify 真实解码验证公钥，engineVerify 经显式 JNI 调用 ARM EVP，并清空消息。
签名生成、PSS/DSA/EdDSA、PKIX 信任判断、系统 CA/撤销/TLS 不注册或明确失败。
公钥保留 Harmony X509PublicKey 的编码型 fallback，不宣称 RSA/EC KeyFactory 和数学参数接口。
NativeBN 增加大小端数组、二补码和十/十六进制值转换；Math.log 为 Java 进制转换提供
既有 Math 浮点原语边界，不在宿主实现证书 parser 或签名算法。

DVM-107：framework Pair/Sparse/ComponentName 与 PrintWriter 的普通算法归 BootDex，
StringBuilder/StringBuffer 共用 CharSequence 区间 append 原语：UTF-16 索引，虚派
length/charAt、null 视为 "null"、自追加取原片段、越界不改变原 buffer。测试覆盖双后端
ComponentName 的短名称打印与 UTF-16/null/自追加/异常边界。
