# 10 · 解释器 v2：预解码线程化内核（threaded）

本章是第二个解释器内核的实施方案，写给**实施阶段的 AI**。动机：现有
switch 内核（`src/runtime/dexvm/interp_exec.cpp` +
`interp_object.cpp` + `interp_arith.cpp`）语义完备、诊断完善，但每条
指令背着可观的固定开销（§2 成本清单）；厚层 title 每帧的 Java 逻辑
（06 §4 风险表既有条目）需要更快的稳态内核。方案是**保留旧内核、并列
新建一个预解码 + 线程化分派的 v2 内核，受检开关切换**，语义逐位等价由
夹具双跑与 exact gate 锁定。

红线继承（一条不破）：**无 JIT**（ADR-0017 硬边界，本方案不生成任何
机器码）；**不改写 dex 指令流**（quickening 红线——v2 的预解码产物是
自有派生缓存，dex 字节与旧内核路径原样不动，裁决见 §3）；**确定性
优先于峰值性能**（06 §4 既有取舍——tick 语义逐位一致是硬验收）；
**性能纪律**（06 §3 第 6 条：先采样后改动，本章 §2 的成本清单即测量
对象，每个优化 WU 必须附采样证据）。

实施纪律沿用 [06 §3](06-migration.md)：上下文装载顺序 `AGENTS.md` →
`docs/state/CURRENT.md` → 本章 → `src/runtime/dexvm/MODULE.md`；逐
opcode 语义出处仍是 AOSP `vm/mterp/c/OP_*.cpp`（07 §2 模式 B），v2
不重新发明任何指令语义——它只改变取指、解码与分派的形态。

## 1. 目标与非目标

| 目标 | 判定 |
| --- | --- |
| 稳态解释吞吐显著高于 switch 内核 | dexasm 微基准 + title 帧采样，同输入 wall time 对比报告（先测量基线，WU 内给数字） |
| 语义与旧内核逐位等价 | 全部 dexasm 夹具双后端参数化跑：终态寄存器/堆/异常/返回值/**tick 计数**逐位一致 |
| 可切换、可回退 | `InterpreterConfig::backend` + profile 受检字段；默认仍是旧内核，切默认是独立裁决（§8） |
| 增量可交付 | bridge 机制（§4）让 v2 从第一个 WU 起就能跑全部 title，家族逐 WU 迁移 |

**非目标**：

- 不删除旧内核（长期共存：旧内核是 v2 的等价性裁判与冷路径承载）；
- 不做 JIT、不做 call-site 内联缓存（06 §3 既有约束，加须先有采样
  证据并另立 WU）；
- 不改帧模型、tag 规则、对象模型、invoke 三路由、异常展开、monitor、
  `<clinit>`、GC 语义——v2 与旧内核共享全部这些子系统（§4 分层）；
- 不引入平台汇编（mterp 的 InterpAsm-*.S 形态明确不借用，07 §3
  红线"只取语义/机制，不取结构"延伸到此）。

## 2. 现状成本清单（测量对象与事实锚点）

逐指令固定开销（`interp_exec.cpp` `Step()`、`interpreter.cpp` `Run()`）：

| # | 开销 | 位置 |
| --- | --- | --- |
| 1 | `Run` 循环每指令一次 `Step()` 函数调用（跨 TU，不可内联），外裹 try 块 | `interpreter.cpp` `Run()` |
| 2 | `frames.back()`（deque 双端索引运算）每指令 ≥ 2 次（入口一次、`advance` lambda 再一次） | `Step()` 开头与 `advance` |
| 3 | 逐次重解码：操作数字段提取、字面量拼装（`const-wide` 4 次移位或）、分支偏移拼装每次执行重算 | `Step()` 各 case |
| 4 | 非算术指令先白走一遍 `ExecuteArithmetic()`（跨 TU 调用 + 内部再判一次家族） | `Step()` 第 58 行形态 |
| 5 | `Tick()`：每指令一次 `stop_requested` relaxed 原子载荷 + 2 个分支 + 计数 | `interpreter_internal.h` |
| 6 | `stats.executed_instructions++` 每指令一次内存自增 | `Step()` |
| 7 | `info.defined` 查表判断每指令一次（precheck 已在链接期拒绝未定义 opcode，热路径冗余） | `Step()` |
| 8 | `RegAt` 每次寄存器访问带 bounds check（precheck 已证明寄存器号 < registers_size，热路径冗余；**tag 检查不是冗余**，是语义与 GC-B 精确根的依赖，保留） | `interpreter_internal.h` |
| 9 | 常量池操作数每次执行走 `linker->ResolveTypeIndex/ResolveFieldIndex/ResolveMethodIndex`（有缓存但仍是函数调用 + 查表）；`new-array` 每次执行做字符串比较（`element == "I"` / `starts_with`） | `interp_exec.cpp`、`interp_object.cpp` |
| 10 | 中央 switch 单一间接跳转点，分支预测器无法按"当前 opcode → 下一 opcode"模式学习 | 分派结构本身 |
| 11 | invoke 参数封送逐字符解析 descriptor 并堆分配（CURRENT.md 既有余项，DVM-32/33 任务单已有分析结论） | `interp_object.cpp` |

DVM-32/33 已消除 intrinsic 逐调用查找与逐指令 thread-local 查找；上表
是剩余的结构性开销，其中 1–4、7、10 是 switch 形态固有，只能换内核
解决——这是 v2 的存在理由。

## 3. 预解码 IR（FastCode）与 quickening 红线裁决

**FastCode**：每方法一份定宽内部指令数组，首次执行时构建（挂在既有
`PrecheckMethod` 懒执行点之后，构建期即预检期，失败语义不变），构建后
只读缓存（method_id 键控，与既有方法解析缓存同类）。条目布局（16 字节
量级，实施时以采样定稿）：

```text
handler   u16   v2 handler 序号（不是 dex opcode：同一 opcode 可有
                checked/fast 两个 handler，见 §5）
a, b, c   u16×3 预提取的寄存器号 / 紧凑操作数
extra     u64   预拼装字面量 | 常量池索引 | 解析缓存槽指针（union）
dex_pc    u32   映射回 u2 流（诊断、try/catch 区间、GC 安全点口径）
```

- **分支目标**预换算为 FastCode 下标；`packed-switch`/`sparse-switch`/
  `fill-array-data` payload 预解析为边表（sparse 保留原始键序与线性
  语义口径，tick 权重记录原 size——权重语义逐位不变，§6）。
- **字面量**（const 家族全部变体）预拼装为 64 位值，执行期一次装载。
- **`new-array`/`filled-new-array` 的元素类别**在构建期判定一次
  （替代逐次字符串比较），存入 handler 选择或 extra。
- **未迁移家族**打 `bridge` handler（§4）。

**quickening 红线裁决**：06 §3 第 6 条禁止的是**改写 dex 指令流**
（odex/quickened 的做法：把解析结果写回指令编码，毁掉原始流）。
FastCode 是与 dex 并存的自有派生只读缓存——dex 字节不动、旧内核照常
从 u2 流执行、FastCode 可随时丢弃重建，性质与既有"方法解析缓存"
完全同类，只是粒度到指令。**裁决：不触碰红线**。本裁决按 README
效力声明补记 06 冲突表。

**GC-B 衔接（09 §3）**：FastCode 是纯元数据，不含 `VmObjectRef`，
GC 不扫描；v2 分配指令 handler 承担与旧内核相同的安全分配点触发检查；
`dex_pc` 字段保证异常展开与诊断仍以 dex pc 口径工作。

## 4. 分层与 bridge：两内核共享一切慢路径

```text
┌────────────────────────────────────────────────┐
│  共享层（不动）：帧模型/tag 规则/JavaObjectModel  │
│  invoke 三路由/intrinsic/native 桥/monitor/     │
│  <clinit>/异常展开/VmExecutionLock/GC           │
├───────────────────────┬────────────────────────┤
│  旧内核（保留，默认）    │  v2 内核（新增）         │
│  u2 流逐指令解码        │  FastCode 预解码         │
│  中央 switch 分派       │  computed goto / 稠密    │
│  Step() 每指令一调      │  switch（按编译器选择）    │
│                        │  单函数稳态循环 + 局部     │
│                        │  状态缓存                 │
└───────────────────────┴────────────────────────┘
```

- **复杂指令走共享 helper**：invoke 家族、字段家族、分配、monitor、
  throw、`<clinit>` 触发点在 v2 中先把局部缓存状态（pc/regs 指针/
  countdown）**sync 回 `Frame`/`InterpreterExecutionState`**，再调用
  与旧内核完全相同的 `Impl` helper（`PushInterpretedFrame`/
  `InvokeIntrinsic`/`EnsureInitialized`/`AllocateInstance`/monitor
  表……），返回后 reload。对照 mterp README："When a C implementation
  for an instruction is desired, the assembly version packs all local
  state into the Thread structure and passes that to the C function."
  ——同一机制，宿主 C++ 化。
- **bridge handler**：预解码期把 v2 尚未实现的 opcode 打成 `bridge`
  ——sync 状态 → 调旧内核 `Step()` 执行这一条 → reload。由此 v2 从
  第一个 WU 起就能跑全部夹具与 title（等价性天然成立），家族逐 WU
  从 bridge 迁入 fast handler，每迁一族只跑该家族直接相关的双后端夹具——增量
  迁移的每一步都被机器判定兜底。
- **帧边界**：v2 稳态循环处理"同帧内直线执行 + 分支"；压帧/弹帧
  （invoke 进解释方法、return、异常展开）回到外层与旧内核共享的
  `Run()` 结构。v1 版本不做跨帧内联循环（那是采样证据驱动的后续项）。

## 5. 分派机制（对照 mterp，只取机制不取结构）

AOSP mterp 的分派谱系（`vm/mterp/README.txt`）：computed-goto（ARM，
定长 handler 区 + `base + opcode*64` 跳转）、jump-table（x86，handler
入口指针表）、all-c（portable 解释器）；另有 main/alt 双 handler 表
机制支撑低成本的 subMode 轮询切换。v2 的宿主 C++ 对应物：

- **clang/gcc**（macOS/arm64、Linux）：computed goto——`&&label`
  handler 地址表，每个 handler 尾部直接 `goto *table[next.handler]`
  （对应 mterp 的 FINISH 宏）。每 handler 独立跳转点让分支预测器学习
  opcode 转移模式（消除 §2 成本 10）。
- **MSVC**（windows-msvc 预设）：无 computed goto / musttail——降级为
  **稠密 switch**（v2 handler 序号连续无空洞，编译器生成跳转表；
  default 分支为 `unreachable` 语义消除界检）。预解码收益（§2 成本
  2–4、7–9）与局部状态缓存收益全部保留，仅分派点形态不同。
- **单函数约束**：computed goto 要求全部 handler 在同一函数体内。
  组装形态：`interp_threaded.cpp` 主 TU 只含循环骨架与 `#include`
  家族片段列表（`interp_threaded/*.inc`）；handler
  体用共享宏拼接，同一份语义源生成两种分派 glue（对照 mterp 的
  config/生成器模式；是否上生成器由 V2-2 实施时裁决，手写片段起步）。
- **循环内局部状态**：FastCode 指针（pc）、`Slot* regs`、tick
  countdown 三个值驻留局部变量（寄存器），仅在慢路径调用前 sync、
  返回后 reload——对照 mterp 以专用寄存器（rPC/rFP/rIBASE）驻留
  同类状态的机制。

**双表机制（后续优化项，默认不做）**：mterp 用 main/alt 双 handler 表
把 subMode 轮询从热路径拿掉（`rIBASE` 在回边/异常/返回时盲刷新）。
v2 的对应物是把 `stop_requested` 检查从逐指令原子载荷改为
"RequestStop 翻转分派表指针到 alt 表"。**v1 不做**：逐指令 relaxed
载荷先保留（与旧内核语义逐位一致优先），该优化需要修订 MODULE.md 的
teardown 契约（"每条指令检查"→"有界延迟"）且必须先有采样证据，列为
独立 WU（§9 V2-5）。

## 6. tick、stop 与统计的逐位等价

这三项是"确定性优先"的落点，v2 必须逐位复刻旧内核语义：

| 语义 | 旧内核 | v2 实现 |
| --- | --- | --- |
| 每指令 1 tick，先加后判 | `Tick()`：`ticks += 1` 后比较预算 | 局部 countdown 初值 `budget − ticks`，每指令递减，命中 0 在**同一条指令**抛 `budget_exhausted`；sync 点回写 `ticks` |
| payload 权重 | `fill-array-data` 按 count、`sparse-switch` 按 size 追加 `Tick(execution, n)` | 同一指令点、同一权重值追加扣减（预解码存下原 size，与边表优化解耦） |
| `stop_requested` 每指令检查 | `Tick()` 内 relaxed load | v1 保持逐指令 relaxed load（双表机制另立 WU，§5） |
| `executed_instructions` | 每指令自增 | 局部计数，sync 点合并回写（对外只在 `Call` 返回后可查，观测等价） |
| tick 预算/深度/堆预算的失败点 | 携带 class/method/pc 诊断 | `dex_pc` 映射保证诊断逐字一致 |

夹具断言"双后端 tick 计数逐位一致"——这是等价性验收里最严的一条，
也是最能兜住实现偏差的一条（任何解码/分派路径不同步都会体现为 tick
漂移）。

## 7. 配置面与切换

- `InterpreterConfig` 新增 `backend`（enum：`switch_dispatch` 默认 |
  `threaded`）；`Interpreter::Call` 入口按 backend 选内核，一次调用内
  不切换（与 execution context 恒定同一纪律）。
- profile v2 `[runtime.dexvm]` 新增受检字段 `interpreter = "switch" |
  "threaded"`（schema + 校验器同步，缺省 = switch）。
- Scenario runner / run-apk 提供 override 开关（对齐 `--fresh` 一类
  既有旗标形态），供 A/B 测量与回退，不落 profile 即不改变 gate 语义。
- `InterpreterStats` 新增 `backend` 标识与 FastCode 构建计数/字节数
  （宿主元数据记账，不计入 guest 堆预算——与方法解析缓存同类）。

## 8. 验证体系与默认切换裁决

- **双后端定向夹具**：每个迁移 WU 只把受影响 opcode 家族的 dexasm 夹具参数化跑两遍
  （doctest 参数化或 CTest 双实例），断言终态寄存器/堆/异常/返回值/tick 逐位一致；
  全部 dexasm 夹具仅在用户明确要求时运行。
- **bridge 覆盖恒等**：V2-2 起任意时刻，"全 bridge"配置（一个家族都
  不迁）的 v2 必须与旧内核逐位一致——bridge 机制本身的回归锚点。
- **结构反例**：预解码期必须复刻链接预检的全部拒绝路径（未定义
  opcode、越界分支、错位 payload……05 §2 反例清单），失败诊断携带
  class/method/dex pc，与旧内核逐字一致。
- **exact-title gate（唯一真实出口）**：A5 `title_flow`、A6
  `bootstrap`、DH 启动 Scenario 以 `threaded` 后端三轮持平（golden
  逐位不变）。
- **性能报告（06 §3 性能纪律的兑现）**：每个迁移 WU 附微基准数字
  （dexasm tight-loop 夹具：算术循环/分支循环/字段循环/invoke 循环
  四型）；V2-6 附 title 级帧采样对比。优化不达预期不合入伪装收益，
  记录数字并裁决去留。
- **能力条目**：`capabilities.toml` 新增 `dexvm.interpreter_threaded`
  （unimplemented → partial → complete 单调推进）；
  `dexvm.interpreter_core` 条目不动（旧内核语义地位不变）。
- **默认后端切换是独立裁决**：三 gate 持平 + 全夹具双跑绿 + 至少一个
  真实 title 的帧采样收益成立后，才允许把默认值翻到 `threaded`，
  以显式 WU + `CURRENT.md`/冲突表记录执行；本设计文档不预设结论。

## 9. WU 分批（批次 · 解释器 v2）

创建任务单时在 `docs/tasks/dexvm/README.md` 按现行序号续编，每个 WU
单会话可完成、依赖显式：

| WU | 一句话目标 | 关键交付 | 机器可判定验收 |
| --- | --- | --- | --- |
| V2-1 FastCode 构建器 | 把 u2 流一次性预解码为定宽 IR，不执行 | FastCode 布局、构建器（挂 PrecheckMethod 后）、分支/payload 映射、结构反例复刻 | 回读一致性：FastCode 反渲染与 u2 流解码逐项一致；反例拒绝与旧内核诊断逐字一致 |
| V2-2 threaded 骨架 + bridge | v2 内核能以"全 bridge"跑通一切 | 单函数循环 + 双分派 glue（computed goto / 稠密 switch）、局部状态缓存与 sync/reload、`backend` 开关、tick countdown | 全 bridge 配置下双后端夹具全量逐位一致（含 tick）；全平台编译（MSVC 降级路径验证） |
| V2-3 直线家族迁移 | moves/const/goto/if/cmp/算术家族入 fast handler | 家族片段 `.inc`、预拼装字面量、寄存器访问去 bounds check（tag 检查保留） | 受影响家族双后端定向测试 + 微基准（算术/分支循环）数字入 WU 文档 |
| V2-4 对象家族迁移 | 字段/数组/switch/分配家族入 fast handler | 解析缓存槽直击（extra 存槽指针）、new-array 元素类别预判定、checked/fast 双 handler 与首执行翻转（内部 IR 单写者自改写，执行锁下安全） | 受影响家族双后端定向测试 + 字段循环微基准；clinit 触发时序夹具不变 |
| V2-5 invoke 与轮询优化 | invoke 家族入 fast handler + args-shorty 预计算（吸收 CURRENT.md 既有余项）；评估 stop 双表机制 | invoke 编组预计算表；双表机制若做则同步修订 MODULE.md teardown 契约并附采样证据 | invoke 双后端定向测试 + invoke 循环微基准；teardown 夹具（RequestStop 有界延迟断言，若改契约） |
| V2-6 gate 复验与裁决 | title 无回归 + 性能报告，默认后端去留裁决 | A5/A6/DH 三轮持平（threaded）；title 帧采样对比报告；能力条目推进；`MODULE.md`/`CURRENT.md`/06 冲突表同步 | 三 gate 通过 + 采样报告数字 + 账本单调推进 |

依赖链：1 → 2 → 3 → 4 → 5 → 6。V2-1/2 不改变任何默认行为；从 V2-3
起每个 WU 独立可合入（bridge 保证任意中间态完整可跑）。

## 10. 风险表

| 风险 | 缓解 |
| --- | --- |
| 双内核语义漂移（最大风险：v2 某指令与旧内核差一个边界） | 夹具双跑 + tick 逐位恒等是每 WU 合入门禁；bridge 渐进使每次差异定位在单个家族内；语义出处仍逐 opcode 对照 AOSP `OP_*.cpp` |
| MSVC 降级路径收益缩水 | 预解码与局部状态缓存的收益与分派机制解耦（§2 成本 2–4、7–9 全平台成立）；微基准分平台出数字，诚实记录 |
| 预解码内存开销（16B/指令 vs 2B/单元） | 上界 = dex 代码体量 × ~8，Gameloft 级 dex（数 MB）对应数十 MB 宿主元数据；进 stats 记账，超预期时按需加"仅热方法预解码"策略（采样驱动，另立 WU） |
| 单函数内核可维护性下降 | 主 TU 只含骨架 + `#include` 家族片段列表；片段按 opcode 语义聚合，`MODULE.md` 登记该组装形态 |
| checked/fast handler 翻转引入状态缺陷 | 翻转只发生在执行锁内（单写者）；翻转前后语义等价由"首执行/次执行各一遍"夹具断言；GC/线程不感知 FastCode |
| 维护双内核的长期成本 | 共享层最大化（§4）：新指令语义只写一次共享 helper，两内核只是取指分派壳；旧内核同时是 v2 的裁判，成本即收益 |
| 投机优化伪收益 | 06 §3 性能纪律：每 WU 附微基准数字，不达预期记录并裁决，禁止无测量合入 |

## 11. 交付时的文档义务

- `src/runtime/dexvm/MODULE.md`：双内核结构、bridge 契约、FastCode
  缓存性质、局部状态 sync 不变量、（若做）teardown 有界延迟修订。
- `capabilities.toml`：`dexvm.interpreter_threaded` 条目与状态推进。
- `docs/state/CURRENT.md`：滚动快照。
- 06 冲突表补记：§3 的 quickening 红线裁决（FastCode 是派生缓存，
  不是指令流改写）；默认后端切换（若发生）单独记录。
