# 模块：loader

## 职责

解析 APK/XAPK/APKM/APKS/目录、二进制 Manifest、DEX L1、ELF、动态链接、OBB 和引擎指纹。

## 公共 API

- `ParseApkArchive` / `ReadApkEntry`：读取单盘 ZIP32 central directory，按 central
  metadata 提取 stored/Deflate。local header 只校验签名、加密标志与载荷范围，名称、
  method/size/CRC 冗余字段及 data descriptor 不交叉匹配。按 API19 普通 ZIP 读取语义
  不比较 CRC；目录余量、EOCD 间隙/文件尾和 Deflate 完整流之后的尾部允许存在。
  `ReadStoredApkEntry` 是要求未压缩且两种尺寸一致的窄接口；
  `StoredApkEntryDataOffset` 复用相同定位与范围校验，发布 stored payload 物理偏移。
- `ValidatedStoredApkEntryData`：校验 stored 尺寸与 local payload 范围后借用只读 span；
  调用方必须保持 APK 存活且不可变。本接口不提供 CRC 完整性保证，不扫描/复制载荷。
- `ReadApkEntryRange`：stored 校验范围后只复制窗口；Deflate 完整扫描受界流并核对输出
  尺寸，只保留 32 KiB history，按 64 KiB 输出检查取消。
  `ApkEntryRangeStatistics` 的扫描量仅累计 Deflate 输出，stored 为 0；交付量独立累计。
- `ParseAndroidBinaryManifest` / `ReadAndroidManifest`：受检解析 binary XML chunk、UTF-8/
  UTF-16 string pool、元素与 typed attribute，产出 package/version/SDK、默认或自定义
  Application 类，以及按声明顺序保留 enabled、逐 intent-filter、alias target 的 Activity
  组件事实；Activity 另保留原始整数 launchMode（缺省 standard）及继承 application/package
  默认值的 taskAffinity（保留显式空值），上层决定支持边界。
  同名 Activity/alias 不拒绝或合并，按 API19 保留各声明及过滤器；
  `FindAndroidManifestActivityComponent` 按组件名取最后声明。alias 的受支持 theme/launchMode/taskAffinity
  在解析时继承首个先前声明的目标，不随后续同名目标改变。
  `NormalizeAndroidManifestClassName` 对齐 API 19 `buildClassName`，
  `ResolveLauncherComponent` 按声明序选择 enabled MAIN+LAUNCHER 过滤器，并使用最后声明的
  enabled 组件配置把 alias 映射到已声明 target，no-launcher/非法 alias 以 typed error 失败。Activity/`activity-alias`
  各自保存独立 `meta-data`，不与 application 或兄弟组件合并；
  保留 `android:exported`：显式布尔写入事实，缺省时按 API 19 在有 intent-filter 时为
  true。`application` icon 保留
  resource id，label 严格区分 resource id 与字面量；DVM-77 继续按声明序提取去重的
  `uses-permission` 与 application `meta-data`。metadata 按 API19 Bundle 保存 string、float、
  boolean、全部 INT 范围（含 color）；dimension/fraction 等非 RIGID 类型记录 diagnostics 后忽略。
  空 key 合法，同 owner 的最后有效写入覆盖；resource 非零优先，否则回退 value。
  `android:resource` 保留 id；value 引用在资源上下文中解析，byte-only API 无上下文时
  保留独立 value reference 交给运行时解析。
  清单 typed getter 支持资源引用、整数 truth 与字段缺省；getInteger 与 getInt 分别受检，
  可转换 string 属性按 TypedValue 转换，non-resource 字段仍限制原始字符串。
  ParseAndroidBinaryManifest 接受显式资源 resolver；ReadAndroidManifest 按需读取 APK ARSC，
  选择默认配置/API19/density160，框架引用经可选 external resolver，缺资源、循环/深度超限
  明确失败，不用资源 id 的非零性猜测值。versionCode 缺省/显式 0、空 versionName 合法；
  无 uses-sdk 保留缺席事实，有 uses-sdk 的 min 缺省 0、target 缺省随 min，重复声明后项更新。
  包名沿 API19 validateName（含 android 特例）。XML 使用声明 headerSize/size，扫描
  preamble 到首节点；首 START 必须是 manifest，根结束即停止，END 名称不另作栈匹配。
  节点最小扩展、属性读取与 chunk 范围仍受界；Res_value.size/res0 不作全局拒绝条件。
  未知节点跳过。字符串池按需解码，非法未使用字符串不拒包；访问转换失败返回空事实，
  按具体 getter 取默认值/忽略或失败。TYPE_STRING 的空事实保留为 Bundle null string，
  不替换为空串。字符串使用 UTF-16 code-unit 的 WTF-8 投影，保留 NUL 与孤立 surrogate；
  UTF8 按 API19 libutils mask 转换，保留声明长度校验并防止转换时越界。
  重复属性取首项，有 resource map 时 Android typed 字段按资源 ID 匹配；重复 application
  警告并跳过整棵后项子树。
  顶层 `<permission>` 另存名称、所属包、protectionLevel、group、flags、description 与
  自身 `meta-data`；权限定义不从 `uses-permission` 推导。重复声明保留，`registered`
  标明当前包首个 group 已知的定义；group 来源为固定 API19 平台与当前包声明，未知 group
  和后续同名定义不注册。protectionLevel 仅按 API19 fixProtectionLevel/-1/flags-base 校验，
  permissionFlags 不另限位，非资源 description 忽略。
  uses-permission 缺失/非字符串名称忽略、空串合法，按声明去重并处理整数 maxSdkVersion。
  DVM-112 保留 application enabled、独立的 service 名称/enabled/过滤器事实；过滤器
  保存 action/category 与 data 条件存在标记，不能将未解析的 data 条件当作没有条件。
  service 名称沿用 API19 buildClassName，缺名/空名/非法名明确失败；
  service 还保存 exported、继承或覆盖的 process/permission、自身 `meta-data` 和
  stopWithTask/isolatedProcess/singleUser flags；singleUser 强制 exported=false。
  服务不参与 Launcher 选择，本层不执行服务解析、实例化或生命周期。
  receiver 独立保存规范类名、enabled/exported、继承或覆盖的 process/permission 与自身
  `meta-data`；exported 缺省取决于 intent-filter 是否存在。receiver 不混入 Activity 列表。
  service/receiver 与 Activity 一样保留所有同名声明，不合并配置或过滤器；运行时按名取后项，
  intent 查询保留逐声明匹配并按组件去重。provider 仍无清单投影，不新增 provider 子系统。
  receiver 同样逐个保存 intent-filter，不合并 action/category；共用过滤器事实还保留
  signed priority、label/icon，供上层查询返回与排序。
  Manifest 顶层 supports-gl-texture 按 ADR-0128 保留 android:name 原始声明，按声明序去重，
  包括未知扩展，供上层压缩纹理策略使用；缺名、空名或名称取值失败跳过该条并记诊断，不拒包。
- ParseCompiledXmlDocument：拥有输入 bytes 的不可变编译 XML 事实，保留原 string pool
  索引/受检按需 WTF-8 转换、resource map、namespace/line/comment、完整 typed/raw 值、
  id/class/style 原始一基索引、namespace/标签 start/end 与 parent node 关联。
  不按名称猜资源 ID，不合并同名不同 namespace 属性，不解引用资源或补布局默认值。
  新接口拒绝不支持的 chunk/位置、非法 pool/special 索引及未配对标签/namespace；
  字符串转换失败抛异常，缺席索引单独返回 nullopt，空串仍为有效值。
  Elements/PullEvents 为同一事实的单向 Legacy 适配；旧入口复用 decoder 并保留
  原有宽松结构策略与坏字符串 fallback，不因新 cursor 准入收紧生产 Legacy。
- ParseCompiledStringPool：独立 ResStringPool byte slice 复用不可变文档的字符串查询，
  不产生 XML 节点。新入口及 strict XML 检查池 header/index/data/style 区间；
  HasStringStyles 表示池含样式数据，integration 的 plain-string 接缝必须拒绝该池，
  不将 span 当普通字符串。旧 Elements/PullEvents 的宽松解码边界不变。
- `ParseBinaryXmlElements`：受检遍历通用 Android binary XML，按文档序返回标签、父索引
  与 generic typed attributes；每个 attribute 保留 namespace/name、value type/data 与
  raw/typed string。旧布局字段暂作单向兼容 adapter，新 widget 语义不得进入 loader。
  `ParseBinaryXmlPullEvents` 从同一受检文档发布编译后仍保留的 START/TEXT/END 高层事件；
  string pool 按需转换，UTF16 保留 code units（WTF-8 投影），UTF8 沿 API19 mask 转换；
  坏字符串取 null，raw_string 保留缺席。首节点前后池替换，节点后的池跳过。
  空名称和未闭合/错名/多余 END 不做格式拒绝；事件深度沿 XmlBlock 计数，父索引仅用于
  START 投影。chunk、节点扩展、属性及字符串读取范围受检，残缺尾部仍明确失败。
- `ParseArsc`：严格读取 resources.arsc（ResTable/string pool/package/type/entry），
  产出 resid ↔ (type, name, typed simple value/文件路径) 双向事实；Res_value type/data
  原样保留给上层有界 string/color/dimension/reference resolver；DVM-121 保留复杂 bag 的
  parent/typed items，重复 child key 按声明序保留；校验 entry/value/map 边界与字符串索引。
  原全局字符串池 chunk（含 style 元数据及索引）与逐 entry 的 typeSpec 配置变更 flags
  无损保留，供上层资源池身份及 TypedArray 六槽输出使用；loader 不持有 theme 状态。
  零包表合法，实际 package chunk 数须与声明一致，全局字符串池不可缺失；DVM-148 同时保留
  orientation、density、SDK、screenLayout size 与屏幕 dp 配置事实，由上层按固定 API 19
  的 qualifier 优先级选择候选；仍不扩展
  locale/动态 qualifier，越界/截断即失败。
- `ReadApkArmNativeLibraries` / `ReadApkNativeLibraryInventory`：稳定枚举
  `lib/armeabi[-v7a]/*.so`，拥有解压字节与小写 SHA-256，并按 ABI、entry basename
  soname、`lib<logical>.so` logical name 建立不可变 inventory；此阶段不解析 ELF
  `DT_SONAME`；selected view 还可枚举当前 ABI 或按精确 APK entry 查询，但不允许跨
  ABI。动态装载保留 entry basename 为规范 inventory identity，若 ELF `DT_SONAME`
  不同则将其作为同一库的链接别名，不以不一致本身拒载。
- `ResolveApkProcessAbi`：只取 runtime-supported ABI 有序列表与 inventory ABI 的首个
  交集；默认优先 `armeabi-v7a` 再 `armeabi`，不读取 Profile/hash。空 inventory 对
  native ABI resolver 仍是 typed `native_abi_required`，rootless Java-capable process
  不调用该 resolver；`ApkSelectedNativeLibraries` 将后续 soname/logical/entry lookup
  永久限制在已选 process ABI。
- `ParseDex(bytes, annotation_limits={})`：从不可信字节解析 DEX 035..040 header、固定 ID 表范围和有序
  `map_list`，并严格解码字符串、类型 descriptor、prototype type_list/shorty；交叉验证
  field/method ID、class_def、接口列表及所有索引与 UTF-16 长度；DVM-69 投影
  InnerClass/EnclosingClass/EnclosingMethod/MemberClasses/Throws system annotations；
  另外保留 class/field/method/parameter runtime-visible 递归 encoded value、
  `AnnotationDefault` 与 class/field/method 的 Signature 字符串片段拼接；Signature 保持 system
  元数据，不作为运行时注解发布。复制、拼接和参数矩阵均计入相同受检预算；encoded value
  kind/value_arg 按 DEX 宽度与复合/null/boolean 规则 fail closed；不执行任何字节码。
  注解 item/set 在单次解析内按偏移缓存受检、不可变结果；引用处仍校验 member owner
  与 AnnotationDefault 声明类型。参数 item 存在性与原始 list.size 独立保存，不要求等于原型
  参数数，不补 synthetic 参数；显式空列表与无 item 不同。返回元数据保持拥有型，深复制另计预算。
  单项保留 depth=16、elements=1024、array=65535，并有独立 item node 上限；
  整体工作量默认 `min(65536+8*data_size, 16 Mi units)`，存储默认
  `min(1 MiB+384*file_size, 256 MiB)`，调用方可传通用硬上限。
  工作量计入解码、缓存查找/引用遍历、投影及副本；存储按宿主元素大小、容器容量增长、
  map 节点保守开销及副本计量，包含临时解析缓冲，不等同进程 RSS 或 guest 堆。
  长度/范围与预算在 reserve/copy 前校验；`resource_limit` 错误保留偏移、预算类型、
  已用量、请求量及上限，`annotation_statistics` 提供成功解析的独立计量。
- `ReadDexClassData(bytes, image)`：解码 class_data 的 delta member 索引与 access flags，
  对 code_item 只提取寄存器、参数、try 数和指令 code-unit 数，不解释指令。
- `AnalyzeDexL1(image, class_data, libraries, signatures)`：输出应用类/方法/native 数量、
  总指令与渲染回调规模、Java 厚度和 DEX 执行候选；引擎规则由声明式 catalog 注入。
- `ReadDexMethodCode(bytes, image, code)`：受检复制方法指令 code-unit 流并解码
  try/catch——try 区间有序、不重叠且落在指令流内；空区间留给 dexvm 惰性方法预检
  产生 VerifyError。handler 偏移必须命中
  encoded handler 列表的真实条目，type 索引与 handler 地址全部受检；不解释指令，
  分支目标/payload 引用等指令级校验属 dexvm 链接预检（ADR-0017）。
- `ReadDexStaticValues(bytes, image, offset)`：受检解码 class_def static_values 的
  encoded_array；支持 boolean/byte/short/char/int/long/float/double/string/type/null，
  size 超界、索引越界与 annotation 等不支持形态均明确失败；ULEB128 允许非最短编码。
- `ParseElf32Arm(bytes)`：从不可信字节解析 little-endian ELF32/ARM ET_EXEC/ET_DYN，
  返回入口、ARM flags、程序头和未知 tag 不丢失的动态项事实模型。
- `ReadElf32DynamicInfo(bytes, image)`：只在 file-backed `PT_LOAD` 范围内解析动态字符串表、
  `DT_NEEDED` 与 `DT_SONAME`；scalar 标签取最后值，缺失必要表、越界和未终止字符串明确失败。
- `BuildElf32LoadPlan` / `LoadElf32Arm`：按宿主页合并 `PT_LOAD`，以临时 RW 装载文件、
  顺序清零 BSS，最后切换为合并后的 W^X 权限；失败时回滚本次新增映射。
- `ReadElf32SymbolTable`：通过边界校验后的 SysV/GNU hash 推导 dynsym 数量，解析名称、
  binding、type、visibility 和 section index；`IsExported` 只接受已定义且外部可见的全局/弱符号。
- `ReadElf32Relocations`：同时解析普通与 PLT ELF32 ARM REL 表，保留目标、符号索引、
  原始类型与表类别；DT_RELA、非 REL 的 DT_PLTREL 和不安全的实际读取/目标明确失败。
- `ApplyElf32ArmRelocations`：原子应用 `NONE/ABS32/REL32/GLOB_DAT/JUMP_SLOT/RELATIVE`，
  写入期间将装载区域切为 RW，成功或失败后均恢复最终 W^X 权限。
- `BuildElf32LinkNamespace` / `ResolveElf32Symbols`：构造递归依赖闭包、依赖优先装载顺序
  与根模块优先的广度查找作用域，按 ELF local/weak/hidden/protected 规则预解析地址。
- `ExtendElf32LinkNamespace`：事务式追加 `dlopen` 模块，保留既有索引与主作用域，
  为动态根构造独立依赖/查找作用域；有版本定义的 provider 按依赖库与版本名精确匹配，
  API 19/22 无版本定义 provider 按 Android 旧 linker 语义回退到名称匹配。
- `Elf32DynamicLinker`：管理稳定 `dlopen` handle、重复打开引用、handle 内 `dlsym` 和
  共享依赖引用，返回需执行的依赖优先初始化与根优先反初始化模块顺序；`Address` 与
  同一活跃命名空间实现 `dladdr` 的模块、load base 和最近 dynsym 反查。
- `LoadElf32ModuleNamespace`：一次完成多模块事实解析、依赖排序、映射、版本化符号解析与
  ARM REL 应用；可注入 builder 追加只参与查找的绝对 HLE 边界模块，且不得删除、替换
  或重排 guest 身份；失败只撤销本次成功新增的映射，不恢复或修改既有映射及受保护 payload。
- `ExtendElf32ModuleNamespace`：在 process-lifetime namespace 上事务追加 guest modules，
  为动态 root 建立独立 scope，并只映射/重定位本次 guest 输入；extender 可在其后追加
  HLE-only module，但不得重排/替换输入。失败只撤销本次新增映射并保留原始错误，成功保留既有索引。
- `ReadElf32LifecycleInfo`：解析 `DT_INIT/FINI`、init/fini arrays 与 `PT_ARM_EXIDX`，
  标量动态标签取最后值，数组指针/尺寸独立缺省，尺寸按完整指针截断；
  EXIDX 取首段并按 memory size/8 计数。实际读取仍要求 file-backed 且范围受检。
- `ReadElf32TlsInfo`：解析唯一 `PT_TLS` 模板，保留初始化字节、BSS 大小与对齐，
  并要求模板由唯一 `PT_LOAD` 覆盖且不回绕。
- `ReadElf32SymbolVersions`：解析 `DT_VERSYM/VERDEF/VERNEED`，将每个 dynsym 映射为
  local/global/definition/requirement 及版本名和依赖库；GNU base definition 的索引 1
  保持 global，旧版 Android 的 local undefined 按无版本导入解析；链表、flags 或索引
  不一致时明确失败。
- 静态导入审计消费者复用 ELF/dynsym/version 事实及 BuildElf32LinkNamespace/LookupElf32Symbol，
  逐根只查 NEEDED scope、保留 visibility/版本/旧 provider 回退规则；聚合未定义 GLOBAL 与
  WEAK 的查找缺口，另标明是否被非 NONE 重定位引用；未引用缺口保留诊断但不拒绝，
  不修改解析表、不执行 guest。
- 后续 Work Unit 在该事实模型上增加映射、符号、重定位和链接命名空间，不重复解析字节。

## 不变量

- APK/AXML/ARSC/DEX/ELF 的 little-endian 标量解码复用 `core::ReadLittleEndian`，
  但每种格式仍在读取前执行自己的范围检查并保留 typed error 与错误上下文。
- 输入不可信；所有偏移、长度和整数运算必须校验。
- APK entry 名称保留非 ASCII 字节，拒绝 NUL；提取路径仍拒绝绝对路径及 `.`/`..`
  segment，保留 SAFETY 边界。重复名称、加密、多磁盘与不支持压缩方法明确失败；
  Deflate stored/fixed/dynamic block 的输出尺寸和回溯距离仍受界，空距离表只允许不使用距离码。
- Manifest 必须有合法包身份，版本可为缺省/显式 0。容器、扩展和属性读取范围受检，
  不把 per-string 终止字节、无用坏字符串或 END 标签名匹配作为全包准入条件。
- native library catalog 只接受 ABI 与 basename 之间恰有一级的 `.so` 路径，空字节失败；
  零长度文件仍发布 SHA-256/ABI/identity，实际 ELF 装载时才失败。
  catalog 以完整 entry path 稳定排序，不隐含 main library 选择；inventory 同一 ABI 的
  soname/logical name 必须唯一，ABI resolver 禁止读取 `so_sha256` 或猜 root library。
- DEX header 至少 0x70 字节且只接受 little-endian 035..040；较大 header 不得与后续 map
  section 重叠；固定表对齐，link/data 按字节范围校验，data 不必结束于文件尾。
  空固定表允许无 map 或匹配的零 count map；unused 不作拒绝条件，非空表与有序 map 事实须一致。
- DEX Modified UTF-8 完整受检；ULEB128 允许非最短编码，仍限制五字节、32 位及读取范围。
  type descriptor 允许 API19 合法 Unicode 并以 UTF8 发布；shorty 仍限 ASCII，
  type/proto 的索引、参数列表和 shorty 须互相一致。
- field 的声明类必须是 class descriptor；method 的声明 owner 允许 class 或数组
  descriptor（数组继承 `Object.clone()`，AOSP libdex 与 dex-format 均允许），仍拒绝
  基元与 `V`；class_def 类型唯一，父类、接口、源码与 data offset 全部受检，只保留
  annotation/class_data/static value 偏移事实。system annotation directory/set/item、
  encoded value、type/method/string index 与 nesting 均受界；只输出反射所需 host facts。
- class_data 成员必须归属当前类、严格递增且 static/direct 类别与 access flags 一致；
  native/abstract 不得有 code，其他方法必须有对齐且完整的 code_item。
- Java 厚度只由版本化阈值和可查询计数推导；引擎指纹不得在生产代码硬编码，必须由
  library basename 与所需导出符号组成的声明式签名提供。
- 小端字段先在足够宽的无符号类型中组合，最终结果只做一次显式收窄。
- `PT_LOAD` 必须满足 file size ≤ memory size、guest 地址不回绕、文件范围有效且对齐同余。
- `PT_DYNAMIC` 采用首段，文件范围完整、条目尺寸正确并由 `DT_NULL` 终止；
  共享 scalar tag 取最后声明，重复 NEEDED 仍逐项保留。
- 动态虚拟地址必须能完整翻译到单个 file-backed `PT_LOAD`，不得读取 BSS 或猜测文件偏移。
- dynamic-info 与 lifecycle 读取共用唯一的虚拟地址→文件偏移和唯一 dynamic tag 私有实现，
  各 API 仍提供自己的 typed error 文案。
- load bias 必须按宿主页对齐；ET_EXEC 不允许 bias；非零入口必须落在 executable `PT_LOAD`。
- 任一宿主页需要同时 W+X 时拒绝装载，不以兼容为由放宽权限。
- dynsym 必须由有效 SysV 或 GNU hash 限定数量；GNU chain 必须在 file-backed 范围内终止。
- REL 先处理 PLT 再处理普通表；尺寸按完整条目截断，允许表重叠与重复目标，
  重复目标依次累积后事务提交。NONE 不读取目标或符号；其他目标可在 BSS，
  但 4 字节范围必须唯一属于 `PT_LOAD` memory，实际表读取必须 file-backed。
- 装载只解析非 NONE 重定位的非零符号索引；未引用 dynsym 事实保留，不作为强缺口拒绝。
  `ResolveElf32Symbols` 的显式全表模式仍供诊断调用，传入 relocation table 时按引用过滤。
- ELF header/program header/dynsym 使用固定 ELF32 布局；EI_VERSION、e_ehsize、
  e_phentsize、DT_SYMENT/RELENT 不作为冗余一致性门槛，e_version 仍校验。
- `DT_NEEDED`、TLS、exidx、符号版本、init/fini 使用同一链接模型。
- 输出只描述事实，不猜测具体游戏身份。

## 禁止

- 不执行 guest 指令，不直接调用 HLE 或宿主图形。
- 不按游戏名、导出符号 if-else 选择启动路径。

## 测试

`tests/loader/` 的单元与畸形输入契约测试；三平台 warnings-as-errors 构建同时约束
小端字段、encoded-value 宽度与 UTF-16 host projection 不依赖实现相关的隐式整型转换。

DVM-105：空 APK inventory 可建立空 ApkSelectedNativeLibraries 视图，以加载系统 JNI。
非空 inventory 缺少指定 ABI 仍拒绝；ResolveApkProcessAbi 对空 inventory 的报错保持。

DVM-121：Manifest application/Activity 的 theme 保留 resource id；activity-alias 不覆盖
目标 Activity 的 theme。loader 不应用主题或展开资源继承，0/缺省回退由 session 处理。
