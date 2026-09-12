# DexVM、BootDex 与 Java 平台边界

返回 [ADR 索引](README.md)。本文件按编号保留决策沿革；后续记录的 `Supersedes`
只替代其明确指出的旧条款，其余结论继续有效。

- [ADR-0017 · 有界 DEX 解释执行与平台内建类边界](#adr-0017)
- [ADR-0028 · DexVM declaration 只保存 own members](#adr-0028)
- [ADR-0029 · DexVM 宿主状态三类所有权](#adr-0029)
- [ADR-0030 · API 19 curated Boot DEX 与方法覆盖](#adr-0030)
- [ADR-0031 · API 19 Unsafe 逻辑位置与原子性](#adr-0031)
- [ADR-0032 · API 19 日期格式化数据与平台边界](#adr-0032)
- [ADR-0033 · 集合 BootDex 所有权与弱引用边界](#adr-0033)
- [ADR-0034 · 后续 Luni 与平台边界](#adr-0034)
- [ADR-0035 · Cipher AES 与 guest OpenSSL](#adr-0035)
- [ADR-0036 · Certificate 与 guest OpenSSL 验签](#adr-0036)
- [ADR-0037 · guest 生产源码与 crypto 制品来源](#adr-0037)
- [ADR-0038 · framework 值类归 BootDex，Activity 保持窄平台边界](#adr-0038)
- [ADR-0039 · UUID 与摘要归 BootDex，mutable native token 归普通字段](#adr-0039)
- [ADR-0040 · 对象序列化协议归 BootDex，VM 仅提供构造与元数据原语](#adr-0040)
- [ADR-0041 · 定时执行器与 FutureTask 执行原版 Java](#adr-0041)
- [ADR-0042 · 服务查询的有界无匹配结果](#adr-0042)
- [ADR-0043 · Throwable 协议归 BootDex，栈捕获归 VM](#adr-0043)
- [ADR-0044 · BackupManager 保留 Java 无服务路径](#adr-0044)
- [ADR-0045 · Bundle 与 Intent extras 归 Java 对象图](#adr-0045)
- [ADR-0046 · Java 布局参数与 UI 布局输入分工](#adr-0046)
- [ADR-0047 · Typeface Java 与字体后端描述符](#adr-0047)
- [ADR-0048 · 文本外观的 Java 值对象与有界样式事实](#adr-0048)
- [ADR-0056 · Runtime hook 状态归 BootDex，显式退出归 guest 进程边界](#adr-0056)

<a id="adr-0017"></a>

## ADR-0017 · 有界 DEX 解释执行与平台内建类边界

- 状态：Accepted（架构方向即日生效；实施启动时间由 roadmap 排期另行决定）
- 日期：2026-08-11
- 关联：[docs/design/dexvm/](../design/dexvm/README.md)（方案设计全文）、
  [roadmap 04 · §7](../roadmap/04-android-runtime.md)（DEX L0–L2 分级框架）
- Supersedes：roadmap 04 §7.4 中"先测量题库、再决定是否投入 L2"的 go/no-go 判定。
  是否投入已决定为"做"；题库测量继续执行，但只用于决定排期、批次顺序与
  平台内建类最小集，不再决定做与不做。

### 背景

profile 驱动的生命周期把每个游戏的 Java 胶水层人工翻译成 `native_call` 序列与
`[[java.class]]` implementation 映射。M8 的 Asphalt 6 推送初始化缺失分析证明该方式
存在系统性风险：Java `onCreate` 的任何一次 JNI 副作用漏抄，都会在很远的调用点以
"requires a valid class reference" 这类形式爆出，且只能靠反汇编逐个回溯；同时每款
游戏约 120–180 行人工逆向的 profile 让兼容成本随题量线性增长。

两个问题共同的根因是 HLE 边界画在"每个游戏自己的 Java 类"上，而这些类的行为
本来就静态存在于 APK 的 `classes.dex` 里，机器可读、机器可执行。

### 决定

- 实现有界 DEX 字节码解释器（模块名 `dexvm`），只解释应用自带 `classes.dex` 中的
  类；`android.*` / `java.*` / `javax.*` 一律作为宿主实现的内建类（intrinsic），
  不加载、不解释任何 framework/core 库字节码。
- HLE 边界从"游戏 Java 类"下移到"平台类 API 面"。游戏类的方法体由解释器真实
  执行，Title Profile 长期退化为 identity、数据布局、预算与 quirk。
- 有界性硬约束：无 JIT/AOT，无 odex/quickened 指令，无多 ClassLoader 层级与动态
  代码加载，无 JDWP/instrumentation；未实现的指令、内建类和内建方法必须记账并
  明确失败，禁止伪造成功。ADR-0001 对完整 ART/Dalvik 的禁令继续有效，本 ADR
  定义"有界解释器 + 平台内建类"位于该禁令之外。
- 对象模型统一：JNI 对象身份的"宿主对象或 VM 对象"双形态（roadmap 04 §7.4 的
  预留）落地为 session 级 JavaObjectModel，并吸收 `JniObjectArrayStore` 所有权
  统一的既有 backlog。
- 以固定 tag（`android-4.4.4_r2`，与 API 19 目标同代、Apache-2.0）vendor
  AOSP `platform/dalvik` 为参考基线：opcode 目录等数据与其机器比对，指令
  与运行时语义逐组件对照其实现；默认不编译不链接，不移植其对象模型/GC/
  线程/JNI 实现体——与 dynarmic/ANGLE/PowerVR 先例同一姿态（设计文档 07 章）。
- 与现有设计冲突时，以 [docs/design/dexvm/06-migration.md](../design/dexvm/06-migration.md)
  的冲突裁决表为准。

### 后果

- Asphalt 6 类"生命周期副作用漏抄"问题在 dexvm 生命周期下结构性消失；每款游戏
  的 Java 胶水翻译成本变为平台内建类的一次性摊销成本。
- 项目新增一个大型长期子系统（解释器、类链接、对象模型、GC、内建类库），必须
  按设计文档的阶段与机器可判定出口推进，禁止一次性大爆炸实现；语义实现
  必须记录 AOSP 出处，禁止凭记忆臆造 Dalvik 行为。
- 过渡期内 profile 驱动与 dexvm 驱动两种生命周期共存，按 title 逐个迁移；能力
  账本只增加新条目，既有条目状态不后退。
- libGDX / AndEngine / 纯 Java 休闲游戏等"Java 厚层"题目从结构上不可支持变为
  可支持，题库上限显著扩大。

<a id="adr-0028"></a>

## ADR-0028 · DexVM declaration 只保存 own members

- 状态：Accepted
- 日期：2026-08-29
- 关联：[DVM-94](../tasks/dexvm/DVM-94.md)

### 背景

早期 intrinsic builder 允许子类再次声明父类方法。这样既让 `Application` 等类型需要复制
`Context` API，又无法区分新增虚方法与真实 override；常量池缓存还只区分
direct-or-static，错误 opcode 可能复用不相容的解析结果。

### 决定

class declaration 只包含本类新增成员、构造器、静态成员、类生命周期方法和真实 override。
继承成员只由 linker 复制 vtable/field layout 产生。普通 `VirtualMethod` 命中父类同签名即
链接失败；真实覆盖必须使用 `OverrideMethod`（final 覆盖使用显式 final 版本），并校验父
签名、final/static/private 与可见性。

可见性比较使用 Java 的 public > protected > package > private 顺序。Intrinsic builder 的
普通虚方法默认 public，但平台 callback 必须按 pinned API 元数据显式写入 access flags；
不得为绕过链接错误而放宽 override 规则。Android 4.4.4 中 Activity 生命周期、
View.onSizeChanged、AsyncTask 回调和 HandlerThread.onLooperPrepared 均属于 protected。

调用解析以 `InvokeKind` 为键，返回含 declared owner、symbolic method 与可选 vtable slot 的
`ResolvedCallSite`。constructor/private 只查声明类，static 可沿父链但不多态，super 从当前
执行类的直接父类分派，`<clinit>` 不继承。descriptor 只在注册/链接时生成 `MethodShape`。

### 后果

`Application/Activity/Service` 自动继承 ContextWrapper；只有 ContextWrapper 的 `mBase`
委托是显式覆盖。反射 declared/public 查询和 declaring class 不再受复制声明污染。lazy 数组
和 Survey 追加依赖地址稳定 linker 存储，不得写回 DEX 或引入 quickening/JIT。

<a id="adr-0029"></a>

## ADR-0029 · DexVM 宿主状态三类所有权

- 状态：Accepted
- 日期：2026-08-29
- 关联：[DVM-95](../tasks/dexvm/DVM-95.md)

### 背景

以裸 handle 为 key 的 Android side map 曾靠枚举全部 key 为 GC root 避免悬挂。这会让 owner
永久存活，child edge 与真正 process root 混在一起，并在句柄复用时存在继承旧状态风险。

### 决定

宿主状态分为三类：

1. session/process root：Application、当前 Activity/Intent、main Looper、scheduler work 等，
   由 session root 明确枚举；
2. owner-attached state：只接受 `VmObjectRef` owner，注册具名 trace/sweep 和可选 clone；owner
   被标记时 trace child，owner 死亡时在 handle 回收前 sweep，禁止 state 反向保活 owner；
3. 非对象 identity：thread token、UiNodeId、resource id、路径和 process singleton，不放进
   owner table，按各自生命周期管理。

所有 owner state 最终经 `RegisterIntrinsicStateTable` 唯一入口进入 GC；无 sweep 不得注册，
声明含 guest reference 的 policy 无 trace hook 时构造失败。clone 默认关闭。

### 后果

session root 不再枚举 owner-map key。死亡 owner 的 child 仅由该 state 持有时可同步回收，句柄
复用不会看到旧状态。新增 API family 必须先选择上述所有权类别，不能用兼容 root 绕过。

<a id="adr-0030"></a>

## ADR-0030 · API 19 curated Boot DEX 与方法覆盖

- 状态：Accepted
- 日期：2026-09-04
- 关联：[DVM-99](../tasks/dexvm/DVM-99.md)
- Supersedes：[ADR-0017](dexvm.md#adr-0017) 中“平台类只有
  intrinsic、不执行 core/framework 字节码”的绝对边界。

### 背景

应用 DEX 已真实执行，但纯 Java 平台 API 仍逐类翻译为 C++ intrinsic，导致每个 title
持续暴露新的类库缺口。`EnumSet` 是 API 19 普通 Java 逻辑，继续手写会复制 libcore
语义且不能让缺口收敛。

### 决定

- 允许随运行时发布一个固定 API 19 `bootdex.jar`。它是从 pinned AOSP
  `android-4.4.4_r2.0.1` 提取的受审最小闭包，不是完整 `core.jar/framework.jar`。
- jar 中 `classes.dex` 的每个 class_def 必须全部注册为 bootstrap class；选择发生在制品
  构建期，运行时不维护类白名单，也不静默跳过 jar 内类。
- linker 依次注册 intrinsic、Boot DEX、application DEX。每个方法保留所属 `DexUnitId`，
  常量池和解析缓存按 unit 隔离。Boot DEX 与 intrinsic 同类时，以 DEX 提供类/字段/层级
  事实；签名精确匹配的 C++ method handler 作为 overlay，其余 DEX 方法解释执行。
- Boot DEX 中未被精确 overlay 的 native 方法拒绝装载。未进入制品的平台类仍按既有
  intrinsic/survey/明确失败规则处理。
- `EnumSet` 试点只增加通用 VM 原语 `Enum.getSharedConstants(Class)`：初始化 enum，按
  `ACC_ENUM` 静态字段读取活对象，以 ordinal 校验并排序，返回强根缓存的 typed array。
  `EnumSet/MiniEnumSet/HugeEnumSet` 语义直接执行 API 19 字节码。

### 后果

纯 Java libcore 能按依赖闭包逐批迁入，而 C++ 收敛到 VM 原语、宿主资源和 Android
service façade。该决定不授权加载完整 framework、Binder/system_server、Zygote、动态
classpath、多应用 namespace 或完整 ART/Dalvik；这些边界继续由 ADR-0001 约束。

<a id="adr-0031"></a>

## ADR-0031 · API 19 Unsafe 逻辑位置与原子性

- 状态：Accepted
- 日期：2026-09-05
- 关联：[DVM-101](../tasks/dexvm/DVM-101.md)、[ADR-0030](dexvm.md#adr-0030)

### 背景

精选 BootDex 的 AtomicInteger、AQS、LockSupport 直接调用 `sun.misc.Unsafe`。
API 19 参考是 libcore `libdvm/src/main/java/sun/misc/Unsafe.java` 与 Dalvik
`vm/native/sun_misc_Unsafe.cpp`，不是 libart 或现代 JDK 的 Unsafe。
OGPlay 的 Java 对象以强类型句柄、带标签槽和 JNI 数组存储表示，不能把 Dalvik 的
宿主内存指针运算照搬到这些存储上。

### 决定

- 类声明与 handler 归现有 `java_concurrent.cpp`；每 VM 的 `UnsafeRuntime` 负责位置解析
  和共享堆读写。它只持 `VmFieldId` 元数据，不持宿主地址、guest 对象或反射 wrapper。
- `objectFieldOffset` 返回从 `2^48` 起的稳定逻辑令牌，按首次查询登记、重复查询复用；
  令牌不是字节偏移，不支持字段地址算术。使用时校验已登记、接收者继承关系和精确字段类型。
  反射对象被回收不影响令牌；static 字段拒绝。令牌仅在所属 VM 内有效，不作跨 VM ABI。
- 数组使用逻辑 base 16 与 API 19 A32 元素 scale（1/2/4/8，引用 4）；只支持对齐的
  int/long/reference 元素操作。读写复用原 JNI primitive/object array store。
- int/long/reference 的 plain、volatile、ordered 和 CAS 全部在 `VmExecutionLock` 内。
  CAS 读写之间不分配、不回调、不停泊；锁的 acquire/release 保证跨 guest 线程可见性，
  plain/ordered 获得比最低要求更强的顺序。未来解除解释执行串行化时必须重审此决定。
- 引用比较按身份；写入保留 `SlotTag::ref` 或数组强边，GC/JNI/DEX 不建立影子状态。
  为保护唯一类型化堆，引用写入校验可赋值性，拒绝类型重解释及任意字节访问。
- `THE_ONE/theUnsafe` 为同一静态强根；`getUnsafe` 根据实际 interpreted caller 的 loader
  限制 application 调用，无 guest caller 的宿主入口按 AOSP null loader 处理。
- `park/unpark` 调用现有 Thread 方法，复用许可、monitor、interrupt、shutdown 与 Clock。
  absolute park 从注入 epoch Clock 转换为现有 Thread 的单调 deadline；缺任一时间源明确
  失败，计算溢出拒绝，不读取宿主时钟。relative nanos 保持 Thread 的向上取整与负数异常。
- `allocateInstance` 先完成真实类初始化，再分配零值字段对象，不执行实例构造器；
  primitive/array/interface/abstract 类拒绝，初始化异常保留原 guest identity。

### 边界

不提供宿主指针、任意内存分配/复制、static field offset、类型混淆、跨对象越界、
任意 class 定义或 ART/JDK 扩展。API 19 并没有这些额外方法；未发布方法继续按 VM
缺口机制记账失败。本项不代表 Executors 并行线程池或整款游戏已经验收。

<a id="adr-0032"></a>

## ADR-0032 · API 19 日期格式化数据与平台边界

- 状态：Accepted
- 日期：2026-09-05
- 关联：[DVM-102](../tasks/dexvm/DVM-102.md)
- 依赖：[ADR-0030](dexvm.md#adr-0030)、
  [ADR-0031](dexvm.md#adr-0031)

### 背景

API 19 `DateFormat`/`SimpleDateFormat` 的 Java 算法依赖 Calendar、数字格式器、LocaleData、
ICU 数据和若干 native formatter 资源。继续在 C++ 复制 pattern、日历或区域算法会形成第二套
Java 行为；直接使用宿主 ICU、locale 或时区数据库又会让结果随机器变化。BootDex 对未绑定
native fail closed，因此迁移前必须冻结全部 class/member/native 边界。

### 决定

#### 固定输入与精确闭包

- Java 输入继续使用 DVM-100 固定的 AOSP `android-4.4.4_r2.0.1 core.jar`，SHA-256 为
  `996557954e45f7192b187b2394259bc8aca6e3946652e03b99d837432f83d1ef`。
- `tools/bootdex/date-family-api19.json` 是迁移清单；
  `tools/bootdex/date_family_audit.py` 从固定 JAR 重组候选 DEX，并对全部对象类型、method_id、
  field_id、native 签名及分类摘要做机器检查。完整派生明细只写入
  `.local/dvm102-date-family-audit.json`。
- 37 个规划根实际需要 5 个附加 class_def：`java.text.Annotation`、`java.util.Grego`、
  `libcore.util.Objects`、`java.math.BigInteger` 和 `java.math.BigDecimal`，所以冻结候选为
  42 类。前 3 个是直接执行依赖；后 2 个用于 `NumberFormat.format(Object,...)` 的真实
  `instanceof` 解析，不能用空类替代。
- `BigInt`、`BigDecimal$1`、`MathContext` 及其算法 helper 仍为分支触达的 deferred 类；
  本阶段不执行大数算法。触达时必须明确失败，后续只有按真实闭包选入后才可宣称支持。
- 当前发行 `bootdex.jar` 不在本 WU 扩大。B～F 完成依赖、native 和 overlay 后，才按
  DVM-100 的 load-all 规则一次发布可装载组合。

#### ICU 输入与资源所有权

- 唯一区域 backend 固定为 AOSP `platform/external/icu4c` 的
  `android-4.4.4_r2.0.1`：tag object
  `002a1cac36d13d912df955249cfd3f02ae4c4b8a`，commit
  `18668f3b015a110275f5cc9a8722b2f65f3333bf`，tree
  `9cfe3adead99244400b80feed85736642348540e`。Gitiles commit archive SHA-256 为
  `8c2a2a2305bbecf17da14fa42fc6222882814d40645253b2183d213e8ac560ae`。
- 固定版本为 ICU `51.1.0.1`、数据版本 `51.1`；发行必须携带并校验该提交的
  `license.html` 和 `unicode-license.txt`。B 阶段另行验证 macOS、Windows 和 Linux 的现代
  工具链构建及 staging，不得回退到宿主系统 ICU。
- 47 个 native 签名全部登记：日期闭包必需的 21 个必须接真实 backend；大数 double/
  digit-list 与本批次外 ICU 查询等 26 个绑定统一的显式未实现路径。任何新增或漏失签名使
  audit 失败。
- native formatter 只由 per-VM runtime 持有。Java `long` 保存受检逻辑令牌，禁止保存宿主
  指针；clone 产生独立资源，Java 实例重复 close 幂等，native 边界的非法/失效
  令牌明确失败，GC sweep 与 VM teardown
  都必须释放资源，不能依赖 `finalize`。

#### Locale、TimeZone 与 VM overlay

- `Locale` 暂由 intrinsic 拥有，但必须补齐真实 API 19 字段和本闭包命中的构造、ROOT、US、
  ENGLISH、default、language/country/script/variant、extension、toString、equals、hashCode、
  clone。首批输入集合固定为 ROOT、`en`、`en_US`、`zh`；默认 Locale 仍由会话注入。集合外
  数据不得静默映射到英语。
- 时区首批只提供 GMT、UTC 和 AOSP Java 代码解析的固定 offset。保留 4 个精确平台 overlay：
  `TimeZone.getDefault()`、两个 `getAvailableIDs`、`getTimeZone(String)`。overlay 只注入默认值
  和窄数据库结果；自定义 GMT pattern 继续调用 DEX 的 `getCustomTimeZone` 和真实
  `SimpleTimeZone` 构造器，不复制解析算法。
- 未交付数据库时，GMT/UTC/固定 offset 以外的具名时区查询明确报告“数据库不可用”；只有
  backend 已能确认 ID 不存在时，才执行 AOSP 的 GMT fallback。`ZoneInfoDB`、
  `TimezoneGetter` 和 `/etc/timezone` 的 `IoUtils` 路径不读取宿主事实。
- VM 另只保留 `Object.clone()` 与 `System.currentTimeMillis()` overlay。后者必须使用统一
  Clock；无注入时明确失败。`Date` 切换时对象流特殊段从旧私有字段 `millis` 同步改为 pinned
  DEX 的 `milliseconds`，wire bytes 与重复引用身份不变。

### 后果

日期、日历、数字和 pattern 语义最终由 pinned AOSP 字节码执行，C++ 只承载 VM 原语、固定
数据和 native 资源。该决定不引入完整 ICU API、完整大数、具名时区数据库、历史 DST、宿主
区域设置或完整 `java.text` 序列化；这些能力仍按真实命中单独扩展。

### 实施补充（2026-09-05，只追加）

固定 ICU common/i18n 由 CMake 直接构建并静态链接；数据经原哈希校验后嵌入，禁用
文件 IO 与动态数据加载。Java 包装、native 签名及 deferred 清单不变。TimeZone 默认值
由 CoreIntrinsicServices.default_timezone 注入（默认 GMT），与 DEX defaultTimeZone 及
Java setDefault 共用状态；固定数据只用于确认具名 ID 是否存在，不扩展具名时区计算能力。
macOS 已验证；原先 B 阶段“三平台验证”要求的 Windows/Linux 部分仍待对应环境验证。

### 工具整合补充（2026-09-06，只追加）

迁移完成后，原 `date-family-api19.json` 清单并入 `tools/bootdex/api19.json` 的
`date_family_audit`，原 `date_family_audit.py` 并入 `tools/bootdex/build_bootdex.py audit`；
两份独立文件删除，以上旧路径由此替代。审计复用 builder 的 API level 与 core.jar 固定输入，
日期 BootDex 子集必须包含于主类清单，外部依赖分类不得与主清单冲突。原有闭包、native、
overlay 和指标摘要检查保持，统一进入 builder 自测；派生报告路径不变。

<a id="adr-0033"></a>

## ADR-0033 · 集合 BootDex 所有权与弱引用边界

- 状态：Accepted
- 日期：2026-09-06
- 关联：[DVM-103](../tasks/dexvm/DVM-103.md)、[ADR-0030](#adr-0030)
- Supersedes：ADR-0029 中 collections 作为宿主 owner-attached table 的实现选择；其他状态表规则不变。

### 决定

- Collection/List/Map 及容器、视图、算法改由 pinned API 19 BootDex 拥有，删除
  CollectionRuntime 和相关 intrinsic。Observable/Random/ThreadLocal 一并迁入；recipe
  仍统一维护于 api19.json，日期审计固定 42 类样本，仅更新依赖归属分类。
- intrinsic 引用的 BootDex 父类/接口延迟至 Link 解析；Miranda 槽只参与虚分派，不扩充
  reflection own members。实例字段与数组是集合唯一存储，GC/clone 使用通用对象路径。
- WeakReference 的 referent 不构成强边；STW 清扫前清空失效目标，直接写 API 19
  ReferenceQueue 字段并通知 wait-set。执行锁内不调用 guest 代码或等待 guest monitor。
  SoftReference/PhantomReference 和宿主 GC 策略不在本轮范围。
- Thread.localValues 供 DEX ThreadLocal 使用；atomic getAndAdd 遵循 Java 位宽回绕。
  Runtime.availableProcessors 返回执行锁对应的单 guest 执行通道事实 1；nanoTime 由统一
  Clock 毫秒值转换，未注入时明确失败，不引入宿主时钟。
- java.io 序列化接口归 BootDex，实际流仍由 IoRuntime 拥有。Externalizable 支持显式 UID
  的协议 2：公共无参构造、真实回调、共享 handle/递归预算、未消费尾部跳过及异常原身份。
  协议 1、默认 UID、数组与任意私有 writeObject/readObject 明确失败。

### 验证与边界

390 个 BootDex class_def 全部链接，集合自身方法无 intrinsic overlay；双后端定向验证
容器、视图、迭代器、clone、弱键 GC、对象流及已有日期行为。类迁移不表示所有 XML、
序列化或并发长尾均已支持；游戏首错推进只记为 reached-fault，不替代 Scenario gate。

<a id="adr-0034"></a>

## ADR-0034 · 普通流与后续 Luni 家族的 BootDex 所有权

- 状态：Accepted
- 日期：2026-09-06
- 关联：[DVM-104](../tasks/dexvm/DVM-104.md)、[ADR-0030](#adr-0030)、[ADR-0033](#adr-0033)
- Supersedes：ADR-0033 中普通流仍由 IoRuntime 拥有的实现选择；对象协议和资源边界保留。

### 决定

- 一份 api19.json 选类、一份 build_bootdex.py 构建/审计；本 WU 新增 131 类，合计 521 类。
  工具/事件、同步器/普通 atomic、Choice/MessageFormat、内存/包装 IO、X500 名字/DER
  和选定 key spec 由 pinned API 19 字节码拥有，不保留普通算法 intrinsic 副本。
- 普通流状态只在 guest 字段/数组中；删除 IoRuntime wrapper-adoption 接口。对象流协议
  通过 source/sink guest 虚方法通信并 trace 强边；资源流和 ZIP 保留既有有界资源实现。
  AssetManager 调用真实 ByteArrayInputStream 构造器。InputStreamReader 使用固定 ICU
  增量解码器及 Reader.lock guest monitor，FileReader 委托该适配器。
- Charset 六个标准编码的值语义、String 编解码与 Locale 大小写复用固定 ICU 51。
  X500 仅包括名字/ASN.1/DER；其标签键触达 BigInteger/BigInt，因此补充 NativeBN 的
  11 个最多 64 位 magnitude 原语，余下 24 个 native 明确失败。每 VM 逻辑令牌受检，
  owner GC/teardown 释放资源；不引入完整 crypto provider、证书验证或大数 backend。
- 同步器复用 AQS/Condition/Unsafe、真实 guest 线程与统一 Clock；AtomicLong 只保留
  VMSupportsCS8 native。long 2addr shift 的 distance 按 int 单槽预检。
- nested guest 调用跨 intrinsic 返回原 throwable，VmJavaThrow 可携带原引用；不通过
  descriptor/message 重建异常，以维持 guest catch、cause 与异常身份。

### 验证与边界

双后端定向验证迁入行为、真实线程阻塞/释放/中断、零超时/barrier break/reset、GC、
编码替换与流包装；所有 BootDex 类全链接，迁入普通方法无 intrinsic overlay。
完整数字 formatter、对象流 custom hooks/数组/默认 UID、Charset provider API、
FieldUpdater/ForkJoin 和证书验证不因类迁移宣称可用；PvZ 首错推进只记 reached-fault。

<a id="adr-0035"></a>

## ADR-0035 · Cipher AES 使用 BootDex Conscrypt 与 guest OpenSSL

- 状态：Accepted
- 日期：2026-09-06
- 关联：[DVM-105](../tasks/dexvm/DVM-105.md)、[ADR-0034](#adr-0034)

### 决定

- Cipher/CipherSpi、JCA 查找、密钥/IV 值和 Conscrypt AES 普通方法由 API 19 DEX 执行。
  通过显式 GuestNativeStatic 声明、正常 JNI native frame、精简 ARM JNI 桥调用 guest
  libcrypto。禁止宿主 AES 替代；不引入完整 libjavacrypto、TLS、证书 provider 或 RSA。
- 首批支持 128/192/256 位 AES：ECB/CBC 的 NoPadding、PKCS5Padding，以及 CTR/NoPadding。
  AndroidOpenSSL 只注册该闭集；裸 AES 默认服务限制为 ECB，避免 JCA fallback 扩大范围。
  OS entropy 作为独立 SecureRandomSpi 服务注入；自设 seed 明确失败。
- Context 使用逻辑令牌，guest registry/per-context mutex 与引用计数保护生命周期。
  Java owner sweep 只排队，GC 后通过 JNI 释放，teardown 在 guest process 停止前清理；
  保存原始 IV 以确保 CTR 的 doFinal/reset 恢复初始状态。JNI 保留真实异常类型和消息。
- 用户明确允许开发期使用设备制品：暂用已核对哈希的 MoKee API 19 ARM libcrypto.so
  和 conscrypt.jar 选类，独立记录临时来源；原 pinned core.jar 和五库不替换。
  这是 data/android 通常仅接受源构建制品规则的本次明确例外；正式发行前自行构建替换。
  构建、选类、哈希和校验继续整合在 build_bootdex.py、api19.json 与 payload manifest。

### 验证与边界

双解释后端运行 NIST AES 已知答案、分段/原位输出、填充/短缓冲/非法参数、IV 重置、
真实随机 IV、两个 guest 线程和 GC/teardown。566 类全链接及普通 Cipher 方法所有权受检。
完整 JCA、AES AlgorithmParameters 编码、wrap/unwrap 长尾、RSA、证书和 TLS 不在验收范围。
真机当前已断开，未宣称完成手机对照或任何游戏 gate；本次验证平台为 macOS。

<a id="adr-0036"></a>

## ADR-0036 · Certificate 使用 Harmony Java 与 guest OpenSSL 验签

- 状态：Accepted
- 日期：2026-09-06
- 关联：[DVM-106](../tasks/dexvm/DVM-106.md)、[ADR-0035](#adr-0035)
- Supersedes：ADR-0034 对 NativeBN 仅 64 位值的限制、ADR-0035 对证书尚未接入的范围描述。

### 决定

- Certificate/X509Certificate/CertificateFactory、Harmony ASN.1/X.509/PKCS7 和证书路径
  编解码从 pinned core.jar 选入 BootDex。复用既有 X500 和普通流，不引入宿主证书 parser，
  不 overlay 普通 verify。javax.security.cert 旧入口同样复用 Java 委托。
- Security 注册原版 DRLCertFactory。AndroidOpenSSL 以精简 SignatureSpi 注册 RSA
  PKCS#1 v1.5/ECDSA 与 SHA1/224/256/384/512 的 10 个组合及 OID 别名；SPI 保存普通
  guest 公钥编码快照和有界消息流，经同一 ARM JNI 桥调用真实 EVP 验签。
  不导入完整 libjavacrypto、TLS 和 Conscrypt X509/BIO native 表面。
- NativeBN 仅扩展值编解码至 17 个原语，输入最多 1 MiB。大数算术继续明确失败。
  OpenSSL key/digest context 在单次 native 调用内释放；共用库安装真实 bionic pthread
  锁和 identity 回调。Signature 累计输入同样限于 1 MiB，超限抛 SignatureException。
- JNI object arrays 对 synthetic 类型通过锁外显式回调调用 DexVM 类型关系；bridge
  持有同一执行锁，不按身份不等误拒绝嵌套数组，也不绕过不兼容写入检查。
- 保留同一 api19.json、build_bootdex.py、cipher.c 和 manifest.libraries。
  设备临时制品授权及后续自行构建替换要求延续 ADR-0035，bootdex.jar 继续不提交。

### 验证与边界

双后端覆盖 DER/PEM、长序列号、名字/有效期/扩展、公钥编码、旧 javax API、证书链验签、
PkiPath/PKCS7 编解码、10 种摘要签名、篡改/错误公钥/未知算法/超限、GC 后复用；独立
OpenSSL fixture 是验签 oracle。PKCS7 仅保证证书集合，PkiPath 保持路径顺序。

签名验证成功不表示证书受信任；PKIX 验证/信任锚、系统 CA、撤销网络、TLS、签名生成、
DSA/PSS/EdDSA 与 RSA Cipher 不包含。公钥使用 Harmony 编码型 fallback，不声明完整
RSA/EC KeyFactory 或数学参数接口。未做真机、Windows/Linux 或游戏 gate 验收。

<a id="adr-0037"></a>

## ADR-0037 · guest 生产源码与 crypto 制品来源

- 状态：Accepted
- 日期：2026-09-07
- 关联：[DVM-106](../tasks/dexvm/DVM-106.md)
- Supersedes：ADR-0035/0036 中 guest JNI 源码位于 tools 及临时分发 ROM libcrypto 的条款。

### 决定

用户要求将 guest 生产代码移出工具目录，并撤回 ROM 提取的 libcrypto.so。
自有 guest 源码统一归 src/guest；AES/验签桥位于 src/guest/crypto/crypto_jni.c，
仅交叉编译至 ARM guest，不加入宿主 runtime 目标。构建编排保留 tools/bootdex/build_bootdex.py，
库名 libogplay_cipher.so 和 data/android/19/lib 产物目录不变。

build-cipher 只消费显式准备的 data/android/19/lib/libcrypto.so，不从 .local 设备目录恢复。
现有 manifest 中来源/哈希是此前验收记录，完整 payload 校验在缺库时继续明确失败。
待用户自行构建后再更新来源约束、哈希并重跑 crypto/payload 验收；不将缺库伪装为可发行。

### 2026-09-08 实现记录

`libcrypto.so` 已替换为 AOSP `platform/external/openssl` 的
`android-4.4.4_r2.0.1` revision `dd1da36b0baa39942f0aef42c4712ef0ad628a83`
在 `aosp_arm-user` 下执行 `make -B -j8 libcrypto` 的 ARM 产物。payload manifest、
source-manifest、OpenSSL NOTICE 和校验器已切换到该来源；设备库不再是 libcrypto 的发行输入。

<a id="adr-0038"></a>

## ADR-0038 · framework 值类归 BootDex，Activity 保持窄平台边界

- 状态：Accepted
- 日期：2026-09-07
- 关联：[DVM-107](../tasks/dexvm/DVM-107.md)
- Supersedes：ADR-0029 中 SparseArray 使用 intrinsic state table 的归属。

### 决定

Pair、SparseArray/LongSparseArray/SparseIntArray/SparseBooleanArray/SparseLongArray、
ContainerHelpers、ComponentName 与 Parcelable 三个接口从 pinned framework.jar 精确选入。
删除原 Pair/Sparse/Parcelable 声明与 Sparse 侧表；对象字段和数组成为唯一状态，普通算法
不加 overlay。ComponentName 打印依赖的 PrintWriter 从 pinned core.jar 一并选入。

API 19 构建将 com.* 放入 framework2.jar；当前本地只有 core.jar/framework.jar。
ArrayUtils 使用本地原始 frameworks/base Java 源码，以 JDK 17 的 --release 7 -g:none
和原版 AOSP dx 编译。构建器固定 Java 文件及 dx/libcore-dex 源码树哈希，校验失败即停止；
manifest 区分 jar 来源与源码编译来源，两次独立构建的 DEX 必须一致。
配方和编排继续归 api19.json/build_bootdex.py，不新增审计脚本或配置副本。

Activity/Intent/Parcel 继续保留窄 intrinsic。Activity 的 mComponent/mIntent 和 Intent 的
mComponent 是普通强引用字段，启动和切换在 onCreate 前附加身份；根启动保留 manifest
alias 名而非实例 Java 类名。getLocalClassName 按包名加点边界截取，getPreferences 虚派
查询本地类名；setIntent 或原 Intent 改换 component 不改写已附加的 Activity component。
同进程显式 startActivity 读取 ComponentName，跨包/隐式解析仍明确失败。

StringBuilder/StringBuffer 仅补 ComponentName 所需的 CharSequence 区间 append 原语，
采用 UTF-16 索引并虚派 length/charAt，支持 null 和自追加，越界不修改原 buffer。
不迁入完整 framework、Activity/Context/Intent/Parcel、Binder 或系统服务；不扩展为
manifest 通用组件解析器，非根 alias 的启动解析仍不在本次范围。

<a id="adr-0039"></a>

## ADR-0039 · UUID 与摘要归 BootDex，mutable native token 归普通字段

- 状态：Accepted
- 日期：2026-09-07
- 关联：[DVM-108](../tasks/dexvm/DVM-108.md)

### 决定

UUID/JCA MessageDigest/Spi、摘要流与 Conscrypt 的 MD5/SHA1/SHA256/SHA384/SHA512 从
pinned core.jar 与既有临时 conscrypt.jar 精确选入。删除 legacy JNI 固定 UUID handler，
Java/JNI 共用原版 UUID。randomUUID 复用已接通的 OS CSPRNG，nameUUIDFromBytes 使用 MD5。
AndroidOpenSSL 仅登记原版摘要服务/别名/OID；7 个 NativeCrypto JNI 入口使用同一 guest
adapter 的 EVP 实现。update 使用最大 64 KiB scratch 分块，不设消息累计长度上限。

原版 OpenSSLMessageDigestJDK 持有 mutable ctx:J；它在初始化、clone、reset 和 final 中
赋值。若另建 owner→token 副本或覆盖普通 Java 方法，会形成双重状态。因此增加通用
字段资源登记：实例 long 字段 + static (J)V cleanup，GC 直接读对象字段，按 cleanup/token
聚合所有 owner，最后 owner 死亡才在 sweep 后清理；teardown 清零字段再清理。
浅 clone 和跨字段别名不提前释放，清理失败保留队列。native 使用单调逻辑 token 与
registry/per-context mutex，不把 EVP 指针暴露给 Java。

对象流不跳过私有 readObject 后返回无效对象：除既有 Date 特例和 Externalizable 协议外，
此类反序列化明确 InvalidClassException。暂不扩展通用对象流回调，UUID 序列化可写，读取拒绝。
原版 OpenSSLProvider 无 SHA-224 MessageDigest；现有 SHA224 验签不因此扩展为摘要服务。
HMAC/Mac、SHA-3、其他 provider 或 TLS 未纳入。配方、构建工具和 ADR 继续合并维护。

<a id="adr-0040"></a>

## ADR-0040 · 对象序列化协议归 BootDex，VM 仅提供构造与元数据原语

- 状态：Accepted
- 日期：2026-09-07
- 关联：[DVM-109](../tasks/dexvm/DVM-109.md)

### 决定

将 ObjectInputStream/ObjectOutputStream、ObjectStreamClass、字段辅助类、异常及直接
依赖从 pinned core.jar 精确选入，新增 28 类，共 818 类。协议、共享 handle、字段读写、
私有回调与默认 serialVersionUID 算法执行原版 Java；删除 IoRuntime/C++ 对象流协议副本。
本决定替代 ADR-0039 对私有 readObject 和 UUID 读取的暂时拒绝，UUID transient 缓存
由自己的 readObject 恢复，不增加 UUID 或 Date 专用处理。默认 UID 的 SHA 复用 guest EVP。

ObjectStreamClass 六个 native 对照 libcore/luni/src/main/native/java_io_ObjectStreamClass.cpp
和 Dalvik JNI：签名读取唯一反射元数据，构造器采用 per-VM 受检逻辑 token；分配实际子类，
只运行 Java 选定的无参构造器，异常保持身份，不套 InvocationTargetException。
constructor lookup/hasClinit 触发初始化，hasClinit 依 AOSP 查找父类并清除查找失败；
静态初始化非 Error 包装 EIIE 并保留 cause，Error 原样传播，后续访问 NCDFE。

VMStack.getClasses 读取执行栈供原版 loader 查询；SoftReference 普通 GC 保留 referent，
分配压力下清除仅软可达对象并入队。String.intern 以弱 canonical 表保持首次对象身份，
DEX 常量提升为强根，清扫移除弱条目后才复用句柄。源/目标、描述符与 handle 是普通对象图。
Modifier/Void/Proxy 的 Java 部分同批迁入；getFieldL 是原版未使用的遗留声明，Proxy 两个
生成 native 和 VMStack 其余四个 native 均显式未实现，调用时记账失败。

不引入完整 Dalvik、动态代理生成或自定义 loader，也不宣称宿主侧表对象可以完整持久化。
沿用统一 recipe/build_bootdex.py 和主题 ADR，不新增独立工具配置或 ADR 文件。

<a id="adr-0041"></a>

## ADR-0041 · 定时执行器与 FutureTask 执行原版 Java

- 状态：Accepted
- 日期：2026-09-08
- 关联：[DVM-110](../tasks/dexvm/DVM-110.md)

### 决定

ScheduledThreadPoolExecutor 依赖 FutureTask 的可覆盖方法、runAndReset、等待者和取消状态。
原有 intrinsic FutureTask 及合成串行执行器无法承接该协议，因此迁入原版 FutureTask、
执行器接口/异常、普通 Executors 工厂包装类、completion service 和拒绝策略，共新增
26 类至 844 类。删除对应普通 handler，不扩展 C++ 调度器。

任务、队列、周期、结果和等待者只在 Java 字段中保存，GC 沿普通对象图追踪。定时队列使用
统一 Clock 的 nanoTime，阻塞使用 AQS/Unsafe/Thread park；一个 guest worker 对应一个
宿主线程，执行仍由 VmExecutionLock 串行。固定频率按上次计划截止时间续排，固定延迟按
本次结束时间续排；关闭、取消、中断和异常处理均由 pinned API19 代码决定。

补齐通用 VM 规则：接口数组可协变到 Object[]，primitive class 只与自身可赋值；
threaded intrinsic invoke 保留已有异常身份，不能重建 FutureTask 结果里的 throwable。
不选入 privileged 工厂/动态安全上下文，不宣称高争用或硬实时精度；Clock 保持现有毫秒
精度与帧泵驱动，不引入新的宿主时间源。继续使用统一配方与主题 ADR。

<a id="adr-0042"></a>

## ADR-0042 · 服务查询的有界无匹配结果

- 状态：Accepted
- 日期：2026-09-08
- 关联：[DVM-112](../tasks/dexvm/DVM-112.md)

### 决定

ServiceConnection 是两个抽象回调组成的纯接口，选入 BootDex；其参数类型沿用既有
ComponentName 与 IBinder 类型身份，不引入 Binder transport 或服务生命周期。

PackageManager.resolveService 的查无结果必须来自事实，而非中性占位。loader 保存
当前 APK 的 service 名称/enabled/过滤器 action/category/data 存在标记及 application
enabled，由 AndroidAppProcess 注入唯一 context；没有注入时保持未知。运行环境只安装
当前 APK，没有外部安装包服务目录。flags=0 且只有 action 的查询可排除 disabled 与 action
不匹配的服务；无候选按 API19 返回 null。潜在匹配、未解析的 data 条件、未知 flags 或
其他查询形态均记账并明确抛 Java 异常，不把未实现的解析能力当作不存在。

### 边界

当前闭包不物化 ResolveInfo/ServiceInfo，不支持其返回类型反射、正匹配解析、服务绑定、
支付或完整 PackageManager。服务确实可匹配时须另开 WU 扩展元数据和行为，禁止返回
虚假的服务对象、发出连接成功回调或加入游戏包名/action 特判。

<a id="adr-0043"></a>
## ADR-0043 · Throwable 协议归 BootDex，栈捕获归 VM

日期：2026-09-08；状态：采用。Supersedes：ADR-0029 中 Throwable 消息/cause 由宿主
状态表持有的约定；其他资源边界保持。

### 决定

- Throwable、StackTraceElement、50 类 java.lang 异常家族及 IOException/
  InvocationTargetException 的构造、消息、cause/suppressed、打印和序列化执行 API 19 Java。
  ordinary 字段与数组是唯一 Java 状态，移除普通方法 overlay。
- nativeFillInStackTrace 保存当前 execution 的方法 ID/dex PC 对到普通 int[]，按 Dalvik
  Exception.cpp 去除顶部 Throwable 实现帧。nativeGetStackTrace 展开为普通 StackTraceElement[]；
  禁止 guest 保存宿主指针。尚未解码源文件/行号时使用 Unknown Source，不能把 dex PC 当行号。
- VM 生成隐式异常时调用 BootDex Throwable 构造器；保留 32 个内部帧与每次最多 64 KiB
  应急分配，作用域结束恢复。限制用于错误报告，不扩大应用通常的栈/堆预算；递归构造失败
  明确终止。ThrowableState 仅保留原有故障诊断栈，不再保存消息/cause 或 Java 强引用。
- 残留平台异常构造器调用同一基类初始化入口。PrintStream 仍为结构化输出边界，补齐
  Appendable；空追加不生成日志。PrintWriter/StringWriter 及 Throwable 打印算法来自 Java。

### 验证与边界

双后端验证构造时捕获、rethrow 不改 Java 栈、独立数组、suppressed/cause、重复帧缩略、
GC、禁用 suppression/栈、错误参数；对象流在真实 guest SHA 后端验证普通异常图往返。
不承诺 Android 所有带宿主资源对象可序列化，不引入系统服务或 native 宿主栈伪装。
KitKat 原版循环 cause 打印行为与其余 Java 递归一样受 VM 栈/执行预算约束。

<a id="adr-0044"></a>
## ADR-0044 · BackupManager 保留 Java 无服务路径

日期：2026-09-08；状态：采用。

### 决定

BackupManager/RestoreObserver 从固定 framework.jar 选入 BootDex。OGPlay 进程不提供
Android 备份服务，因此只覆盖私有 checkServiceBinder() 平台查询入口，保持 sService
为空并记账 dexvm.backup_service；不引入 ServiceManager、Binder 或 system_server。
构造器与所有公开方法执行原版 Java：dataChanged 不排队，requestRestore 返回 -1，
beginRestoreSession 返回 null。通知返回不表示备份成功；恢复失败不调用观察者。
发现非空 sService 时明确抛 UnsupportedOperationException，不覆盖未知服务状态。

### 验证与边界

双后端验证 Java 字段/GC、实例与静态通知、失败码、无回调以及意外服务注入。
只承诺无服务执行分支，不发布 IBackupManager/RestoreSession/RestoreSet 的服务实现
或这些未选入类型的反射能力。远程备份、恢复和传输器仍未实现。

<a id="adr-0045"></a>
## ADR-0045 · Bundle 与 Intent extras 归 Java 对象图

日期：2026-09-08；状态：采用。Supersedes：既有 Bundle/Intent extra 宿主类型分表存储。

Bundle/CREATOR、ArrayMap/MapCollections 及内部类直接选入 BootDex。Bundle 的映射、
装箱、类型检查和复制由 Java 持有，Intent.mExtras 为唯一 extra 来源；Serializable
进程内传递保持对象身份，不执行无意义的对象流往返。getExtras 返回原版浅副本，
映射独立、键和值引用共享；GC 由普通字段/数组追踪。ApplicationInfo 也使用同一 Bundle。

Parcel 保持既有进程内 typed atom 契约，Bundle 只覆盖 writeToParcel/readFromParcel
两个传输入口：写入时创建 Java Bundle 浅副本，读取时再次复制映射，Parcel 只保留
该副本的 guest 强引用。此模式不编码 Android 字节协议，不声称跨进程、Binder 或
宿主文件描述符传输；尚未接通的原版 parcelled 分支仍明确失败。无源数据时不得伪造
成功读取。与原先 Bundle typed snapshot 相同，嵌套对象身份共享，深序列化不在范围。

双后端验证覆盖/null、Serializable 与普通 boxed/string 的交叉读取、浅副本、
ArrayMap 碰撞和活视图、Intent/Parcel GC 边、CREATOR 数组实际类型以及旧 API 回归。

<a id="adr-0046"></a>
## ADR-0046 · Java 布局参数与 UI 布局输入分工

日期：2026-09-08；状态：采用。Supersedes：LayoutParams 由宿主 ui_layout_params
侧表保存、setMargins/addRule 自动发布布局变更的约定。

五类通用/边距/Frame/Linear/Relative 布局参数进入 BootDex，普通字段与数组是唯一
Java 参数状态。View 保存原参数引用；setLayoutParams、add/updateViewLayout、
requestLayout 及 dirty geometry 查询将这些字段转换为 UiTree 的布局输入快照。
UiTree 仍唯一拥有 hierarchy、dirty 与测量/布局结果，不保存 guest 引用，也不执行 Java。
去掉参数宿主副本；直接 Java 字段写入和 setMargins/addRule 本身不触发 traversal，
调用方按 Android 契约请求布局。刷新跨 Java 回调必须保护引用，并重查当前绑定。

当前 UI 为 LTR，start/end 的解析由原版参数类完成；仅发布渲染器能消费的规则，
baseline、alignWithParent、动画等未实现语义明确记账并失败，不按游戏特判。
RelativeLayout gravity 在原有 sibling/parent 解析之后整体平移子节点，保持相对位置；
布局参数算法在 Java，真实显示行为继续归既有 UI 引擎，不迁入整个 framework View 系统。

<a id="adr-0047"></a>
## ADR-0047 · Typeface Java 与字体后端描述符

2026-09-08，接受，DVM-120。

原版 Typeface 的常量、缓存、equals/hash、样式查询与工厂应运行 Java；不再由
intrinsic 创建没有状态的 Typeface 占位对象。其 native 边界返回有限不可变描述符，
编码 family 与样式，既不是 guest 指针，也不拥有待释放的分配。

现有内置 bitmap font 提供四种真实可测量/绘制的样式；family 维持逻辑身份，
字形使用该后端的回退字体。普通 TextView 引用归 Java 字段，渲染样式归 UiNode；
不把字体描述符扩展成 Skia 对象，也不伪称支持外部字体。文件/asset 加载明确失败记账。
Java nativeUnref 验证描述符后没有分配需要释放。Canvas/Paint 的完整字体 API 不在本次范围。

<a id="adr-0048"></a>
## ADR-0048 · 文本外观的 Java 值对象与有界样式事实

2026-09-08，接受，DVM-121。

文本外观依赖资源含义，不能把 attr id 硬当作它所引用的 style，也不能补一个空 handler。
ARSC reader 保留原始 bag parent/items，Manifest 保留主题 id，session 负责 Activity
实例前后的事实传递；资源解析、UI 值应用仍由 integration 的受检边界完成。

ColorStateList、StateSet 与 R.attr 元数据归 BootDex，不建立 C++ 颜色列表副本。
TextView 样式解析只消费相关属性，支持 APK parent/reference/attribute 链；平台主题仅
投影本次登记的 hint/highlight/link 属性。已有 TypedValue Java 算法完成尺寸换算，
已有 Typeface Java 工厂完成字体选择。常规色进入真实 renderer，其他颜色保持可查询字段。

不迁入完整 TextView/Resources/AssetManager 或 framework-res；未登记的 framework 样式、
selector 与 stateful 渲染、完整 theme 和文本特效不得伪造成功。所有引用链有界，
可预判的类型/特性错误在发布 UI 外观前失败，guest override 自身异常保留原语义。

<a id="adr-0049"></a>
## ADR-0049 · ICU 归 API 19 guest 并统一 JNI 桥

2026-09-08，接受，DVM-122。Supersedes：ADR-0033 的 host ICU 构建、嵌入数据与
IcuFormatterRuntime，以及 DVM-105 的独立 `libogplay_cipher.so`。

BootDex 审计所需 ICU native 必须执行 guest `libicuuc.so`/`libicui18n.so`，数据固定挂载为
`/system/usr/icu/icudt51l.dat`。桥只声明并调用 ICU 51 C ABI，避免 NDK libc++ 与 API 19
STLport C++ ABI 混用。formatter token 与 ICU 对象留在 guest；Java owner 清扫和 VM teardown
经 close 回收。宿主不得下载、编译、链接 ICU，也不得嵌入 ICU 数据。

crypto 与 ICU 保持独立源码模块，但由固定 NDK r25c ARMv7 API 19 工具链生成唯一
`libogplay_jni.so` 和 JNI_OnLoad。构建器校验全部输入哈希、两次输出一致、ELF ABI、SONAME、
DT_NEEDED；payload validator 对 manifest 事实复核。禁止恢复 `libogplay_cipher.so`，禁止引入
完整 libjavacore。只有 `api19.json` required_backend 可注册；其他 native 必须保持明确失败。

完整具名时区/历史 DST、大数及 double/digit-list formatter、完整 ICU 查询不随迁移扩大。
标准六字符集解码和受限 Locale 大小写可保留宿主有界实现，但不得依赖 host ICU。

<a id="adr-0050"></a>
## ADR-0050 · guest ICU 桥按 pinned libcore 语义收紧

2026-09-08，接受，DVM-122 补验。整数 formatter 继续只用 ICU 51 C ABI：按字段重复格式化并在
integer 区间识别所有 grouping symbol，禁止调用 ICU C++ `FieldPositionIterator`。LocaleData 的
日期 pattern、相对日、country 与货币从 common-data resource/C API 取得，不在宿主或 JNI 手写区域表。

`parse` 在既有非 BigDecimal 边界内同时取得 ICU int64/double 结果并返回 API 19 Number 类型；
复杂 Unicode case mapping 经 BootDex ICU guest native 调用 `u_strToLower/Upper`。common data 在
`udata_setCommonData` 后禁用文件访问并立即初始化，卸载顺序固定为关闭对象、`u_cleanup`、释放数据。
具名时区库仍不交付；任何非 GMT/UTC/custom-offset ID 明确失败，禁止静默降级为 GMT。

<a id="adr-0051"></a>
## ADR-0051 · 有界控件默认样式与 compound drawable

2026-09-08，接受，DVM-121。延续 ADR-0048，不引入完整 framework-res 或 View framework。

三参控件构造经调用方 Context 的主题、APK parent/alias 确认默认样式，再使用其 Resources
解析尺寸；仅发布登记的 legacy Widget.Button(.Small)/TextAppearance.Small.Inverse 投影。
未知主题、APK widget style 或文本外观覆盖必须明确失败，不能忽略并套用固定默认值。
投影只承诺当前 enabled 文本、居中和 clickable；保留既有有界 Button 外观，9-patch 背景
记入可查询缺口，状态色/完整字体与主题外观不随构造器支持扩大。

compound drawable 的 Java API 形状归 TextView；integration 解析资源并在四槽全部成功后
一次发布，UiTree 保存唯一尺寸/资源事实，runtime/ui 完成 measure/raster。失败不得留下
部分 mutation；控件子类继承同一 API，禁止专属 Button 副本或游戏分支。guest onLayout、
完整滚动与推广网络内容不属于初始化验收，后续按真实触发补齐。

<a id="adr-0052"></a>
## ADR-0052 · PreferenceManager Java 入口与偏好编辑提交边界

2026-09-08，接受，DVM-121。PreferenceManager 与 SharedPreferences 三接口使用
API 19 BootDex 原版声明/Java；具体 Impl 保留 integration intrinsic，复用唯一
preferences_xml/VFS，不引入 android.app.SharedPreferencesImpl、QueuedWork 或系统服务。

每次 edit 创建独立标量缓冲，commit/apply 先执行 clear 再合并修改、一次发布到已提交
store；getAll 返回独立快照。Editor 缓冲随 guest owner GC 清理。apply 使用 API 19
接口文档允许的同步 commit 兼容方式，不承诺异步写队列。string-set 与变更监听暂不
支持，明确失败并记账；默认文件名及 Context 虚派交给 BootDex Java。

<a id="adr-0053"></a>
## ADR-0053 · API 19 guest 环境以 Bionic `environ` 为进程权威

2026-09-08，接受，DVM-127。

OGPlay 为每个 guest 进程创建独立、确定性的 API 19 初始环境；不继承宿主 shell、用户、
区域或代理变量。首批只发布兼容层已有真实路径支撑的 `PATH`、`ANDROID_ROOT`、
`ANDROID_DATA` 与 `EXTERNAL_STORAGE`。配置先经名称、重复项及容量校验，再以独立 guest
页中的 A32 `envp` 传给 Bionic；初始化失败沿进程事务回滚。

Bionic 初始化后的 `environ` 是该进程的唯一运行时权威。Java `System.getenv` 通过
`CoreIntrinsicServices` 受检读取它，因此 native `setenv/putenv/unsetenv` 的成功变化不会与
Java 形成旧快照。无参查询使用 AOSP `System$SystemEnvironment` 保持不可修改 Map 与类型
检查语义。读取限制条目数、单项和总字节；畸形或未终止的 native 环境明确失败。

新增变量必须先证明其 guest 资源存在；不得为某款游戏添加生产分支，也不得复制尚无支撑的
`BOOTCLASSPATH`、ASEC、loop 或宿主 `LD_LIBRARY_PATH`。

<a id="adr-0054"></a>
## ADR-0054 · `ANDROID_ID` 归沙盒平台身份配置所有

2026-09-09，接受，DVM-128。

OGPlay 不运行 SettingsProvider、Binder 或多用户系统，但仍按 API 19 数据语义为每个持久
title 沙盒维护一个 64 位小写十六进制 `ANDROID_ID`。首次创建使用 HAL OS CSPRNG；值由
`SandboxStore` 原子保存，跨进程启动稳定，清除沙盒后可变化。ephemeral 沙盒每次生成新值。
这是一项隔离的兼容层身份，不读取宿主硬件，不跨 title 关联，也不得与 Build serial、
telephony id 或旧 `installation_id` 混用。

`meta.toml` schema 2 增加受检 `android_id`；schema 1 仅在首次初始化身份时显式迁移，未知
schema、未知键及畸形身份仍明确失败。`AndroidGuestPlatformConfig.android_id` 是 JNI 与
DexVM 的共同装配事实，DexVM 只保存只读 secure 子集。当前只实现实际命中的静态
`Settings.Secure.getString`：未知键返回 `null`；写入、跨用户、观察者及整数便利接口不因
本决定扩张。禁止用完整 framework Settings/ContentProvider 或静默占位替代该边界。

<a id="adr-0055"></a>
## ADR-0055 · 已迁移平台类的 JNI 与 Java 共用 VM 所有者

2026-09-09，接受，DVM-130。

guest 会话中已有 VM 实现的平台类由 DexVmGuestBridge 发布到 JNI，禁止提前安装同名
HLE 方法以遮蔽解释调用。Build/SystemProperties/Bundle 普通 Java 归 BootDex；Context、
Telephony、Settings.Secure、AudioTrack 等有界行为仍由现有 DexVM intrinsic 提供。
JNI registry 只承担成员与身份映射，字段、服务 singleton、PCM player 映射不得有第二份状态。
`activity.current` 使用生命周期当前 Activity，不另建宿主 Activity。

已迁移类要求调用方装配 DexVM/BootDex；无 VM 的 native/HLE 会话明确缺失，不保留伪替身。
本次不迁移无对应 VM 实现的 ViewRoot、应用兼容回调及独立 headless 契约 HLE，也不扩张
Android 系统服务范围。定向测试通过不替代游戏 gate 或跨平台验收。


<a id="adr-0056"></a>
## ADR-0056 · Runtime hook 状态归 BootDex，显式退出归 guest 进程边界

2026-09-12，接受，DVM-150。

Runtime 的普通方法和注册引用图采用固定 API 19 core.jar；不复制 Java 列表、检查顺序和
同步协议。Thread.hasBeenStarted/start/join 复用既有唯一线程状态，hook 不另建执行器。
System.exit 委托 Runtime.exit，平台只绑定 nativeExit。nativeExit 保存 per-VM 退出码，
通过非 Java 的 thread_stopped 控制展开停止 guest；禁止调用宿主 exit 或在 worker 中 join 自己。
进程所有者接收退出状态后完成原有线程 join、持久化与资源销毁；已退出 VM 不再接受 Java 调用。

宿主 Stop 保持取消语义，不自动视作 Java 正常退出，也不新增 non-daemon 自动退出。
Runtime.exit 按原版先启动全部 hook 再 join；halt 跳过 hook。无限等待、重入等待仍可阻塞
正常 exit，不伪造超时成功。现有 Thread 未捕获异常策略继续适用；Java finalization 尚未
支持时拒绝启用 runFinalizersOnExit(true)。其余 Runtime native 不因类迁入而自动声明支持。

纯 Java 会话的 root join 同样表示统一 Clock 的帧驱动阻塞。worker 的 timed wait 在该事实下
复用既有串行 deadline 补时机制；不创建新计时源，也不让正常 runnable root 下的 worker
自行推进时间。验证包括真实 LogManager Handler 清理、timed hook 与 Activity/Application 退出。
