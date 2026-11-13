# OGPlay 开发设计参考

> 按需参考：仅在用户明确要求读取或使用本参考时查看。日常开发不主动加载，
> 不作为默认开发指令，也不替代项目现行契约、ADR 或具体任务要求。

本文件只讨论能力范围与实现路线，不规定每次开发的操作清单。
工作流、验证和文档更新见 [AGENTS.md](../../AGENTS.md)，操作入口见
[开发方式手册](README.md)。

## 方法一：ROM 原样运行，宿主接缝按需实现

**以游戏持续可玩为目标：Android Java 层整体来自 API19 ROM jar 并原样执行，
由真实触达决定宿主要接通哪些 native 方法与进程内服务；接缝内保留必要的 Android 可观察行为，
以最小充分的通用实现降低长期开发与维护成本。**

启动类路径是 ogplay.jar + 原样 ROM jar（core、framework、framework2、ext、conscrypt、
telephony-common、android.policy），类型结构、普通 Java 状态和算法都由 ROM 提供，
不再按需裁剪或手写 framework 类。需要取舍的是宿主一侧：ROM 代码在 native 方法、
ServiceManager 查到的系统服务、系统 ELF 与设备资源处落到 OGPlay。类能加载、
方法能链接，不代表这些接缝已经接通；进入 native/GLES 也只是阶段目标，
后续经 JNI 使用的资源、输入、音频、存档、线程和生命周期仍需逐项识别。

本方法用于决定“接通多少”；方法二用于决定“用什么底座兑现已确定的契约”。

### 按接缝类型选择默认策略

| ROM 触达的接缝 | 默认策略 |
| --- | --- |
| 类加载、继承、接口与签名解析 | 由 ROM jar 提供，宿主无需动作；不据此认定对应 native 或服务已实现 |
| ROM Java 的 native 方法 | 按真实触达实现；未实现的成员明确失败并记账（[ADR-0147](../adr/dexvm.md#adr-0147)） |
| ServiceManager 返回的系统服务 | 进程内本地 AIDL 服务；每个 AIDL 方法都要有声明：如实实现，或审定后显式拒绝并记账 |
| 定位、电源、电话、网络等设备状态 | 按统一模拟设备状态（普通手机、用户关闭相关功能）给出 API19 服务在该状态下的结果，不接宿主真实服务 |
| UI 展示类属性、无返回值的 UI 设置 | 忽略并 `RecordUiDegraded`；有实际危害的属性（如密码掩码）不降级 |
| 游戏持续运行的关键路径 | 实现实际依赖的行为，不能以“只需过启动”为由省略 |

调用次数少、只在启动时调用、属于广告或辅助组件，均不足以证明其行为可省略。
返回值、异常、对象身份、状态变化或回调被后续 ROM 或游戏逻辑消费时，应纳入必要契约。

### 通用性约束

- **游戏发现需求，通用能力定义实现。** 不依赖游戏名、包名、调用栈、特定输入常量或
  当前样本的调用顺序；不能将某个游戏不使用的分支直接认定为无意义。
- **裁剪接缝范围，不裁剪成样例。** 已接通的 native 或服务方法覆盖一般合法输入、重复调用和
  对象隔离；范围外保留可扩展边界，不为尚无需求的能力提前建设完整系统。
- **简化宿主机制，不改 ROM 行为。** 服务内部可用简单数据结构代替 system_server 的数据库和
  IPC，但 ROM Java 的状态、算法和控制流程原样保留；相关查询、操作、状态与资源生命周期
  必须一致，复用唯一 VFS、Clock、线程及 UI 底座。
- **合法缺席优先于伪实现。** Android 契约允许的不可用结果可以是正式实现，并非一律抛异常；
  不用 null 冒充应返回的对象。UI 展示类边界见 `AGENTS.md`。
- **近似需有边界与证据。** 降低非核心表现保真度时，明确记录差异、通用适用条件及验证结果，
  不宣称与 AOSP 等价。不能仅因当前游戏不在意，就改变时序、线程归属或控制流程。

### 实现路线与扩展条件

ROM 类整类原样运行，宿主只实现 native 和服务接口；不替换 ROM 类，也不用 intrinsic 覆盖
ROM 的非 native 方法。ogplay.jar 中现存的同名替换类及非 native 覆盖按阶段计划移除，
新代码不得增加。服务可以是本进程 Binder/Stub 对象，但不能借此另建 ROM 已有的
普通 Java 算法、状态或资源所有权，也不为有限需求补齐进程外系统。

扩展范围的依据是新的真实依赖：区分 ROM 类可链接、实际调用到宿主接缝，以及影响后续流程的
返回值或回调；先判断模拟设备状态下的合法不可用路径是否足够，再决定如实实现。
类型可链接、有限行为支持和游戏持续可玩是不同层次，不能相互替代。

通用性不等于一次接通更多接口，而是让已接通的小范围具有稳定语义，其他游戏可复用，
后续需求可扩展。它不能保证所有游戏兼容，但应避免为推进一个游戏而制造其他游戏的障碍。

## 方法二：契约驱动兼容，底座最小充分

**以应用可观察行为为兼容契约，Java 层以 ROM 原版为准；宿主接缝优先复用成熟实现，
对不必复刻的系统内部机制采用 OGPlay 自有的最小充分底座，并通过测试证明兼容性。**

OGPlay 面向游戏进程的通用能力，不以复刻 Android 系统为目标。公开 API/ABI 与约定行为
对齐 API19 ROM，宿主侧的服务、进程和存储结构按实际需要实现。“最小充分”指完整兑现
已声明范围所需的最小实现；优化目标是长期总成本，而非代码最少、依赖最少或最快越过报错。

### 适用条件

- 能明确划分应用契约与内部机制，内部可经 native 或服务接口替换。
- 照搬 AOSP 底座会引入超出项目范围的进程外系统组件，或显著增加维护成本。
- 现有 OGPlay 基础设施或成熟库可实现所需行为，并能通过独立测试证明。

### 实现路线的选择

Java 层不做路线选择，一律随 ROM 执行；下表只用于宿主接缝背后的实现，三种路线可以组合。

| 路线 | 优先采用的情况 | 必须评估的代价 |
| --- | --- | --- |
| 随附 API19 系统 ELF | ROM native 依赖的系统库可在游戏进程内独立运行 | ABI、依赖闭包、宿主适配和补丁维护 |
| 复用成熟后端 | 图形、2D 绘制、密码/TLS、数据库等复杂机制已有可靠实现（ANGLE、Skia、SQLite 等） | 许可证、平台支持、安全更新和适配成本 |
| 自有最小充分底座 | 原版依赖进程外系统服务，但所需契约可用进程内服务或宿主模块兑现 | 语义维护、测试覆盖、并发隔离和后续扩展成本 |

只有在契约可兑现、总成本更低且边界可验证时，才选择自有实现。不能仅因“属于底层”
就重写，也不能用减少库数量替代技术判断。既有 Bionic、BootDex、ANGLE 等选择继续遵循
各自契约。版本与安全策略偏离 API19 时，应显式记录差异，
不能一边改变行为，一边宣称完全一致。

### 不可简化与可替换的边界

| 必须按约定兑现的应用契约 | 可按需替换的内部机制 |
| --- | --- |
| 类/方法/ABI、类型关系、Provider 和服务查找 | 服务内部数据结构、宿主适配方式 |
| 返回值、异常、状态转换、真实回调 | 进程内服务替代 system_server、daemon 与 IPC |
| 生命周期、线程、资源释放、持久化与隔离 | 存储布局、索引、缓存和资源组织 |
| 格式互操作、权限与信任、密钥导出规则 | 满足相同约束的既有库或自有底座 |

“内部黑盒”须有证据：即使不是公开 SDK，应用仍可能通过反射、JNI、显式 Provider、
文件或 native 符号观察它。已被依赖的行为应纳入支持契约，或明确记为不支持，不能静默改变。

范围一旦声明，成功与失败语义都必须兑现：异常、回调、并发、持久化、隔离和资源释放
不是可选完善项。范围外明确失败并记账；明确失败只是边界诚实，不代表该能力已经兼容。
架构应允许同类应用复用和能力自然扩展，不为当前游戏的输入特制实现。

### 示例：证书信任与 TLS

保留 AOSP SSL/KeyStore 公开 API，自有 Java Provider 连接 guest libssl/libcrypto 和唯一
NetworkRuntime；AndroidCAStore 使用版本化 CA 包和只读视图，无需复制 Android 系统服务。
AndroidKeyStore 若确有需求，可另做应用隔离的软件密钥底座，不必作为 TLS 前置依赖。

但 AndroidCAStore/AndroidKeyStore 类型本身可被应用访问：查询、异常、隔离与密钥导出等
语义仍必须兑现。不能用返回成功、跳过验证或伪造硬件保护代替实现。具体方案见
[信任验证与 TLS](../design/storage-security.md)及 [ADR-0066](../adr/dexvm.md#adr-0066)。

## BootDex 与 dexvm 链接定向回归

JNI 一致性：`python3 tools/conformance/jni_tests.py --coverage --check-baseline`（自测 `--self-test`；原理见 [诊断与测试](../design/diagnostics-testing.md)）。

改 DexVM/ROM native 语义后，按 AGENTS.md 选择直接相关的一致性用例；
`python3 tools/conformance/dalvik_tests.py --check-baseline` 是完整套件入口，未经明确要求不跑全量。目标用例转 pass 后用
`--update-baseline` 提交新基线（有回归时拒绝更新）。

涉及 BootDex 或 dexvm 链接的改动，按影响范围仅构建 `ogplay_tests`，使用：

```sh
build/release/ogplay_tests -tc="*survey*,*DVM-103*,*DVM-254*,*BootDex*,*boot dex*,*PointerCoords*"
```

## dexasm 夹具与 AOSP 源码对照

`tools/dexasm.py` 接受受限文本 IR，输出确定性的 DEX 035；不承诺 smali 兼容。
按改动选用已有夹具；确需新增边界时再写最小输入，不引入 Android SDK 作为夹具生成前置。

```sh
python3 tools/dexasm.py --input /tmp/fixture.dexasm --output /tmp/fixture.dex
```

需要校验汇编器本身时运行 `python3 tools/dexasm.py --self-test`：golden 锁定字节，
`tests/dexvm/dexasm_readback_tests.cpp` 用生产解析器回读结构，两者验证的问题不同。
指令预期值在用例注释记录对应 AOSP 文件/函数或规范章节；反例应保留 class/method/DEX PC。

本地 `.aosp/api19/dalvik/` 的 KitKat 资料仅作参考，不默认编译或链接进运行时。
opcode 修改可用 `tools/generate_dexvm_opcodes.py --verify-aosp <资料根目录>` 核对固定来源，
以脚本要求的 `opcode-gen/bytecode.txt`、`libdex/DexOpcodes.h` 等锚点为准。

| 问题 | AOSP 对照入口 |
| --- | --- |
| 指令、NaN、溢出与检查顺序 | `vm/mterp/c/OP_*.cpp`、`docs/dalvik-bytecode` |
| DEX 布局与结构反例 | `libdex/DexFile.h`、`DexSwapVerify.cpp`、`docs/dex-format` |
| 链接预检、类初始化和解析 | `vm/analysis/CodeVerify.cpp`、`vm/oo/Class.cpp`、`Resolve.cpp` |
| 赋值关系、异常与 monitor | `vm/oo/TypeCheck.cpp`、`vm/Exception.cpp`、`vm/Sync.cpp` |
| 根枚举、JNI 和线程附着 | `vm/alloc/MarkSweep.cpp`、`vm/Jni.cpp`、`vm/Thread.cpp` |

先对规范和同代实现，再用精确夹具/设备行为裁决差异并记录在测试注释。
规范与实现冲突时，以目标时代真实实现行为为兼容依据；不凭现代 JDK 行为改 oracle。
只参考语义，不整体搬入 dvm、mterp 汇编/JIT、odex/quickened 或其宿主布局假设。
原版 Java 随 ROM 执行，不根据参考资料重写普通集合等算法。
可选 dexdump 交叉解析不属于默认依赖，也不能以工具缺席替代已有解析回读。
