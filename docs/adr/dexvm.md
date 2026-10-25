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
- [ADR-0034 · 普通流与后续 Luni 家族的 BootDex 所有权](#adr-0034)
- [ADR-0035 · Cipher AES 使用 BootDex Conscrypt 与 guest OpenSSL](#adr-0035)
- [ADR-0036 · Certificate 使用 Harmony Java 与 guest OpenSSL 验签](#adr-0036)
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
- [ADR-0049 · ICU 归 API 19 guest 并统一 JNI 桥](#adr-0049)
- [ADR-0050 · guest ICU 桥按 pinned libcore 语义收紧](#adr-0050)
- [ADR-0051 · 有界控件默认样式与 compound drawable](#adr-0051)
- [ADR-0052 · PreferenceManager Java 入口与偏好编辑提交边界](#adr-0052)
- [ADR-0053 · API 19 guest 环境以 Bionic `environ` 为进程权威](#adr-0053)
- [ADR-0054 · `ANDROID_ID` 归沙盒平台身份配置所有](#adr-0054)
- [ADR-0055 · 已迁移平台类的 JNI 与 Java 共用 VM 所有者](#adr-0055)
- [ADR-0056 · Runtime hook 状态归 BootDex，显式退出归 guest 进程边界](#adr-0056)
- [ADR-0057 · Builder 字段唯一所有权与 String 快照边界](#adr-0057)
- [ADR-0058 · 地址对象归 BootDex，网络资源归受控宿主边界](#adr-0058)
- [ADR-0059 · URI 普通值语义归固定 API 19 BootDex](#adr-0059)
- [ADR-0060 · DexVM 堆采用 Dalvik 式目标增长而非固定预算](#adr-0060)
- [ADR-0064 · 通用 KeyStore 使用自有 BKS Provider 与既有 guest crypto](#adr-0064)
- [ADR-0065 · NativeCrypto 类身份归固定 BootDex，native 按类显式准入](#adr-0065)
- [ADR-0066 · TLS 保留 AOSP API，采用自有 Provider 与进程内底座](#adr-0066)
- [ADR-0067 · 受限注解运行时由 DEX 元数据与每 VM 实现类构成](#adr-0067)
- [ADR-0068 · SQLite Java 栈归 BootDex，host 引擎只经唯一 VFS](#adr-0068)
- [ADR-0075 · 原版 NativeActivity Java 与当前进程 NDK 桥](#adr-0075)
- [ADR-0077 · BootDex 自带系统资源 ID 映射](#adr-0077)
- [ADR-0089 · DEX 注解按唯一数据解析并分层计量](#adr-0089)
- [ADR-0147 · API19 表面机器生成，行为按需实现](#adr-0147)
- [ADR-0148 · 纯 Java ROM 类按规则自动纳入 BootDex](#adr-0148)

- [ADR-0150 · guest 链接错误与 VM 内部故障分离](#adr-0150)
- [ADR-0151 · finalizer 由原版守护线程执行、堆查询共用真实配置](#adr-0151)

- [ADR-0152 · Java primitive/String payload 使用稳定 guest backing](#adr-0152)
- [ADR-0154 · 已有 intrinsic 的 ROM Java 类撤换为 BootDex](#adr-0154)
- [ADR-0155 · BootDex 自身可达闭包准入](#adr-0155)
- [ADR-0156 · intrinsic 与 overlay 父类的 ROM 子类准入](#adr-0156)
- [ADR-0157 · 进程内 Java View 遍历与宿主投影](#adr-0157)
- [ADR-0158 · 固定 ROM 成员级行为组合](#adr-0158)
- [ADR-0159 · 组合 ROM 普通方法的开放世界虚派闭包](#adr-0159)
- [ADR-0160 · 有界生成式主题投影与属性构造 native 接缝](#adr-0160)
- [ADR-0161 · VD-10 叶服务、特殊容器与宿主 Dialog 边界](#adr-0161)

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

### ADR-0160 精确修订（2026-10-24）

保留不引入完整 framework-res、完整 theme 的边界；登记范围内的主题值改由固定源码/ROM
生成的有界投影提供，配置、属性消费闭包及明确失败规则见 [ADR-0160](#adr-0160)。
本节原有已验证的 Legacy 外观契约保持；这不授权整类迁入或提前接通属性构造。

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

### ADR-0160 精确修订（2026-10-24）

保留不引入完整 framework-res、完整 theme 的边界；登记范围内的主题值改由固定源码/ROM
生成的有界投影提供，配置、属性消费闭包及明确失败规则见 [ADR-0160](#adr-0160)。
本节原有已验证的 Legacy 外观契约保持；这不授权整类迁入或提前接通属性构造。

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

<a id="adr-0057"></a>

## ADR-0057 · Builder 字段唯一所有权与 String 快照边界

2026-09-12，接受，DVM-151。

AbstractStringBuilder、StringBuilder、StringBuffer 与 IntegralToString 一次性切换为固定 API 19
BootDex；删除全部 builder 内容侧表和普通方法 overlay，不保留按方法渐进迁移开关。
RealToString 的原生 digit generator 只读写原版字段；其余转换、追加与同步执行原版 Java。
Character 暂留现有 wrapper 边界，五个 UTF-16 array 原语不读取 builder 字段或保存内容。

String 仍使用现有 VM/JNI immutable UTF-16 store；内部 (II[C)V 构造将区间复制为独立快照。
不声称物理共享 AOSP String.value 数组，也不在 String 中增加另一份 guest 字段表示。
Builder 的 shared 标志、detach、capacity 与 count 均由原版 Java 决定；保守复制可能多一次
分配，但已返回 String 的内容不受后续编辑影响。反射访问 String 私有 value/offset/count 及
物理数组别名不是当前 VM String 契约。若以后迁移 String，必须统一 JNI store 和 GC owner，
不得靠双写恢复影子数组。本次保留 builder 字段引用的精确 GC 强边和对象流原版序列化格式。

<a id="adr-0058"></a>

## ADR-0058 · 地址对象归 BootDex，网络资源归受控宿主边界

2026-09-12，接受，DVM-152。

InetAddress 家族、AddressCache、InetSocketAddress 与 NetworkInterface 一次性切换为固定
API 19 BootDex。IP 字节、hostname、scope 与 endpoint 只存在 guest 字段；NetworkRuntime
删除地址对象侧表，只保存 socket、stream 和 datagram 等不可由普通 Java 表示的资源。

Libcore 保留原版 Os/Posix 转发形状。宿主仅实现 Android 常量、数字地址转换、经
NetworkPolicy/NetworkTransport 授权的名称查询及确定性 guest uname。不得读取宿主机器身份、
DNS 或网卡；未支持的 Posix、NetworkInterface ioctl 和可达性路径明确失败。

<a id="adr-0059"></a>

## ADR-0059 · URI 普通值语义归固定 API 19 BootDex

2026-09-12，接受，DVM-153。

URI、内部 encoder、URISyntaxException 与 UrlUtils 一次切换为 BootDex，删除 URI 的 C++
字段投影和部分解析算法。URI 的全部可变缓存、解析结果和序列化协议只由原版 Java 字段维护。
URL 尚未迁移，继续执行现有有界 intrinsic；它仍使用的 ParseUri C++ 辅助函数不属于 URI
状态，保留到 URL 后续迁移。URI 新闭包没有 native 或宿主网络边界。

<a id="adr-0060"></a>

## ADR-0060 · DexVM 堆采用 Dalvik 式目标增长而非固定预算

2026-09-13，接受。Supersedes ADR-0017 中“默认 64 MiB 固定预算”和 GC-B 设计中的
`heap_budget_bytes/gc_watermark_percent` 配置裁决。

DexVM 堆区分初始目标、普通增长上限与绝对安全上限。64 MiB 仅是初始 GC 目标；分配越过
目标时先做普通 GC，仍不足则在增长上限内扩展目标，越过增长上限时再做一次 before-OOM
完整 GC，最后才抛 `OutOfMemoryError`。GC 后目标由 live set、利用率及 min/max free 区间
重新计算；增长只改变记账目标，不预留或提交宿主内存。

默认初始目标 64 MiB、增长上限 512 MiB、绝对上限 1 GiB、目标利用率 75%、空闲区间
2..8 MiB。Profile 配置改为 `[runtime.dexvm.heap]`，旧的两个平面字段直接拒绝，不保留兼容
解释。宿主分配失败映射为 Java OOM；所有判断只依赖确定性字节记账。不得以游戏分支或单次
大数组豁免绕过该状态机。

<a id="adr-0064"></a>

## ADR-0064 · 通用 KeyStore 使用自有 BKS Provider 与既有 guest crypto

- 状态：Accepted（架构方向确认，实施尚未开始）
- 日期：2026-09-16
- 关联：[KeyStore 开发规划](../design/dexvm/13-keystore.md)
- Supersedes：无；延续 ADR-0030、0035、0036、0037、0049 的 Java/native 所有权。

### 背景

当前 KeyStore 缺失暴露的是通用 Java 安全能力缺口。仅满足空库初始化或跳过某个 SDK
构造器不能提供真实兼容性。项目已有 guest libcrypto、JCA、证书和 VFS 基础，用户要求
不引入 BouncyCastle 运行时库，并从长期能力建设出发实现。

### 决定

保留 API 19 原版 KeyStore/KeyStoreSpi 及相关公开类；新增自有 Java Provider 与 BKS SPI，
经固定编译工具进入 BootDex。条目、状态、格式和异常归 guest Java；必要密码原语经现有
libogplay_jni.so 调用 guest libcrypto；持久化只使用调用方流和既有 VFS。
新生产目录必须有 MODULE.md，不把生产算法藏入构建脚本或 C++ intrinsic。

正式默认类型保持 API 19 的 BKS。只有真实条目、文件互操作和密钥保护主链验收后才注册
默认 BKS 服务并配置 keystore.type；中间成果不得冒充完整 BKS。Provider 使用自有身份，
不冒充 BC，也不引入宿主 JVM、系统 AndroidKeyStore、Binder 或硬件密钥服务。
BC 仅可作为隔离测试 oracle/参考，不进入生产依赖。

验收包括 API 状态、证书及私钥/对称密钥、密码保护、BKS 版本差异、外部双向互读、真实
跨会话持久化和失败/资源生命周期。分阶段实施不缩减这些最终条件；算法和格式支持范围
在能力账本单独列明。游戏触发案例只用于集成验证，不定义通用实现的分支或验收边界。

### 后果

减少新增运行时依赖，同时由项目承担 BKS codec、历史兼容和测试维护成本。
PKCS12/JKS 等格式后续各自实现，不能替换默认类型以规避 BKS。
KeyStore 不承担 PKIX、系统 CA 或 TLS；网络链另行验收，保持默认网络策略和明确失败。
上线 TLS 的后端版本与维护策略需要独立设计，不自动继承旧版离线 crypto 的选择。

<a id="adr-0065"></a>

## ADR-0065 · NativeCrypto 类身份归固定 BootDex，native 按类显式准入

- 状态：Accepted
- 日期：2026-09-16
- 关联：[DVM-171](../tasks/dexvm/DVM-171.md)
- Supersedes：DVM-169 的手工 `NativeCrypto` 类壳；延续 ADR-0035、0036、0039、0049。

固定 API 19 `conscrypt.jar` 的 `NativeCrypto` 是类、字段、普通方法、常量表和 native descriptor
的唯一事实源。VM 只为该类显式准入其原版 native 声明到 guest JNI，不在 intrinsic catalog
重复列举方法；未导出的入口仍由统一缺口账本明确失败。

原版加载名 `javacrypto` 在 API 19 系统库边界解析到唯一 `libogplay_jni.so`。原版 Java
`<clinit>` 不替换；受限 native `clinit` 复用 `JNI_OnLoad` 已完成的线程回调和当前算法注册，
不引入 libjavacrypto/libssl 或据 TLS 名称表发布 TLS。字段型 native token 的资源规则在 VM
统一装配，OGPlay 私有验签放在独立私有类，不污染原版 ABI。

<a id="adr-0066"></a>

## ADR-0066 · TLS 保留 AOSP API，采用自有 Provider 与进程内底座

- 状态：Accepted（架构方向；TLS 能力尚未实现）
- 日期：2026-09-16
- 关联：[DVM-173](../tasks/dexvm/DVM-173.md)、[完整规划](../design/dexvm/14-tls-trust.md)
- 延续 ADR-0064、0065；不改变原版 NativeCrypto 类身份和当前未实现入口的失败行为。

### 背景

KeyStore 已闭合，同源 libssl/libcrypto 已入 payload。用户明确要求公开 API 对齐 AOSP，
内部服务最小化自研，不复制 Android 系统。AndroidCAStore/AndroidKeyStore 类型本身对应用
可见，简化只适用于内部底座，不能伪造可观察语义。

### 决定

原版 javax.net.ssl 公开 API 进入 BootDex，自有 OGPlayJSSE Java Provider 管理普通状态。
内部验证与 TLS 调用私有 JNI，统一 guest libssl/libcrypto；BIO 只交换密文，实际网络仍经
唯一 NetworkRuntime policy/raw transport，不在宿主重复实现 TLS，不新增 Binder/daemon。
AndroidCAStore 采用可追溯版本化 CA 包和 API19 只读视图；AndroidKeyStore 不是 TLS 前置，
需要时另交付应用隔离软件服务，不冒充硬件保护。Provider 身份和所有支持边界如实发布。

信任、主机名、网络授权分别验证；自定义 guest manager 必须真实执行。旧版库可用于本地
兼容开发，在线发布须完成版本维护与补丁审计；替换后端须评估旧 native ABI，不能直接覆盖。

### 后果

减少 Android 系统依赖，但项目承担 Java SPI、BIO/线程生命周期、CA 更新和后端维护责任。
先交付显式/默认信任，再交付本地 TLS/HTTPS，最后受控在线；完整 PKIX、AndroidKeyStore、
SSLEngine 与 server TLS 独立记账。本记录不把任何未实现能力提升为 complete。

<a id="adr-0067"></a>

## ADR-0067 · 受限注解运行时由 DEX 元数据与每 VM 实现类构成

- 状态：Accepted
- 日期：2026-09-17
- 关联：[DVM-181](../tasks/dexvm/DVM-181.md)、[DVM-145](../tasks/dexvm/DVM-145.md)
- 延续 ADR-0017、0029；不开放 `Proxy.newProxyInstance`、动态定义或解释器按方法名截获。

### 背景

API 19 的 `Class.getAnnotation` 需要真实 runtime-visible 注解对象，成员调用必须能
check-cast / instanceof / invoke-interface，并区分显式值与 `AnnotationDefault`。
AOSP AnnotationFactory 依赖 Proxy 与 Method.getDefaultValue 闭包；既有 Field
marker 把注解接口直接实例化，无法提供成员行为。初始 Link 之后仍允许数组与原语类
合成，但不得破坏已发布 class/method 引用的稳定性。

### 决定

采用「DEX 不可变注解元数据 + 每 VM 受限注解实例 + 标准反射入口」。

- loader 只保存事实：可见性、类型、按名称的成员和递归 encoded value。Boot 与 App
  的索引不得交叉解释。
- linker 在登记时把索引投影为所属 DexUnit 的描述符/名称。运行期唯一正式接入点是
  `DexClassLinker::DefineRestrictedAnnotationClass`：仅从已加载的 annotation
  interface 生成具体实现类，使用 `deque` 追加存储，并走既有 `LinkClass` 填充
  vtable/iftable。不得改写已链接类的成员表。
- 实现类方法在生成时绑定 intrinsic handler，成员调用走正常虚派/接口分派；实例状态
  归 per-VM `AnnotationRuntime` 的具名 state table，参与 GC。不引入进程级 guest
  引用缓存，也不把注解接口直接实例化。

### 后果

Class 与 Field 查询可共享物化后端；Method.getDefaultValue 复用同一默认值。
Method/Constructor/参数注解、通用 Proxy 与完整反射仍明确未实现。生成类对
`Class.forName` 可见，但不作为应用可定义类型或动态 classpath。

<a id="adr-0068"></a>

## ADR-0068 · SQLite Java 栈归 BootDex，host 引擎只经唯一 VFS

- 状态：Accepted
- 日期：2026-09-18
- 关联：[DVM-186](../tasks/dexvm/DVM-186.md)、[开发规划](../design/dexvm/15-sqlite.md)
- DVM-186 已按本决定实现并同步数据库与 Android integration 契约。

### 背景

DVM-88 的 OGDB1 只实现有限表/行存储，缺真实 SQL、完整 schema/约束和事务。
继续扩写 C++ facade 会重复承担 Android Java 生命周期与数据库算法维护。
固定 SQLite amalgamation 已接入构建与运行时。

### 决定

原版 API 19 SQLiteDatabase/OpenHelper、会话/连接池、语句和 Cursor Java 栈进入 BootDex；
native 边界连接 host 原版 SQLite，所有文件只经 OGPlay VFS/SandboxStore。
host 只保管真实 connection/statement/window 资源；32 位 Java native 字段保存受检令牌，
不保存宿主指针，不复制 Java 事务栈。Clock、线程、GC 和释放复用既有边界。

首期验证 rollback-journal、进程内并发、取消、跨会话与进程崩溃恢复；WAL、跨进程共享、
跨库原子提交和动态扩展不在范围。生产实现不保留旧自定义格式识别或迁移；文件格式由
SQLite 引擎判断，损坏库交给 API 19 默认处理器记录、删除并重建。
新 SQLite 与 API 19 的 SQL/排序/配置差异显式验证，不能宣称完全等价。

### 替代范围与后果

本决定替代 DVM-88 / API 19 能力栈及 integration MODULE 中数据库使用
自定义格式、宿主侧普通 Java 状态和“不调用 host SQLite”的策略；唯一 VFS、应用隔离、
明确失败及其余能力边界继续有效。项目承担 VFS 锁/同步、Android native 适配、
collation 依赖和版本维护；不承担重写 SQL 引擎或实现完整 Android 数据库系统的义务。

<a id="adr-0075"></a>

## ADR-0075 · 原版 NativeActivity Java 与当前进程 NDK 桥

- 状态：Accepted
- 日期：2026-09-28
- 关联：[DVM-195](../tasks/dexvm/DVM-195.md)
- Supersedes：无；延续 ADR-0030、0038 的 Java/native 所有权。

### 背景

NativeActivity 不只是 Activity 类型名：原版 Java 根据组件 meta-data 选择库与入口，
持有 native handle，并转发生命周期及窗口/输入回调。独立 NativeActivity runner 自建
进程，不能共享现有 Java Activity 的对象、JNI、地址空间和库加载身份。

### 决定

NativeActivity 的生命周期、字段及 NativeContentView 归固定 API 19 BootDex；私有 native
方法通过 intrinsic 进入当前 AndroidGuestProcess。Manifest 保存每个 Activity 自己的
meta-data，getActivityInfo 仅读取当前 APK，nativeLibraryDir 对应 selected-ABI 库的 guest VFS。

NativeActivityRuntime 只拥有 guest ABI 分配和 JNI global refs，借用现有 loader、内存、
JNI/JavaVM 与执行器；不拥有第二个进程。NativeActivity 加载使用 dlopen 语义，仅执行
constructor；之后显式 Java 加载仍能在同一映射上执行一次 JNI_OnLoad。回调按创建线程
进入 guest，只调用当时登记的槽，销毁解除资源与引用。

Window.takeSurface/takeInputQueue 保存真实所有权；解除时先结束旧代际，首次遍历前解除
不产生旧 Surface 回调。SurfaceHolder 和 InputQueue 的 Java 身份进入同一对象模型，
NDK 窗口/队列/AssetManager 使用受检进程内注册。每次窗口创建分配独立身份，显式 acquire
引用可延续至 release；解除所有权后旧窗口不得重新激活。EGL 通过 façade 注入的只读接口
检查身份，拒绝旧窗口的 create/bind/swap，不建立 EGL 到 Android module 的直接依赖。

### 后果与边界

当前只覆盖单宿主窗口的 API 19 ARMv7 路径，复用 SDL3/ANGLE。窗口维持会话尺寸与 RGBA8；
其他 buffers geometry 返回 EINVAL。InputQueue 支持 ident polling，callback looper 明确失败；
AssetManager 支持 APK asset 的 open/read/seek/length/close，单文件上限 64 MiB。
保存状态复制预算 1 MiB，复制后调用 guest free。不引入 Binder、系统 Looper 服务、
完整 NDK、相机或输入法系统；缺失入口不伪装为成功。通用 fixture 验证不等于游戏兼容验收。

<a id="adr-0077"></a>

## ADR-0077 · BootDex 自带系统资源 ID 映射

- 状态：Accepted
- 日期：2026-09-29
- 实施：[DVM-198](../tasks/dexvm/DVM-198.md)

### 背景

API19 ROM 的内部资源 ID 不同。DVM-194 将受审 ID 编译进宿主，导致换 ROM 后构建失败，
即使更新映射也需要重编译 C++。配置值属于 OGPlay 平台策略，ID 属于当前 Java 制品。

### 决定

配方只维护资源语义/类型/消费者与平台配置值。构建从最终 DEX 的资源查询参数提取 ID，
以 schema 1 JSON 内嵌同一 BootDex JAR，manifest 记录映射/输入哈希。输入哈希用于追溯，
不与某一 ROM 固定值比较。保留资源类型、覆盖、冲突和制品一致性验证；不能证明 ID 时失败。

运行时复用封存的 boot_classpath 归档，每 context 延迟加载一次并缓存成功或失败结果。
只有 package 0x01 的资源查询使用该映射；不读取 APK 同名条目、不访问宿主文件、不提供旧 ID
别名。原版 SQLiteGlobal/CursorWindow 保持 Java，平台值保留 DELETE 等已验证配置。

### 后果

宿主完成一次升级后，ROM 资源 ID 变化只需重建 BootDex；不需要设备在线或完整 framework-res。
旧 BootDex 缺少映射时显式失败，需要重新生成。任意厂商类/native 改动仍须单独适配。

<a id="adr-0089"></a>

## ADR-0089 · DEX 注解按唯一数据解析并分层计量

- 状态：Accepted
- 日期：2026-09-30
- 关联：[DVM-205](../tasks/dexvm/DVM-205.md)
- Supersedes：无；保持 ADR-0067 的注解语义与输出所有权。

### 背景

注解总节点固定上限混合了单项复杂度、输入总规模和重复引用的解析工作量，
正常大 DEX 会在普通 Signature 注解处耗尽预算；仅缓存或提高常量都不能消除规模问题。

### 决定

loader 在每次 ParseDex 内按偏移缓存 annotation item/set，共享不可变解析结果。
声明相关约束在引用位置校验。DexImage 返回既有拥有型元数据，缓存不跨调用或 DexUnit。
输出深复制计入独立的工作量与存储预算，不因缓存命中免除资源消耗。

资源策略分为单项节点/深度/元素限制、整体工作量、容器存储三层。
整体预算随受检 DEX 大小增长并有硬上限；默认值与统计接口由 loader MODULE 维护，
不消费游戏身份、Profile 或 guest 堆配置。解析、引用遍历、投影和复制计入工作量；
分配前校验完整范围，存储计量含缓存、容器增长、临时缓冲及副本。
这是保守的容器存储账本，不承诺精确 RSS 或 allocator 元数据计量。
资源超限使用 typed resource_limit，诊断保留预算类型、偏移、用量及上限；
注册层追加 boot/application 来源，不丢原 reason/offset。

### 后果

正常输入规模增长无需逐游戏调节点常量；恶意共享引用的遍历与复制放大仍有界。
语义和索引检查不因缓存或预算策略放宽。未查询的 method/parameter 注解仍受检，
不新增其反射查询能力。

<a id="adr-0147"></a>

## ADR-0147 · API19 表面机器生成，行为按需实现

- 状态：Accepted（用户于 2026-10-20 批准；分 WU2b-1/2 实施）
- 日期：2026-10-20
- 关联：[DVM-252 / WU2a](../tasks/dexvm/DVM-252.md)、[DIAG-4](../tasks/diagnostics/DIAG-4.md)
- Supersedes：ADR-0030 对未选入 BootDex 的平台声明只能逐项手写的边界；
  curated BootDex 的行为准入、own-member 所有权与 OGPlay 非模拟器边界继续有效。

### 理由

历史分类的 M1–M3 共 149/415 单元（35.9%），包含低置信度及混合标签，不能当精确成本。
WU1 在真实旧版本上命中 3/3 形状首错；固定十项历史样本消融命中 7/10，
其中可表示为形状的 7/7，属于乐观条件召回率，不证明全部缺口可达。
32 APK 的修正扫描仍有上万项 ROM 表面缺口；逐项补声明无法摊销重复发现成本。
因此将类型结构与真实行为分开承诺：准确声明不等于兼容实现，未实现必须可查询并明确失败。

### 决定

1. 形状层从 API19 ROM 的四个 JAR 机器生成：类、父类/接口、own 方法/字段、
   修饰符与真实常量值；包括 ROM 实际含有的 @hide 类和非公开成员。
   只覆盖实际 class_def，不把 android.support、androidx、com.google 等 APK 库按前缀纳入；
   ROM 中实际存在的同包类仍按来源判定。不得以 SDK stubs 或 C++ 正则代替权威输入。
2. 固定输入 SHA256；不匹配即拒绝生成，更新来源必须另行审查：
   - core.jar：`996557954e45f7192b187b2394259bc8aca6e3946652e03b99d837432f83d1ef`
   - framework.jar：`90fc9fd36b45db1810ecf51e40119222da081b552f9a64a6d0c09cac09f98a48`
   - framework2.jar：`b6aba8bec45e2a2faa7a4e1e8ae3d8e450259f7e28c8dae6ca2c31dc34e06bea`
   - ext.jar：`618663a2f417f7eb4ae903d25809ffdd90fe5b5236184c530228dd66bfcdb65c`
3. 生成器在 tools/，确定性产物及输入溯源在 data/dexvm/；src/ 只有通用合并机制。
   手写声明优先；生成项只补缺，不覆盖现有类型/成员、handler 或字段所有权。
   现有 intrinsic 与 BootDex 同类的 overlay 规则保留；差异必须可诊断，不静默改写事实。
4. 行为继续由手写 intrinsic、精选 BootDex 原版 Java 与平台 handler 提供。
   生成方法为 declared-unimplemented，调用时记账并明确失败；骨架构造器同样未实现。
   常量提供真实值；非常量 static 字段及骨架类实例字段在访问时记账并明确失败，
   禁止以 null/0 冒充行为。不得生成伪成功的桩、clinit 或对象状态。
5. JNI/反射必须区分“成员不存在”和“成员存在但行为未实现”。表面存在且查找种类、
   描述符、static/可见性等合法的成员，查找成功，调用或非恒定字段访问时才明确失败。
   API19 不存在的成员，JNI 返回 NULL 并置 NoSuchMethodError/NoSuchFieldError；
   反射抛 NoSuchMethodException/NoSuchFieldException，遵循 4.4 的相应查找规则。
   此决定不放宽 JNI pending-exception 门禁，也不把未实现方法伪装为缺成员。
6. 规模约 9k 类、66k 公开/保护方法、31k 公开/保护字段；@hide/私有成员完整生成后会更多。
   索引允许预载，类对象、链接元数据与布局必须首次解析时才按需物化，禁止启动全量建类。
   冷启动耗时、常驻内存、首次解析及缓存重复解析必须测量并守住既有预算。
7. 只面向游戏进程直接可观察的 API 结构，不承诺 Android 系统行为齐全。
   不引入 Binder/system_server/Zygote、完整 ART/Dalvik 或手机运行能力。
   整包迁移原版 Java 加窄覆盖（L2）按包单独评估，不属于本 ADR。

### 备选与取舍

| 方案 | 成本与收益 | 主要风险 |
| --- | --- | --- |
| a. 逐次运行撞错后手写 | 初期投入低，已知行为好控制；每个 title 重复发现、翻译与验证 | 声明/层次/常量反复漏项，维护成本随题库增长，难证明表面一致 |
| b. 四个 ROM JAR 全量 BootDex + intrinsic 覆盖 | 可直接复用原版算法，但需审计全部 native、初始化、overlay 与闭包 | 原版系统服务依赖大量越界；拉入 Binder/完整 VM 或伪成功均不可接受；启动与内存成本高 |
| c. 生成骨架 + 既有行为（推荐） | 一次建设来源提取、懒合并及失败状态，机械消除结构长尾；行为逐项兑现 | 查找成功改变探测分支；元数据与实现优先级、字段失败状态及内存开销需专门测试 |

### 风险与批准后的验证要求

已有游戏原本在 JNI 查找失败后回退的路径，可能变为查找成功、调用时失败；
新接口还会改变可赋值性/反射枚举。不能以扫描缺口下降断言可玩性提高。
WU2b 先测试懒物化、继承/own-member、手写优先、常量与非恒定字段失败、
JNI/反射存在/缺失/static/异常分支、未实现记账及重复解析的对象身份。
用固定输入/32 APK 比较 class/hierarchy/member 缺口和 declared-unimplemented 的迁移，
逐项区分机械覆盖与行为支持，保留误报/漏报清单及冷启动/内存指标。
对已适配游戏复跑既有正式场景（按手册三轮），覆盖 JNI 探测、启动、菜单及保存/重载；
无现成场景的路径明确标未验收。出现探测分支回归时停用对应生成批次并分析通用规则，
不得按游戏名在 src/ 加例外。WU2a 的纯形状 BootDex 配方扩充沿用 ADR-0030，
不依赖本提案批准，不实施上述新运行时语义。

<a id="adr-0148"></a>

## ADR-0148 · 纯 Java ROM 类按规则自动纳入 BootDex

- 状态：Accepted（用户于 2026-10-20 预先批准）
- 日期：2026-10-20
- 关联：[DVM-255 / WU3](../tasks/dexvm/DVM-255.md)、[ADR-0030](#adr-0030)、[ADR-0147](#adr-0147)
- Supersedes：ADR-0030 “BootDex 类集合逐个人工精选”的方式；其 native 拒载、overlay、
  `jar 内每个 class_def 全部注册`及非模拟器边界继续有效。审计对象改为下列规则本身。

### 背景

ADR-0147 的骨架补齐了全部 ROM 类型，但未实现成员只能明确失败；其中相当一部分在 AOSP 中是
纯 Java 实现（TML 的首错 `MotionEvent$PointerCoords.<init>()V` 即是）。逐类手工精选无法随
题库摊销，而这些类的原版字节码本身自洽，不需要手写 C++。

### 决定

1. **准入条件**（全部满足才准入，由 `tools/bootdex/select_rom_java.py` 机器判定）：
   - 类在固定 ROM 四个 jar（core/framework/framework2/ext）中，且当前运行时来源为 skeleton；
   - 自身没有 native 方法；未被 intrinsic/overlay 占用，不在现有 BootDex，也不在 DVM-102
     日期审计冻结的非 boot_dex 分类中；
   - 父类链为 Object、其他准入类，或不含 overlay 的现有 BootDex 类；继承 intrinsic 类的类
     （含 Enum、Throwable、Handler、View 等）本批不准入，由级联拒绝；
     已由 [ADR-0156](#adr-0156) 修订父类准入规则。
   - `<clinit>` 的传递调用闭包（含 new-instance/静态字段/invoke-static 触发的类初始化与
     BootDex 字节码）全部已实现，不得触及骨架中未实现的方法、构造器或非常量字段；
   - 普通方法可以引用未实现成员，执行到时沿 ADR-0147 明确失败。
2. **保守的闭包分析**：invoke-virtual/interface 的目标不能唯一确定时，按“可能触达未实现”
   拒绝。唯一性来自：接收者在同方法内由 new-instance 得到的精确类型，或在全部 ROM 类层次中
   该方法只有一个具体实现。反射/类加载/线程启动入口（Class.forName、Method.invoke、
   Constructor.newInstance、Thread.start 等）同样拒绝。宁可拒绝，不可误收。
3. **排除项**：`<clinit>` 闭包触达 Binder、IBinder、IInterface、ServiceManager、Parcel 的类
   及其子类型；与 intrinsic 有双向对象交互的类——已实现的 intrinsic 方法/字段签名会产出或
   消费它，或 C++ 源码按名字引用它——列入单独清单，不自动准入（其子类因父类规则级联拒绝）。
4. **容量**：BootDex 为单一 `classes.dex`，字段/方法索引受 64K 限制；全部准入类无法同批装入。
   批次范围为“被样本库引用的准入类 + 其父类链与 `<clinit>` 必需类闭包”（`--scope referenced`），
   其余准入类在样本库上不产生转化；纳入它们需要多 dex BootDex，另行评估。
   已由 [ADR-0155](#adr-0155) 修订收录范围与容量余量。
5. **机制**：选择器默认只写报告，`--apply` 才修改配方；`--only` 要求指定类必须被规则准入，
   否则报告原因并失败，不得绕过规则。不得按游戏名或类名硬编码豁免；出现回归时先定位类，
   再判断是规则缺陷（修规则后重新选择）还是应加入规则化排除条件的个别类问题。
6. **不变的承诺**：不当模拟器、不伪造成功、手写优先；不实现 intrinsic 行为，不准入继承
   intrinsic 类的类（另行评估）。
   已由 [ADR-0156](#adr-0156) 修订继承 intrinsic 的限制。

### 后果

- 新增 ROM 纯 Java 类不再需要逐类审批，审计焦点转为规则及其自测（含经虚调用链间接触达
  未实现方法的拒绝用例）。
- 类被纳入后其原版构造器/方法真实执行，探测分支行为可能变化：准入前后必须对已适配游戏做
  前后对照冒烟；BootDex 体积、首帧耗时与内存变化需测量，退化超过 10% 停止并汇报。
- 宿主侧仍有大量未实现的 intrinsic 成员；本规则只消除其中“原版即纯 Java”的部分。

<a id="adr-0150"></a>

## ADR-0150 · guest 链接错误与 VM 内部故障分离

- 状态：Accepted（WU7-1b 用户明确授权）
- 日期：2026-10-21
- 关联：[DVM-256](../tasks/dexvm/DVM-256.md)、[DVM-254](../tasks/dexvm/DVM-254.md)、ADR-0147

### 决定

1. 用独立、强类型的 Java linkage failure 通道表示 guest 可观察错误，不从 DexVmError
   的 reason/message 推测 Java 类名。解释器两后端共用异常展开出口；宿主直接 Call 也返回
   VmCallOutcome 中的真实 Java throwable，嵌套调用保持 guest catch 和继续执行。
2. 类首次实际链接时检查继承关系、final 与循环等；失败标记只属于失败类及依赖它的类。
   首次传播 ICCE / VerifyError / LinkageError 等原始类别，后续使用该不可用类抛 NCDFE。
   失败类不重新布局，不发布可执行的半成品；不污染其他类或成功缓存。final 方法覆盖
   对照 Class.cpp:2848 的 bail：createVtable 返回 false 且没有 pending 时抛 VirtualMachineError。
3. 指令解析时缺方法/字段抛 NSME/NSFE，static/instance 或 interface 调用种类不符抛 ICCE，
   访问违反 loader/package/member 规则抛 IAE。缓存只保存成功的结构解析，不能绕过 caller
   访问校验。new abstract/interface 抛 InstantiationError；abstract 调用抛 AME。
   访问检查采用 Dalvik AccessCheck.cpp 的规则，不引入全量 verifier 数据流实现。
4. bootstrap 的 intrinsic、BootDex、固定 ROM skeleton 按真实来源 parent-first；只有已提供
   的类型遮蔽应用类，包名前缀本身不能禁止应用 DEX 的独有类。未提供 android.* 类型仍由
   application loader 定义，不覆盖已登记平台类，不引入动态 classpath。
5. DexVmError 留给宿主装配/声明错误、无效内部 handle/slot/cache、未预检输入进入 FastCode、执行预算、
   生命周期取消和线程 owner 不变量等。DEX 二进制解析/宿主声明损坏和未支持 native
   仍沿既有明确失败和账本；合法载入方法的结构预检拒绝则是 guest VerifyError。
   不将内部 bug 伪装成 Java linkage failure，也不伪造修复或成功。
6. JNI/反射沿 DVM-254 的查找与 pending/包装边界适配新类型，区分缺类与已失败类；
   survey 的既有显式合成与记账保留，新链接/访问失败同样抛错，不增加中性返回。
   VM 内部异常/平台对象工厂可创建抽象类型的受控实例，guest new 与 JNI AllocObject
   才执行实例化限制，不能把 VM 工厂误当作 guest new。

### 依据与验证

对照 Dalvik 4.4 Class.cpp（dvmLinkClass、CLASS_ERROR / throwEarlierClassFailure）、
Resolve.cpp、AccessCheck.cpp、CodeVerify.cpp 和 mterp 指令。用双后端 catch 后继续执行、
首次/再次失败类、缺成员/种类/访问/分派及真实 bootstrap 遮蔽回归验证；内部无效 handle
仍必须失败。跑目标 dalvik/tests、全量基线棘轮与九个既有正常基础场景，缺口保留在 DVM-256。

<a id="adr-0151"></a>

## ADR-0151 · finalizer 由原版守护线程执行、堆查询共用真实配置

- 状态：Accepted
- 日期：2026-10-21
- 关联：[DVM-258 / WU7-4](../tasks/dexvm/DVM-258.md)
- Supersedes：ADR-0033 中 PhantomReference 不支持的边界；沿用 ADR-0029、0040、0060 的所有权与堆模型。

### 决定

对象模型通过显式实例分配 observer 通知解释器，由解释器在创建可 finalize 对象时调用
固定 BootDex 的 FinalizerReference.add。referent、zombie、列表和队列只保存在 Java 字段。
GC 按 soft/weak → finalizer 保活 → phantom 的次序处理，再以复标记后的存活图清扫；
守护线程用既有 VmThreadRuntime，每个 guest daemon 对应真实宿主线程。

FinalizerReference/Daemons 保留原版 finalize、异常日志、sentinel 与 10 秒 watchdog；
时间只由统一 Clock 提供，超时退出只作用于 guest 进程。固定 ROM 队列按 head 后进先出，
sentinel 可越过已有工作，因此 VM 边界额外保留进入时批次并等待它完成，不等待后续新批次。
对象可复活，但不会第二次自动 finalize；原有 native token 的 owner GC/teardown 回收继续存在。

Runtime 内存统计与 ActivityManager 档位只读取 JavaObjectModel。VMRuntime 普通单例和
已退役的旧 external-allocation 协议执行原版 Java；有效 native 分配记账驱动同一 GC/finalizer，
不伪造分配成功、不借宿主可用内存猜测 guest 档位。未接通的 VMRuntime 接口继续明确失败。

### 验证

双后端 GC、对象复活/一次 finalize/异常吞掉、引用队列、堆守恒及配置/档位一致性回归，
加原版七目标、全量基线和九基础场景；证据写 DVM-258 / DIAG-7，不以基础场景代替 title gate。

<a id="adr-0152"></a>

## ADR-0152 · Java primitive/String payload 使用稳定 guest backing

- 状态：Accepted
- 日期：2026-10-22
- 来源：WU8-2c-D 调查；WU8-2c-I1 按用户审阅定稿。实施见 [DVM-260](../tasks/dexvm/DVM-260.md)。
- 关系：细化 [ADR-0016](runtime.md#adr-0016) 的直接表发布条件；沿用
  [ADR-0057](#adr-0057) 的 String 不可变快照/私有字段边界和
  [ADR-0060](#adr-0060) 的逻辑堆增长规则。本决策不切换尚未验收的生产 store。

### 背景与现状证据

Dalvik 4.4 `Jni.cpp:2130–2152、2355–2386、2598–2645` 返回被 pin 的数组/UTF-16 字节本体，
isCopy=FALSE；0/ABORT 解除 pin，不回滚写入，COMMIT 保留 pin。`Jni.cpp:556–569` 与
`alloc/Visit.cpp:226–228` 明确把 pin 表作为 GC roots。因此“不移动”不能推出“pin 只需诊断”：
长期未 Release 的合法 pointer 仍必须阻止 backing 被回收。

以 `4844110a` 为调查基点，代码位置与结论如下；行号用于追溯，不代表已改实现。

| 调查项 | 结论及代码位置 |
| --- | --- |
| payload 位置 | `runtime/jni/jni_array.cpp:65–89、189–197` 保存八类 vector 的 variant；`jni_object.cpp:48–67、149–156` 保存 UTF-16 vector。均是宿主堆字节，不在 guest reservation。 |
| 权威数据 | `runtime/dexvm/object_model.cpp:617–629、671–715、729–797` 将 String/primitive 内容委托同一 JNI store；`261–334` 导入 JNI identity，没有两套长期 Java/JNI 权威内容。 |
| GC 与生命周期 | `object_model.cpp:489–549` 非压缩 mark-sweep 调用 store.Delete，再退役 Record；句柄槽稳定到 sweep，宿主 Record vector 自身仍可扩容搬移。String active lease 已接 roots；`integration/dexvm_bridge.cpp:1900–1904` 尚未接数组 access roots，数组 Delete 只会因 active_accesses 抛错。 |
| 当前复制点 | `jni_array.cpp:147–184` Acquire 返回复制，Release 写回/丢弃；`jni_guest_array_bindings.cpp:182–200、244–315` 再经 4 MiB guest arena 搬运。String UTF-16/MUTF-8 lease 共用 16 MiB copy 预算。 |
| 映射能力 | `memory/address_space.cpp:86–102、163–181、232–258` 的 4 GiB reservation 支持在内部分配 payload，访问宿主地址为 base+GuestAddress；没有任意 vector 的外部 backing 导入/别名 API。 |
| CPU 与快照 | `address_space.cpp:620–636` 将 RW 非 X 页交直接表，并有独立读表；`cpu/dynarmic.cpp:449–479` 订阅变更并绑定页表。`address_space.cpp:432–488` restore 会替换 reservation/base，旧 host pointer 失效。 |

表中路径均位于 `src/`。不能只把 JIT 页表指到 vector.data()：受检 bus、CAS、snapshot 仍读
base+address，会再次产生不一致的存储。没有证据表明当前 HAL 可把任意 malloc 页别名进来。
Windows HAL 用 VirtualAlloc reserve/commit，macOS 用 mmap/mprotect；它们只暴露
reserve/commit/protect/decommit（`hal/virtual_memory.h:35–48`），不提供共享映射所有权协议。

WU8-2b 两后端各 359/399，相关失败现为 **36**（array 34、String 2），不是历史统计 35；
多出的是 WU8-2a 暴露的 StringCritical isCopy。070 当前只差 allocateDirect 的 hasArray。
WU8-2c-D 调查只读已有报告，未重跑测试或游戏；宿主页查询为 16 KiB，guest 页仍为 4 KiB。
九个已有场景 state.json 无数组长度/类型分布，不能据此选择池大小或承诺 FPS/RSS 收益。

### 决策

采用 **A 的修正版**：primitive 数组及 immutable String 的权威 payload 自创建起放在
同一进程 AddressSpace 内，使用稳定、不可移动的 typed byte block；Java、JNI、NIO 访问
同一字节。对象头、句柄、GC 图与方法/字段布局仍归 DexVM，不引入完整 Dalvik heap/object ABI。
**pin/lease 是 GC 与资源生命周期机制，不只是诊断计数**；无需搬移 pin 不等于无需保活。

#### 存储与依赖

- integration/process 拥有 payload allocator，显式注入 store。runtime/jni 只定义强类型
  payload/borrow 句柄与 typed accessor 接口，不依赖 AddressSpace 实现、DexVM 或 OS；jni_guest
  将有效借用解析为强类型 GuestAddress。不得以全局裸指针、TLS 猜 owner 或跨层反向调用串接。
- store 的 authority 是 kind/length/owned block/view；Region 等可返回临时值快照，但不维护
  与 guest block 双写的长期 vector。单元夹具可显式使用 host-copy provider；生产 direct
  provider 缺失时必须明确失败并记账，不能自动回退复制却返回 FALSE。
- 从创建起就确定 guest 地址；8-byte 对齐足以容纳现有八种元素，按 ARM 小端、位模式
  访问，保留负数、char、long/double 高位、float NaN。固定长度不通过 resize 换址。
  空数组/空串也定义有效的零长度借用地址与独立 lease，不能因 payload 为零返回伪 NULL。
- allocator 从统一 guest 布局选择不重叠范围，跳过现有 ELF、stack/TLS、mmap、NIO 等映射；
  不在本 ADR 猜一个空闲固定地址或预占整个 1 GiB。使用页内小块池/大块路径，整块生命周期
  归 owner；不能每个小 String 独占一个 4/16 KiB 页，也不能误释放同宿主页的相邻 guest 页。
- 新分配要有明确调用线程/分配 context，owner 在 store 锁外进入 GC/堆预算；Java 与 JNI
  New 的入口共用收费和失败回滚，导入不重复收费。现有 FromIdentity 的 String/primitive
  Record 未像 Java New 一样 Reserve，这个差异不能原封不动带进新预算。

#### 借用、GC 与并发

- 过程级 lease 记录 object identity、payload、address/generation、kind 和引用次数；重复
  Get 可返回同一地址，但每次都有独立 pin。Elements/Critical、Chars/StringCritical 的
  配对仍分别校验；0/ABORT 只解除一次，COMMIT 不解除，不做写回或恢复旧值。
- 数组 active leases 必须在 mark/weak 处理之前作为强根。String 暂保留现有 lease 对
  String identity 的保活合同，同时保活 block；这比 Dalvik 仅 pin backing char[] 更保守，
  不据此宣称精确 weak-reference 时序等价。detach 不自动回收可能由别的 native 线程
  持有的地址；shutdown 必须先停止并 join CPU/线程，再退役 leases/backing。
- 托管 payload 页使用与普通 RW 非 X guest 页相同的映射策略，照常进入 JIT 直接
  读写表（独立读表也正常发布；observer 仍按既有规则禁用快径）。现有模型本就由 JIT
  直接访存、宿主 HLE 同时访问同一 backing；数组 payload 与 guest malloc 缓冲区没有
  本质差异。跨线程可见性沿用 monitor/volatile/pthread/futex 的现有 happens-before，
  不新增无同步数据竞争或多元素一致快照承诺。
- 不采用 managed payload checked-only 策略：持有数组指针的混音、解码、像素循环不能
  每次访存都走受检回调。只有实测证明需要时，才另议诊断开关，不作为本阶段生产策略。
- 宿主 typed accessor 在构造时验证 block 所有权、范围及类型对齐，持有不可移除的
  block/provider 生命周期；每次只做数组/视图边界检查，再经 base+guest 地址读写，
  不调用持全局锁的 AddressSpace::Read/Write，不向外暴露长期裸宿主 span。活跃 accessor
  阻止 Release/reuse，避免在每次元素访问重复检查 lease；关闭 provider 后最后一个 view
  退役才回收映射。owner 必须串行化 accessor 创建/释放与 block 回收。
- 前提是 provider 注册的范围在映射锁内受保护：munmap/mprotect/mremap、MAP_FIXED
  替换（含任意部分重叠）在修改前整体拒绝并记 ledger；普通不涉及这些范围的 syscall
  保持既有行为。provider 活跃期间拒绝独立 restore，AddressSpace 不移动且晚于 owner
  析构，reservation base 因而稳定。分配/释放仍发布世代和 MappingChange，并遵守既有
  CPU quiescence；直接访存线程停止后才能回收其可能使用的 block。

#### String 与 070

- Dalvik String.value 是 char[]（`vm/oo/Object.h:241–258`）。本方案让 String 使用与 char[]
  相同的 typed UTF-16 payload 后端和 offset/count 视图，没有第二份 chars 副本；GetStringChars/
  Critical 返回此块地址、FALSE。MUTF-8 仍编码复制且 TRUE，其预算、release 合同保留。
- 不新增影子的 Java String.value 字段，也不把此方案称为已经迁移原版 String 对象布局。
  ADR-0057 的公共 char[] 构造与 builder 快照继续；可变输入必须复制或有完整 COW，不能
  因为共用池就让 Java 修改原数组污染已返回的 String。只读 String 视图可共用 backing；
  const-cast 写入 JNI String 指针不受支持，不用它做别名验收。精确 private value/offset/
  count 反射及只 pin char[] 的弱引用时序需另行统一真实 backing-array 对象与 GC 边。
- 070 的判据是 `java/nio/ByteBuffer.java:68–72 → MemoryBlock.java:123–127`：
  allocateDirect 创建 non-movable byte[] 并取其 address；DirectByteBuffer.java:86–103
  对非只读且有 array 的块返回 hasArray=true。当前 `nio_runtime.cpp:223–243` 单独分配
  address，没有 array；不是只缺一个布尔返回。
- allocateDirect 应分配真正的 Java byte[]，NIO storage 同时引用它和同一 payload 地址；
  arrayOffset/byteOffset、slice/duplicate、readonly 共用此所有权。NIO trace 保活 array，
  用户单独保留 array 时 Buffer 回收不能 unmap 它，不能由两个 destructor 重复释放。
  NewDirectByteBuffer 包装外部 native 地址继续非拥有、无 byte[]；不把任意地址伪造为
  managed array，也不顺带更改 GetDirectBufferCapacity 的 heap-buffer 历史语义分歧。

### 备选方案比较

| 方案 | 改动面与性能 | 风险与跨平台可行性 |
| --- | --- | --- |
| A：统一 guest backing（已选） | 改 allocator/provider、两 store、对象模型计量、GC roots、guest borrow、内存策略及 NIO；借用从 O(N) 复制变为有界元数据操作，释放不搬运 payload。 | typed accessor 热路径须以 vector 对照微基准验收；VAS/页碎片、漏 Release、restore 是主要成本。复用 Windows/macOS 现有 reservation/commit，无需页别名；两平台仍需实际验证。 |
| B：首次 JNI 借用才迁移或别名 | 未暴露对象保留宿主存储，首次迁移 O(N)，原子切换所有 Java/Region/NIO 访问到新 authority；降低未借出小对象的 guest 占用。 | 迁移后不得在 Release 时迁回，已发布地址和多借用必须稳定；长期维护双后端、迁移峰值/回滚。malloc vector 非页对齐且可能与别的分配共页，不能直接别名。真 alias 需两平台新 HAL 协议及粒度/寿命/快照验证，当前未具备。 |
| C：复制 + 边界写回 | 维持现有 arena，增加 COMMIT/ABORT/回调前同步，改动较局部；每次同步仍 O(N)。 | 当前 TRUE 已如实，不能改 FALSE。可改善部分释放断言，却覆盖不到 isCopy=FALSE、无 Release 的长期写、无 JNI 边界的即时可见及双向并发更新；JIT 直接写也不受普通 observer 完整观察。两平台易做，但不满足本单目标。 |

不选择将 vector.data() 塞进 JIT 表的“半别名”方案，也不把后台定期同步当作可见性合同。
若实测说明 A 的地址占用或访问性能不可接受，应重新评估 B；本决策不预先承诺布局参数
或游戏性能收益。

### 后果、配额与存档边界

- 4 GiB 是地址容量，不是已提交 RAM。逻辑堆仍按 ADR-0060 增长，另计 committed/slab
  空闲、pin bytes/count、VAS 连续空间；同一 block 只收一次字节费用，借用不再收一份
  payload。分配/提交/地址或 pin 元数据预算失败明确映射 Java OOME/记账，预留现有 OOM
  异常路径所需应急空间。不把旧 4 MiB Elements 或 16 MiB 编码预算当作新的全堆上限。
- 首版不承诺完整 VM save-state：MemorySnapshot 只有页/权限，不能恢复句柄、GC 图、
  provider 子分配表或 leases；restore 又会换 base。有活跃 managed provider 时拒绝单独
  restore，除非未来具备配对的全 VM 恢复事务。单独内存快照可作诊断，不能标成游戏存档。
  普通 VFS/Preferences/对象流存档读写仍为相同逻辑值，不序列化 host pointer。
- 长期未 Release 会成为真实 pinned live set，不能为省内存偷偷回收；提供对象/字节/
  kind/次数/持续阶段的诊断。释放后继续写是非法使用；generation 防止错误 release，
  不宣称能在页内池中识别每一次 raw stale write 或消除全部 ABA。
- 依赖复制的旧断言需要明确分层：`tests/runtime/jni_array_tests.cpp:56–80` 的 ABORT 丢弃、
  `jni_guest_instance_calls_tests.cpp:313–329` 的 TRUE/丢弃第二次写、字符串 copy budget 与
  Critical TRUE 不能继续充当生产 direct 合同。保留显式 copy fixture 的测试，同时为
  production provider 测 FALSE、立即可见、ABORT 保留和 GC pin；不能删掉生命周期、
  cross-kind、double release、预算/回滚、异常与边界检查来提高通过率。

### 分阶段实施与验收

每阶段单独 Work Unit、提交及报告；每项限定一个存储/入口主题，可在一次会话实施。
阶段 1 只通过夹具验证基座；共同验收是既有 pass 零回归、233/233 静态覆盖保持。涉及生产
路径的阶段跑 JNI/dalvik 双后端基线和同配置 9 基础场景，不能低于 WU8-2b 的 6/9，
且原通过场景及 checkpoint 不得新增失败。Windows/macOS 分别留证，不能以单平台通过
标记跨平台完成；host-copy 夹具不替代生产 guest backing 验证。

| 阶段 | 实施范围（一会话） | 具体验收与预期转 pass |
| --- | --- | --- |
| 1. backing 基座 | integration 注入 provider、页内 allocator、typed accessor、受保护映射、所有权/世代与 restore 拒绝、配额失败回滚；先用夹具，不切生产 store。 | 4/16 KiB 宿主模型下对齐/邻页/预算/回收、与普通 RW 页直接表策略一致，受保护映射拒绝 guest 修改，CPU/accessor 双向可见与快照拒绝；Release 下 1e7 读写与 vector 比值不超过 1.5，否则停在本阶段；JNI 仍 359/399。不拿夹具页量当游戏分布。 |
| 2. primitive authority | Java/JNI 创建、导入、Region/aget/aput/clone 切到唯一 backing；先保持显式 copy borrow；统一原生入口收费与数组 access roots。 | 类型/位模式/边界、native-only 创建/import 只收费一次、pin 无普通引用仍能 GC 保活、释放后可回收；无预定新 pass。授权代表场景采样长度分桶、峰值 live/pin/committed、碎片和 callback/帧耗时，决定池参数。 |
| 3. 数组 direct borrow | 八类 Elements 与 PrimitiveCritical 去掉 staging，采用对象/kind/pointer 的计数 lease；0/ABORT/COMMIT 按 Dalvik，detach/shutdown 顺序收口。 | **34 条**：八类 isCopy/alias/ABORT/COMMIT 共 32 + Critical isCopy/alias 2，预计 JNI 393/399；新增重复借用、跨 native 线程发布、无 Release 长持有/GC、COMMIT 后存活及 teardown 用例。性能不够则不放行生产迁移。 |
| 4. String direct UTF-16 | String authority 使用同源 char payload，Chars/Critical 借地址；保留 immutable/builder 快照和 UTF8 copy，不迁移私有字段布局。 | **2 条** StringChars/StringCritical isCopy，预计 JNI 395/399；NUL/surrogate/空串/offset、GC 长借用、两种 kind 隔离、UTF8 TRUE 及 builder/intern/hash 不变。 |
| 5. NIO 共用 byte[] | allocateDirect 的真实 array/address、slice/readonly/owner trace 及单次回收；external JNI direct 保持非拥有。 | **070 整例**双后端转 pass（若其余分类不变，dalvik 85/91），byte[]/buffer/JNI pointer 双向互见，array/切片单独存活和回收均正确；JNI 395/399 保持，不顺带修 heapCapacity。 |

五阶段之后仍有 NIO heapCapacity 1 条和 pending 契约分歧 3 条，不在本方案消除范围。
无 Release 的长期使用必须靠 backing 寿命成立，不能只以当前短探针通过宣布长期游戏风险已消失。

### ADR-0152 阶段 3 实施决定

沿用 [ADR-0153](runtime.md#adr-0153) 的原则，以 Dalvik 4.4 非 CheckJNI 实现为行为判据。
Jni.cpp:2355–2386、2598–2617 直接借用 primitive payload，isCopy=FALSE；
Release 仅判断 mode 是否为 JNI_COMMIT：等于时无操作，其余值（含 ABORT/非法值）解除一次 pin，
不复制、不恢复写入。数组身份与类型仍验证，但不校验 elems，不区分 Elements/Critical。
这替代本 ADR 原数组 object/kind/pointer 配对要求；String 的 kind/pointer 配对保持不变。

pin 计数归 primitive store，原因是 store 已拥有数组身份、Delete 检查及 GC roots 枚举，
无需 provider 反向理解 VM 对象；长期 view 保活 backing，非零 pin 在 mark/weak 前作强根。
重复获取返回同一稳定地址，每次计数加一；pin 不绑定线程，detach 不清除。
无配对 unpin 仅结构化 warn + ledger（数组、真实槽、guest LR），状态不变；
首次计数超过 Dalvik kPinComplainThreshold=10 时每个数组生命周期警告一次。
停止并 join 全部 guest 线程/CPU 后才统一退役 pin，诊断残留数组数与字节数。
不增加内存序承诺；显式 host-copy provider 不支持 guest direct borrow，明确失败而无复制回退。

### ADR-0152 阶段 4 实施决定

按用户确认的 Dalvik Jni.cpp:2130–2153、2623–2645，String Chars/Critical 共用 String 身份
pin；Release 只按 jstr unpin，不校验 chars 指针，也不区分两类 UTF-16 入口。无配对仅 warn
与 ledger，不中止。这替代原文及阶段 3 实施决定中暂保留的 String kind/pointer 严格配对。
UTF8 编码复制的 pointer/token 配对、TRUE 和预算保持不变；pin String 对象及 block 的保守
GC 边界、immutable 快照/私有字段边界仍依 ADR-0057，不实施真实 String.value 布局。

<a id="adr-0154"></a>

## ADR-0154 · 已有 intrinsic 的 ROM Java 类撤换为 BootDex

- 状态：Accepted（用户于 2026-10-23 复核批准）
- 日期：2026-10-23
- 关联：[ADR-0148](#adr-0148)、[DVM-265 / WU9-5b](../tasks/dexvm/DVM-265.md)
- 关系：补充 ADR-0148，不取代其默认准入规则。

### 背景

WU9-5a 确认部分已有 intrinsic 的算法本为 ROM 纯 Java；ADR-0148 决定 1、3、6
未授权撤换这些实现。继续手写会重复维护算法与状态，故提供显式、受审的撤换入口。

### 决定

1. 已有 intrinsic 类满足 ADR-0148 除本类 intrinsic 占用及其待清理交互以外的全部
   准入规则时，可以显式选择撤换为固定 ROM 原版 BootDex。默认选择行为不变；
   依赖、父类、初始化闭包、Binder、日期冻结和容量限制继续有效。
2. 自身 native 只能保留已有、经审的同签名 intrinsic native 绑定作为 overlay，不得新增 native。
3. 状态所有权整体移交 Java，宿主不得再读写该类私有字段或保存替代状态。
   确需 VM 访问的字段必须是 ROM 明确约定给 VM 的字段，并逐个列出；首批无此例外。
4. 删除该类手写声明及普通 handler；其他 C++ 点名要么删除，要么经 VM 调用 Java 方法。
   必要的 native overlay 与类型签名/接口关系保留，不能据此重建状态或算法。
5. 每批撤换必须满足 framework_contract 无新增偏差、dalvik 双后端零回归、九场景门禁通过。
   撤换类原偏差随来源转为 BootDex 消失，不能通过漏导出类或放宽基线隐藏回归。
6. ThreadGroup、Thread、Context、Handler 等具有生命周期或宿主状态耦合的类不在本次范围，
   需要单独设计；不引入完整 Android、Binder 或完整 VM。

### 后果

纯 Java 算法、构造器及字段布局以 ROM 为唯一所有者，C++ 仅保留受审 native 边界。
每批须审查全部宿主耦合并增加 DEX 行为回归；验证失败的类退回 intrinsic 并记录原因。
首批目标与实际门禁结果见 DVM-265；未经场景门禁不能宣称兼容验收完成。


<a id="adr-0155"></a>

## ADR-0155 · BootDex 自身可达闭包准入

- 状态：Accepted（WU10-1 用户明确授权）
- 日期：2026-10-23
- Supersedes：[ADR-0148](#adr-0148) 决定 4 的收录范围与容量假设。
- 关联：[DVM-266 / WU10-1](../tasks/dexvm/DVM-266.md)、[ADR-0154](#adr-0154)

### 背景

样本库未直接引用的骨架仍可被现有 BootDex 方法体调用。原先“其余准入类在样本库上
不产生转化”的假设不成立；已有类的初始化依赖也不能只靠新候选的初始化闭包补齐。
收录判据应来自平台代码自身，避免样本集合决定可执行的平台依赖。

### 决定

1. 收录范围改为现有配方 BootDex 类与 intrinsic 声明引用的类，沿方法体引用到不动点
   可达的全部 ADR-0148 准入类，加上其父类链与 `<clinit>` 必需闭包。
   方法体边包括 new-instance、invoke 目标所属类、字段所属类，以及
   const-class/check-cast/instance-of 和对象数组类型引用。被拒绝类是边界，不继续遍历其代码。
   新范围名为 `--scope closure`；样本库引用不再是收录条件，`--scope referenced` 保留作诊断。
2. 仍使用单一 `classes.dex`；method_ids、field_ids 必须低于 65536，并保留余量，
   本批上限为 60000。超过预算则分批收录并明确未完成，或另行评估多 dex，不放宽准入规则。
3. 不变量：BootDex 内可达且规则可准入的骨架类数量为 0。
   `select_rom_java.py --check-closure` 必须列出缺口并以非零状态阻止验收；
   检查使用重新导出的真实运行时 surface，不能以配方存在替代制品已装载。
4. ADR-0148 的 native、Binder、intrinsic 交互、父类、初始化唯一目标和日期冻结规则
   不变；ADR-0154 的显式撤换仍需单独审查，本次不新增 intrinsic 撤换或 native。

### 后果

闭包可随制品扩充而改变准入结果，build/check 后重新导出 surface、检查并必要时迭代。
索引、加载/首次解析、冷启动与 RSS、定向回归和一致性门禁须留下前后证据；
性能预算以端到端指标（冷启动/首帧耗时、RSS）为准，退化超过 10% 停止；BootDex 注册加载与 Link
的组件耗时随类数近似线性增长，只作记录。任何必需门禁不达标则停止并报告。游戏场景由编排方在沙盒外验收，闭包为零不代表兼容验收。


<a id="adr-0156"></a>

## ADR-0156 · intrinsic 与 overlay 父类的 ROM 子类准入

- 状态：Accepted（WU10-2 用户明确决定）
- 日期：2026-10-23
- Supersedes：[ADR-0148](#adr-0148) 决定 1、6 的父类限制。
- 关联：[DVM-267 / WU10-2](../tasks/dexvm/DVM-267.md)、[ADR-0154](#adr-0154)、[ADR-0155](#adr-0155)

### 背景

原限制是 WU3 推迟的范围，未论证一律拒绝的必要性。当时手写 intrinsic 将可覆写方法
误声明为 final 的链接风险，已由 WU9-1 将 framework_contract A1 清零。仍需逐类检查
父类可用成员，防止构造和常用路径的失败点后移、探测分支改变。

### 决定

1. ROM 类的父类链出现 intrinsic 或带 overlay 的 BootDex 类不再一律拒绝；选择器统一
   检查本类不覆写父类链上 `overridable=false` 的实例方法。framework_contract A1=0
   保证父类契约与 ROM 一致，选择器逐类复核。
2. 本类自身全部方法的字节码不得触及父类链 intrinsic 或 overlay 部分未实现的构造器、
   方法或字段；不只分析 `<clinit>`。此条件比普通方法“到达时明确失败”更严格。
   报告分别使用 `parent-intrinsic-unimplemented-member` 和 `overrides-nonoverridable`。
   组合类按 ADR-0158 VD-06a 补充。
3. ADR-0148 其余规则不变：本类无 native、初始化闭包唯一且已实现、Binder 排除、
   intrinsic 交互排除和日期冻结；ADR-0155 的 closure 范围、零缺口与索引预算不变。
   ADR-0154 的显式撤换规则不变，不新增 intrinsic 撤换或 native。
4. 父类链含 Binder 的继续拒绝；父类本身被拒绝的继续级联拒绝。默认选择和 closure
   范围共用上述规则，不允许按类名豁免。

### 后果

干净的 Enum、Handler、Thread 等 ROM 子类可复用现有父类实现。触及父类未实现成员的类
（主要为 View 系）暂缓，待手写外观补齐 ROM 状态后另行评估；不放宽“不伪造成功”。
五个父类族的 DEX 回归及撤去配方的负对照、契约、一致性和端到端性能证据见 DVM-267。
任何必需门禁不达标即停止；游戏场景和帧时间由编排方在沙盒外验证。

<a id="adr-0158"></a>

## ADR-0158 · 固定 ROM 成员级行为组合

- 状态：Accepted（编排方按用户授权于 2026-10-24 决定）
- 日期：2026-10-24
- 关联：[DVM-276 / VD-03](../tasks/dexvm/DVM-276.md)、[ADR-0147](#adr-0147)、
  [ADR-0154](#adr-0154)、[ADR-0156](#adr-0156)
- 关系：补充 ADR-0154 整类撤换；精确修订 ADR-0147 手写优先的显式 manifest 例外。

### 决定

1. 允许固定 ROM 类通过精确成员来源 manifest 组合原版字节码、受审平台 adapter / 既有
   C++ overlay 和明确失败成员。每类只有一个 ROM class_def、完整字段布局及 clinit owner；
   不复制字段子集，不登记第二份 Java 状态，不以默认零或空 clinit 冒充初始化。
2. 每个方法按完整签名列明来源、ROM flags 和原始方法体 hash；每个字段列明类型、flags、
   ROM 来源及 encoded value / VM 清零后 ROM 构造与 clinit 的初始化策略。
   原版 DEX 方法体保留，不改写成代理桩。未选成员声明保留，调用时按 ADR-0147 记账失败。
3. 普通方法 overlay 只在 manifest 逐签名批准；审查项包括理由、输入、输出、状态 owner
   与失败边界。ROM 来源清除同签名旧 handler，失败来源禁用字节码与旧 handler；
   不扩大未启用类的通用 overlay 优先级。类形状以 ROM 为准，额外 intrinsic 字段/方法拒绝。
4. 新 native 只限显式受审的平台服务接缝，逐签名批准并绑定真实 handler；不授权全类
   native 准入。未选 native 也保留声明并明确失败。此例外替代 ADR-0154 决定 2 在本批
   显式组合成员上的限制；guest JNI 动态注册与其他 native 准入规则不放宽。
5. build/check 校验输入、原始方法体身份、成员来源冲突、精确依赖、普通 overlay 与 native
   越权，并输出封存 JAR 内的 manifest。相同输入两次生成逐字节一致；空表不增加 JAR entry，
   保持既有制品身份。运行时只消费封存制品，不提供用户配置或动态成员开关。
6. 默认 selector、ADR-0156 全部 own 方法及父成员审计不变；组合校验复用其层级/分派分析，
   被选字节码不能调用未选且无 handler 的成员，不能用“未来实现”填补依赖。
   ADR-0155 closure 零缺口和单 DEX 预算继续有效；四个不可达类不强制收录。
7. 构造/clinit、访问 flags、虚分派、字段通道、GC 强边与两解释后端按每批真实变更验证。
   surface 必须反映 bytecode / handler / unimplemented 的实际来源，不能以配方存在标记实现。

### 后果与本批边界

成员级组合成为 View 状态迁移的基础工具，不代表 View 遍历或任一游戏已兼容。
VD-03 只启用测试 ROM 样例；生产 manifest 为空，closure 增量为零。
纯 ROM clinit 是本批唯一初始化 owner，尚不提供构造或 clinit adapter 生成器。


### VD-J1 实施补充（编排方决定，2026-10-24）

组合类的 clinit 必须为 rom，其依赖按 ADR-0148/0155 常规准入规则收录，不放宽检查。
逐字段来源 rom 表示 Java 唯一事实来源且已正确维护，failure 保留形状并在访问时走
ADR-0147 记账失败；构建依赖检查及运行期 linker 共用此可用性，被选 ROM 读写 failure 字段
视为漏依赖。构造器 overlay 可作受审初始化 adapter，逐字段列出 ROM 默认值依据。


### VD-06a 补充（编排方决定，2026-10-24）

ADR-0156 决定2面向整类准入；ADR-0158 组合中 failure/overlay 的原字节码在运行期禁用，
分别明确失败或执行受审 handler。因此组合类的 own-body 父成员审计只覆盖 source=rom
的活动方法体和 clinit；failure/overlay 原方法体不进入引用审计。
ADR-0156 决定1的 overrides-nonoverridable 形状检查仍覆盖全部成员；决定3、4及非组合类
默认 selector 的全部方法体审计完全不变。
活动体的 direct/static/super/精确接收者 failure 调用及本类 failure 字段仍按 ADR-0159
决定1严格拒绝；普通未知接收者虚派按 ADR-0159 及其修订入 ledger，支持路径动态命中须为0。
自测要求：组合 failure 体触及父未实现成员可通过；同体改为 rom 的严格父调用以
parent-intrinsic-unimplemented-member 拒绝；禁用成员也不能覆写不可覆盖方法；
非组合类原全 own-body 用例保持。


<a id="adr-0157"></a>

## ADR-0157 · 进程内 Java View 遍历与宿主投影

- 状态：Accepted（编排方按用户授权于 2026-10-24 决定）
- 日期：2026-10-24
- 关联：[ADR-0154](#adr-0154)、[ADR-0156](#adr-0156)、[ADR-0158](#adr-0158)、
  [DVM-277 / VD-J1](../tasks/dexvm/DVM-277.md)
- Supersedes：ADR-0154 决定 3、5 对本批 View 状态迁移的以下限制；其他条款保留。

### 决定

1. integration 在 owner guest 线程调度 managed-window 根的 measure/layout；Java 唯一拥有
   guest 几何与布局 flags，UiTree 接受只读 geometry/hierarchy 投影，不执行 guest。
2. 投影使用 generation、content/metrics revision 与 epoch；Surface、input、render 共享
   最终发布的同一 geometry。guest 异常不回滚已执行的 Java 动作，失败遍历不发布快照。
3. onSizeChanged 保持 ROM setFrame 的同步时序；第一轮 layout 中有效 request 可触发第二轮，
   第二轮请求留至下一 owner tick。宿主 buffer/holder 生命周期独立管理。
4. 纯宿主 UI 可拥有独立 C++ 布局根，不能重排 Java 窗口；presenter 只读快照，
   只有 owner 调度入口可执行 guest。
5. 精确替代 ADR-0154 决定 3 中本批 View 私有字段的宿主读取禁令：只允许 integration
   通过逐字段列账的绑定 token 读取 Java 字段并生成投影；ui 不读取 Java 字段。
   初始化及系统接缝 adapter 如需写 Java 状态，必须逐字段、方法、位掩码受审列账；
   measured/bounds 不允许 C++ 反向写，字段真值不得另建 side-table 权威。
6. 本批替代 ADR-0154 决定 5 的“九场景全部通过”为冻结 baseline 差分门禁：保住六项通过，
   三项已知失败不提前；一致性基线保留已知失败，不得重置基线隐藏回归。
7. ADR-0156 全 own-body 父成员审计不变；native、Binder、初始化及索引预算不豁免。
   不引入 ViewRootImpl 系统实现、Binder 或完整 Android。
8. ui 契约按实施阶段区分 guest 投影与纯宿主分区；ui 不执行 guest、不保存 Java 引用的
   不变量完整保留。

### 实施边界

本决定不等于遍历已落地。VD-J1 仅使用逐字段投影授权；遍历、发布协议、宿主根隔离及
最终 owner 切换由后续 WU 实施，各阶段以任务单及实际验证为准。


<a id="adr-0159"></a>

## ADR-0159 · 组合 ROM 普通方法的开放世界虚派闭包

- 状态：Accepted（编排方按用户授权于 2026-10-24 决定）
- 日期：2026-10-24
- Supersedes：精确修订 [ADR-0158](#adr-0158) 决定6对普通方法的依赖检查；
  [ADR-0148](#adr-0148) 决定2的 clinit 规则不变。
- 关联：[DVM-279](../tasks/dexvm/DVM-279.md)。

### 理由

ADR-0148 决定1允许普通方法引用未实现成员，执行到时沿 ADR-0147 明确失败。
ADR-0158 决定6对组合普通方法复用 clinit 级“虚派目标唯一”分析，严于该约定，
使 View.measure→onMeasure 这类框架可覆盖回调无法组合。Android 4.4 本身采用开放世界虚派。

### 决定

1. 构建期仍拒绝被选 ROM 方法中的 invoke-direct/static/super、同方法 new-instance 精确
   接收者调用指向 failure；目标必须已实现（rom/overlay/intrinsic/BootDex 字节码）或本批一起
   迁移。本类 failure 字段读写仍是漏依赖，DYNAMIC_TARGETS 反射/动态入口仍拒绝。
   clinit 及其传递闭包使用原规则，不复用普通方法的放宽摘要。
2. 普通方法的 invoke-virtual/interface 目标不唯一时不再拒绝。build/check 在 BootDex
   可链接类集合（BootDex 字节码类、组合类、intrinsic/骨架类）上枚举所有可能目标，按调用点
   签名记录完整目标签名及 implemented/failure 来源，确定性封存为 manifest dispatch ledger。
   guest 应用类覆写属于应用代码，不在构建期集合内。failure 目标仍按 ADR-0147 运行记账并
   明确失败，不静默返回、不因进入 ledger 改为 implemented。
3. 每批声明的支持路径必须以运行记账证明动态未实现命中为0；任务单列明支持路径实际命中的
   虚派目标。静态 ledger 不是运行覆盖证明。
4. ADR-0155 closure 零缺口及单 DEX 预算、ADR-0156 父成员审计、ADR-0158 其他决定不变。
   surface 反映真实来源；cha_precise 仍仅作敏感性分析，不用于准入。
5. 自测覆盖非唯一虚派通过及全目标/来源 ledger；direct/static/super 指向 failure、精确
   接收者 failure、本类 failure 字段读写、DYNAMIC_TARGETS、clinit 非唯一虚派仍拒绝；
   同输入 ledger 两次逐字节相同。保留关闭放宽时 View.measure 候选按旧错误拒绝的负对照。

### 后果

本决定只调整显式组合普通方法的证明边界，不改变默认 selector，不承诺所有 ROM 虚派目标
可运行。每批仍须以有界支持路径和动态记账验收，未闭合的阶段不得借此扩大类准入或默认启用。


### VD-05 修订（编排方决定，2026-10-24）

编排方按用户授权选择“唯一 failure 虚派封存为运行期明确失败”，不新增硬件 DisplayList 能力。
开放世界虚派取决于运行期接收者，ROM 层次中目标个数不构成实现证明；API19 软件路径的
mDisplayList 初值真实为 null，只有 HardwareRenderer/getDisplayList 创建该硬件对象。

1. 普通方法的 invoke-virtual/interface 接收者不是同方法 new-instance 精确类型时，无论
   目标唯一与否均按决定2封存 dispatch ledger，逐目标签名保留 implemented/failure 来源；
   failure 目标运行时仍按 ADR-0147 记账并明确失败，不获得实现信用。
2. invoke-direct/static/super、精确接收者指向 failure、本类 failure 字段、DYNAMIC_TARGETS
   与 clinit 及其传递闭包仍沿严格规则拒绝；不修改默认 selector 或 ADR-0156。
3. ledger 中 failure 目标接收者来自本类字段时，该字段必须为 rom 可用性，且所有 BootDex
   写入者均为 failure 或只写 null，以 ROM 构造初值 null 为准；否则拒绝。构建检查覆盖实际
   配方全部类、寄存器别名与分支汇合，任务单逐字段列账，不靠测试对象碰巧为 null。
4. 自测覆盖唯一 failure 未知接收者通过并入账、精确接收者仍拒绝、字段约束正反例及严格
   clinit；保留关闭修订时真实 setFrame 候选按原错误拒绝的负对照。支持路径运行记账必须为0。


### VD-06a 修订（编排方决定，2026-10-24）

编排方按用户授权决定：VD-05 修订第3条的全 BootDex null-writer 约束为充分条件，
不是必要条件。真实 Java hierarchy 允许 mParent 非 null；字段仍必须为 rom 可用性。
对字段接收者的每个 failure 虚派目标，以下两条满足其一即可：

1. **(a) 原条件**：该字段的全部 BootDex 写入者为 failure，或 ROM 字节码只写 null；
   overlay 写入者不借原版方法体证明 null，继续校验 encoded 初值、别名与 CFG 分支汇合。
2. **(b) 不可分配条件**：该 failure 目标的声明类，以及提供该分派目标的全部子类，在
   当前进程不能存在实例。全部构造器均为 failure；abstract/interface 本身不可实例化，
   且不存在可实例化的实现子类提供该目标。宿主 C++ 不分配这些类型，必须机器扫描
   NewIntrinsicInstance、按名/按 class-id 及对象模型分配点，不接受人工声明。
   构造器失败仍沿 ADR-0147；不能把不可分配目标标为 implemented。
3. 无法证明宿主分配参数的值域时，不能宣称“没有该类型分配”。JNI AllocObject、
   Unsafe.allocateInstance 等跳过构造器的入口同样属于宿主分配审计，不由构造器 failure
   自动覆盖。任一条件不能成立仍拒绝，不能靠测试中没有创建该对象放行。
4. 构建检查对每个按 (b) 放行的目标封存确定性不可分配证据：提供该目标的类型、
   构造器来源及宿主分配扫描结果/源码身份；check 重算并拒绝过期证据。
   自测含全部构造器 failure 正例、已实现构造器/可实例化子类反例、宿主直接或动态分配
   反例、确定性及篡改证据拒绝；保留仅启用 VD-05 条件时 assignParent 被拒绝的负对照。
5. View/ViewGroup 的实例真实存在，getTextDirection/getTextAlignment 等 failure getter
   不能借 (b) 获得信用，必须迁移 ROM 或受审 adapter。其他精确调用、字段可用性、
   clinit 严格闭包、默认 selector 与 ADR-0156 均不变。支持路径动态未实现命中仍须为0。

#### VD-06a 第三次修订（编排方决定，2026-10-24）

编排方按用户授权决定：Unsafe.allocateInstance、JNI AllocObject 和宿主原始分配均可绕过
构造器，因此 (b) 通常不能证明实例不存在；真实父链又不满足 (a)。正确性由运行期
ADR-0147 明确失败与支持路径动态零命中保证，字段接收者证明只用于封存信心分类。

1. VD-05 修订第3条及 VD-06a 的 (a)/(b) 不再作为准入门槛。检查器仍对每个字段接收者的
   failure 目标计算 `a` / `b` / `unproven` 分类，确定性封存字段来源、初始化、writer、
   构造器及宿主分配证据；不能证明时记录 unproven，不据此拒绝或将目标标 implemented。
   共用宿主扫描只存一份，逐目标证据以哈希和索引引用；check 重算并拒绝过期账。
2. 决定1的 direct/static/super、同方法 new-instance 精确接收者指向 failure、本类 failure
   字段、DYNAMIC_TARGETS 与 clinit 传递闭包仍严格拒绝。支持路径动态未实现命中必须为0；
   默认 selector 与 ADR-0156 全 own-body 父成员审计不变。
3. hierarchy 支持路径必须包含 root→ViewGroup→ViewGroup→叶的真实父链；可达的
   View/ViewGroup failure getter 必须迁移 ROM 或受审 adapter，不得缩小路径回避。
   OGPlay 根 View 的 mParent 为真实 null，没有 ViewRootImpl；作为已列账的系统接缝偏差，
   不以构造虚假系统对象补齐。未闭合的 WU 不宣称此路径已支持。
4. 保留宿主分配扫描及 raw-host-allocation 运行负对照，作为信息和回归；
   自测要求 unproven 进入 ledger 且不拒绝、failure 来源不变、严格拒绝仍在、
   封存两次逐字节一致及篡改证据拒绝。

<a id="adr-0160"></a>

## ADR-0160 · 有界生成式主题投影与属性构造 native 接缝

- 状态：Accepted（编排方按用户授权于 2026-10-24 决定）
- 日期：2026-10-24
- 实施：[DVM-286 / VD-09a-1](../tasks/dexvm/DVM-286.md)、[DVM-287 / VD-09a-2](../tasks/dexvm/DVM-287.md)
- 依据：[VD-09a 分析](../../.local/analysis/vd-09a/REPORT.md) §5、§7、§8；
  [编排方方案决定](../../.local/analysis/view-dir/DECISION.md)“VD-09a 方案决定”。
- 关系：精确修订 ADR-0048/0051 的主题值提供方式；保留禁止完整 framework-res/完整
  theme；ADR-0077 小型系统查询、ADR-0147 明确失败及 ADR-0158 成员组合继续有效。

### 决定

1. 采用方案 C：A 的 native ABI 与固定 AOSP 源码/ROM 生成的有界主题投影。范围为
   Theme、Theme.Holo、Theme.DeviceDefault 及其 values 中派生的 Theme.* 变体；
   属性仅取已登记 styleable 消费闭包，配置须显式登记。未知主题、配置或属性明确失败。
   不引入完整 framework-res 或完整 theme，不在 src 手写主题数值表。
2. Theme/TypedArray/XmlBlock$Parser/StringBlock 按 ADR-0158 精确成员组合，禁止整类
   迁入。integration 以强类型 token 唯一持有受检 XML cursor/theme native 状态；
   TypedArray 六槽数据与转换、View/LP 状态及布局算法归原版 Java。
3. 投影与 BootDex 同封存，使用独立 schema/version/hash；固定输入、ID 绑定及闭包在
   build/check 校验。与 ADR-0077 schema 1 查询显式分流，重叠冲突即构建失败，
   不改变旧映射含义，也不从 APK 同名项兜底系统资源。
4. 私有 ROM ID 与源资源不符即拒绝该组输入；需要变更时重建同源整组制品。
   禁止猜值、旧 ID 别名或数值补丁，AOSP tag 相同不构成 ROM 私有 ID 一致证明。
5. 本轮排除 scrollbar Drawable、ninepatch、selector、光学布局和动画；请求即明确失败。
   在 VD-10 前重新评估必要依赖并单独升级，不以空 Drawable 或默认几何冒充实现。
6. 只有登记的主题/配置/属性查询可报“确证缺席”；投影区分已知值、确证缺席和不支持。
   应用主题覆盖及非零 defStyleAttr 按完整优先级：显式 XML > XML style > 默认
   style > theme 同名属性。保留 bag parent/reference/?attr、有界循环检查、
   defStyleAttr 替代 defStyleRes、force/copy 与 @null 遮蔽，不把未知当缺席。

### 顺序与边界

1. 09a-1：编译 XML 事实与受检 cursor；现有 Elements/PullEvents 从同一模型导出，
   Legacy 输出保持；不暴露 native 或成功属性构造。
2. 09a-2：XmlBlock/Parser 与 StringBlock 有界 plain-string 接缝及 owner/GC 绑定。
3. 09a-3：主题投影生成器、applyStyle 与 TypedArray 六槽 ABI。
4. 09a-4：Context/Resources/Theme/TypedArray 精确成员准入、五类及 LP 属性构造。
5. 09a-5：实验 inflater 接入，单独验收 include/merge、context/style/LP 与失败回滚。

每阶段独立机器验收；生产默认继续 Legacy。后续阶段未完成不得以本 ADR 视为已实现。
不引入 Binder/WMS/system_server、完整 TextView 栈或第二份宿主几何权威。

### VD-09a-3 补充（编排方决定，2026-10-24）

投影登记范围精确为 values 中可由固定 ROM R 字段证明名→ID 的
Theme/Theme.Holo/Theme.DeviceDefault 及其派生 Theme.* 变体（当前92个）。
其余无 ROM 绑定依据的主题（当前22个）为未登记主题，请求即明确失败，
诊断包含主题名及“无 ROM ID 绑定”；不猜ID、不生成别名、不当作缺席。
未登记主题的 parent XML 可按名称参与已登记主题合并；若必须依赖其ID
才能确定合并，则将受影响主题也降为未登记并列账。
生成器逐项输出已登记与未登记主题及原因。missing-rom-style-field
仅使该主题不登记；public-id-mismatch 等ID不符仍拒绝整个构建。

### VD-09a-5 实施补充（2026-10-25）

编排方预先决定：LayoutInflater.createView 的 ROM 反射路径属于 ADR-0159 严格拒绝的
DYNAMIC_TARGETS，按 ADR-0158 决定3替换为逐项受审 adapter；不放宽动态入口准入。
review 对照、真实 guest ClassLoader/Constructor、Java 缓存/Filter/参数 owner 与
异常消息/cause 边界见 [DVM-290](../tasks/dexvm/DVM-290.md)。其余 inflate/Factory/
include/merge 执行原 ROM，Window XML 通过未发布父容器隔离失败后再转入稳定内容根。
固定 ROM 不含 PhoneLayoutInflater，实验 companion 复用 AOSP 对应前缀及克隆协议。

实施方按编排方常设原则决定，待编排方复核：native ABI 审计按固定配对区分
LayoutInflater 声明。Legacy 仍必须有 nativeInflate；实验 ROM 必须无 native，
其他受审 ABI 逐签名/flags 不变。构建器的原生产审计、成员调用及 clinit 规则不变。
这不授权新 native、完整控件栈或未实现成员伪成功。


<a id="adr-0161"></a>

## ADR-0161 · VD-10 叶服务、特殊容器与宿主 Dialog 边界

- 状态：Accepted（编排方按用户授权于 2026-10-25 决定）
- 日期：2026-10-25
- 依据：[VD-10 分析](../../.local/analysis/vd-10/REPORT.md) §2、§3、§7、§8；
  [编排方方案决定](../../.local/analysis/view-dir/DECISION.md)“VD-10 方案决定”。
- Supersedes：仅精确修订 [ADR-0160](#adr-0160) 决定5，见决定2。
- 实施：[DVM-291 / VD-10a-1](../tasks/dexvm/DVM-291.md)、[DVM-292 / VD-10a-2](../tasks/dexvm/DVM-292.md)、[DVM-293 / VD-10a-P](../tasks/dexvm/DVM-293.md)、[DVM-294 / VD-10a-3](../tasks/dexvm/DVM-294.md)、[DVM-295 / VD-10a-4](../tasks/dexvm/DVM-295.md)、[DVM-296 / VD-10a-5](../tasks/dexvm/DVM-296.md)、[DVM-297 / VD-10a-6](../tasks/dexvm/DVM-297.md)、[DVM-298 / VD-10a-7](../tasks/dexvm/DVM-298.md)。

### 决定

1. 叶构造、onMeasure 与 clinit 所需的有状态 Paint 测量采用逐签名受审 adapter，
   不迁完整 ROM TextView/Paint/TextPaint 栈。保留 ADR-0158 的完整 ROM shape、原 clinit
   owner 与成员来源账；clinit 实际调用的 Paint 默认状态、antiAlias、measureText 必须真实
   执行，不能因结果未用而跳过。文本语义由宿主服务单一拥有；纯叶测量只收不可变值，
   返回 desired/constrained、明确 state/baseline 与不可变 line-plan，不接树或 VM、不写几何。
   Java adapter 真实调用 setMeasuredDimension，Java 独占 measure/cache/layout/bounds/flags；
   自定义覆写正常虚派，invoke-super 才进入 adapter，同次遍历允许 ROM 补测。
2. 对 ADR-0160 决定5作精确修订：仅允许叶 src/compound 消费所需的有界 APK
   BitmapDrawable、ColorDrawable 与单态 ColorStateList（ROM valueOf）。仍拒绝 selector、
   stateful colors、资源 NinePatchDrawable、系统默认皮肤、scrollbar Drawable、光学布局、
   动画及任意 Drawable/custom draw；Legacy ninepatch 像素支持不等于 Java 对象支持。
   值桥必须闭合登记 styleable/主题消费集、有类型文件/reference 六槽解析、ROM ID/cookie/
   density/config、对象身份/GC/backing 与失败原子性；未知或不支持值不得降为 @null。
   background/foreground/divider 非空值仅在其完整消费契约另行验收后开放。
3. 默认 Button/EditText 主题不要求立即可用；按真实优先级解析并对不支持请求明确失败，
   允许应用显式受支持主题覆盖。不得偷改 defStyleAttr、删默认请求或复制 Legacy 固定皮肤。
   完整系统皮肤/selector 如有需求，先开独立资源与状态对象能力包。
4. Java mScrollX/Y 是唯一位置事实；宿主只提交输入 delta，由 Java scrollTo/clamp。
   VD-02 投影扩 XY，同 epoch 统一 screen/clip/raster/hit，不重写 child local geometry。
   保留有界拖动/CANCEL；完整 dispatchTouchEvent、fling、EdgeEffect 与 scrollbar 不随之开放。
5. Surface fixed-size 不解除 managed buffer 限制；受审转换 holder 0 与 ROM −1 的 layout
   sentinel，不匹配正 fixed-size 继续失败。managed viewport、guest bounds、holder frame/
   真实 buffer 与呈现超采样分开记账；不得由 View bounds 推造 buffer，不启 WMS/updateWindow。
6. Video 保持现有播放协议与 intrinsic 叶测量的显式 adapter，不给 ROM 比例算法实现信用。
   源320×240、双 EXACT200×100：旧服务200×100，ROM133×100，差异单列；改变比例须另议。
   metadata 仅在 owner 校验 generation 后失效布局，工作线程不写 geometry 或发布中快照。
7. ProgressBar 进度实现明确延后，不能将旧 noop/空白光栅带入实验成功面。EditText 独立
   WU，保留真实编辑事务、焦点/监听与输入；完整 Editor/IME/StaticLayout、shaping/bidi 等
   长尾另开能力包。未支持成员一律按 ADR-0147 记账并明确失败。
8. Dialog 每 modal 独立 HostLayout 树，与 guest 窗口通过共同 presentation epoch 合成。
   快照包含 guest window epoch refs、host modal revision、metrics revision 与 modal stack，
   render/input 原子使用同一快照；纯 modal 变更不推进 guest epoch。宿主按钮焦点独立，
   guest 恢复使用身份/generation。禁止整棵 guest 树 LayoutUiTree 或切换 geometry owner；
   构建失败保留旧快照，不执行 guest 布局回调。

### 顺序与验收边界

10a-1 纯叶接口 → 10a-2 值消费边界 / 10a-P clinit Paint 前置 →
10a-3 Text 构造形状 → 10a-4 Text 测量/光栅 → 10a-5 Image / 10a-6 Button →
10a-7 EditText → 10b-1 两 Scroll 构造 → 10b-2 纵向容器 → 10b-3 横向容器 →
10b-4 Surface → 10b-5 Video → 10c-1 宿主树隔离 → 10c-2 发布/输入合成 → VD-11。
10c-1 可先独立开发，发布整合依赖前序边界。每 WU 单独验收，不因本 ADR 扩大当前成功面。

10a-1 保持 Legacy 逐值结果；compound 可用宽度修正如改变旧结果，则保留旧公式，
新服务提供统一 line-plan，差异列入 DVM-291 留待10a-4。Java 叶构造/adapter 本轮不开放。
支持路径动态未实现命中须为0；严格 direct/static/super/精确接收者、本类 failure 字段与
clinit 审计不变。生产默认继续 Legacy，不引入 Binder/WMS/system_server 或完整 Android。

### VD-10b-2 精确补充（编排方预先决定，2026-10-25）

[DVM-300](../tasks/dexvm/DVM-300.md) 默认 overScrollMode 的构造依赖允许受审 EdgeEffect
idle 接缝；此为编排方在任务中预先决定，不是实施方按常设原则调整审计范围。
ROM 新建对象的 STATE_IDLE/isFinished、finish、尺寸/位置记录保持真实 Java 状态；构造
保留非资源默认值、Rect、density 最小宽度与真实 interpolator。系统 glow/edge Drawable
及资源派生尺寸标为 failure（未支持 glow 资源），不加载或伪造资源成功。
onPull/onAbsorb、draw 调用及离开 idle 的 release 路径按 ADR-0147 记账失败；idle release
只做原版清零后返回。宿主有界 delta→Java scrollTo 不触发 pull，没有动画时间源。
决定4对完整 EdgeEffect 动画/绘制、ROM touch、fling/scrollbar 的排除与其他审计规则不变。


### VD-10c-2 实施边界（2026-10-25）

[DVM-305](../tasks/dexvm/DVM-305.md) 的共同呈现序列由 integration owner 原子发布。
guest_window_epoch_refs 只借用既有完整窗口快照；modal_stack 冻结独立 HostLayout 树，
不复制 guest 几何。metrics revision 来自完整 guest target，viewport/density 同时匹配后才
发布；resize 先在所有宿主副本布局并检验容纳性，任一失败保留旧共同快照及旧宿主集合。
Java 测量/独立 guest 提交已经发生的动作不回滚，不能将此保留称作 guest 状态回滚。
设备metrics待齐时，旧modal隐藏/退役可在最后完整metrics下发布移除，不混入新几何。
纯 modal show/update/dismiss/输入只推进共同/宿主序列；Compose 不执行 guest 或遍历。
最上层 modal 捕获绑定分区及代次；撤销捕获先于集合发布，取消的尾事件不穿透新 owner。
普通 guest 触摸也持有共同快照的事件租约，listener 改树沿既有 requestLayout 由 owner 调度。
无组合/准入审计规则范围调整；ADR-0147、精确调用、本类 failure 字段及 clinit 规则保持。
