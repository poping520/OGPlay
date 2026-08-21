# 06 · 迁移、阶段划分与冲突裁决

## 1. 阶段划分

里程碑编号在启动时按 roadmap 顺序分配（下文以 M-DEX 指代），任务单遵循
ADR-0012 与 M8 命名约定（`WU-M<编号>-NNN`）。每阶段出口都是机器可判定的，
且每个 WU 触及 ≤ 10 文件、单会话可完成。

### 阶段 0 · 地基（不触碰运行时，可在 M8 间隙执行）

| WU | 内容                                                                                                                                    | 出口 |
| --- |---------------------------------------------------------------------------------------------------------------------------------------| --- |
| 0-0 | AOSP Dalvik 本地参考树：`.local/aosp/dalvik` 对齐 `android-4.4.4_r2`，不入库、不编译、不链接（[07 §1](07-aosp-reference.md)）                               | 参考资料可按需校验 |
| 0-1 | `tools/dex_dependency_survey.py`：题库静态测量（引擎指纹、Java 厚度、平台类引用直方图），输入含存量 profile `[[java.class]]` impl id 语料（校准方法级接管 intrinsic 最小集，03 §3） | self-test + 本地题库报告产出 |
| 0-2 | `data/dexvm/dalvik_opcodes.json` + 生成器                                                                                                | `--check` 与 self-test 过 CTest；外部 AOSP 锚点比对按需执行 |
| 0-3 | `tools/dexasm.py` 核心（类/方法/常用 format）                                                                                                  | golden SHA + L1 回读核对 |
| 0-4 | dexasm 补齐 try/catch、payload、静态初始值                                                                                                     | 反例夹具可构造 |
| 0-5 | 评估 `libdex`+`dexdump` host-only 交叉裁判（[07 §4](07-aosp-reference.md)）                                                                   | 结论（采用或降级）记入 WU 文档 |

阶段 0 产出的测量报告用于校准后续批次顺序与 intrinsic 最小集（ADR-0017：
数据决定排期，不再决定做与不做）。

### 阶段 1 · 解释器内核（headless，无线程无 GC-B）

`loader.dex_code` → 类链接（注册/层级/布局/vtable/预检）→ JavaObjectModel
（含 array/string store 迁移，存量 JNI 测试全绿是硬门禁）→ 帧与分派 →
指令家族逐 WU 闭合（moves/const/arith/cmp/branch/array/instance/invoke）→
异常与 `<clinit>` → GC-A 预算 arena。
**出口**：指令一致性套件全绿；full CTest 无回归。

### 阶段 2 · 边界互通 + java.* P1 + 方法级接管

JNI 出向编组、入向第三路由、`samples/minimal_dex` 双向夹具、java.* P1
（语言核心 + System/Math）、字符串统一、方法级接管所需的首批 android.*
intrinsic（SharedPreferences/Context 受限面等，按 0-1 语料裁定）。
**出口**：边界互通夹具全绿；native↔解释器嵌套与异常穿透双向可证；
05 §4 gate 0（方法级接管：存量 title 删胶水行、Scenario 三轮持平）。

### 阶段 3 · 生命周期反转 + pilot 迁移

Manifest 组件读取、`dex_activity` 模板、Activity/GLSurfaceView/Bundle 等
android.* intrinsic 挂接、profile v2 schema、pilot title 迁移。
**出口**：05 §4 gate 1 与 gate 2（pilot 三轮 + Asphalt 6 副作用自动闭合）。

### 阶段 4 · 线程/GC-B/厚层能力

Thread intrinsic、wait/notify 扩展、GC-B、java.* P2/P3（集合/装箱/IO）。
**出口**：05 §4 gate 3（libGDX 类 title 启动 Scenario）。

### 阶段 5 · 迁移收尾

存量 title 逐个迁至 v2；`[[java.class]]`/`native_call` 相关装配路径进入
维护态（不删除——迁移完成前仍是生产路径）；文档与账本收尾。
**出口**：全部存量 title 在 v2 下三轮 gate 持平；CURRENT/capabilities 收敛。

## 2. 冲突裁决表

本方案与现有设计的全部已识别冲突。**裁决一律以本方案为准**；实施中发现
新冲突时追加到本表（只追加，不改写历史行）。

| # | 现有设计 | 冲突点 | 裁决 |
| --- | --- | --- | --- |
| 1 | AGENTS.md 范围边界"禁止完整 ART/Dalvik" | "有界解释器"是否越界 | 不越界：ADR-0017 定义边界（无 framework 字节码/JIT/动态加载）。启动实施时在 AGENTS.md 范围段补一句"有界 DEX 解释见 ADR-0017"，禁令本身不动 |
| 2 | roadmap 04 §7.4 "用题库数据决定要不要投入 L2" | go/no-go 判定 | Superseded（ADR-0017）：已决定投入；测量只定排期与批次 |
| 3 | M5 profile `native_call` 生命周期驱动机制 | 与生命周期反转互斥 | v2 `dex_activity` 模式下禁止 `native_call`；v1 机制保留为存量模式，随阶段 5 迁移进入维护态。M5 已验收结论不改写 |
| 4 | M5 profile `[[java.class]]` + `session.profile_java` 装配 | 应用类由 DEX 取代声明 | 同上：v2 禁止；impl handler 目录本身保留，转为 intrinsic 方法的实现载体 |
| 5 | `session.lifecycle_templates` 三模板封闭枚举 | 需扩枚举 | 新增 `dex_activity`；新 title 默认走它 |
| 6 | `runtime/jni` invocation engine"方法必须映射 implementation handler，missing 即失败" | 应用类方法无 handler | 新增第三路由（解释执行）；missing 语义只对 intrinsic 保留 |
| 7 | monitor 契约"不实现 Object.wait/notify"（WU-M8-006/007 与 jni MODULE.md） | 需要 wait-set | Superseded：阶段 4 扩展 wait-set；`InterruptWaiters/Shutdown` 两级语义保留并覆盖 wait-set |
| 8 | `JniObjectArrayStore` 由 array binder 内部持有（CURRENT 既有 backlog） | 所有权归属 | 吸收进 JavaObjectModel（本方案把该 backlog 变成正式设计的一部分） |
| 9 | Title Profile v1 schema 冻结 + 200 行上限 | 无法表达 v2 | v1 继续冻结服务存量；新增 v2（04 §7），校验器双版本并行 |
| 10 | capabilities 状态单调不后退 | title 迁移后 v1 装配路径使用减少 | 不后退：存量条目状态不动；dexvm 全部走新条目；迁移完成后 v1 相关条目 note 标注"维护态"，状态字段不变 |
| 11 | `runtime.framework_*` 声明式 HLE"只绑定 profile 实际引用的方法" | intrinsic 是代码定义全量目录 | intrinsic 目录为准（03 §1）；framework 存量 handler 原样复用，绑定判定从"profile 引用"改为"目录声明 + 命中记账" |
| 12 | `android/os/Bundle`"注册类、方法为空"的现状（platform identity 存量） | 与真实键值 intrinsic 冲突 | 升级为真实键值语义（03 §4）；在 dexvm 启动前，M8 若按既有分析先做"仅声明"方案，属于过渡态，不构成本方案障碍 |
| 13 | 上一轮讨论的"profile 覆盖审计工具 / 引擎模板"中期方案 | 与本方案的长期路线重叠 | 定位为过渡工具：M8 期间仍可做（成本低、见效快）；dexvm 落地后自然退役，不投入超过 M8 实际需要的量。WU-M8-011 实证后升格为**应做**：profile impl id ↔ handler 目录对账进 CTest，缺口在构建期可见而不是运行期点击时爆出 |
| 14 | M8 批次 2"JNI/Java 调用族按通用语义批量实现"的持续扩张 | 每新 title 需人工逆向胶水；行为敏感组（license/billing/online）必须逐个反编译取证，成本随题量线性且不摊销（WU-M8-011 实证：DUNQ 引用 16 个缺失 id，人工只能诚实闭合语义无歧义的 3 个，13 个滞留） | v1 胶水目录**冻结增长**：只补当前 gate 实际阻塞且语义无歧义的调用族；行为敏感组不再人工实现，保持 missing 明确失败并登记为方法级接管（04 §1 / 05 §4 gate 0）候选。已实现 handler 不删除（存量 title 生产路径），随阶段 5 进维护态 |
| 15 | 上游 Dalvik mterp quickening / 自改写 opcode | 与 OGPlay 原 DEX 不改写红线及可切换双后端冲突 | 只构建按 `LinkedMethod` 生命周期持有的只读 `FastCode` 派生缓存；解析结果、内部索引与 handler 分类只写缓存，不写回 u2 指令流，switch 后端始终以原 DEX 为事实源 |
| 16 | Interpreter v2 的可用后端与生产默认值 | “实现完成”是否等于默认切换 | 否：Profile/CLI 可显式选择 threaded，但默认切换是独立裁决。DVM-58 的 A5 三轮 exact 持平，title wall-time 无稳定收益；DH 两后端同边界受阻且 A6 按测试边界未跑，故默认保持 switch，能力保持 partial |

## 3. AI 实施规范（对 02-ai-workflow 的 dexvm 特化）

1. **上下文装载顺序**：`AGENTS.md` → `docs/state/CURRENT.md` → 本目录
   README → 当前 WU 涉及的章节 → 相关 `MODULE.md`。禁止凭记忆实现 Dalvik
   语义——语义断言以 opcode 目录与一致性夹具为准。
2. **AOSP 参考纪律**：实现任何指令家族、链接步骤、monitor/GC/JNI 语义前，
   必须研读 [07 §2](07-aosp-reference.md) 对照表指定的 AOSP 文件，并在
   WU 文档与测试注释记录出处（文件 + 函数）；与参考不一致时按 07 §5 的
   仲裁顺序裁决并记录。参考的是语义，不是结构——AOSP 的平台耦合代码
   （bionic/liblog/信号/内存布局假设）一概不进入 OGPlay。
3. **WU 切分红线**：指令家族、intrinsic 批次、链接步骤是天然 WU 边界；
   禁止把"解释器 + intrinsic + 生命周期"混进同一 WU（对齐 M8"JNI/GLES/VFS
   不混"的既有规则）。
4. **诚实失败纪律**：任何未实现 opcode/intrinsic/反射面在实现 WU 合入前
   必须已经"记账 + 明确失败"可测——先立失败测试，再补实现（quirk 的
   "关闭即失败"纪律推广到能力缺口）。
5. **每 WU 收尾**：`MODULE.md` 同步、`capabilities.toml` 状态推进、
   `CURRENT.md` 滚动快照——存量规则，无新增。
6. **性能纪律**：解释器优化必须先有测量（对齐 WU-PERF/优化验收闭环先例：
   先采样定位，后改动，禁止投机优化）；任何"缓存已解析结果"的优化不得
   改写指令流（quickening 红线）。

## 4. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| 指令语义错实现（最大风险：错一条指令 = 游戏离奇行为） | 每 opcode 对照 AOSP `vm/mterp/c/OP_*.cpp` 实现并在测试记录出处（07 §2）；逐 opcode 一致性夹具先行；边界值显式清单（05 §2）；出错时 `dexvm.stack` + tagged 寄存器诊断可精确定位 |
| 参考代码误导（AOSP 实现含历史包袱与平台耦合） | "只取语义，不取结构"红线（07 §3）；分歧按 07 §5 仲裁顺序裁决并记录；目录类数据一律机器比对而非人工转写 |
| 解释性能不足（厚层 title 每帧 Java 逻辑） | 无 JIT 是硬边界；缓解靠解析缓存、家族内 dispatch 优化、tick 采样定位热点；接受"确定性优先于峰值性能"的项目既有取舍 |
| intrinsic 面失控（滑向重写 JDK） | 03 §3 批次 + 命中记账驱动：只实现真实命中；每批次前看 `dexvm.unimplemented` 聚合 |
| Manifest 组件解析踩坑（旧 aapt 产物花样多） | 已有二进制 Manifest 严格解析基础设施；失败即明确报错，不猜测 |
| GC-B 引入并发缺陷 | 精确根 + STW + 非移动是最保守设计；安全点复用久经考验的 slice/interrupt 机制；A 期 arena 先跑通全部其他子系统 |
| 迁移期双路径维护负担 | 阶段 3 起每完成一个 gate 立即迁移对应 title；双路径共存期以"存量 title 数"为上界，不新增 v1 title |
| dexasm 自身有 bug 导致夹具失真 | dexasm 输出经 L1 解析器独立回读核对；golden SHA 锁定；真实 APK 的 DEX 同时作为链接期回归输入（只链接不执行也能验证解析） |

## 5. 与 M8 的关系

M8 继续按 profile 路线推进到 Asphalt 6 主界面 gate——它交付短期结果，同时
为本方案积累三样东西：完整的 GLES/audio/线程边界（dexvm 直接复用）、
Asphalt 6 的副作用清单（阶段 3 gate 2 的断言素材）、profile 成本的真实数据
（阶段排期依据）。两条线的首个汇合点是**阶段 2 的方法级接管 gate 0**
（早于原定的阶段 3 pilot 迁移）。

M8 期间的过渡纪律（WU-M8-011 复盘后生效，对应裁决 13/14）：

- v1 `[[java.class]]` 胶水目录冻结增长——只闭合当前 gate 实际阻塞且语义
  无歧义的调用族（如 analytics 记账/计数），行为敏感组保持 missing 明确
  失败，缺口清单即方法级接管的迁移素材。
- profile impl id ↔ handler 目录对账做成机器门禁（一个小 WU）：所有
  `data/profiles/*.toml` 引用的 impl id 对照代码侧注册目录出报告，缺口
  构建期可见；该门禁在 dexvm 阶段 5 后随 v1 一起进维护态。
- 每次人工补 handler 的实际成本（定位、取证、文件数）记入 WU 文档，作为
  dexvm 排期的持续证据流。
