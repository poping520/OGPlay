#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "ogplay/memory/address_space.h"
#include "runtime/boundary/core/boundary_catalog.h"

namespace ogplay::runtime {

inline constexpr memory::GuestAddress kOpenSlesAbiBegin{0x71500000U};
inline constexpr memory::GuestAddress kOpenSlesIidValuesBegin{0x71501000U};
inline constexpr std::size_t kOpenSlesStaticAbiBytes = 0x2000U;

struct OpenSlesIidValue final {
    std::uint32_t time_low{};
    std::uint16_t time_mid{};
    std::uint16_t time_hi_and_version{};
    std::uint16_t clock_seq{};
    std::array<std::uint8_t, 6> node{};
};

struct OpenSlesIidDescriptor final {
    std::string_view name;
    OpenSlesIidValue value;
    memory::GuestAddress variable_address{};
    memory::GuestAddress value_address{};
};

[[nodiscard]] std::span<const OpenSlesIidDescriptor> OpenSlesIids() noexcept;
[[nodiscard]] std::span<const BoundaryExportDefinition>
OpenSlesDataExports() noexcept;
void MapOpenSlesStaticAbi(memory::AddressSpace& address_space);

}  // namespace ogplay::runtime
