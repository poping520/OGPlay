#pragma once

#include <compare>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace ogplay::runtime::dexvm {

// Strong fixed-width handles (docs/design/dexvm/02-architecture.md §5).
// They never expose host pointers and stay stable for the session lifetime.
template <typename Tag>
class DexVmHandle final {
public:
    explicit constexpr DexVmHandle(const std::uint32_t value = 0) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr std::uint32_t Value() const noexcept {
        return value_;
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return value_ != 0;
    }

    auto operator<=>(const DexVmHandle&) const = default;

private:
    std::uint32_t value_{};
};

struct DexClassIdTag;
struct VmMethodIdTag;
struct VmFieldIdTag;
struct VmObjectRefTag;

using DexClassId = DexVmHandle<DexClassIdTag>;
using VmMethodId = DexVmHandle<VmMethodIdTag>;
using VmFieldId = DexVmHandle<VmFieldIdTag>;
// Register-width reference handle into the JavaObjectModel handle space;
// value 0 is null.
using VmObjectRef = DexVmHandle<VmObjectRefTag>;

// Guest-visible linkage failures carry no guest refs; VM entry/dispatch owns
// throwable allocation and Java unwinding. Never infer these from host errors.
enum class JavaLinkageKind : std::uint8_t {
    no_class, no_method, no_field, incompatible_class_change, illegal_access,
    instantiation, abstract_method, verify, linkage, circularity, virtual_machine,
};
class JavaLinkageError final : public std::runtime_error {
public:
    JavaLinkageError(JavaLinkageKind kind, std::string message)
        : std::runtime_error(std::move(message)), kind_(kind) {}
    [[nodiscard]] JavaLinkageKind Kind() const noexcept { return kind_; }
    [[nodiscard]] const char* Descriptor() const noexcept {
        switch (kind_) {
        case JavaLinkageKind::no_class: return "Ljava/lang/NoClassDefFoundError;";
        case JavaLinkageKind::no_method: return "Ljava/lang/NoSuchMethodError;";
        case JavaLinkageKind::no_field: return "Ljava/lang/NoSuchFieldError;";
        case JavaLinkageKind::incompatible_class_change: return "Ljava/lang/IncompatibleClassChangeError;";
        case JavaLinkageKind::illegal_access: return "Ljava/lang/IllegalAccessError;";
        case JavaLinkageKind::instantiation: return "Ljava/lang/InstantiationError;";
        case JavaLinkageKind::abstract_method: return "Ljava/lang/AbstractMethodError;";
        case JavaLinkageKind::verify: return "Ljava/lang/VerifyError;";
        case JavaLinkageKind::linkage: return "Ljava/lang/LinkageError;";
        case JavaLinkageKind::circularity: return "Ljava/lang/ClassCircularityError;";
        case JavaLinkageKind::virtual_machine: return "Ljava/lang/VirtualMachineError;";
        }
        return "Ljava/lang/LinkageError;";
    }
private:
    JavaLinkageKind kind_;
};

enum class DexVmErrorReason : std::uint8_t {
    incompatible_class_change,
    invalid_image,
    duplicate_class,
    unknown_class,
    invalid_hierarchy,
    invalid_member,
    invalid_override,
    invalid_code,
    invalid_register,
    invalid_operand,
    invalid_opcode,
    unimplemented_opcode,
    unimplemented_intrinsic,
    unresolved_reference,
    budget_exhausted,
    heap_budget_exhausted,
    frame_overflow,
    clinit_failure,
    object_model_failure,
    native_bridge_unavailable,
    uncaught_exception,
    // A Java thread was asked to stop at teardown and unwound out of its
    // interpreted frames instead of being killed.
    thread_stopped,
    internal_invariant,
};

class DexVmError final : public std::runtime_error {
public:
    DexVmError(const DexVmErrorReason reason, std::string message)
        : std::runtime_error(std::move(message)), reason_(reason) {}

    [[nodiscard]] DexVmErrorReason Reason() const noexcept { return reason_; }

private:
    DexVmErrorReason reason_;
};

}  // namespace ogplay::runtime::dexvm
