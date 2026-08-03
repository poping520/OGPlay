#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>

#include "ogplay/memory/address.h"

namespace ogplay::memory {

enum class PageProtection : std::uint8_t {
    none = 0,
    read = 1U << 0U,
    write = 1U << 1U,
    execute = 1U << 2U,
};

[[nodiscard]] constexpr PageProtection operator|(const PageProtection left,
                                                 const PageProtection right) noexcept {
    return static_cast<PageProtection>(static_cast<std::uint8_t>(left) |
                                       static_cast<std::uint8_t>(right));
}

enum class AccessType : std::uint8_t { read, write, execute };
enum class FaultReason : std::uint8_t { unmapped, permission_denied };

class MemoryFault final : public std::runtime_error {
public:
    MemoryFault(GuestAddress address, AccessType access, FaultReason reason,
                std::uint64_t thread_id);

    [[nodiscard]] GuestAddress Address() const noexcept { return address_; }
    [[nodiscard]] AccessType Access() const noexcept { return access_; }
    [[nodiscard]] FaultReason Reason() const noexcept { return reason_; }
    [[nodiscard]] std::uint64_t ThreadId() const noexcept { return thread_id_; }

private:
    GuestAddress address_;
    AccessType access_;
    FaultReason reason_;
    std::uint64_t thread_id_;
};

class AddressSpace final {
public:
    AddressSpace();
    ~AddressSpace();
    AddressSpace(AddressSpace&&) noexcept;
    AddressSpace& operator=(AddressSpace&&) noexcept;
    AddressSpace(const AddressSpace&) = delete;
    AddressSpace& operator=(const AddressSpace&) = delete;

    [[nodiscard]] std::uint64_t ReservedSize() const noexcept;
    [[nodiscard]] std::uint64_t PageSize() const noexcept;
    void Map(const GuestRange& range, PageProtection protection);
    void Protect(const GuestRange& range, PageProtection protection);
    void Unmap(const GuestRange& range);
    void Validate(const GuestRange& range, AccessType access,
                  std::uint64_t thread_id = 0) const;
    void Read(GuestAddress address, std::span<std::byte> destination,
              std::uint64_t thread_id = 0) const;
    void Write(GuestAddress address, std::span<const std::byte> source,
               std::uint64_t thread_id = 0);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::memory
