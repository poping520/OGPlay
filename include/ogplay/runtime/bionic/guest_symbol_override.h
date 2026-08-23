#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace ogplay::runtime {

struct GuestSymbolOverrideDescriptor final {
    std::string_view library;
    std::string_view symbol;
    std::uint16_t local_id{};
    std::uint8_t parameter_count{};
};

[[nodiscard]] std::span<const GuestSymbolOverrideDescriptor>
GuestSymbolOverrides() noexcept;

}  // namespace ogplay::runtime
