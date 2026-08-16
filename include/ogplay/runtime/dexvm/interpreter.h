#pragma once

#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/core/logger.h"
#include "ogplay/runtime/dexvm/class_linker.h"
#include "ogplay/runtime/dexvm/object_model.h"

namespace ogplay::runtime::dexvm {

// Interpreter kernel (02 §7..§9): tagged frames, table dispatch, three-way
// invoke routing (interpreted / intrinsic / native bridge), real exception
// unwinding and the <clinit> state machine. Per-opcode semantics follow the
// AOSP mterp C implementations at the pinned baseline; conformance fixtures
// record the exact source file per family (05 §2).

struct VmValue final {
    enum class Kind : std::uint8_t { void_value, cat1, wide, ref };
    Kind kind{Kind::void_value};
    std::uint32_t cat1{};
    std::uint64_t wide{};
    VmObjectRef ref{};

    [[nodiscard]] static VmValue Void() { return {}; }
    [[nodiscard]] static VmValue Int(const std::int32_t value) {
        VmValue out;
        out.kind = Kind::cat1;
        out.cat1 = static_cast<std::uint32_t>(value);
        return out;
    }
    [[nodiscard]] static VmValue Float(const float value) {
        VmValue out;
        out.kind = Kind::cat1;
        out.cat1 = std::bit_cast<std::uint32_t>(value);
        return out;
    }
    [[nodiscard]] static VmValue Long(const std::int64_t value) {
        VmValue out;
        out.kind = Kind::wide;
        out.wide = static_cast<std::uint64_t>(value);
        return out;
    }
    [[nodiscard]] static VmValue Double(const double value) {
        VmValue out;
        out.kind = Kind::wide;
        out.wide = std::bit_cast<std::uint64_t>(value);
        return out;
    }
    [[nodiscard]] static VmValue Ref(const VmObjectRef reference) {
        VmValue out;
        out.kind = Kind::ref;
        out.ref = reference;
        return out;
    }

    [[nodiscard]] std::int32_t AsInt() const {
        return static_cast<std::int32_t>(cat1);
    }
  [[nodiscard]] float AsFloat() const { return std::bit_cast<float>(cat1); }
    [[nodiscard]] std::int64_t AsLong() const {
        return static_cast<std::int64_t>(wide);
    }
  [[nodiscard]] double AsDouble() const { return std::bit_cast<double>(wide); }
};

// Thrown by intrinsic handlers and internal helpers to raise a Java
// exception; the interpreter converts it into a pending VM throwable.
struct VmJavaThrow final {
    std::string descriptor;
    std::string message;
};

struct VmStackEntry final {
    std::string class_descriptor;
    std::string method_name;
    std::uint32_t pc{};
};

struct VmCallOutcome final {
    VmValue value;
    VmObjectRef exception;                 // invalid = normal completion
    DexClassId exception_class;
    std::string exception_message;
    std::vector<VmStackEntry> exception_stack;
};

class Interpreter;
class VmMonitorTable;

// Strong handle selecting one independent interpreter execution state. The
// linker, object model and intrinsic catalog remain owned by Interpreter;
// frames, pending exception, ticks and monitor recursion are selected by
// this context.
class InterpreterExecutionContext final {
public:
    [[nodiscard]] std::uint64_t Token() const noexcept { return token_; }
    [[nodiscard]] bool BelongsTo(const Interpreter* interpreter) const noexcept {
        return owner_ == interpreter;
    }

private:
    const Interpreter* owner_{};
    std::uint64_t token_{};
    friend class Interpreter;
};

struct InterpreterExecutionSnapshot final {
    std::size_t frame_depth{};
    bool has_pending_exception{};
    std::uint64_t ticks{};
    std::size_t held_monitor_count{};
    std::uint32_t native_depth{};
    bool stop_requested{};
};

// Serializes bytecode execution across the real host threads of 04 §3. One
// guest Java thread is one host thread and blocks for real, but only one of
// them interprets at a time: linker resolution caches, the object model
// arena and the intrinsic side tables keep a single writer. The lock is
// held recursively by its owning host thread; blocking primitives release
// every level and restore the same depth after they are resumed.
class VmExecutionLock final {
public:
    using BlockingObserver =
        void (*)(void* context, std::thread::id thread, bool blocked) noexcept;

    VmExecutionLock();
    ~VmExecutionLock();
    VmExecutionLock(const VmExecutionLock&) = delete;
    VmExecutionLock& operator=(const VmExecutionLock&) = delete;

    void Acquire();
    void Release();
    // Drops every level held by the calling thread and reports the depth so
    // a blocking primitive can restore it; 0 when the caller held nothing.
    [[nodiscard]] std::size_t ReleaseForBlocking();
    void ReacquireAfterBlocking(std::size_t depth);
    [[nodiscard]] bool HeldByCurrentThread() const;
    // Observes real host threads entering/leaving guest blocking scopes.
    // The callback runs outside the execution-lock mutex and must not throw.
    // Passing nullptr for both arguments detaches the observer.
    void SetBlockingObserver(void* context, BlockingObserver observer) noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class VmExecutionLockScope final {
public:
    explicit VmExecutionLockScope(VmExecutionLock& lock) : lock_(&lock) {
        lock_->Acquire();
    }
    ~VmExecutionLockScope() { lock_->Release(); }
    VmExecutionLockScope(const VmExecutionLockScope&) = delete;
    VmExecutionLockScope& operator=(const VmExecutionLockScope&) = delete;

private:
    VmExecutionLock* lock_;
};

struct IntrinsicContext final {
    Interpreter& vm;
    VmObjectRef receiver;
    std::span<const VmValue> arguments;
};

// Stage-2 boundary: native methods resolve through this bridge into the A32
// guest call executor. Without a bridge, native invokes are accounted and
// fail explicitly (never silently succeed).
class NativeMethodBridge {
public:
    virtual ~NativeMethodBridge() = default;
    [[nodiscard]] virtual VmValue Invoke(const LinkedMethod& method,
                                         VmObjectRef receiver,
                                         std::span<const VmValue> arguments) = 0;
};

struct InterpreterConfig final {
    std::uint32_t max_frames{512};
    std::uint64_t tick_budget{200'000'000ULL};
};

struct InterpreterStats final {
    std::uint64_t executed_instructions{};
    std::uint64_t method_calls{};
    std::uint64_t intrinsic_calls{};
    std::uint64_t native_calls{};
    std::uint64_t classes_initialized{};
};

class Interpreter final {
public:
    Interpreter(DexClassLinker& linker, JavaObjectModel& model,
                NativeMethodBridge* bridge,
                core::CapabilityLedger& ledger, InterpreterConfig config = {});
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    // Executes a method to completion on the current host thread. Arguments
    // must match the descriptor ('this' first for instance methods).
    [[nodiscard]] VmCallOutcome Call(VmMethodId method,
                                     std::span<const VmValue> arguments);
    [[nodiscard]] VmCallOutcome Call(
        const InterpreterExecutionContext& context, VmMethodId method,
        std::span<const VmValue> arguments);

    // Creates a new execution context. VmThreadRuntime gives each guest Java
    // thread one of these plus a real host thread; callers may also create
    // one directly to interleave independent calls on a single thread.
    [[nodiscard]] InterpreterExecutionContext CreateExecutionContext();
    void DiscardExecutionContext(const InterpreterExecutionContext& context);
    [[nodiscard]] InterpreterExecutionSnapshot ExecutionSnapshot(
        const InterpreterExecutionContext& context) const;

    // Teardown handshake: the context unwinds out of its interpreted frames
    // with DexVmErrorReason::thread_stopped instead of being killed. Checked
    // once per instruction, so a guest loop cannot ignore it.
    void RequestStop(const InterpreterExecutionContext& context);

    // Completes teardown after the context's host thread has been joined.
    // Native/A32 re-entry can observe thread_stopped below an outer Java
    // frame, so joining is the ownership proof that makes dropping those
    // unreachable frames safe. A context without a stop request is refused.
    void UnwindStoppedExecutionContext(
        const InterpreterExecutionContext& context);

    [[nodiscard]] VmExecutionLock& ExecutionLock() noexcept;
    // Session-wide object monitors: owner, recursion and wait set, keyed by
    // execution context token (04 §4).
    [[nodiscard]] VmMonitorTable& Monitors() noexcept;
    [[nodiscard]] std::uint64_t CurrentContextToken() const;
    // Guest native frames live on the single root guest stack, so parking a
    // thread that has one is refused rather than corrupting it.
    [[nodiscard]] std::uint32_t CurrentNativeDepth() const;
    [[nodiscard]] core::CapabilityLedger* Ledger() const noexcept;

    // Ensures a class is initialized (triggers <clinit>); returns a Java
    // exception outcome if initialization fails.
    [[nodiscard]] VmCallOutcome EnsureClassInitialized(DexClassId java_class);
    [[nodiscard]] VmCallOutcome EnsureClassInitialized(
        const InterpreterExecutionContext& context, DexClassId java_class);

    [[nodiscard]] DexClassLinker& Linker() noexcept;
    [[nodiscard]] JavaObjectModel& Model() noexcept;
    [[nodiscard]] const InterpreterStats& Stats() const noexcept;

    // Helpers shared with intrinsic handlers.
    [[nodiscard]] VmObjectRef NewStringUtf8(std::string_view utf8);
    [[nodiscard]] std::string StringUtf8(VmObjectRef string_ref) const;
    [[nodiscard]] VmObjectRef MakeThrowable(std::string_view descriptor,
                                            std::string_view message);
    void SetThrowableMessage(VmObjectRef throwable, VmObjectRef message);
    [[nodiscard]] VmObjectRef ThrowableMessage(VmObjectRef throwable) const;

    // Optional structured logger for guest-visible output (System.out,
    // android.util.Log) and uncaught-exception diagnostics.
    void SetLogger(core::Logger* logger) noexcept;
    [[nodiscard]] core::Logger* Log() const noexcept;

    // Intrinsic instance side state (StringBuilder buffers, collections).
    [[nodiscard]] std::u16string& BuilderBuffer(VmObjectRef instance);
    [[nodiscard]] std::vector<VmObjectRef>& ListStorage(VmObjectRef instance);
    [[nodiscard]] std::vector<std::pair<VmObjectRef, VmObjectRef>>&
    MapStorage(VmObjectRef instance);

    // Writes a reference into an intrinsic static field (System.out etc.).
    void SetIntrinsicStaticRef(std::string_view class_descriptor,
                               std::string_view field_name,
                               std::string_view field_descriptor,
                               VmObjectRef value);
    // Applies a conclusion-level Profile preset after the guest class has
    // completed initialization. bits contains the exact Dalvik slot bits
    // (wide values use all 64 bits; references use the low 32 bits).
    void SetStaticFieldBits(std::string_view class_descriptor,
                            std::string_view field_name,
                            std::string_view field_descriptor,
                            std::uint64_t bits);
    // Allocates an intrinsic-class instance (vm_instance form).
  [[nodiscard]] VmObjectRef
  NewIntrinsicInstance(std::string_view class_descriptor);
    // Java equality used by collections: string content equality when both
    // sides are strings, reference identity otherwise.
    [[nodiscard]] bool JavaEquals(VmObjectRef left, VmObjectRef right) const;

  // Object.notify/notifyAll: validates that the calling context owns the
  // receiver monitor, then moves waiters out of its wait set.
  void NotifyMonitor(VmObjectRef receiver, bool all) const;
  // Object.wait: parks on the receiver monitor and throws
  // InterruptedException when the wait ended in an interrupt.
  void WaitOnMonitor(VmObjectRef receiver, std::int64_t timeout_millis) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    friend class InterpreterAccess;
};

}  // namespace ogplay::runtime::dexvm
