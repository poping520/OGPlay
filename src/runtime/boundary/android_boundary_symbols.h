#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/boundary/boundary_catalog.h"

namespace ogplay::runtime::detail {

struct HleThunkDescriptor final {
    std::string_view library;
    std::string_view name;
    std::uint16_t local_id{};
    std::uint8_t parameter_count{};
};

[[nodiscard]] std::vector<BionicHleSymbol> BuildAndroidBoundarySymbols(
    AndroidApi api = AndroidApi::api19);
[[nodiscard]] std::vector<HleThunkDescriptor> BuildAndroidBoundaryDescriptors(
    std::span<const BionicHleSymbol> symbols);
[[nodiscard]] const HleThunkDescriptor* DecodeAndroidBoundaryThunk(
    std::uint64_t pc,
    std::span<const HleThunkDescriptor> descriptors) noexcept;

}  // namespace ogplay::runtime::detail
