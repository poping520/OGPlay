# 02 · 核心架构

## 1. 模块位置与依赖方向

dexvm 是 runtime 层新子模块，遵守 ADR-0013 的 runtime 子模块边界模式：

```text
src/runtime/dexvm/          解释器内核、类链接、对象模型、intrinsic 分派接口
include/ogplay/runtime/dexvm/
tests/dexvm/                一致性夹具与契约测试
data/dexvm/                 opcode 目录（声明式 JSON）
```

依赖关系（只允许向下）：

- `dexvm` → `core`（日志/错误/能力账本）、`loader`（DEX 镜像与代码读取）、
  `runtime/jni`（对象身份、引用、异常、签名、MUTF-8、native registry、monitor）。
- `dexvm` **不依赖** `runtime/framework`。intrinsic 由 dexvm 定义的显式接口
  `PlatformClassProvider` 注入，`runtime/integration` 在 session 装配时把
  framework 的实现接进来（依赖倒置，符合"回调必须通过显式接口"）。
- `session` 依赖 `dexvm` 完成生命周期编排；`frontend`/`agent` 不直接触碰 dexvm。

每个新目录建立 `MODULE.md`；`src/runtime/MODULE.md` 与 `docs/modules/INDEX.md`
同步登记子模块行。

## 2. 现有资产复用表

dexvm 是加法，不是重写。下表是设计时已核对的复用点：

| 现有资产 | 能力条目 | 在 dexvm 中的角色 |
| --- | --- | --- |
| DEX L1 解析 | `loader.dex_l1` | 镜像、string/type/proto/field/method 表、class_def、code 元信息（本方案扩展出指令流读取，见 §4） |
| JNI 对象身份/引用/异常 | `runtime.jni` | 对象模型基座；"宿主对象或 VM 对象"双形态是 MODULE.md 既有承诺 |
| JNI class registry / field store / array / string store | `runtime.jni_guest_*` | 迁移进统一 JavaObjectModel（见 §6），存量测试保留 |
| JNI native registry（RegisterNatives + `Java_` 导出名） | `runtime.jni_guest_native_registration`、`runtime.jni_native_export_names` | 解释器 `invoke-*` 命中 native 方法时的解析源 |
| A32 guest call 执行器（tick 预算/切片） | `runtime.a32_guest_call_executor` | 解释器调用 native 方法的执行后端 |
| JNI monitor table | `runtime.jni_guest_monitors` | `monitor-enter/exit` 指令与 synchronized 方法的实现（04 §4 扩展 wait/notify） |
| framework HLE 与 platform identity | `runtime.framework_*`、`runtime.android_legacy_platform_identity` | android.* intrinsic 的实现存量 |
| 统一 Clock、结构化日志、能力账本 | `hal.clock`、`core.*` | 时间源、诊断、缺口记账 |
| Scenario runner | `automation.*` | exact-title 唯一验证出口不变 |

## 3. 运行全流程（dexvm 生命周期）

```text
session bootstrap（identity 精确匹配，复用现有 profile bootstrap）
  → loader: APK → classes.dex → DexImage（L1）
  → dexvm: 类元数据注册 + intrinsic 目录合并（同一命名空间，冲突即失败）
  → dexvm: 实例化入口 Activity（类名来自二进制 Manifest，见 04 §2）
  → 解释执行 onCreate：真实触发全部 nativeInit / JNI 副作用
       ├─ invoke native 方法 → 签名驱动编组 → A32 执行器 → guest 机器码
       └─ guest 机器码回调 JNI → invocation engine 第三路由 → 解释器嵌套帧
  → GLSurfaceView intrinsic 捕获 setRenderer 注册
  → 宿主渲染循环逐帧调用解释执行的 onDrawFrame / 输入回调
  → teardown：沿用现有 interrupt → join → finalizer → detach → shutdown 顺序
```

## 4. 指令目录与代码读取

**声明式 opcode 目录**（沿用 `gles.idl_codegen` 的成熟模式）：

- `data/dexvm/dalvik_opcodes.json`：dex 035 基准全部已定义 opcode（256 个
  编码点中 218 个已定义，unused 空洞 `0x3e-0x43`、`0x73`、`0x79-0x7a`、
  `0xe3-0xff` 显式列出并拒绝）。条目样例：

```json
{
  "opcode": "0x90", "name": "add-int", "format": "23x",
  "operands": [
    { "role": "dest", "kind": "cat1" },
    { "role": "src",  "kind": "cat1" },
    { "role": "src",  "kind": "cat1" }
  ],
  "pool_ref": "none", "branch": false, "can_throw": false, "wide": false
}
```

- 指令格式为 dex 035 的 24 种标准格式（排除 odex 专用与 038+ 新格式）：

| 组 | 格式 | 代表指令 |
| --- | --- | --- |
| 无操作数 | `10x` | `nop`、`return-void` |
| 寄存器移动 | `12x` `22x` `32x` | `move` 及 from16/16 变体 |
| 立即数 | `11n` `21s` `21h` `31i` `51l` | `const/4`、`const/16`、`const/high16`、`const`、`const-wide` |
| 单寄存器 | `11x` | `move-result`、`throw`、`return` |
| 跳转 | `10t` `20t` `30t` | `goto` 及 /16、/32 |
| 条件分支 | `21t` `22t` | `if-eqz`、`if-eq` |
| 常量池引用 | `21c` `22c` `31c` | `const-string`、`iget`、`const-string/jumbo` |
| 三地址运算 | `23x` | `add-int`、`cmpl-float`、`aget` |
| 字面量运算 | `22b` `22s` | `add-int/lit8`、`add-int/lit16` |
| payload 引用 | `31t` | `fill-array-data`、`packed/sparse-switch` |
| 调用 | `35c` `3rc` | `invoke-*` 与 range 变体 |

- `tools/generate_dexvm_opcodes.py`：生成确定性 C++ 解码表与 dispatch
  骨架；`--self-test` 核对 opcode 数、格式覆盖与空洞区间；
  `--verify-aosp` 可与 `.local/aosp/dalvik` 的 AOSP 基线做按需机器比对——`opcode-gen/bytecode.txt`
  （AOSP 自己的机器可读 opcode 定义源）逐项等价、`libdex/DexOpcodes.h`
  枚举二次核对、`libdex/InstrUtils.cpp` 宽度/标志表核对（见 [07 §2 模式 A](07-aosp-reference.md)）。
  目录内容不靠人抄，靠机器比对。
- 未定义 opcode 解码即明确失败并记账；odex/quickened 与 038+ 指令
  **不进目录**——命中即"未定义"路径。

**loader 扩展（`loader.dex_code`，新能力条目）**：L1 已解析出 `DexCodeInfo`
（registers/ins/outs/tries/insns 规模）；L2 补齐受检读取：

- 指令流：u2 单元序列 + `packed-switch`/`sparse-switch`/`fill-array-data`
  payload 的边界与对齐校验（payload 必须 4 字节对齐、位于方法指令区内、
  被且仅被 `31t` 指令引用）；
- try/catch：`try_item` 区间与 handler 列表（含 catch-all），区间不重叠、
  按 start_addr 有序、偏移全部落在指令流内；
- 静态字段初始值：`encoded_array`（class_def 的 static_values），类型与
  字段 descriptor 匹配校验；
- 结构布局与 uleb128/sleb128 读取对照 AOSP `libdex/DexFile.h`、
  `DexCatch.h`、`Leb128.h`；反例校验清单直接对标
  `libdex/DexSwapVerify.cpp` 的逐节规则（[07 §2](07-aosp-reference.md)）；
- 全部越界、错位、非法索引在读取期明确失败——延续项目"严格解析、拒绝
  凑合"的既有风格（ZIP/Manifest/ELF 解析同款）。

## 5. 类链接

强类型 `DexClassId` / `VmMethodId` / `VmFieldId`（固定宽度句柄，不暴露宿主指针，
与 jni 模块不变量一致）。链接分四步，均可单独测试：

1. **注册**：session 装配时一次性注册全部 class_def 元数据（数百个类，成本
   可忽略），同时合并 intrinsic 目录。应用类与 intrinsic 类同名冲突即装配失败。
2. **按需层级解析**：`Link()` 只完成 intrinsic 启动底座；APK 类在首次解析、
   实例化或方法调用时才链接其可达 super 链与接口表。DEX 中未触达的广告/SDK/
   可选组件即使缺 framework 层级也不成为进程启动依赖；首次触达仍在任何布局或
   执行前明确失败（survey 模式则合成并记账该次真实命中）。super 链与接口表可
   跨边界指向 intrinsic 类（`GLGame extends
   android.app.Activity` 是常态）。intrinsic 类为此声明自己的虚方法表与可被
   override 的方法集（见 03 §2）。循环继承、final 类被继承、接口当 super
   等结构错误在对应类首次链接时明确失败。
3. **布局与 vtable**（算法参考 AOSP `vm/oo/Class.cpp` 的 vtable 构建）：
   - 实例字段布局：从 super 布局末尾起，按声明顺序追加，宽度对齐
     （ref/cat1 一槽、wide 两槽对齐）；intrinsic 基类贡献一个不透明宿主
     状态槽。字段偏移在链接后不变。
   - vtable：复制 super vtable，逐个 virtual 方法按"名 + descriptor"匹配
     override（命中则替换同槽位，否则追加新槽）；解释类 override
     intrinsic 虚方法时替换的是 intrinsic 声明的槽——宿主侧经同一 vtable
     派发进入解释器，这是生命周期反转的核心机制。仅 `overridable` 标记的
     intrinsic 方法可被替换（03 §2），替换未标记方法即链接失败。
   - 接口表（iftable）：聚合全部直接与继承接口；接口方法在运行期按
     receiver 类查其 vtable 匹配实现。
   - 静态字段按类分配类型化槽位，`encoded_array` 初始值在 `<clinit>` 语义
     内物化（有 `<clinit>` 时先赋初值再执行）。
4. **方法静态预检（轻量，非完整 verifier）**：首次链接某方法时校验分支目标
   落在指令流内且对齐指令边界、寄存器编号 < registers_size、宽寄存器成对、
   payload 引用合法、`ins` 与 descriptor 参数宽度一致。规则子集取自 AOSP
   `vm/analysis/CodeVerify.cpp`，但**不做**全量类型推导数据流分析——运行期
   tagged 寄存器（§7）负责兜底类型混淆，失败携带 class/method/pc 诊断。
   常量池解析时机与缓存口径参考 `vm/oo/Resolve.cpp`；instanceof/checkcast/
   数组协变的可赋值性规则参考 `vm/oo/TypeCheck.cpp`。

**`<clinit>` 语义**（状态机参考 AOSP `vm/oo/Class.cpp` 的 `dvmInitClass`）：
per-class 状态机 `uninitialized → initializing(thread) → initialized | failed`。
同线程重入按 JLS 放行（可见部分初始化）；跨线程等待走统一 Clock 超时
（预算来自 profile），超时以结构化诊断失败而不是死等；`failed` 粘滞为
NoClassDefFoundError，后续使用该类明确失败。触发点封闭枚举：
`new-instance`、`invoke-static`、`sget/sput` 家族、`Class.forName(initialize=true)`、
子类初始化连带 super（接口不连带，仅在自身静态字段被访问时初始化）；
`const-class` 与 `instanceof` **不**触发。`<clinit>` 内的 `System.loadLibrary`
是这代游戏的常见形态，见 04 §2 启动序列第 4 步。

## 6. 统一对象模型：JavaObjectModel

session 级唯一所有者，吸收既有 backlog（`JniObjectArrayStore` 从 array binder
内部持有改为 session 级统一所有权）。对象身份四种形态，共用同一固定宽度
引用句柄空间：

| 形态 | 内容 | 来源 |
| --- | --- | --- |
| VM 实例 | class id + 字段槽板（按链接期布局） | `new-instance` |
| 宿主背衬实例 | class id + 不透明宿主状态句柄 | intrinsic 构造（AudioTrack、GLSurfaceView…） |
| VM 数组 | 元素类型 + 长度 + 存储（迁移现有 primitive/object array store） | `new-array` / JNI NewXxxArray |
| VM 字符串 | UTF-16 存储（迁移现有 string store）+ intern 表 | `const-string` / JNI NewString* |

关键性质：

- JNI local/global 引用表直接指向 JavaObjectModel 句柄——native 侧和解释器
  看到**同一个对象**，不存在影子拷贝。
- `jclass`/`jmethodID`/`jfieldID` 统一由链接器产出。`FindClass` 对应用类查
  DEX 元数据、对平台类查 intrinsic 目录，**不再需要 profile 声明**；
  `GetStaticMethodID` 查真实方法表。Asphalt 6 的 17 个推送方法查表因此
  自动成立。
- 对象非移动（GC 不压缩，见 04 §5），句柄生命周期内稳定，现有 copy-based
  的 GetStringChars / Get*ArrayElements 语义原样保留。

## 7. 解释器内核

**帧**：`registers_size` 个 tagged 槽 + 方法引用 + pc + 调用者链。tag 同时
服务运行期类型检查与 GC 精确根扫描。帧深度有界（默认 512，profile 可调），
越界抛 StackOverflowError（真实语义）。

**tag 规则**（tag ∈ `{uninit, cat1, wide-lo, wide-hi, ref}`）：

| 场景 | 规则 |
| --- | --- |
| 方法入口 | 实参寄存器按 descriptor 打 tag（`this` = ref；J/D 占对齐 wide 对；其余 cat1）；非实参寄存器 = `uninit`，读取即失败（合法编译产物不会读，命中即坏 dex 或解释器缺陷） |
| const 家族 | `const*` → cat1；`const-wide*` → wide 对；`const-string/class` → ref |
| move 家族 | `move` 要求 cat1、`move-wide` 要求完整对、`move-object` 要求 ref 或零值 cat1 |
| **零值放宽** | 值为 0 的 cat1 可用于任何需要 ref 的位置并重打为 ref/null——Dalvik "zero type"惯例，`const/4 v0, 0` 后既可 `if-eqz` 又可当 null 传参；**非零** cat1 用作 ref 一律失败 |
| wide 完整性 | 半对读取、错位覆盖（写 cat1 进 wide-hi 使对残缺后再按 wide 读）→ 失败并携带 pc |
| invoke 编组 | 实参逐个按被调方法 descriptor 校验 tag 再复制 |
| aget/aput | 数组元素类别来自数组对象的运行期类型；opcode 与元素类别不符（如 `aget-object` 作用于 `int[]`）→ 失败 |

**分派**：生成的解码表 + switch 分派。实现按指令家族拆文件（moves / const /
cmp / branch / array / instance / invoke / arith 等），保持职责内聚，且
每个家族是天然的 WU 边界。每 opcode 语义实现必须对照 AOSP
`vm/mterp/c/OP_*.cpp`（每 opcode 一个独立 C 文件，[07 §2 模式 B](07-aosp-reference.md)），
测试注释记录出处。

**方法解析缓存**：每 method_id 一个解析槽
`{unresolved | interpreted(VmMethodId) | native(guest 地址 + 签名) |
intrinsic(handler id)}`，首次解析后缓存——是内存内缓存，不是 dex
quickening，不改写指令流。v1 基线**无** call-site 内联缓存；后续加须先有
tick 采样证据（06 §3 性能纪律）。

**invoke 家族语义**：

- `invoke-virtual`：解析得 vtable 槽位，按运行期 receiver 类查
  `vtable[index]`；`invoke-super`：当前类 super 的 vtable 同槽位；
  `invoke-direct/static`：直接目标；`invoke-interface`：receiver 类按
  "名 + descriptor"匹配实现（iftable 范围内）。
- 参数传递：被调解释方法帧有 `registers_size = N`、`ins = M`，实参写入
  `v(N-M) .. v(N-1)`（Dalvik ins 约定）；`35c` 与 `3rc` 只是实参来源编码
  不同。native 方法按签名编组 A32 调用帧交给现有执行器（04 §1 编组表）；
  intrinsic 方法调用注入的 `PlatformClassProvider` handler。
- `move-result*` 必须紧随 invoke/filled-new-array，其余位置命中即失败。

**确定性**：无浮点环境依赖的指令语义按 Dalvik 规范定义并逐项进夹具：
`cmpl/cmpg` 的 NaN 偏置方向、`div/rem-int` 的 `MIN_INT / -1` 结果与除零
ArithmeticException、移位量取低 5/6 位掩码、窄化转换截断、float↔int 转换
的饱和语义等。

## 8. 异常

- `throw` 指令传播 VM Throwable（class + message + cause + 惰性栈回溯：
  method/pc 链，渲染推迟到查询——沿用 WU-PERF-01 的"格式化推迟"原则）。
  `throw` null → NullPointerException（真实语义）。
- 解释器展开（匹配算法参考 AOSP `vm/Exception.cpp` 的 `dvmFindCatchBlock`）：
  按当前 pc 所在 `try_item` 的 handler 列表**声明顺序**逐个做 instanceof
  判定（含 intrinsic 异常类），catch-all 兜底；无命中则弹帧、在调用方
  invoke pc 上重复；synchronized 方法弹帧前释放其 monitor。
- 隐式异常统一入口：NPE（null receiver/数组/throw）、
  ArrayIndexOutOfBounds、ArithmeticException、ClassCastException、
  NegativeArraySizeException、StackOverflowError、OutOfMemoryError 由
  解释器在对应指令点构造，走同一展开路径。
- JNI 边界双向映射到现有 pending exception 模型：native 返回时带 pending
  → 解释器视作该 invoke 抛出；解释器异常穿出到 native 调用方 → 设置
  pending，`ExceptionOccurred/Check/Describe/Clear` 语义不变。
- 线程顶层未捕获：结构化诊断（完整 Java 栈 + guest 线程号）+ session 失败。
  相比现状"requires a valid class reference"式的远点爆雷，这是可观测性的
  直接升级。

## 9. 内核明确不含

线程调度（复用 1 guest 线程 = 1 宿主线程模型）、时间（统一 Clock）、
IO（intrinsic → VFS）、图形音频（intrinsic → 现有 GLES/audio 边界）。
解释器内核只做：取指、解码、执行、帧管理、异常展开、三路 invoke 分派。
