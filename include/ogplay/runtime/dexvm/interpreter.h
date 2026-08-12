#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/capability_ledger.h"
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
        std::uint32_t bits{};
        static_assert(sizeof(bits) == sizeof(value));
        __builtin_memcpy(&bits, &value, sizeof(bits));
        out.cat1 = bits;
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
        std::uint64_t bits{};
        __builtin_memcpy(&bits, &value, sizeof(bits));
        out.wide = bits;
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
    [[nodiscard]] float AsFloat() const {
        float value{};
        __builtin_memcpy(&value, &cat1, sizeof(value));
        return value;
    }
    [[nodiscard]] std::int64_t AsLong() const {
        return static_cast<std::int64_t>(wide);
    }
    [[nodiscard]] double AsDouble() const {
        double value{};
        __builtin_memcpy(&value, &wide, sizeof(value));
        return value;
    }
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

struct IntrinsicContext final {
    Interpreter& vm;
    VmObjectRef receiver;
    std::span<const VmValue> arguments;
};

using IntrinsicHandler = std::function<VmValue(IntrinsicContext&)>;

class IntrinsicRegistry final {
public:
    void Register(std::string handler_id, IntrinsicHandler handler);
    [[nodiscard]] const IntrinsicHandler* Find(
        std::string_view handler_id) const;
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    std::vector<std::pair<std::string, IntrinsicHandler>> handlers_;
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
                IntrinsicRegistry intrinsics, NativeMethodBridge* bridge,
                core::CapabilityLedger& ledger, InterpreterConfig config = {});
    ~Interpreter();
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    // Executes a method to completion on the current host thread. Arguments
    // must match the descriptor ('this' first for instance methods).
    [[nodiscard]] VmCallOutcome Call(VmMethodId method,
                                     std::span<const VmValue> arguments);

    // Ensures a class is initialized (triggers <clinit>); returns a Java
    // exception outcome if initialization fails.
    [[nodiscard]] VmCallOutcome EnsureClassInitialized(DexClassId java_class);

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

    // Registers the built-in java.* core handlers (object/string/throwable).
    void RegisterCoreBuiltins();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    friend class InterpreterAccess;
};

}  // namespace ogplay::runtime::dexvm
