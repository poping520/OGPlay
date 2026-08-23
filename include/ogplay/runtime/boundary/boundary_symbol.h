#pragma once

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
inline constexpr std::uint32_t kBionicHleThunkEnd = 0x71000000U;

struct BionicHleSymbol final {
    std::string library;
    std::string symbol;
    memory::GuestAddress address;
};

class BionicHleSymbolProvider final : public core::GuestSymbolProvider {
public:
    explicit BionicHleSymbolProvider(std::span<const BionicHleSymbol> symbols);

    [[nodiscard]] std::optional<memory::GuestAddress> Lookup(
        std::string_view library, std::string_view symbol) const;
    [[nodiscard]] std::vector<BionicHleSymbol> Exports(
        std::string_view library) const;
    [[nodiscard]] std::optional<core::SymbolizedAddress> Resolve(
        std::uint64_t address) const override;

private:
    std::vector<BionicHleSymbol> symbols_;
};

}  // namespace ogplay::runtime
