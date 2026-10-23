#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/logger.h"
#include "ogplay/memory/address.h"

namespace ogplay::runtime {

inline constexpr std::uint32_t kBionicHleThunkBegin = 0x70000000U;
// RX thunks and fixed data reservations are disjoint, independent of catalog size.
inline constexpr std::uint32_t kBionicHleThunkEnd = 0x70010000U;
inline constexpr std::uint32_t kBoundaryThunkCapacity = (kBionicHleThunkEnd - kBionicHleThunkBegin) / 4U;
inline constexpr memory::GuestRange kBoundaryGles1Strings{memory::GuestAddress{0x70010000U}, 0x10000U};
inline constexpr memory::GuestRange kBoundaryGles2Strings{memory::GuestAddress{0x70020000U}, 0x10000U};
inline constexpr memory::GuestRange kBoundaryEventTags{memory::GuestAddress{0x70F00000U}, 0x100000U};
inline constexpr memory::GuestRange kBoundaryOpenSlesAbi{memory::GuestAddress{0x71800000U}, 0x3000U};
inline constexpr memory::GuestRange kBoundaryOpenSlesObjects{memory::GuestAddress{0x71900000U}, 0x100000U};
inline constexpr memory::GuestRange kBoundaryEglStrings{memory::GuestAddress{0x71C00000U}, 0x10000U};
inline constexpr memory::GuestRange kBoundaryGles3Strings{memory::GuestAddress{0x71E00000U}, 0x10000U};
inline constexpr memory::GuestRange kBoundaryBufferMaps{memory::GuestAddress{0x72000000U}, 0x2000000U};
inline constexpr std::array kBoundaryFixedRegions{kBoundaryGles1Strings, kBoundaryGles2Strings,    kBoundaryEventTags,
                                                  kBoundaryOpenSlesAbi,  kBoundaryOpenSlesObjects, kBoundaryEglStrings,
                                                  kBoundaryGles3Strings, kBoundaryBufferMaps};
static_assert([] {
    constexpr memory::GuestRange thunks{memory::GuestAddress{kBionicHleThunkBegin},
                                        kBionicHleThunkEnd - kBionicHleThunkBegin};
    for (std::size_t i = 0; i < kBoundaryFixedRegions.size(); ++i) {
        if (thunks.Overlaps(kBoundaryFixedRegions[i])) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (kBoundaryFixedRegions[i].Overlaps(kBoundaryFixedRegions[j])) return false;
    }
    return true;
}());

enum class BoundarySymbolKind : std::uint8_t { function, data };

struct BionicHleSymbol final {
    std::string library;
    std::string symbol;
    memory::GuestAddress address;
    BoundarySymbolKind kind{BoundarySymbolKind::function};
    std::uint32_t size{4U};
};

class BionicHleSymbolProvider final : public core::GuestSymbolProvider {
public:
    explicit BionicHleSymbolProvider(std::span<const BionicHleSymbol> symbols);

    [[nodiscard]] std::optional<memory::GuestAddress> Lookup(
        std::string_view library, std::string_view symbol) const;
    [[nodiscard]] std::optional<memory::GuestAddress> LookupAny(
        std::string_view symbol) const;
    [[nodiscard]] std::vector<BionicHleSymbol> Exports(
        std::string_view library) const;
    [[nodiscard]] std::optional<core::SymbolizedAddress> Resolve(
        std::uint64_t address) const override;

private:
    std::vector<BionicHleSymbol> symbols_;
};

}  // namespace ogplay::runtime
