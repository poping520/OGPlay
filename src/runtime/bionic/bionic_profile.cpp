#include "ogplay/runtime/bionic/bionic_profile.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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
constexpr std::array<std::string_view, 5> kInterceptedLibcSymbols{
    "memcmp", "memcpy", "memmove", "memset", "strlen"};

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

std::uint32_t ExecuteBionicMemoryIntercept(
    memory::AddressSpace& address_space,
    const BionicMemoryInterceptCall& call) {
    if (call.thread_id == 0 || call.symbol.empty()) {
        throw BionicProfileError(
            "Bionic memory intercept requires a symbol and thread");
    }
    const auto destination = memory::GuestAddress{call.arguments[0]};
    const auto source = memory::GuestAddress{call.arguments[1]};
    const auto count = call.arguments[2];
    constexpr std::size_t kChunkSize = 4096;

    if (call.symbol == "strlen") {
        if (call.maximum_string_bytes == 0) {
            throw BionicProfileError("strlen intercept has a zero bound");
        }
        try {
            const auto length = address_space.CStringLength(
                destination, call.maximum_string_bytes, call.thread_id);
            if (length > std::numeric_limits<std::uint32_t>::max()) {
                throw BionicProfileError("strlen result overflows A32");
            }
            return static_cast<std::uint32_t>(length);
        } catch (const std::length_error&) {
            throw BionicProfileError("strlen intercept exceeded its bound");
        }
    }

    if (std::find(kInterceptedLibcSymbols.begin(),
                  kInterceptedLibcSymbols.end(), call.symbol) ==
        kInterceptedLibcSymbols.end()) {
        throw BionicProfileError("Bionic memory intercept is not implemented: " +
                                 std::string(call.symbol));
    }
    if (count == 0) {
        return call.symbol == "memcmp" ? 0U : destination.Value();
    }
    if (call.symbol == "memset") {
        address_space.Validate({destination, count}, memory::AccessType::write,
                               call.thread_id);
        std::array<std::byte, kChunkSize> bytes;
        const auto buffered = std::min<std::size_t>(count, bytes.size());
        std::memset(bytes.data(), static_cast<int>(call.arguments[1] & 0xffU),
                    buffered);
        std::uint64_t offset{};
        while (offset < count) {
            const auto size = static_cast<std::size_t>(
                std::min<std::uint64_t>(buffered, count - offset));
            address_space.Write(destination.Add(offset),
                                std::span{bytes}.first(size), call.thread_id);
            offset += size;
        }
        return destination.Value();
    }

    address_space.Validate({source, count}, memory::AccessType::read,
                           call.thread_id);
    if (call.symbol != "memcmp") {
        address_space.Validate({destination, count}, memory::AccessType::write,
                               call.thread_id);
    } else {
        address_space.Validate({destination, count}, memory::AccessType::read,
                               call.thread_id);
    }
    std::array<std::byte, kChunkSize> left;
    std::array<std::byte, kChunkSize> right;
    if (call.symbol == "memcmp") {
        std::uint64_t offset{};
        while (offset < count) {
            const auto size = static_cast<std::size_t>(
                std::min<std::uint64_t>(kChunkSize, count - offset));
            address_space.Read(destination.Add(offset),
                               std::span{left}.first(size), call.thread_id);
            address_space.Read(source.Add(offset),
                               std::span{right}.first(size), call.thread_id);
            for (std::size_t index = 0; index < size; ++index) {
                const auto lhs = std::to_integer<std::uint8_t>(left[index]);
                const auto rhs = std::to_integer<std::uint8_t>(right[index]);
                if (lhs != rhs) {
                    return std::bit_cast<std::uint32_t>(
                        static_cast<std::int32_t>(lhs) - rhs);
                }
            }
            offset += size;
        }
        return 0;
    }

    const auto overlap_backwards =
        call.symbol == "memmove" && destination > source &&
        static_cast<std::uint64_t>(destination.Value()) <
            static_cast<std::uint64_t>(source.Value()) + count;
    std::uint64_t remaining = count;
    while (remaining != 0) {
        const auto size = static_cast<std::size_t>(
            std::min<std::uint64_t>(kChunkSize, remaining));
        const auto offset = overlap_backwards ? remaining - size
                                              : count - remaining;
        address_space.Read(source.Add(offset), std::span{left}.first(size),
                           call.thread_id);
        address_space.Write(destination.Add(offset),
                            std::span{left}.first(size), call.thread_id);
        remaining -= size;
    }
    return destination.Value();
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
