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

enum class DexVmErrorReason : std::uint8_t {
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
    // A thread tried to park while one of its guest native frames is still
    // live. The A32 executor owns a single root guest stack, so parking
    // there would hand a live frame to another thread: recorded gap,
    // explicit failure, never silent corruption.
    blocking_in_native,
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
