#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "ogplay/core/capability_ledger.h"
#include "ogplay/memory/address.h"

namespace ogplay::runtime {

using JniBoolean = std::uint8_t;
using JniByte = std::int8_t;
using JniChar = std::uint16_t;
using JniShort = std::int16_t;
using JniInt = std::int32_t;
using JniLong = std::int64_t;
using JniFloat = float;
using JniDouble = double;
using JniSize = JniInt;

template <typename Tag>
class JniHandle final {
public:
    explicit constexpr JniHandle(const std::uint32_t value = 0) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr std::uint32_t Value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool IsNull() const noexcept { return value_ == 0; }

    auto operator<=>(const JniHandle&) const = default;

private:
    std::uint32_t value_{};
};

struct JniReferenceTag;
struct JniMethodIdTag;
struct JniFieldIdTag;
using JniReference = JniHandle<JniReferenceTag>;
using JniMethodId = JniHandle<JniMethodIdTag>;
using JniFieldId = JniHandle<JniFieldIdTag>;

enum class JniStatus : JniInt {
    ok = 0,
    error = -1,
    detached = -2,
    version = -3,
    no_memory = -4,
    exists = -5,
    invalid_arguments = -6,
};

inline constexpr JniInt kJniVersion1_6 = 0x00010006;
inline constexpr std::size_t kJniReservedSlotCount = 4;
inline constexpr std::size_t kJniNativeInterfaceSlotCount = 233;

class JniSlot final {
public:
    explicit constexpr JniSlot(const std::uint16_t value = 0) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr std::uint16_t Value() const noexcept { return value_; }

    auto operator<=>(const JniSlot&) const = default;

private:
    std::uint16_t value_{};
};

[[nodiscard]] std::span<const std::string_view> JniNativeInterfaceSlots() noexcept;
[[nodiscard]] std::string_view JniSlotName(JniSlot slot);
[[nodiscard]] std::optional<JniSlot> FindJniSlot(std::string_view name) noexcept;
[[nodiscard]] std::string JniCapabilityId(JniSlot slot);

class JniUnimplementedCall final : public std::runtime_error {
public:
    JniUnimplementedCall(JniSlot slot, std::uint64_t link_register);

    [[nodiscard]] JniSlot Slot() const noexcept { return slot_; }
    [[nodiscard]] std::uint64_t LinkRegister() const noexcept {
        return link_register_;
    }

private:
    JniSlot slot_;
    std::uint64_t link_register_{};
};

class JniFunctionTable final {
public:
    explicit JniFunctionTable(core::CapabilityLedger& ledger) noexcept;

    void Bind(JniSlot slot, memory::GuestAddress target);
    void Seal();

    [[nodiscard]] bool IsSealed() const noexcept { return sealed_; }
    [[nodiscard]] bool IsBound(JniSlot slot) const;
    [[nodiscard]] memory::GuestAddress Resolve(
        JniSlot slot, std::uint64_t link_register) const;

private:
    core::CapabilityLedger* ledger_{};
    std::array<std::optional<memory::GuestAddress>,
               kJniNativeInterfaceSlotCount> targets_{};
    bool sealed_{};
};

}  // namespace ogplay::runtime
