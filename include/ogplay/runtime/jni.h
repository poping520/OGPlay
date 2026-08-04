#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <memory>
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

enum class JniObjectDomain : std::uint8_t { host, dex_vm };

struct JniObjectIdentity final {
    JniObjectDomain domain{JniObjectDomain::host};
    std::uint64_t value{};

    bool operator==(const JniObjectIdentity&) const = default;
};

enum class JniReferenceKind : std::uint8_t { local, global, weak_global };

enum class JniReferenceErrorReason : std::uint8_t {
    invalid_thread,
    invalid_object,
    invalid_reference,
    wrong_reference_kind,
    capacity_exceeded,
    frame_underflow,
    duplicate_thread,
};

class JniReferenceError final : public std::runtime_error {
public:
    JniReferenceError(JniReferenceErrorReason reason, std::string message);

    [[nodiscard]] JniReferenceErrorReason Reason() const noexcept {
        return reason_;
    }

private:
    JniReferenceErrorReason reason_;
};

struct JniReferenceLimits final {
    std::size_t local_per_thread{512};
    std::size_t global{4096};
    std::size_t weak_global{4096};
};

class JniReferenceTable final {
public:
    explicit JniReferenceTable(JniReferenceLimits limits = {});
    ~JniReferenceTable();
    JniReferenceTable(const JniReferenceTable&) = delete;
    JniReferenceTable& operator=(const JniReferenceTable&) = delete;
    JniReferenceTable(JniReferenceTable&&) noexcept;
    JniReferenceTable& operator=(JniReferenceTable&&) noexcept;

    void AttachThread(std::uint64_t thread_id,
                      std::size_t initial_local_capacity = 16);
    void DetachThread(std::uint64_t thread_id);
    [[nodiscard]] bool IsThreadAttached(std::uint64_t thread_id) const;

    void EnsureLocalCapacity(std::uint64_t thread_id,
                             std::size_t additional_capacity);
    void PushLocalFrame(std::uint64_t thread_id, std::size_t capacity);
    [[nodiscard]] JniReference PopLocalFrame(
        std::uint64_t thread_id, JniReference result = JniReference{});

    [[nodiscard]] JniReference NewLocal(std::uint64_t thread_id,
                                        JniObjectIdentity object);
    [[nodiscard]] JniReference NewGlobal(JniObjectIdentity object);
    [[nodiscard]] JniReference NewWeakGlobal(JniObjectIdentity object);
    void DeleteLocal(std::uint64_t thread_id, JniReference reference);
    void DeleteGlobal(JniReference reference);
    void DeleteWeakGlobal(JniReference reference);

    [[nodiscard]] std::optional<JniObjectIdentity> Resolve(
        std::uint64_t thread_id, JniReference reference) const;
    [[nodiscard]] bool IsSameObject(std::uint64_t thread_id,
                                    JniReference left,
                                    JniReference right) const;
    void ClearWeakReferencesTo(JniObjectIdentity object);

    [[nodiscard]] std::size_t LocalCount(std::uint64_t thread_id) const;
    [[nodiscard]] std::size_t GlobalCount() const;
    [[nodiscard]] std::size_t WeakGlobalCount() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
