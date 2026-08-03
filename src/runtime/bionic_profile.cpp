#include "ogplay/runtime/bionic_profile.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ogplay::runtime {
namespace {

constexpr std::array<std::string_view, 5> kGuestLibraries{
    "libc.so", "libm.so", "libdl.so", "libstdc++.so", "libz.so"};
constexpr std::array<std::string_view, 9> kBoundaryLibraries{
    "libEGL.so",       "libGLESv1_CM.so", "libGLESv2.so",
    "libGLESv3.so",   "libOpenSLES.so",  "libandroid.so",
    "libjnigraphics.so", "liblog.so",     "libmediandk.so"};
constexpr std::array<std::string_view, 24> kInterceptedLibcSymbols{
    "memcmp",  "memcpy",  "memmove", "memset",  "strcat", "strchr",
    "strcmp",  "strcpy",  "strcspn", "strlen",  "strncat", "strncmp",
    "strncpy", "strnlen", "strpbrk", "strrchr", "strspn", "strstr",
    "pthread_create", "pthread_exit", "pthread_join", "pthread_self",
    "pthread_mutex_lock", "pthread_mutex_unlock"};

constexpr BionicProfile kApi19{AndroidApi::api19, "4.4", "bionic/19",
                               kGuestLibraries, kBoundaryLibraries};
constexpr BionicProfile kApi22{AndroidApi::api22, "5.1", "bionic/22",
                               kGuestLibraries, kBoundaryLibraries};
constexpr BionicProfile kApi23{AndroidApi::api23, "6.0", "bionic/23",
                               kGuestLibraries, kBoundaryLibraries};

[[nodiscard]] bool Contains(const std::span<const std::string_view> values,
                            const std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] std::string_view CanonicalName(
    const loader::Elf32LinkModule& module) {
    if (module.dynamic.soname.has_value()) return *module.dynamic.soname;
    return module.name;
}

[[nodiscard]] bool IsHleThunk(const memory::GuestAddress address) {
    const auto code_address = address.Value() & ~std::uint32_t{1};
    return code_address >= kBionicHleThunkBegin &&
           code_address < kBionicHleThunkEnd;
}

void BindInterceptedExports(loader::Elf32LinkModule& module,
                            const BionicProfile& profile,
                            const BionicHleSymbolProvider& provider) {
    const auto library = CanonicalName(module);
    for (auto& symbol : module.symbols.symbols) {
        if (!symbol.IsExported() ||
            RouteBionicSymbol(profile, library, symbol.name) !=
                BionicSymbolRoute::host_intercept) {
            continue;
        }
        const auto address = provider.Lookup(library, symbol.name);
        if (!address.has_value()) {
            throw BionicProfileError("declared Bionic intercept has no HLE symbol: " +
                                     std::string(library) + ":" + symbol.name);
        }
        constexpr std::uint16_t kSectionAbsolute = 0xfff1;
        symbol.value = *address;
        symbol.section_index = kSectionAbsolute;
    }
}

[[nodiscard]] loader::Elf32LinkModule MakeBoundaryModule(
    const std::string_view library,
    const std::set<std::string, std::less<>>& imported_symbols,
    const BionicHleSymbolProvider& provider) {
    loader::Elf32LinkModule module;
    module.name = std::string(library);
    module.dynamic.soname = module.name;
    module.symbols.symbols.push_back(
        {"", memory::GuestAddress{}, 0, 0, 0, 0, 0});
    for (const auto& symbol : imported_symbols) {
        const auto address = provider.Lookup(library, symbol);
        if (!address.has_value()) continue;
        constexpr std::uint16_t kSectionAbsolute = 0xfff1;
        module.symbols.symbols.push_back(
            {symbol, *address, 4, 1, 2, 0, kSectionAbsolute});
    }
    return module;
}

}  // namespace

BionicHleSymbolProvider::BionicHleSymbolProvider(
    const std::span<const BionicHleSymbol> symbols) {
    std::set<std::pair<std::string, std::string>> names;
    std::set<std::uint32_t> addresses;
    for (const auto& symbol : symbols) {
        if (symbol.library.empty() || symbol.symbol.empty()) {
            throw BionicProfileError(
                "Bionic HLE symbol requires a library and symbol");
        }
        if (!IsHleThunk(symbol.address)) {
            throw BionicProfileError(
                "Bionic HLE symbol address is outside the thunk range");
        }
        if (!names.emplace(symbol.library, symbol.symbol).second) {
            throw BionicProfileError("duplicate Bionic HLE symbol: " +
                                     symbol.library + ":" + symbol.symbol);
        }
        if (!addresses.insert(symbol.address.Value() & ~std::uint32_t{1}).second) {
            throw BionicProfileError("duplicate Bionic HLE thunk address");
        }
        symbols_.push_back(symbol);
    }
}

std::optional<memory::GuestAddress> BionicHleSymbolProvider::Lookup(
    const std::string_view library, const std::string_view symbol) const {
    const auto found = std::find_if(
        symbols_.begin(), symbols_.end(), [&](const BionicHleSymbol& candidate) {
            return candidate.library == library && candidate.symbol == symbol;
        });
    if (found == symbols_.end()) return std::nullopt;
    return found->address;
}

std::optional<core::SymbolizedAddress> BionicHleSymbolProvider::Resolve(
    const std::uint64_t address) const {
    const auto found = std::find_if(
        symbols_.begin(), symbols_.end(), [&](const BionicHleSymbol& candidate) {
            return candidate.address.Value() == address ||
                   (candidate.address.Value() & ~std::uint32_t{1}) == address;
        });
    if (found == symbols_.end()) return std::nullopt;
    return core::SymbolizedAddress{found->library, found->symbol, 0, "hle"};
}

const BionicProfile& SelectBionicProfile(const std::uint32_t api) {
    switch (api) {
        case 19:
            return kApi19;
        case 22:
            return kApi22;
        case 23:
            return kApi23;
        default:
            throw BionicProfileError("unsupported Android Bionic API " +
                                     std::to_string(api));
    }
}

BionicSymbolRoute RouteBionicSymbol(const BionicProfile& profile,
                                    const std::string_view library,
                                    const std::string_view symbol) {
    if (library.empty() || symbol.empty()) {
        throw BionicProfileError("Bionic route requires a library and symbol");
    }
    if (Contains(profile.boundary_libraries, library)) {
        return BionicSymbolRoute::host_boundary;
    }
    if (library == "libc.so" &&
        std::find(kInterceptedLibcSymbols.begin(),
                  kInterceptedLibcSymbols.end(), symbol) !=
            kInterceptedLibcSymbols.end()) {
        return BionicSymbolRoute::host_intercept;
    }
    return BionicSymbolRoute::guest_execution;
}

loader::Elf32LinkNamespace BuildBionicLinkNamespace(
    const BionicProfile& profile, const std::string_view root_name,
    const std::span<const loader::Elf32LinkModule> guest_modules,
    const BionicHleSymbolProvider& hle_symbols) {
    std::vector<loader::Elf32LinkModule> modules(guest_modules.begin(),
                                                 guest_modules.end());
    std::set<std::string, std::less<>> present_names;
    std::set<std::string, std::less<>> imported_symbols;
    for (auto& module : modules) {
        const auto library = CanonicalName(module);
        if (Contains(profile.boundary_libraries, library)) {
            throw BionicProfileError(
                "HLE boundary library must not be supplied as a guest ELF: " +
                std::string(library));
        }
        present_names.insert(module.name);
        present_names.insert(std::string(library));
        for (const auto& symbol : module.symbols.symbols) {
            if (symbol.section_index == 0 && !symbol.name.empty()) {
                imported_symbols.insert(symbol.name);
            }
        }
        BindInterceptedExports(module, profile, hle_symbols);
    }

    std::set<std::string, std::less<>> required_boundaries;
    for (const auto& module : modules) {
        for (const auto& needed : module.dynamic.needed) {
            if (!present_names.contains(needed) &&
                Contains(profile.boundary_libraries, needed)) {
                required_boundaries.insert(needed);
            }
        }
    }
    for (const auto& library : required_boundaries) {
        modules.push_back(
            MakeBoundaryModule(library, imported_symbols, hle_symbols));
    }
    return loader::BuildElf32LinkNamespace(root_name, modules);
}

}  // namespace ogplay::runtime
