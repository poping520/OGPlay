# DVM-32 · intrinsic handler 链接期绑定（消除逐调用查找）

## 目标（一句话）

把 intrinsic handler 的解析从"每次调用对约 486 个注册字符串做线性查找"，改为
"首次调用绑定一次、之后读指针直调"，运行时零容器查询，miss（未实现）语义与
记账行为逐位不变。

## 背景与问题证据（实现前请逐条核实仍成立）

1. `IntrinsicRegistry` 的存储是 `std::vector<std::pair<std::string,
   IntrinsicHandler>>`（`include/ogplay/runtime/dexvm/interpreter.h`），
   `Find` 逐项字符串比较（`src/runtime/dexvm/interpreter.cpp` 头部），
   `Register` 的查重同样线性——注册期整体 O(n²)。
2. 解释器每次 invoke 一个 intrinsic 方法都会执行
   `intrinsics.Find(method.intrinsic_handler)`
   （`Interpreter::Impl::InvokeIntrinsic`，`src/runtime/dexvm/interpreter.cpp`
   约 300 行处）。当前注册量约 486 个（386 个字面量注册 + 循环批量注册），
   随 title 适配持续增长，此成本随目录规模线性恶化。
3. 装配时序决定了查找结果终生不变（`src/runtime/integration/dexvm_bridge.cpp`
   构造函数）：`RegisterIntrinsics` → `RegisterDex` → `Link()` 之后类目录冻结；
   registry 由 `platform_handlers` 回调填充、move 进 `Interpreter`、再
   `RegisterCoreBuiltins()` 补齐 core handler，之后**没有任何代码路径再向
   registry 注册**。即 `LinkedMethod.intrinsic_handler → handler` 的映射在首次
   `Call` 前已完全固定。
4. gap survey 合成方法的 handler id 是 `"survey.unimplemented"`
   （`src/runtime/dexvm/class_linker.cpp`），该 id 从未被注册，靠
   `InvokeIntrinsic` 的 miss 分支（记账 + survey 中性桩 / 抛
   `UnsatisfiedLinkError`）工作——本 WU 必须保持这一点。

## 方案

本质是**绑定**，不是换容器：把映射求值从每次调用提前到首次调用一次，缓存进
方法本身。容器更换只是注册期查重和绑定那一次查找的配角。

### 1. `LinkedMethod` 增加解析缓存

`include/ogplay/runtime/dexvm/class_linker.h` 的 `LinkedMethod` 增加：

```cpp
// Lazily bound by the interpreter on first invoke; null = not registered
// (survey stubs and unimplemented intrinsics stay on the miss path).
// Logically-const cache: single writer guaranteed by VmExecutionLock.
mutable const IntrinsicHandler* resolved_handler{};
```

`intrinsic_handler` 字符串字段**保留**，继续作为记账、日志与诊断键；运行时
热路径不再读它。

### 2. `IntrinsicRegistry` 内部换 `unordered_map`

- 存储改为 `std::unordered_map<std::string, IntrinsicHandler, 透明哈希,
  std::equal_to<>>`（异构查找，`Find(string_view)` 不构造临时 string）。
- `Register` 查重变 O(1)，重复 id 仍抛 `DexVmErrorReason::internal_invariant`
  （现有行为，勿改文案以外的语义）。
- **地址稳定性是硬约束**：绑定缓存的是 registry 内 `IntrinsicHandler`
  （`std::function`）对象的地址。`unordered_map` 节点稳定、rehash 不搬移
  元素，满足；**禁止**改用任何会搬移元素的 flat map / sorted vector。
- 新增 `Freeze()`：冻结后 `Register` 抛 `internal_invariant`。

### 3. 冻结点：首次执行自动冻结

`Interpreter` 的任一执行入口（`Call` 各重载、`EnsureClassInitialized`）在
进入时若 registry 未冻结则调用 `Freeze()`。选择"首次执行自动冻结"而非要求
装配方显式调用，是为了：现有 `tests/dexvm/*` 直接构造 `Interpreter` 而不经
bridge，自动冻结让全部既有测试与装配代码**零改动**，语义上也更准确——
"开始解释执行后 intrinsic 目录不可变"。

### 4. `InvokeIntrinsic` 惰性绑定

```cpp
const auto* handler = method.resolved_handler;
if (handler == nullptr) {
    handler = intrinsics.Find(method.intrinsic_handler);   // 每方法至多一次
    method.resolved_handler = handler;                     // 锁内单写者
}
if (handler == nullptr) {
    // 现有 miss 路径原样保留：RecordUnimplemented + survey 中性桩 /
    // UnsatisfiedLinkError。survey 合成方法（survey.unimplemented）与
    // 未实现 intrinsic 永远落在这里。
}
```

注意上述写法对"真 miss"（`Find` 也返回 null）每次调用都会重查一次。为保证
miss 也是一次性成本，请用**独立的 bound 标记**或哨兵值区分"未绑定"与
"已绑定为 null"，例如 `mutable bool handler_bound{}` 或把哨兵定义为指向静态
空 handler 的常量指针；两者择一，保证真 miss 的第二次调用同样不查容器
（miss 分支本身的记账/抛错行为不变）。

线程安全依据：`MODULE.md` 既有不变量——全部执行入口持 `VmExecutionLock`，
"linker 解析缓存、object model arena 与 intrinsic 侧表只有单写者，全部
intrinsic handler 都在锁内运行"。缓存写发生在锁内，与既有 linker 常量池
解析缓存同一模式，不需要原子量。

### 5. 生命周期不变量（写入 `MODULE.md`）

- registry 由 `Interpreter` 拥有，`Freeze()` 后不可再注册；handler 对象地址
  在 registry 生命周期内稳定。
- `resolved_handler` 指向 registry 拥有的对象；linker 与 Interpreter 同由
  `DexVmGuestBridge` 拥有、同生共死（析构顺序：threads → vm → model →
  linker），缓存指针不会悬垂。
- 不改 `IntrinsicHandler` 签名（`std::function` 保留——handler 几乎都是带
  捕获的 lambda）；本 WU 不做裸函数指针化。

## 明确不做（边界）

- 不动 `Step()`/`Tick()` 每指令的 `Execution()` thread-local 哈希查找
  （另立 WU）。
- 不做 invoke 参数封送的 args-shorty 预计算（另立 WU）。
- 不重组 intrinsic 实现文件、不引入 builder（问题 2 的独立专项）。
- 不改任何 miss/survey/记账语义，不改 handler 注册 API 的对外形态
  （`Register(id, handler)` 签名不变）。

## 触及文件（预计 ≤ 7）

| 文件 | 改动 |
| --- | --- |
| `include/ogplay/runtime/dexvm/interpreter.h` | registry 存储与 `Freeze()` |
| `include/ogplay/runtime/dexvm/class_linker.h` | `LinkedMethod` 缓存字段 |
| `src/runtime/dexvm/interpreter.cpp` | `Register/Find/Freeze` 实现、`InvokeIntrinsic` 绑定、执行入口冻结 |
| `src/runtime/dexvm/MODULE.md` | registry 冻结与绑定不变量 |
| `tests/dexvm/interpreter_tests.cpp` | 新增验收测试（见下） |
| `tests/dexvm/gap_survey_tests.cpp` | 如需，补"绑定不破坏 survey"断言 |

## 验收（机器可判定）

1. **行为零变化**：Windows/MSVC `windows-msvc` 预设全量 CTest 全绿
   （当前基线 702/702），dexasm 一致性夹具与 gap survey 开/关对照不变。
2. **新增测试**（`tests/dexvm/interpreter_tests.cpp`）：
   - 冻结语义：首次 `Call` 之后再 `Register` 抛
     `internal_invariant`；`Call` 之前注册仍成功。
   - 重复注册：同一 id 注册两次仍抛（既有语义在新容器上保持）。
   - miss 语义回归：调用一个已声明但未注册 handler 的 intrinsic 方法，
     两次调用**都**记账（`dexvm.intrinsic.<id>`）并抛
     `UnsatisfiedLinkError`——锁定"绑定缓存不吞掉第二次 miss 的记账"。
   - 绑定正确性：同一 intrinsic 方法调用两次返回一致结果（走缓存路径）。
3. **survey 回归**：`gap_survey_tests` 现有用例全绿（合成方法仍答中性值、
   命中计数不变）。
4. **性能证据（非硬性 gate）**：用 dexasm 夹具构造一个循环调用 intrinsic
   （如 `StringBuilder.append`）的微基准，记录改动前后 `Call` 耗时对照，
   证据留 `.local/evidence/wu-m9-032/`。宿主性能波动大，不设阈值断言，
   只要求方向为降且量级可解释（预期消除每调用 ~486 次字符串比较）。

## 收尾清单

- `docs/tasks/dexvm/README.md` 索引行状态更新。
- `src/runtime/dexvm/MODULE.md` 同步不变量（见 §5）。
- `capabilities.toml` 无新能力条目（纯内部性能改造，不新增/不降级任何
  能力状态）；如改动 `dexvm` 相关条目的 note，只允许追加事实。
- `docs/state/CURRENT.md` 滚动快照按惯例更新。
- 提交前：`cmake --preset windows-msvc`、`cmake --build --preset
  windows-msvc`、`ctest --preset windows-msvc` 全部通过。
