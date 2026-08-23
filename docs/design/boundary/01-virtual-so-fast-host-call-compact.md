# 01 · Android Native Boundary：Virtual SO 与 Fast Host Call

本章把当前 `runtime/boundary` 的集中式 HLE 重构为可长期扩展的
**Virtual SO Module + Fast Host Call** 架构，写给**实施阶段的 AI**。

目标只有两个：

1. Android 外部 SO（`liblog.so`、`libandroid.so`、`libEGL.so`、
   `libGLESv1_CM.so`、`libGLESv2.so`……）以独立虚拟模块表达；
2. guest 调用虚拟模块函数的稳态路径压缩为
   **PC → dense slot → `{fn, self}` → handler**，不再依赖中央 route、
   字符串查找或 `Cpu::Run()` 退出/重入。

本章改变的是 **Virtual SO 的组织方式** 与 **Guest→Host transport**。
JNI 仍保持独立的 JNIEnv/JavaVM ABI；只复用 CPU Fast Host Call。
`RegisterNatives`、`JNI_OnLoad`、JNI 对象/引用/异常/调用语义不并入
Virtual SO。

实施上下文顺序：

`AGENTS.md` → `docs/state/CURRENT.md` → 本章 →
涉及的 `src/runtime/*/MODULE.md` → 对应测试。

**结构性迁移不得顺手改变既有功能语义。**
例如当前 `liblog` 若仍是 stub，模块迁移先保持等价；
真正实现 `__android_log_print` 等另立功能任务。

---

## 1. 目标与非目标

| 目标 | 验收 |
| --- | --- |
| 每个 boundary SO 独立模块化 | SONAME、exports、API gate、handler 绑定集中在自己的 module 描述；新增 SO 不修改中央 route/switch |
| Virtual ELF 稳定 | 一个虚拟 SO 对当前 Android API 暴露完整的 **OGPlay 已支持 export 集**，不得按当前 imports 临时裁剪 |
| 冷/热路径分离 | ELF、symbol、API、diagnostic 元数据只用于初始化；稳态调用只访问 dense hot table |
| 高频调用低固定成本 | Dynarmic 成功处理 boundary/JNI fast host call 时不 `HaltExecution()`，不返回外层 `Cpu::Run()` |
| C++ 结构简单 | module 使用普通 `final` concrete type；不用 SO 继承树，不以 vtable 作为函数分派 |
| JNI 共用 transport | JNIEnv/JavaVM 继续是独立 ABI，只共用 CPU host-call hook 与 A32 register/stack view |
| 可增量迁移 | legacy slow path 保留为 bridge，未迁移函数继续按旧语义工作 |

**非目标：**

- 不把 JNI 做成 Virtual SO；
- 不重写 JNI family、对象模型、引用、异常、`RegisterNatives`、`JNI_OnLoad`；
- 不把整个 `libc.so` 虚拟化；真实 guest Bionic `libc.so` 继续执行；
- 不拆散 EGL/GLES 必须共享的 GL context、surface 与 object state；
- 不要求本章实现所有 Android NDK API；
- 不要求本章解决 variadic/callback 等全部复杂 ABI；
- 不把 Linux syscall 或 guest-call return trap 改成 boundary module。

---

## 2. 现状事实锚点

实施前逐项核实；主干若已变化，先修订事实再改代码。

| # | 当前事实 | 设计动作 |
| --- | --- | --- |
| 1 | `BionicProfile` 单独维护 `boundary_libraries`；`BuildAndroidBoundarySymbols()` 又维护另一份实现列表 | 由 `BoundaryCatalog` 成为 Virtual SO 的唯一事实来源 |
| 2 | `MakeBoundaryModule()` 只按当前 undefined imports 生成 synthetic dynsym | 改为按 module catalog 生成完整 active exports |
| 3 | `ExtendBionicLinkNamespace()` 对已存在的 synthetic module 不再补 export | 完整 export 后动态扩展才安全 |
| 4 | `HleRoute`、全局 function id、参数数量表、巨型 `Dispatch()` 集中管理 | 模块独立注册；hot path 直接 `{fn,self}` |
| 5 | `AndroidBoundaryHle::Impl` 同时持有 EGL/GLES、surface、input/looper、frame、trace 等状态 | module 与 shared service 分层；迁移期允许 facade 保留 |
| 6 | boundary thunk 是 4-byte Thumb `svc #2; bx lr`，但当前只映射一个 page | 改为多页 sealed thunk arena |
| 7 | boundary `SVC #2` 会让 Dynarmic `HaltExecution()`，随后 `GetState`/dispatch/`SetState` | 增加 CPU Fast Host Call，成功时直接在 JIT callback 内处理 |
| 8 | JNI 也使用固定 Thumb thunk，`SVC #3`，并同样走 `Cpu::Run()` 退出 + `GetState/SetState` | JNI 后续接入同一 CPU fast transport |
| 9 | JNI dispatcher 使用固定 slot ABI；JNIEnv/JavaVM 地址布局已经独立且明确 | 地址布局与 JNI 语义保持不动 |
| 10 | `runtime/execution` 仍承担 Linux SVC、guest lifecycle 与 slow HLE consumption | slow path 保留；fast transport 放 `cpu` |
| 11 | guest-call observer 目前依赖外层 SVC 安全边界 | observer 存在时先强制 slow path，除非后续单独定义 fast observer 语义 |
| 12 | nested guest call 会创建独立 `DynarmicCpu`，clone/thread CPU 需要共享相同运行契约 | fast hook 必须被 nested/clone CPU 正确继承；禁止同一 JIT 递归进入 |

---

## 3. 架构裁决

### 3.1 Module 是冷路径抽象，Hot Entry 才是热路径抽象

Virtual SO 的 module 负责：

- SONAME；
- Android API 可见性；
- export catalog；
- Virtual ELF identity；
- handler 与 module state 的绑定；
- diagnostic/symbolization 元数据。

正常调用不经过 module 查找：

```text
guest PLT
   ↓
Virtual SO thunk
   ↓
SVC #2
   ↓
slot = (pc - thunk_begin) >> 2
   ↓
hot[slot] = { fn, self }
   ↓
fn(self, call)
   ↓
continue guest JIT
```

热路径禁止：

- SONAME/symbol 字符串比较；
- `unordered_map`；
- `HleRoute`；
- module virtual `Invoke()`；
- `std::function`；
- `Cpu::GetState()` / `SetState()`；
- 成功调用后返回外层 `Cpu::Run()`。

### 3.2 Virtual SO 不建立继承树

不采用：

```cpp
class BoundaryModule {
public:
    virtual ~BoundaryModule() = default;
    virtual Result Invoke(FunctionId, Call&) = 0;
};
```

采用普通 concrete type：

```cpp
class LogModule final {
public:
    explicit LogModule(core::Logger& logger) noexcept
        : logger_(logger) {}

    std::int32_t AndroidLogWrite(
        std::int32_t priority,
        GuestCString tag,
        GuestCString text);

private:
    core::Logger& logger_;
};
```

统一性由静态 descriptor/traits 与一次 type erasure 提供：

```cpp
struct BoundaryModuleInstance final {
    const BoundaryModuleDescriptor* descriptor{};
    void* instance{};
};
```

继承只用于真正需要运行时替换的 service/backend contract，
不得为了“统一 module”引入 vtable。

### 3.3 Catalog 与 Runtime 分开

```text
BoundaryCatalog
    纯元数据
    SONAME / exports / API
    Virtual ELF
    deterministic thunk layout

BoundaryRuntime
    当前 session 的 module instances
    hot table
    thunk mapping
    fast call
```

`BoundaryCatalog` 不依赖 ANGLE surface、input、audio 或运行期窗口状态，
因此 link preflight 可以只使用 catalog，不构造完整 graphics boundary。

### 3.4 Fast Host Call 属于 `cpu`

Guest→Host fast transport 是 CPU backend 能力，不属于某个 Android module。

推荐抽象：

```cpp
namespace ogplay::cpu {

struct A32HostCallContext final {
    std::span<std::uint32_t, 16> registers;
    std::uint64_t thread_id{};
    memory::GuestAddress pc;
};

enum class HostCallResult : std::uint8_t {
    handled,
    unhandled,
    fault,
};

using HostCallFn =
    HostCallResult (*)(
        void* userdata,
        std::uint32_t svc,
        A32HostCallContext&) noexcept;

struct HostCallHook final {
    HostCallFn invoke{};
    void* userdata{};
};

}
```

CPU 只负责：

```text
SVC
 ↓
HostCallHook
 ↓
handled ? continue JIT : existing slow stop
```

CPU 不知道 `liblog`、EGL、JNIEnv 等上层概念。

### 3.5 JNI 复用 transport，不成为 Virtual SO

保持：

```text
GuestJniAbi
  JNIEnv table ─┐
  JavaVM table ─┼→ SVC #3 → JniGuestCallDispatcher → runtime/jni
                ┘
```

只把 SVC #3 的 transport 接入 CPU Fast Host Call。

以下保持原有 ownership：

- JNI slot 定义；
- JNI variadic / `V` / `A` 参数解码；
- references / exceptions / strings / arrays；
- method/field invocation；
- `RegisterNatives`；
- `JNI_OnLoad`。

### 3.6 libc intercept 与 Virtual SO 分开

```text
Virtual SO:
  liblog.so
  libEGL.so
  ...

Guest ELF override:
  real libc.so
    ├─ memcpy  → host thunk
    ├─ strlen  → host thunk
    └─ ...
```

两者可以复用 thunk/fast-call engine，
但 ELF ownership 与 metadata 必须区分。

---

## 4. Module Descriptor 与 Catalog

推荐最小描述：

```cpp
using BoundaryInvokeFn =
    void (*)(void* self, A32BoundaryCall& call) noexcept;

struct BoundaryExportDescriptor final {
    std::string_view name;
    std::uint16_t local_id{};
    AndroidApiRange api{};
    BoundaryExportKind kind{BoundaryExportKind::host_call};

    // cold only
    const A32BoundarySignature* signature{};

    // host_call
    BoundaryInvokeFn invoke{};
};

struct BoundaryModuleDescriptor final {
    std::string_view soname;
    AndroidApiRange api{};
    std::span<const BoundaryExportDescriptor> exports;
};
```

module local id 只用于 trace、debug、测试和 symbolization；
**hot path 不按 local id 再做一次 switch**。

模块可以用 traits：

```cpp
template <>
struct BoundaryModuleTraits<LogModule> {
    static constexpr std::string_view soname = "liblog.so";
    static constexpr auto exports = std::to_array({
        MakeExport<&LogModule::AndroidLogWrite>(
            "__android_log_write"),
        MakeExport<&LogModule::AndroidLogPrint>(
            "__android_log_print"),
    });
};
```

`BoundaryCatalog::Seal(api)` 后产生不可变布局：

```cpp
struct BoundaryModuleLayout final {
    const BoundaryModuleDescriptor* descriptor{};
    std::uint32_t first_slot{};
    std::uint32_t slot_count{};
};
```

Seal 阶段完成：

1. API 过滤；
2. SONAME 唯一性检查；
3. export name/local id 唯一性检查；
4. deterministic global slot 分配；
5. Virtual ELF symbol address 计算；
6. cold lookup index 构建；
7. thunk arena 总大小计算。

运行阶段不再修改 catalog。

---

## 5. Virtual ELF：完整 Active Export

synthetic module 的 dynsym 必须来自 module catalog，而不是当前 imports：

```cpp
for (const auto& export_ : catalog.ActiveExports(module)) {
    AddAbsoluteSymbol(
        export_.name,
        catalog.ThunkAddress(module, export_));
}
```

**完整**指：

> 当前 Android API 下，OGPlay 对该 Virtual SO 已声明支持的全部 export。

不要求一次覆盖真实 Android SO 的全部公开 ABI；
但**已声明支持**的 export 必须从 module 第一次建立起全部存在。

这样：

```text
初始 ELF 只 import __android_log_write
later dlopen plugin import __android_log_print
```

仍能使用同一个已存在的 `liblog.so`，无需补 module dynsym。

未实现 symbol 不得以 fake success 方式加入 catalog。

---

## 6. Thunk Arena 与 Hot/Cold Layout

继续使用 4-byte Thumb thunk：

```asm
svc #2
bx  lr
```

但不再限制一页。

```cpp
struct BoundaryThunkArena final {
    memory::GuestAddress begin;
    std::uint32_t slot_count{};
};
```

地址：

```cpp
thunk = begin + slot * 4 + 1; // Thumb bit
```

module 获得连续 slot block，便于：

- module identity；
- `dladdr`/diagnostic；
- trace/statistics；
- 检测错误 PC；
- deterministic tests。

### Hot data

```cpp
struct BoundaryHotEntry final {
    BoundaryInvokeFn invoke{};
    void* self{};
};
```

正常调用只碰此表。

### Cold data

```cpp
struct BoundaryColdEntry final {
    std::string_view soname;
    std::string_view symbol;
    const A32BoundarySignature* signature{};
    std::uint16_t module_id{};
    std::uint16_t local_id{};
};
```

只用于 loader、debug、trace、error。

thunk code 与 hot table 在 session 运行前 Seal；
运行期不扩容、不移动。

---

## 7. Module State 与 Shared Service

“每个 SO 独立 module”不等于“每个 SO 独占状态”。

例如：

```text
GraphicsBoundaryContext
      ▲      ▲      ▲
      │      │      │
   EglModule Gles1Module Gles2Module
```

共享：

- `GuestGlContext`；
- ANGLE context；
- surface；
- GL object namespace；
- graphics trace/state。

其他模块依赖显式 service：

```text
LogModule      → Logger/LogSink
AndroidModule  → Looper/Input/NativeWindow services
OpenSlModule   → Audio service
```

禁止：

- module 通过 global singleton 找 service；
- module 在调用期间动态 `registry.Find()` 其他 module；
- 为了 SO 独立而复制本应共享的 graphics state。

迁移期间可以保留 `AndroidBoundaryHle` facade，
但新 module 不得再增加新的中央 route 分支。

---

## 8. A32 Call View 与 Typed Binder

抽出轻量 guest-call view，直接引用 live CPU registers：

```cpp
class A32BoundaryCall final {
public:
    std::uint32_t Word(std::size_t index) const;

    std::uint32_t R0() const noexcept;
    std::uint32_t R1() const noexcept;
    std::uint32_t R2() const noexcept;
    std::uint32_t R3() const noexcept;

    memory::GuestAddress Sp() const noexcept;
    std::uint32_t Lr() const noexcept;
    std::uint64_t ThreadId() const noexcept;

    void ReturnU32(std::uint32_t) noexcept;
    void ReturnU64(std::uint64_t) noexcept;
};
```

前 4 个 word 直接来自 r0-r3；
只有第 5 个参数之后才读取 guest stack。
能 bulk-read 时不得逐 word 反复访问 memory API。

普通 C ABI export 用编译期 binder：

```cpp
MakeExport<&LogModule::AndroidLogWrite>(
    "__android_log_write");
```

module 业务实现不直接操作 `args[0]`：

```cpp
std::int32_t LogModule::AndroidLogWrite(
    std::int32_t priority,
    GuestCString tag,
    GuestCString text);
```

guest pointer 必须使用强类型：

```cpp
GuestPtr<T>
GuestCString
GuestSpan<T>
```

不得把 guest address 直接 `reinterpret_cast` 成 host pointer。

复杂 ABI：

- variadic；
- `va_list`；
- callbacks；
- 特殊 struct return；
- reverse callback thunk；

允许走 custom wrapper 或 guest stub，
不要让一个“万能 binder”污染所有普通 export。

---

## 9. CPU Fast Host Call

Dynarmic 当前所有 SVC 都会记录 stop 并 `HaltExecution()`。
新路径只对明确注册的 fast SVC 尝试 host hook：

```cpp
void CallSVC(std::uint32_t immediate) override {
    if (host_call_.invoke != nullptr) {
        A32HostCallContext call{
            .registers = LiveRegisters(),
            .thread_id = thread_id_,
            .pc = CurrentSvcPc(),
        };

        switch (host_call_.invoke(
            host_call_.userdata,
            immediate,
            call)) {
        case HostCallResult::handled:
            return; // 不 HaltExecution
        case HostCallResult::unhandled:
            break;
        case HostCallResult::fault:
            RecordHostCallFault(...);
            return;
        }
    }

    RecordStop(...); // existing slow path
}
```

fast path 第一阶段只消费：

- `SVC #2`：Virtual SO；
- `SVC #3`：JNI guest。

其他 SVC 继续旧路径。

### 错误边界

C++ exception 不得跨 Dynarmic callback/JIT frame 传播。

handler 应 `noexcept`，失败转换为：

- structured host-call fault；
- 或 `unhandled` 回 slow path；
- 或上层已有的明确 guest/runtime error 状态。

具体 fault carrier 可按现有 CPU fault 风格实现，
但必须可测试、可诊断。

### observer 兼容

当前 guest-call `slice_observer` 会在外层消费 HLE SVC 后被调用。
在未定义等价的 fast observer 语义前：

> **启用 observer 的 session 强制 boundary/JNI 使用 slow path。**

不得为了性能静默改变 observer 调用边界。

### re-entry

fast callback 内禁止对**同一个 JIT 实例**递归 `Run()`。

需要 Guest→Host→Guest 的场景继续使用当前 nested guest-call 策略：
创建/使用独立 CPU context，并继承相同 host-call hook。

clone/new thread CPU 也必须继承同一 hook。

---

## 10. Boundary Fast Router

`SVC #2` 的 router 只做常数时间定位：

```cpp
HostCallResult BoundaryRuntime::TryFastCall(
    cpu::A32HostCallContext& cpu_call) noexcept {

    const auto pc = cpu_call.pc.Value();

    if (pc < thunk_begin_ || pc >= thunk_end_)
        return HostCallResult::unhandled;

    const auto offset = pc - thunk_begin_;
    if ((offset & 3U) != 0U)
        return HostCallResult::unhandled;

    const auto slot = offset >> 2U;
    if (slot >= hot_.size())
        return HostCallResult::unhandled;

    const auto entry = hot_[slot];
    if (entry.invoke == nullptr)
        return HostCallResult::unhandled;

    A32BoundaryCall call{...};
    entry.invoke(entry.self, call);

    return call.Failed()
        ? HostCallResult::fault
        : HostCallResult::handled;
}
```

不要在 router 中恢复：

```text
route → module → function id → switch
```

---

## 11. JNI 接入范围

JNI 本章只做 transport 对接：

```text
JNI thunk SVC #3
      ↓
CPU HostCallHook
      ↓
JniGuestCallDispatcher::TryFastCall
      ↓
existing JNI family handler
```

保留：

- JNIEnv 233-slot 与 JavaVM 8-slot layout；
- receiver 校验；
- thread id 校验；
- existing slot binding/seal contract；
- JNI-specific `Call*Method` / `V` / `A` decoder；
- return width 编码；
- capability ledger；
- expected-unbound 语义。

JNI 可以把：

```cpp
std::function<JniGuestCallResult(...)>
```

进一步降为：

```cpp
struct JniHotEntry {
    JniInvokeFn invoke{};
    void* context{};
};
```

但这不是 Virtual SO 模块化的前置条件。
若本次改动范围过大，可以保留现有 handler storage，
先消掉 JIT halt / `GetState` / `SetState`。

---

## 12. Loader、Namespace 与 Preflight

### Native dependency closure

遇到 `DT_NEEDED`：

```text
if BoundaryCatalog.Contains(soname, api):
    不加载真实 guest ELF
    确保 synthetic module 存在
else:
    按 APK / supplied Bionic ELF 正常加载
```

catalog 不应把“声明为 boundary 但没有实现 module”的 SONAME 判为可用。

### Namespace builder

`BuildBionicLinkNamespace()` / `ExtendBionicLinkNamespace()`：

- 不再收集 imports 来裁剪 synthetic dynsym；
- 直接按 catalog active exports 建立 Virtual ELF；
- 已存在 module 不需要后续 enrich；
- deterministic SONAME/module order 保持可测试。

### Preflight

preflight 只依赖：

```text
Bionic profile
BoundaryCatalog
Virtual ELF builder
loader
```

不得为了 symbol metadata 构造：

```text
ANGLE backend
surface
input
full AndroidBoundaryHle runtime state
```

### guest-supplied 同名 boundary SO

若 SONAME 被 catalog 声明为 Virtual SO，
guest/APK 提供的同名 ELF 不得无声覆盖它。
保持当前“boundary 优先”的语义，并给出明确 diagnostic。

---

## 13. 性能、生命周期与测试

### 结构性性能 Gate

正常 fast path：

- 不产生外层 supervisor-call stop；
- 不调用 `Cpu::GetState/SetState`；
- 不做字符串 lookup；
- 不经过 `HleRoute`；
- 不经过 module virtual call；
- 不访问 cold metadata；
- <= 4 word 参数不读 guest stack。

### Microbenchmark

至少覆盖：

1. empty 0-arg host call；
2. 4-word host call；
3. 5+ word stack call；
4. representative GLES hot call；
5. JNI fixed slot call；
6. forced slow-path baseline。

记录 backend、host arch、build type、iterations、ns/call 或 calls/s、
fast/slow 对比；有条件再补 title sample。
**先测量，不预设性能倍数。**

### 生命周期

catalog/layout seal 后只读；hot table 地址稳定。
teardown：

```text
stop guest threads
→ 禁止新 host call
→ detach CPU host-call hook
→ unmap thunk arena
→ destroy module instances/services
```

不得让 CPU callback 持有已析构 runtime 指针。

### 测试矩阵

**Catalog / ELF**

- SONAME/export/local id 唯一；
- API filter；
- deterministic slot/address；
- synthetic dynsym = 完整 active exports；
- late `dlopen` 新 import 能解析既有 Virtual SO；
- catalog 与 boundary declaration 不再漂移。

**A32 / CPU**

- r0-r3、stack args、64-bit return；
- guest pointer fault；
- typed/custom binder；
- `handled` 不 halt；
- `unhandled` 进入 slow path；
- fault 可诊断；
- nested/clone hook inheritance；
- observer 强制 slow path；
- teardown 无 dangling callback。

**Boundary**

- `liblog`、`libandroid`、EGL/GLES 迁移前后行为等价；
- graphics shared state 不复制；
- hot slot 指向正确 module instance；
- dynamic load 正确。

**JNI**

- 既有 JNI tests 全过；
- SVC #3 fast/slow 返回与失败语义一致；
- unbound/receiver/thread 校验一致；
- `RegisterNatives` / `JNI_OnLoad` 不受影响。

---

## 14. WU 分批

压缩为 **4 个 WU**。每个 WU 必须独立可编译、可测试、可回退。

### BND-1 · Virtual SO 基础与完整 ELF

目标：先完成模块化，不改变调用 transport。

交付：

- `BoundaryCatalog` + static descriptor/traits；
- `BoundaryModuleInstance` type erasure；
- deterministic module-local id / global slot layout；
- synthetic ELF 改为完整 active exports；
- loader/namespace/preflight 改为使用 catalog；
- profile 重复 boundary SONAME 列表退出事实来源；
- `liblog`、`libandroid`、EGL/GLES 先以 module 描述包装，
  handler 可继续转发 legacy `AndroidBoundaryHle`；
- late import regression。

验收：

```text
新增 Virtual SO 不再修改中央 symbol catalog；
Virtual ELF 不按 import 裁剪；
现有 title 行为等价。
```

### BND-2 · Boundary Fast Host Call

目标：一次完成 Guest→Virtual SO 热路径。

交付：

- 多页 sealed thunk arena；
- hot/cold table；
- live-register `A32BoundaryCall`；
- typed binder + guest pointer 强类型；
- CPU generic `HostCallHook`；
- Dynarmic `SVC #2` fast path；
- O(1) PC→slot→`{fn,self}` router；
- observer slow-path fallback；
- nested/clone hook inheritance；
- fast/slow equivalence + benchmark。

不要求本 WU 重写 EGL/GLES 业务实现。

验收：

```text
正常 SVC #2：
不退出 Cpu::Run()
不 GetState/SetState
不 route/switch
不字符串 lookup
```

### BND-3 · JNI Fast Transport 与 libc Override 分离

目标：复用同一 CPU transport，但保持上层 ownership。

交付：

- `SVC #3` 接入 HostCallHook；
- JNI dispatcher 增加 live-register fast entry；
- JNI slot layout/family/decoder 不变；
- fast/slow JNI 语义等价；
- 可选：有性能证据时把 JNI slot `std::function` 降为 `{fn,context}`；
- libc host intercept 从 Virtual SO metadata 中分离为
  `GuestSymbolOverride`；
- libc override 复用 thunk/host-call engine，但仍属于真实 guest `libc.so`。

不改变：

```text
RegisterNatives
JNI_OnLoad
JNI object/reference/exception
JNI method/field/string/array semantics
```

### BND-4 · Legacy 收口

目标：删除迁移期中央结构，固化长期架构。

交付：

- 剩余 boundary exports 迁入 concrete modules；
- 提取共享 services，尤其 graphics context；
- 删除不再需要的：
  - `HleRoute`
  - 全局 fallback function id
  - 平行 parameter-count catalog
  - 巨型中央 `Dispatch()`
  - import-driven synthetic export builder
- `AndroidBoundaryHle` 若保留，只允许作为 facade；
- 更新相关 `MODULE.md`；
- 全量 exact/scenario/title gate + benchmark 报告。

完成后新增 SO 的主要改动应是：

```text
modules/<name>/<name>_module.h/.cpp
+ catalog registration
+ tests
```

---

## 15. AI 实施纪律与风险

实施纪律：

1. 先核实 §2 事实锚点并读相关 `MODULE.md`；
2. structural refactor 不修功能语义 bug；
3. fast path 不使用 `std::function`、字符串查找、module vtable dispatch；
4. C++ exception 不跨 CPU/JIT callback；
5. slow path 在等价性 gate 通过前不得删除；
6. 不复制 JNI/A32/graphics 已有语义；
7. 修改依赖边界同步更新 `MODULE.md`；
8. 性能改动必须有结构性证据和 benchmark；
9. 未实现 Android export 不得注册 fake handler。

| 风险 | 裁决 |
| --- | --- |
| 模块化引入额外 virtual dispatch | module 只在 cold path；hot table 直接 `{fn,self}` |
| late dlopen 因 dynsym 裁剪失败 | module 首次建立即发布完整 active exports |
| 新增 SO 仍修改多张中央表 | `BoundaryCatalog` 是唯一事实来源 |
| GLES module 拆分导致状态复制 | module 独立、graphics service 共享 |
| thunk 超过一页 | 多页 arena |
| fast SVC 改变 observer | observer 强制 slow path |
| exception 穿过 JIT | `noexcept` + structured fault |
| 同 JIT re-entry | 禁止；使用独立 nested CPU |
| JNI 被抽象成 SO | 只共享 transport |
| libc intercept 与 Virtual SO 混淆 | 独立 `GuestSymbolOverride` |
| binder 无法覆盖复杂 ABI | custom wrapper / guest stub |
| preflight 依赖完整 graphics runtime | catalog 与 runtime state 分离 |

---

## 16. 完成定义

### 模块化

- 每个 Virtual SO 有独立 descriptor/module implementation；
- 不存在 SO 继承树；
- catalog 是 SONAME/export 唯一事实来源；
- synthetic ELF 使用完整 active exports；
- dynamic extension 不需要 enrich 既有 Virtual SO；
- 新增 SO 不修改中央 route/switch。

### 性能

正常调用：

```text
guest thunk
→ SVC #2
→ CPU HostCallHook
→ slot
→ hot[slot]
→ fn(self, call)
→ continue JIT
```

且：

- 不返回外层 `Cpu::Run()`；
- 不 `GetState/SetState`；
- 不查 SONAME/symbol；
- 不走 module vtable；
- 不访问 cold metadata。

### JNI

- JNI 仍是独立 JNIEnv/JavaVM ABI；
- SVC #3 使用同一 CPU fast transport；
- JNI family/decoder/对象语义保持；
- `RegisterNatives` 与 `JNI_OnLoad` ownership 不变。

### 长期架构

```text
                     cpu
                      │
               Fast Host Call
                      │
          ┌───────────┴───────────┐
          │                       │
 runtime/boundary          runtime/jni_guest
          │                       │
 Virtual SO modules         JNIEnv/JavaVM ABI
          │                       │
 shared services              runtime/jni
```

长期原则：

> **模块层面解耦，调用层面扁平化。**

> **Virtual SO 是 ELF/代码组织抽象；Hot Entry 是执行抽象。**

> **JNI 与 Virtual SO 共用 Guest→Host transport，但不共用上层语义模型。**
