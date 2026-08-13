# DVM-34 · intrinsic 声明即绑定：基础设施与双通道

## 目标（一句话）

让 intrinsic 方法声明直接内嵌 `IntrinsicHandler`（声明即绑定），建立
`IntrinsicClassBuilder` 与新旧双通道，字符串 handler id 从此只属于待迁移的
存量代码，新代码不再产生任何 id。

## 依赖

- DVM-32（懒绑定与 registry 冻结）：其 miss 语义与测试是本 WU 双通道回退
  路径的基线。
- 本 WU 是 DVM-35..37 迁移批次的地基；不迁移任何存量 handler。

## 背景与问题证据（实现前请核实仍成立）

1. `IntrinsicMethodDecl.handler` 与 `IntrinsicClassDecl.clinit_handler` 是
   字符串 id（`include/ogplay/runtime/dexvm/class_linker.h` 36-64 行）；
   handler 实现在另一批文件里以同一字符串 `Register`。id 是两个登记点之间的
   人造 join key：拼错/漏注册只能在首调时以 `UnsatisfiedLinkError` 暴露，
   声明与实现分居两处是"落地混乱"的结构性原因。
2. DVM-32 已把 `IntrinsicHandler` 的 using 声明放进 `class_linker.h`，
   内嵌无新增依赖。
3. 装配点：`DexVmGuestBridge` 构造（`dexvm_bridge.cpp` 513-547 行）接受
   `platform_catalog`（span of decl）+ `platform_handlers`（registry 回调）
   两个入参；core 侧 `CoreIntrinsicCatalog()` + `RegisterCoreBuiltins()` 同样
   是目录/实现两张皮。
4. 约 486 个注册、233 个唯一类描述符分散在 dexvm 3 个 handler 文件与
   integration 13 个文件中。

## 方案

### 1. 声明结构内嵌 handler（注意聚合初始化兼容）

`IntrinsicMethodDecl` **末尾追加** `IntrinsicHandler implementation;`；
`IntrinsicClassDecl` **末尾追加** `IntrinsicHandler clinit_implementation;`。
存量字符串字段 `handler`/`clinit_handler` 原位保留到 DVM-37 才删除——存量
catalog 文件大量使用聚合初始化，新成员只能追加在末尾，禁止插入或重排。

`DexClassLinker::RegisterIntrinsics` 把 `implementation` move 进
`LinkedMethod`（新增拥有型成员 `IntrinsicHandler implementation;`），
`clinit_implementation` 进 `LinkedClass`。

### 2. `IntrinsicClassBuilder`（新文件，dexvm 层）

`include/ogplay/runtime/dexvm/intrinsic_builder.h` +
`src/runtime/dexvm/intrinsic_builder.cpp`：

```cpp
class IntrinsicClassBuilder final {
public:
    explicit IntrinsicClassBuilder(std::string descriptor);
    IntrinsicClassBuilder& Super(std::string descriptor);
    IntrinsicClassBuilder& Implements(std::string descriptor);
    IntrinsicClassBuilder& MarkInterface();
    // handler 传 {} 表示"有意声明未实现"（保留记账 miss 语义）。
    IntrinsicClassBuilder& Static(std::string name, std::string descriptor,
                                  IntrinsicHandler handler);
    IntrinsicClassBuilder& Virtual(std::string name, std::string descriptor,
                                   IntrinsicHandler handler);
    IntrinsicClassBuilder& Overridable(std::string name,
                                       std::string descriptor,
                                       IntrinsicHandler handler);
    IntrinsicClassBuilder& Field(std::string name, std::string descriptor,
                                 bool is_static);
    IntrinsicClassBuilder& ConstantInt(std::string name,
                                       std::string descriptor,
                                       std::int64_t value);
    IntrinsicClassBuilder& ConstantString(std::string name,
                                          std::string value);
    IntrinsicClassBuilder& Clinit(IntrinsicHandler handler);
    [[nodiscard]] IntrinsicClassDecl Build() &&;
};
```

`Build()` 做装配期校验，失败抛 `DexVmError(internal_invariant)`：类/方法/
字段 descriptor 形状合法（复用既有 descriptor helper）、同类内
（name, descriptor）不重复、interface 不得有实例字段。校验时机从"首调爆炸"
提前到"装配即爆"。builder 产出的 decl 不写字符串 id 字段。

### 3. 解释器双通道（迁移期形态）

- `InvokeIntrinsic`：`method.implementation` 非空 → 直调（无查找、无绑定）；
  否则走 DVM-32 现状（懒绑定 id → miss 记账/survey/`UnsatisfiedLinkError`）。
- `EnsureInitialized` 的 clinit 分发同构：`clinit_implementation` 优先，
  否则按现状 `Find(intrinsic_clinit_handler)`。
- miss 诊断键切换：`RecordUnimplemented` 与 `UnsatisfiedLinkError` 文案改用
  `<owner描述符>.<方法名><descriptor>`（id 可能为空且本来就还原不出签名）；
  id 仅在非空时作为附注保留。DVM-32 在 `interpreter_tests.cpp` 中对
  `dexvm.intrinsic.<id>` 键的断言同步更新。

## 边界（不做）

- 不迁移任何存量 catalog/handler（DVM-35/36）。
- 不删除 registry/id 通道的任何代码（DVM-37）。
- 不做类型安全 DSL（descriptor 仍是运行时字符串，构造期校验形状）。
- 不用宏或全局静态自注册；装配保持显式注入。

## 触及文件（预计 ≤ 9）

`class_linker.h`/`class_linker.cpp`、`interpreter.cpp`、新
`intrinsic_builder.h`/`intrinsic_builder.cpp`、`CMakeLists.txt`、
`src/runtime/dexvm/MODULE.md`、`tests/dexvm/interpreter_tests.cpp`
（含既有 miss 键断言更新）。

## 验收（机器可判定）

1. 全量 CTest 全绿（当前基线 705/705），存量 id 通道行为不变。
2. 新增测试（`interpreter_tests.cpp`）：
   - builder 声明的 intrinsic 类（含 static/virtual/overridable 方法、常量
     字段、clinit handler）经 `RegisterIntrinsics` + `Link` 后可直调，
     结果正确且不经过 registry（registry 保持空即可证明）；
   - builder 校验：重复方法、非法 descriptor、interface 带实例字段在
     `Build()` 即抛 `internal_invariant`；
   - `implementation` 为空且无 id 的方法：调用记账键为
     `dexvm.intrinsic.<owner>.<name><descriptor>` 并抛
     `UnsatisfiedLinkError`，两次调用都记账；
   - 双通道共存：同一 VM 内新通道类与旧 id 通道类各自正确分发。
3. 收尾：`MODULE.md` 公共 API/不变量新增声明即绑定通道；
   `docs/state/CURRENT.md` 滚动更新；`capabilities.toml` 无能力变化。
