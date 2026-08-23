#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ogplay/memory/address_space.h"

namespace ogplay::runtime {

class AndroidBoundaryServices final {
public:
    explicit AndroidBoundaryServices(memory::AddressSpace& address_space) noexcept
        : address_space(address_space) {}

    void Write32(const std::uint32_t address, const std::uint32_t value,
                 const std::uint64_t thread_id) const {
        if (address == 0U) return;
        std::array<std::byte, 4> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(value >> (index * 8U));
        }
        address_space.Write(memory::GuestAddress{address}, bytes, thread_id);
    }

    memory::AddressSpace& address_space;
};

}  // namespace ogplay::runtime
