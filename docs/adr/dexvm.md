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
- 保留同一 api19.json、build_bootdex.py、cipher.c 和 manifest.cipher_native。
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
