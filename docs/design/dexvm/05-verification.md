# 05 · 验证体系

所有行为必须机器可判定（AGENTS.md 既有要求）。dexvm 的特殊性在于：被测物
是"执行任意字节码的解释器"，因此验证体系的核心是**可离线确定性生成的
DEX 夹具**。

## 1. dexasm：确定性 DEX 汇编器

`tools/dexasm.py`：纯 Python、零外部依赖（项目已有 ZIP/Manifest/ELF 级二进制
工具先例），把受限文本 IR 汇编为合法 dex 035 文件：

- 输入：类/字段/方法声明 + 指令助记符序列 + try/catch 标注 + 静态初始值。
  IR 是 smali 风格的受限子集（**不承诺 smali 兼容**），示例：

```text
.class LFixture; .super Ljava/lang/Object;
.method static divide(II)I .registers 3
  :try_start
  div-int v0, v1, v2
  return v0
  :try_end
  :handler
  const/4 v0, -1
  return v0
  .catch Ljava/lang/ArithmeticException; { :try_start .. :try_end } :handler
.end
```

- 输出：完整 dex（header、adler32 校验和、SHA-1 签名、string/type/proto/
  field/method 池的规范排序与去重、uleb128 编码、4 字节对齐规则均按
  dex-format 落实），字节级确定（同输入必同输出）；池布局规则以 vendor
  的 AOSP `libdex/DexFile.h` 与 `docs/dex-format` 为实现依据；
- `--self-test`：内置样例汇编后与 golden SHA-256 比对，且经 L1 解析器回读
  核对结构——golden 锁字节，回读锁结构，双保险；
- 明确非目标：不支持整个 dex 语法面，只支持测试需要的受限子集，超出即
  显式报错——它是测试工具，不是通用汇编器。

不引入 Android SDK（d8/dx/javac）作为测试依赖：离线、可审计、可精确构造
边界用例（非法索引、错位 payload、越界分支——这些 d8 根本产不出来）是
dexasm 的存在理由。可选增强：把 AOSP `libdex`+`dexdump` 编译为 host-only
测试工具，对每个夹具做"我方汇编 ↔ AOSP 解析"交叉裁判（评估与降级条件
见 [07 §4](07-aosp-reference.md)）。

## 2. 指令一致性套件

`tests/dexvm/` 按指令家族组织，每家族一组 dexasm 夹具 + 机器断言：

- **语义正例**：每 opcode 至少一个夹具，断言寄存器终态/堆终态/返回值。
  边界值显式覆盖：`cmpl/cmpg` 的 NaN 偏置、`div-int MIN/-1` 溢出、除零
  ArithmeticException、移位掩码、窄化转换截断、wide 对齐等。**每个夹具的
  预期值必须在测试注释记录语义出处**（对应 AOSP `vm/mterp/c/OP_*.cpp`
  文件或 `docs/dalvik-bytecode` 章节，[07 §2 模式 B](07-aosp-reference.md)）——
  预期值不允许"按直觉写"。
- **结构反例**：非法 opcode、越界分支、寄存器越界、错位 payload、非法
  tag 使用（非零 cat1 当 ref、半 wide 对）——断言在链接预检、解码期或
  执行期明确失败且诊断携带 class/method/pc；反例清单对标
  `libdex/DexSwapVerify.cpp` 与 `vm/analysis/CodeVerify.cpp` 的规则面。
- **异常路径**：try/catch 命中、跨帧展开、finally（编译产物形态）、
  catch-all；athrow null → NPE。
- **`<clinit>`**：触发点、同线程重入、失败粘滞、跨线程等待超时。
- 目录 self-test：`data/dexvm/dalvik_opcodes.json` 的 opcode 数、format 覆盖、
  unused 空洞与生成代码一致（对齐 `gles.idl_codegen` 的 `--check` 模式）。

## 3. 边界互通夹具

扩展 `samples/`（对齐 `minimal_ndk` 先例）：`samples/minimal_dex/` 离线构建
"dexasm 产出的 DEX + 极小 ARMv7 `.so`"组合载荷，覆盖：

- 解释器 → native：`native` 方法经 `Java_` 导出与 `RegisterNatives` 两条链
  各一组，参数/返回值全类型矩阵；
- native → 解释器：guest 代码经 JNI `CallStatic*/Call*Method` 回调解释方法，
  含嵌套（解释 → native → 解释）与异常穿透双向；
- 对象一致性：native 侧 `Set*Field` 后解释器读到同一值（同一对象模型的
  直接证据）；
- 线程：Java `Thread.start` 与 native `pthread_create` + attach 双向、
  monitor 竞争、wait/notify 跨线程配对。

## 4. exact-title gate（唯一的真实验证出口）

M8 规则不变：**不得退回人工 smoke**，exact 验证一律走 Profile-backed
Scenario runner：

0. **方法级接管 gate**（阶段 2 出口，最早的真实 title 价值点）：选一个存量
   title，从其 v1 profile 删除一批 `[[java.class]]` 胶水方法行（优先取
   WU-M8-011 对账清单中"无 handler、需反编译取证"的行为敏感组），被删方法
   落到第三路由解释执行；迁移前同一 Scenario 三轮持平，且 profile 行数
   净减少。生命周期仍走 v1 `native_call`，不依赖 profile v2。
1. **pilot gate**：现有 Gameloft title 之一切到 `dex_activity` profile v2，
   跑迁移前同一 Scenario（同帧数预算、同 golden、无 fault、clean shutdown）
   三轮通过——与 profile 路线逐位对照是迁移正确性的硬标准。
2. **Asphalt 6 副作用 gate**：bootstrap Scenario 断言推送初始化链自动闭合
   （`mClassGLGame` 非空路径走通），profile 无任何 java/native_call 补丁。
3. **厚层 gate**：一款 libGDX 或同类 title 的启动 Scenario——检验集合/线程/
   GC-B 批次的真实成色。
4. 回归断言沿用：`null_pointer_calls = 0`、未实现命中单调性 CI 门禁
   （`core.capability_ledger` 存量机制直接覆盖 dexvm 新条目）。

## 5. 工程门禁

- 使用对应平台预设构建受影响目标并运行直接相关的单点/定向测试；只有用户明确要求
  才运行全量 CTest，三平台严格出口只进入明确授权的里程碑验收；
- dexasm 与 opcode 生成器进 CTest（`tools.*_self_test` 模式）；
- 解释器按 opcode 家族保持职责内聚，避免无语义的 `misc/common` 聚合；
- profile v2 校验器门禁与 v1 并行。

## 6. 新增能力条目（capabilities.toml）

| 条目 | 内容 |
| --- | --- |
| `tools.dexasm` | 确定性 DEX 汇编器 + self-test + golden/回读双保险 |
| `tools.dex_dependency_survey` | 题库 Java 厚度/引擎指纹/平台类引用测量（roadmap 04 §7.2 落地） |
| `loader.dex_code` | 指令流/try-catch/静态初始值受检读取 |
| `dexvm.opcode_catalog` | 声明式指令目录 + 生成器 `--check`；外部 AOSP 机器比对按需执行 |
| `dexvm.class_linker` | 注册/层级/布局/vtable/静态预检 |
| `dexvm.object_model` | JavaObjectModel 统一对象身份（吸收 array/string store 迁移） |
| `dexvm.interpreter_core` | 帧/分派/invoke 三路由/确定性语义 |
| `dexvm.exceptions` | throw/展开/JNI 双向映射/未捕获诊断 |
| `dexvm.clinit` | 类初始化状态机 |
| `dexvm.jni_bridge` | 出向编组 + 入向第三路由 |
| `dexvm.intrinsics_java_core` | java.* P1–P3 批次（可再分条目） |
| `dexvm.intrinsics_android_core` | android.* 目录挂接与新增 |
| `dexvm.threads` | Thread intrinsic + attach 对等 |
| `dexvm.wait_notify` | monitor wait-set 扩展 |
| `dexvm.gc` | A 期 arena → B 期精确标记清除（状态按期推进） |
| `session.dex_activity_lifecycle` | 生命周期反转模板 |
| `session.title_profile_v2` | schema v2 与校验器 |

状态推进遵守既有单调规则（unimplemented → stub → partial → complete）；
`[profiles.*]` 存量条目不因迁移后退（见 06 冲突表第 10 项）。
