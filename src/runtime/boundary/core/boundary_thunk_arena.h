#pragma once

#include <cstddef>
#include <cstdint>

#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {

class BoundaryThunkArena final {
public:
    explicit BoundaryThunkArena(memory::AddressSpace& address_space) noexcept;

    void Map(std::size_t slot_count);
    [[nodiscard]] bool IsMapped() const noexcept;
    [[nodiscard]] std::size_t DecodeSlot(std::uint64_t pc) const noexcept;
    [[nodiscard]] static std::size_t SlotForAddress(
        std::uint64_t address) noexcept;
    [[nodiscard]] static constexpr std::size_t InvalidSlot() noexcept {
        return static_cast<std::size_t>(-1);
    }

private:
    memory::AddressSpace& address_space_;
    std::size_t mapped_bytes_{};
};

}  // namespace ogplay::runtime
