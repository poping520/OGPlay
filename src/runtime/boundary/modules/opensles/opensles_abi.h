#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "ogplay/memory/address_space.h"
#include "runtime/boundary/core/boundary_catalog.h"

namespace ogplay::runtime {

inline constexpr auto kOpenSlesAbiBegin = kBoundaryOpenSlesAbi.Start().Add(0U);
inline constexpr auto kOpenSlesIidValuesBegin = kBoundaryOpenSlesAbi.Start().Add(4096U);
inline constexpr auto kOpenSlesObjectVtable = kBoundaryOpenSlesAbi.Start().Add(8192U);
inline constexpr auto kOpenSlesEngineVtable = kBoundaryOpenSlesAbi.Start().Add(8448U);
inline constexpr auto kOpenSlesPlayVtable = kBoundaryOpenSlesAbi.Start().Add(8704U);
inline constexpr auto kOpenSlesBufferQueueVtable = kBoundaryOpenSlesAbi.Start().Add(8960U);
inline constexpr auto kOpenSlesVolumeVtable = kBoundaryOpenSlesAbi.Start().Add(9216U);
inline constexpr auto kOpenSlesOutputMixVtable = kBoundaryOpenSlesAbi.Start().Add(9472U);
inline constexpr auto kOpenSlesObjectArenaBegin = kBoundaryOpenSlesObjects.Start();
inline constexpr std::size_t kOpenSlesStaticAbiBytes = kBoundaryOpenSlesAbi.Size();
inline constexpr std::size_t kOpenSlesObjectArenaBytes = kBoundaryOpenSlesObjects.Size();

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
