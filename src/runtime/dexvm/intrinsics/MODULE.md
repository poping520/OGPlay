# 模块：runtime/dexvm/intrinsics

intrinsic 的逻辑单位仍然是 Java class：每个 class 恰好一个 TU-private
`Declare_*()`，handler 与对应声明同址；物理文件只按 API family 聚合。
目录固定为 `catalog.cpp` 加 `java_lang/classloading/reflect/io/util/regex/zip/nio/net/xml/
concurrent.cpp` 11 个 family TU。family 文件只向 `catalog.h` 暴露 `Append*()`，
`catalog.cpp` 不感知 family 内具体 class，也不得包含行为。

family TU 按 API 语义与共享状态聚合，以控制翻译单元数量。
禁止新增 `misc`/`common`/`all` 等无语义聚合文件、字符串 core handler id、
全局静态自注册，以及 android.* 声明和行为顺手修改。

`java.*`、`javax.net.*`、`javax.xml.*` 与 `org.xml.sax.*` 均由 core 发布；需要平台事实的
Locale、Timer、SSL singleton 与 SAX handler 通过 `CoreIntrinsicServices` 窄接口注入，
core 不依赖 `DexVmAndroidContext`。
Timer/TimerTask 仅保留 Java 参数、重复调度与取消入口；deadline、执行队列、Clock 和
生命周期由注入的 scheduler 窄接口拥有。core 禁止反向读取 Android context 或恢复
cooperative next-frame task queue。

`java_util.cpp` 中的 collection 段是 pinned libcore `java.util` 核心集合 family：集中声明
Collection/List/Set/Map、Iterator/ListIterator、Queue/Deque、常用抽象基类以及
ArrayList/LinkedList/ArrayDeque、HashMap/LinkedHashMap、HashSet/LinkedHashSet；既有
Vector/Stack/Hashtable 也迁入同一 family。DVM-87 在同一 family 增加常用 Arrays/
Collections 算法和固定 offset Calendar/TimeZone；handler 只做 Java 参数/异常边界与 virtual
`equals/hashCode` 派发，sequence/map/view/entry/iterator 的宿主状态和生命周期统一委托
`CollectionRuntime`。Tree/Sorted/Navigable、并发集合、完整算法长尾与 DST/locale 时区数据库
不在该 family 范围内，缺失能力必须继续明确失败。

`java_regex.cpp` 的 Pattern/Matcher 只承诺 String 输入与已登记 API 的 bounded regex 语义；
非法语法/flag 必须抛 Java 异常，不伪造匹配。`java_concurrent.cpp` 的 FutureTask、串行 executor
和 atomic family 复用 `VmThreadRuntime` 的真实 Thread identity；不创建第二套 scheduler，
不扩展到并行池、scheduled executor 或 concurrent collection。

`java_io.cpp` 聚合 pinned libcore `java.io`：stream/reader/filter/buffer/byte-array/data
handle、File 与文件 reader/writer handle。每个 Java class 仍保留 TU-private `Declare_*()`；
流状态、`FileDescriptor` 逻辑来源和文件访问只委托 per-VM `IoRuntime`，不得回读 Android
session context。逻辑描述符不保存宿主句柄；`FileInputStream.getFD()` 只发布路径 identity，
读游标继续属于 stream。`File` 路径/对象、访问、类型、过滤列表与文件 rename 语义以
pinned API 19 libcore 为准；相对绝对化只消费 `IoRuntime` 注入的 guest 工作目录，`mkdir`
与 `mkdirs` 必须分别保持单级和递归语义。`FilenameFilter`/`FileFilter` 通过 guest virtual
`accept` 过滤直接子项并传播回调异常。VFS 尚无权限位修改能力，`setWritable(true)` 只在
目标已存在且本来可写时成功，其他请求明确失败，不得伪造权限变更。
未注入 `IoFileSystem` 与普通 ENOENT 不得合并：前者必须抛明确 Java 异常。filter、排序、
比较等 handler 若把新 guest 引用带过 nested guest call，必须使用 interpreter 的
execution-local `RootScope` 保活，宿主 `VmObjectRef` 容器本身不是 GC 根。

`java_zip.cpp` 聚合 `ZipEntry`/`ZipInputStream`。输入源只经 `IoRuntime` single-owner
接管，archive/entry/cursor/close 状态只委托 per-VM `ZipRuntime`；ZIP32 结构校验、inflate
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
