# DexVM、BootDex 与 Java 平台边界

返回 [ADR 索引](README.md)。本文件只记录现行决定；旧文由 Git 保存。

- [ADR-0017 · 有界 DEX 解释执行与平台边界](#adr-0017)
- [ADR-0029 · Java 状态与宿主资源的唯一所有权](#adr-0029)
- [ADR-0066 · 密码、KeyStore 与 TLS 的 guest 依赖边界](#adr-0066)
- [ADR-0068 · SQLite Java 栈与唯一 VFS 上的宿主引擎](#adr-0068)
- [ADR-0147 · 未实现平台成员明确失败](#adr-0147)
- [ADR-0152 · Java primitive/String 的稳定 guest backing](#adr-0152)
- [ADR-0167 · Locale 的 ROM Java 归属与单一设备语言地区](#adr-0167)
- [ADR-0172 · Dalvik 堆默认值对齐 API19 1 GB 设备档位](#adr-0172)

<a id="adr-0017"></a>

## ADR-0017 · 有界 DEX 解释执行与平台边界

- 状态：Accepted
- 日期：2026-08-11

### 背景

应用自身已有 DEX，手工重放生命周期会遗漏副作用，使适配成本按游戏增长。

### 决定

应用与 API19 ROM Java 方法在同一 DexVM 中解释执行，启动类路径为 ogplay.jar 与原样 ROM jar。
ROM ActivityThread、主 Looper、ViewRootImpl 执行应用启动、生命周期与软件绘制；宿主提供 native
和进程内服务接口，不启动 system_server、Zygote 或 Binder IPC。新代码不得替换 ROM 类或覆盖
ROM 非 native 方法；现存替换和覆盖按阶段移除，查询方法见 [平台边界](../design/dexvm.md)。
只提供稳定 application/boot loader 与封存类路径，不开放动态 classpath、自定义加载权限、
完整 verifier、DEX JIT/AOT、odex/quickened、JDWP 或 instrumentation。
JNI 与 Java 共用对象身份、字段、数组/String payload 及 GC；不能复制一套宿主 Java 状态。
Dalvik 4.4 固定源码作为语义参考，不编译链接其完整对象模型、GC、线程或 JNI 实现。
未实现的指令、方法与平台边界记账并明确失败，禁止伪成功。

### 后果

平台普通算法可复用一次，游戏胶水无需人工翻译；这仍是有界解释器，不是完整 ART/Dalvik。

<a id="adr-0029"></a>

## ADR-0029 · Java 状态与宿主资源的唯一所有权

- 状态：Accepted
- 日期：2026-08-29

### 背景

将 owner-map key 当 GC root 会永久保活对象，并把普通 Java 值与宿主资源混为一谈。

### 决定

普通 Java 字段、数组、容器、异常及协议由 BootDex/VM 对象图唯一拥有，不保留 C++ 影子算法或状态。
宿主资源状态分三类：
1. session/process root，由 owner 明确枚举 Application、活动组件、scheduler work 等根；
2. owner-attached state，以 VmObjectRef 为 owner，经具名 state table 注册 trace/sweep；
   仅 owner 存活时 trace child，回收 handle 前 sweep，禁止反向强根保活 owner；clone 默认关闭；
3. thread token、window ID、resource id、路径及 process singleton 等非对象 identity，按自身寿命管理。
声明含 guest 引用的 state 必须有 trace，所有 owner 表必须有 sweep，不能用兼容 root 绕过。
Java 字段中的 native 逻辑 token 按显式字段/cleanup 登记，别名共同持有资源，最后 owner 才释放。
GC、Java 显式 close、finalizer 与 teardown 共用关闭事实，不能重复释放或伪造非法 token 成功。
已准入的 finalizer 图与队列归 Java，GC 保活后由真实 guest daemon 执行；不借 finalizer 代替
宿主资源必须具备的 sweep/退出回收。

### 后果

句柄复用不会继承旧状态；新增 API family 先确认权威 owner，再选择引用边与资源释放协议。

<a id="adr-0066"></a>

## ADR-0066 · 密码、KeyStore 与 TLS 的 guest 依赖边界

- 状态：Accepted
- 日期：2026-09-16

### 背景

公开安全 API 需要真实算法与隔离资源，同时不能引入完整 Android 服务或双份宿主 TLS。

### 决定

公开 JCA/证书/KeyStore/SSL API 与普通协议执行受审 API19 Java；native 密码操作经唯一
libogplay_jni.so 调用 guest libcrypto/libssl，不在宿主重复实现 AES、证书 parser 或 TLS。
生产 guest 源码归 src/guest，构建编排归 tools；统一桥使用固定 ARMv7 API19 工具链与来源清单。
crypto/ssl 来自可追溯 AOSP OpenSSL 构建，不恢复设备提取库或独立 cipher SO。
原版 NativeCrypto 的完整类身份由固定 BootDex 提供，只准入受审 native；javacrypto 解析到统一桥，
未导出入口明确失败，不因名字或类声明存在宣称完整 Conscrypt。
自有 OGPlayKeyStore 管理 BKS 条目/格式/保护，状态仅在 Java，持久化只用调用方流/VFS；
不引入 BouncyCastle 生产依赖，不冒充 BC、系统 AndroidKeyStore 或硬件保护。
自有 OGPlayJSSE 经私有 JNI 与内存 BIO 完成客户端 TLS，网络只经唯一 NetworkPolicy/raw transport，
默认离线。trust manager 必须真实执行，信任、主机名与 I/O 授权分别验证。
资源使用逻辑 token 与 owner/GC/退出回收；不把 host pointer、host FD 或机器身份带入 guest。
旧 OpenSSL 只支持本地兼容开发证据，公开在线交付须完成版本维护和补丁审计；
公开互联网 CA 包、完整 PKIX、AndroidKeyStore、SSLEngine 与 server TLS 不在当前完成范围。

### 后果

项目承担 SPI、格式互操作与后端维护；本地 BKS/TLS 已有实现，不能再描述为未开始或在线已验收。

<a id="adr-0068"></a>

## ADR-0068 · SQLite Java 栈与唯一 VFS 上的宿主引擎

- 状态：Accepted
- 日期：2026-09-18

### 背景

自定义数据库格式和 C++ Java facade 难以提供真实 SQL、事务与 Android 生命周期。

### 决定

原版 API19 SQLiteDatabase/OpenHelper、连接池/会话、语句与 Cursor Java 栈进入 BootDex。
固定 SQLite amalgamation 在宿主执行，所有主库、rollback journal 与临时文件只经 guest VFS/
SandboxStore，不接受宿主路径；只保管 connection/statement/window 资源，不复制 Java 事务栈。
Java native 字段保存受检逻辑 token；Clock、线程、取消、GC 与释放使用统一边界。
主库、journal 与锁共用 VFS 规范文件身份，路径别名不能产生独立锁。
支持 rollback-journal 与进程内并发；WAL、ATTACH/DETACH、跨进程共享和动态扩展不提供。
文件格式由 SQLite 判断；不保留旧格式识别、迁移或回退，损坏按 API19 默认处理器处理。

### 后果

不重写 SQL 引擎，但承担 VFS 同步/锁与版本差异验证；不承诺完整 API19 SQLite 等价或断电耐久性。

<a id="adr-0147"></a>

## ADR-0147 · 未实现平台成员明确失败

- 状态：Accepted
- 日期：2026-10-20

### 背景

平台类型与成员存在，不证明行为已实现；静默返回零值会让游戏走进错误分支且难以定位。

### 决定

启动类路径按固定顺序加载 ogplay.jar 和原样 API19 ROM jar；归档哈希与 DEX 条目由装配校验。
ROM 类的方法、字段及初始化执行原版字节码，native 和本地服务边界由宿主绑定。
类可链接不代表全部行为已实现；未绑定 native 调用记账并抛 UnsatisfiedLinkError。
未实现的存量 intrinsic 方法/构造器及非常量字段访问明确失败，不返回默认 null/0。
真实常量保留原值，不生成伪 clinit、空成功构造或替代对象状态。

JNI/反射区分“成员缺失”与“存在但未实现”：合法查找可成功，执行/字段访问才暴露行为缺口；
缺成员或缺类按 Dalvik 发布对应 NoSuch*/NoClassDefFound 错误，不能把未实现伪装成不存在。
未绑定 native 明确失败，诊断 surface 必须报告真实来源。

无返回值的纯显示设置按 AGENTS.md 忽略并调用 `RecordUiDegraded` 记录警告，不抛异常。
ROM 方法照常执行；native/服务入口只对符合降级边界的展示请求使用该规则。
监听器注册、层级变更、输出参数、密码掩码和所有返回值不降级，继续遵循真实状态或明确失败。

### 后果

缺类与未实现行为分别诊断；运行完整 ROM jar 不表示提供完整 Android 系统服务。

<a id="adr-0152"></a>

## ADR-0152 · Java primitive/String 的稳定 guest backing

- 状态：Accepted
- 日期：2026-10-22

### 背景

JNI 长期借用、Java 访问和 NIO 必须看到同一 payload；复制写回不能提供 Dalvik 的直接可见性。

### 决定

生产 primitive array 与 immutable String 自创建起使用进程 AddressSpace 内稳定 typed block/view，
Java/JNI/NIO 共用唯一字节；对象头、句柄、GC 与布局仍归 VM，不引入 Dalvik heap ABI。
integration 拥有 allocator 并显式注入 JNI store；下层只定义强类型 provider/borrow/accessor，
不反向依赖内存实现或通过全局/TLS 猜 owner。host-copy 仅显式夹具，无生产复制回落。
固定长度、ARM 小端、至少 8-byte 对齐，零长有有效地址；页内池与大块分配不预占整堆。
provider 范围受映射账本保护，部分重叠的 unmap/protect/replace/mremap 先拒绝并记账；
活跃 provider 拒绝独立 restore，AddressSpace base 稳定且晚于 owner/view 析构。
页照常进入受保护 JIT 数据表；typed accessor 保活 block，单次验证后仅做视图边界检查。
Java/JNI New 与 import 共用唯一 VM 收费/失败回滚；逻辑堆、实际提交、VAS、pin 分别计量。
Elements/Critical、Chars/StringCritical 返回同一 backing、isCopy=FALSE；pin 按数组或 String
身份跨线程计数，重复 Get 同址但各自加 pin，detach 不解除。Release 忽略 pointer/kind；
数组 COMMIT 无操作，其余 mode（含 ABORT）仅减一次 pin，不写回或恢复；String 同样按身份 unpin。
无配对释放只 warn/ledger，首次 pin>10 告警一次；pin 在 mark/weak 前作强根。
String pin 同时保活 String 与 block，较 Dalvik 仅 pin char[] 更保守；MUTF-8 仍复制、TRUE 并配对释放。
String 的可变输入和 builder 保持独立快照，不新增影子 value/offset/count 或完整私有字段 ABI。
allocateDirect 创建真实 byte[] 并与所有 NIO view 共享地址/GC；JNI 外部 direct 地址非拥有、无数组。
先停止/join guest 线程与 CPU，再退役 pin/backing；长期未 Release 不得偷偷回收。

### 后果

借用避免 O(N) 复制，但漏释放会形成真实 pinned live set；不承诺额外内存序、stale write/ABA
检测或完整 VM save-state，单独页快照不能当作游戏存档。Windows 生产路径仍待验证。

<a id="adr-0167"></a>

## ADR-0167 · Locale 的 ROM Java 归属与单一设备语言地区

- 状态：Accepted
- 日期：2026-10-28

### 决定

java.util.Locale 的字段、常量、构造、默认值及 setDefault、显示名、ISO3、枚举和复制协议
统一执行 AOSP android-4.4.4_r2.0.1 core.jar 原版 Java；删除宿主 Locale intrinsic 与 ISO3 注入。
ICU 准入扩展至显示语言/国家/变体、ISO3 语言/国家及可用区域枚举，仍只使用固定 guest ICU51
C ABI 与同一 icudt51l.dat；未知代码、空串和 MissingResourceException 决策保持 ROM 语义。
未准入 native 仍逐签名明确失败，不引入宿主 ICU 或 ICU C++ ABI。

DexVmAndroidContext 的 locale_language（默认 en）和 locale_region（默认 US）是唯一设备事实。
对齐 AndroidRuntime.readLocale 的两个字符语言/地区输入：仅接受小写两字母语言及大写两字母地区，
非法输入拒绝；不读取宿主 locale。System 初始化经既有服务写 user.language/user.region，
boundary 的 AConfiguration 经显式 provider 读取同源投影。Locale.setDefault 仅修改 Java 进程默认值，
不改变设备事实；Resources.getConfiguration 每次读取当前 Java 默认值。

后续设置项与 persist.sys.* 必须接入同一设备事实；本次不接设置 UI、不改 Bionic 属性区、
旧 headless FrameworkLocaleHle 或资源 locale qualifier。

### 后果

Java 算法与缓存没有宿主副本；设备语言地区与应用可修改的 Java 默认 Locale 各有明确归属。
验证入口及范围见 [Android 平台契约](../../src/runtime/integration/dexvm_android/MODULE.md)。

<a id="adr-0172"></a>

## ADR-0172 · Dalvik 堆默认值对齐 API19 1 GB 设备档位

- 状态：Accepted
- 日期：2026-10-30

### 背景

虚拟设备内存快照为 1 GiB，旧 Dalvik 64/512/1024 MiB 默认堆不匹配。
Runtime.maxMemory 会影响应用按比例预分配；Manifest 的 largeHeap 必须与实际活动堆上限一致。

### 决定

采用 AOSP android-4.4.4_r2.0.1 的 frameworks/native/build/
phone-xhdpi-1024-dalvik-heap.mk：start 8 MiB、growth 96 MiB、maximum 256 MiB，
target utilization 0.75、min free 512 KiB、max free 8 MiB。六个参数均复用现有模型。
普通设备 memory class 保留初始 growth limit，clearGrowthLimit 仅改变活动分配上限。
Manifest 解析 application largeHeap，ApplicationInfo.flags 发布 FLAG_LARGE_HEAP=0x00100000；
依 ActivityThread.handleBindApplication 顺序，在创建 Application 前调用原版
VMRuntime.getRuntime().clearGrowthLimit()，将活动上限放开至 maximum。
不新增 Profile 堆覆盖，不对单个游戏放宽。

### 后果

普通应用 maxMemory=96 MiB，largeHeap 为 256 MiB；getMemoryClass 始终 96，
getLargeMemoryClass 为 256。Native guest 内存不纳入 Java 堆上限。
跨游戏 OOM 需保留分配栈与所需大小并停止，不能静默回退旧档位；不改变 1 GiB 设备事实。
