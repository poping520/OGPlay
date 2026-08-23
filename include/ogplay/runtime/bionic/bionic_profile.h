#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/core/logger.h"
#include "ogplay/loader/link_namespace.h"
#include "ogplay/memory/address_space.h"
#include "ogplay/runtime/android_api.h"
#include "ogplay/runtime/boundary/boundary_symbol.h"

namespace ogplay::runtime {

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
};

struct BionicMemoryInterceptCall final {
    std::string_view symbol;
    std::array<std::uint32_t, 4> arguments{};
    std::uint64_t thread_id{};
    std::size_t maximum_string_bytes{1024U * 1024U};
};

class BionicProfileError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] const BionicProfile& SelectBionicProfile(std::uint32_t api);
[[nodiscard]] BionicSymbolRoute RouteBionicSymbol(
    const BionicProfile& profile, std::string_view library,
    std::string_view symbol);
[[nodiscard]] std::uint32_t ExecuteBionicMemoryIntercept(
    memory::AddressSpace& address_space,
    const BionicMemoryInterceptCall& call);
[[nodiscard]] loader::Elf32LinkNamespace BuildBionicLinkNamespace(
    const BionicProfile& profile, std::string_view root_name,
    std::span<const loader::Elf32LinkModule> guest_modules,
    const BionicHleSymbolProvider& hle_symbols);
[[nodiscard]] loader::Elf32LinkNamespaceExtension ExtendBionicLinkNamespace(
    const BionicProfile& profile,
    const loader::Elf32LinkNamespace& link_namespace,
    std::string_view root_name,
    std::span<const loader::Elf32LinkModule> guest_modules,
    const BionicHleSymbolProvider& hle_symbols);

}  // namespace ogplay::runtime
