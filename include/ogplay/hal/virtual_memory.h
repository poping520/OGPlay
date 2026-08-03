#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ogplay::hal {

enum class MemoryProtection : std::uint8_t {
    none = 0,
    read = 1U << 0U,
    write = 1U << 1U,
    execute = 1U << 2U,
};

[[nodiscard]] constexpr MemoryProtection operator|(const MemoryProtection left,
                                                   const MemoryProtection right) noexcept {
    return static_cast<MemoryProtection>(static_cast<std::uint8_t>(left) |
                                         static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr bool HasProtection(const MemoryProtection value,
                                           const MemoryProtection flag) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

class VirtualMemoryReservation {
public:
    virtual ~VirtualMemoryReservation() = default;
    [[nodiscard]] virtual std::byte* Base() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t Size() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t PageSize() const noexcept = 0;
    virtual void Commit(std::uint64_t offset, std::uint64_t size,
                        MemoryProtection protection) = 0;
    virtual void Protect(std::uint64_t offset, std::uint64_t size,
                         MemoryProtection protection) = 0;
    virtual void Decommit(std::uint64_t offset, std::uint64_t size) = 0;
};

[[nodiscard]] std::unique_ptr<VirtualMemoryReservation> ReserveVirtualMemory(
    std::uint64_t size);

}  // namespace ogplay::hal
