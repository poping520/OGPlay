#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "ogplay/memory/address_space.h"
#include "runtime/boundary/core/boundary_catalog.h"

namespace ogplay::runtime {

inline constexpr memory::GuestAddress kOpenSlesAbiBegin{0x71800000U};
inline constexpr memory::GuestAddress kOpenSlesIidValuesBegin{0x71801000U};
inline constexpr memory::GuestAddress kOpenSlesObjectVtable{0x71802000U};
inline constexpr memory::GuestAddress kOpenSlesEngineVtable{0x71802100U};
inline constexpr memory::GuestAddress kOpenSlesPlayVtable{0x71802200U};
inline constexpr memory::GuestAddress kOpenSlesBufferQueueVtable{0x71802300U};
inline constexpr memory::GuestAddress kOpenSlesVolumeVtable{0x71802400U};
inline constexpr memory::GuestAddress kOpenSlesOutputMixVtable{0x71802500U};
inline constexpr memory::GuestAddress kOpenSlesObjectArenaBegin{0x71900000U};
inline constexpr std::size_t kOpenSlesStaticAbiBytes = 0x3000U;
inline constexpr std::size_t kOpenSlesObjectArenaBytes = 0x100000U;

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
void MapOpenSlesStaticAbi(memory::AddressSpace& address_space,
                          const BoundaryModuleDescriptor& module);

}  // namespace ogplay::runtime
