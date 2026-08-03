# 模块：loader

## 职责

解析 APK/XAPK/APKM/APKS/目录、二进制 Manifest、DEX L1、ELF、动态链接、OBB 和引擎指纹。

## 公共 API

- `ParseElf32Arm(bytes)`：从不可信字节解析 little-endian ELF32/ARM ET_EXEC/ET_DYN，
  返回入口、ARM flags、程序头和未知 tag 不丢失的动态项事实模型。
- `ReadElf32DynamicInfo(bytes, image)`：只在 file-backed `PT_LOAD` 范围内解析动态字符串表、
  `DT_NEEDED` 与 `DT_SONAME`，重复、缺失、越界和未终止字符串均明确失败。
- `BuildElf32LoadPlan` / `LoadElf32Arm`：按宿主页合并 `PT_LOAD`，以临时 RW 装载文件、
  顺序清零 BSS，最后切换为合并后的 W^X 权限；失败时回滚本次新增映射。
- `ReadElf32SymbolTable`：通过边界校验后的 SysV/GNU hash 推导 dynsym 数量，解析名称、
  binding、type、visibility 和 section index；`IsExported` 只接受已定义且外部可见的全局/弱符号。
- `ReadElf32Relocations`：同时解析普通与 PLT ELF32 ARM REL 表，保留目标、符号索引、
  原始类型与表类别；RELA、元数据残缺、表重叠及不安全目标均明确失败。
- `ApplyElf32ArmRelocations`：原子应用 `NONE/ABS32/REL32/GLOB_DAT/JUMP_SLOT/RELATIVE`，
  写入期间将装载区域切为 RW，成功或失败后均恢复最终 W^X 权限。
- `BuildElf32LinkNamespace` / `ResolveElf32Symbols`：构造递归依赖闭包、依赖优先装载顺序
  与根模块优先的广度查找作用域，按 ELF local/weak/hidden/protected 规则预解析地址。
- `ExtendElf32LinkNamespace`：事务式追加 `dlopen` 模块，保留既有索引与主作用域，
  为动态根构造独立依赖/查找作用域；版本化导入按依赖库与版本名精确匹配。
- `ReadElf32LifecycleInfo`：解析 `DT_INIT/FINI`、init/fini arrays 与 `PT_ARM_EXIDX`，
  对重复、元数据残缺、非对齐及非 file-backed 范围明确失败。
- `ReadElf32TlsInfo`：解析唯一 `PT_TLS` 模板，保留初始化字节、BSS 大小与对齐，
  并要求模板由唯一 `PT_LOAD` 覆盖且不回绕。
- `ReadElf32SymbolVersions`：解析 `DT_VERSYM/VERDEF/VERNEED`，将每个 dynsym 映射为
  local/global/definition/requirement 及版本名和依赖库，链表或索引不一致时明确失败。
- 后续 Work Unit 在该事实模型上增加映射、符号、重定位和链接命名空间，不重复解析字节。

## 不变量

- 输入不可信；所有偏移、长度和整数运算必须校验。
- 小端字段先在足够宽的无符号类型中组合，最终结果只做一次显式收窄。
- `PT_LOAD` 必须满足 file size ≤ memory size、guest 地址不回绕、文件范围有效且对齐同余。
- `PT_DYNAMIC` 最多一个、文件范围完整、条目尺寸正确并由 `DT_NULL` 终止。
- 动态虚拟地址必须能完整翻译到单个 file-backed `PT_LOAD`，不得读取 BSS 或猜测文件偏移。
- load bias 必须按宿主页对齐；ET_EXEC 不允许 bias；非零入口必须落在 executable `PT_LOAD`。
- 任一宿主页需要同时 W+X 时拒绝装载，不以兼容为由放宽权限。
- dynsym 必须由有效 SysV 或 GNU hash 限定数量；GNU chain 必须在 file-backed 范围内终止。
- REL 表必须 file-backed 且互不重叠；目标可在 BSS，但 4 字节范围必须唯一属于 `PT_LOAD` memory。
- `DT_NEEDED`、TLS、exidx、符号版本、init/fini 使用同一链接模型。
- 输出只描述事实，不猜测具体游戏身份。

## 禁止

- 不执行 guest 指令，不直接调用 HLE 或宿主图形。
- 不按游戏名、导出符号 if-else 选择启动路径。

## 测试

`tests/loader/` 的单元与畸形输入契约测试。
