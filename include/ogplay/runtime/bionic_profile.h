#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/logger.h"
#include "ogplay/loader/link_namespace.h"

namespace ogplay::runtime {

enum class AndroidApi : std::uint8_t { api19 = 19, api22 = 22, api23 = 23 };

enum class BionicSymbolRoute : std::uint8_t {
    guest_execution,
    host_intercept,
    host_boundary,
};

struct BionicProfile final {
    AndroidApi api{};
    std::string_view android_release;
    std::string_view data_directory;
    std::span<const std::string_view> guest_libraries;
    std::span<const std::string_view> boundary_libraries;
};

inline constexpr std::uint32_t kBionicHleThunkBegin = 0x70000000U;
inline constexpr std::uint32_t kBionicHleThunkEnd = 0x71000000U;

struct BionicHleSymbol final {
    std::string library;
    std::string symbol;
    memory::GuestAddress address;
};

class BionicProfileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class BionicHleSymbolProvider final : public core::GuestSymbolProvider {
public:
    explicit BionicHleSymbolProvider(std::span<const BionicHleSymbol> symbols);

    [[nodiscard]] std::optional<memory::GuestAddress> Lookup(
        std::string_view library, std::string_view symbol) const;
    [[nodiscard]] std::optional<core::SymbolizedAddress> Resolve(
        std::uint64_t address) const override;

private:
    std::vector<BionicHleSymbol> symbols_;
};

[[nodiscard]] const BionicProfile& SelectBionicProfile(std::uint32_t api);
[[nodiscard]] BionicSymbolRoute RouteBionicSymbol(
    const BionicProfile& profile, std::string_view library,
    std::string_view symbol);
[[nodiscard]] loader::Elf32LinkNamespace BuildBionicLinkNamespace(
    const BionicProfile& profile, std::string_view root_name,
    std::span<const loader::Elf32LinkModule> guest_modules,
    const BionicHleSymbolProvider& hle_symbols);

}  // namespace ogplay::runtime
