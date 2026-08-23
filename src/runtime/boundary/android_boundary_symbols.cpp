#include "android_boundary_symbols.h"

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/gles/gles_dispatch.h"

namespace ogplay::runtime::detail {
namespace {

constexpr std::uint32_t kThunkStride = 4U;

[[nodiscard]] HleRoute RouteFor(const std::string_view library,
                                const std::string_view name) {
    if (library == "libc.so") return HleRoute::bionic_memory;
    if (library == "libandroid.so") return HleRoute::android;
    if (library == "libEGL.so") return HleRoute::egl;
    if (library == "liblog.so") return HleRoute::log;
    if (library == "libGLESv2.so") return HleRoute::gles2;
    if (library == "libGLESv1_CM.so") {
        if (gles::FindGlesFunction(gles::GlesApi::gles1, name).has_value()) {
            return HleRoute::gles1;
        }
        return HleRoute::gles1_extension;
    }
    throw std::logic_error("Android boundary symbol has no HLE route");
}

[[nodiscard]] std::uint16_t FunctionIdFor(const HleRoute route,
                                          const std::string_view name) {
    if (route == HleRoute::gles1 || route == HleRoute::gles1_extension ||
        route == HleRoute::gles2) {
        const auto api = route == HleRoute::gles1
                             ? gles::GlesApi::gles1
                         : route == HleRoute::gles1_extension
                             ? gles::GlesApi::gles1_extensions
                             : gles::GlesApi::gles2;
        const auto id = gles::FindGlesFunction(api, name);
        if (!id.has_value()) {
            throw std::logic_error("generated GLES symbol has no function id");
        }
        return *id;
    }
    if (route == HleRoute::bionic_memory) {
        static constexpr std::array names{"memcpy", "memmove", "memset",
                                          "memcmp", "strlen"};
        const auto found = std::find(names.begin(), names.end(), name);
        if (found == names.end()) {
            throw std::logic_error("unknown Bionic override symbol");
        }
        return static_cast<std::uint16_t>(found - names.begin());
    }
    const auto& catalog = AndroidBoundaryCatalog(AndroidApi::api19);
    for (const auto& module : catalog.Modules()) {
        const auto found = std::find_if(
            module.exports.begin(), module.exports.end(),
            [&](const auto& export_) { return export_.name == name; });
        if (found != module.exports.end()) return found->local_id;
    }
    throw std::logic_error("boundary symbol has no module-local id");
}

[[nodiscard]] std::uint8_t ParameterCountFor(const HleRoute route,
                                             const std::uint16_t function_id) {
    if (route == HleRoute::gles1 || route == HleRoute::gles1_extension ||
        route == HleRoute::gles2) {
        const auto api = route == HleRoute::gles1
                             ? gles::GlesApi::gles1
                         : route == HleRoute::gles1_extension
                             ? gles::GlesApi::gles1_extensions
                             : gles::GlesApi::gles2;
        const auto count = gles::DescribeGlesFunction(api, function_id)
                               .parameter_count;
        if (count > (std::numeric_limits<std::uint8_t>::max)()) {
            throw std::length_error("GLES parameter count overflows descriptor");
        }
        return static_cast<std::uint8_t>(count);
    }
    if (route == HleRoute::bionic_memory) {
        static constexpr std::array<std::uint8_t, 5> counts{3, 3, 3, 3, 1};
        return counts.at(function_id);
    }
    throw std::logic_error("non-GLES parameter count requires module metadata");
}

}  // namespace

std::vector<BionicHleSymbol> BuildAndroidBoundarySymbols() {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> overrides{{
        {"libc.so", "memcpy"}, {"libc.so", "memmove"}, {"libc.so", "memset"},
        {"libc.so", "memcmp"}, {"libc.so", "strlen"},
    }};
    std::vector<BionicHleSymbol> result;
    const auto& catalog = AndroidBoundaryCatalog(AndroidApi::api19);
    result.reserve(catalog.SlotCount() + overrides.size());
    for (const auto& module : catalog.Modules()) {
        for (const auto& export_ : module.exports) {
            result.push_back({module.soname, export_.name, export_.address});
        }
    }
    for (const auto& [library, symbol] : overrides) {
        result.push_back({std::string(library), std::string(symbol),
                          memory::GuestAddress{kBionicHleThunkBegin +
                              static_cast<std::uint32_t>(result.size()) *
                                  kThunkStride + 1U}});
    }
    return result;
}

std::vector<HleThunkDescriptor> BuildAndroidBoundaryDescriptors(
    const std::span<const BionicHleSymbol> symbols) {
    std::vector<HleThunkDescriptor> result;
    result.reserve(symbols.size());
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        const auto& symbol = symbols[index];
        const auto code_address = symbol.address.Value() & ~std::uint32_t{1};
        const auto expected = kBionicHleThunkBegin +
                              static_cast<std::uint32_t>(index) * kThunkStride;
        if (code_address != expected) {
            throw std::logic_error("Android boundary thunk catalog is not dense");
        }
        const auto route = RouteFor(symbol.library, symbol.symbol);
        const auto function_id = FunctionIdFor(route, symbol.symbol);
        auto parameter_count = std::uint8_t{};
        if (route == HleRoute::bionic_memory || route == HleRoute::gles1 ||
            route == HleRoute::gles1_extension || route == HleRoute::gles2) {
            parameter_count = ParameterCountFor(route, function_id);
        } else {
            const auto* module = AndroidBoundaryCatalog(AndroidApi::api19)
                                     .FindModule(symbol.library);
            if (module == nullptr) {
                throw std::logic_error("boundary descriptor module is missing");
            }
            const auto export_ = std::find_if(
                module->exports.begin(), module->exports.end(),
                [&](const auto& candidate) {
                    return candidate.name == symbol.symbol;
                });
            if (export_ == module->exports.end()) {
                throw std::logic_error("boundary descriptor export is missing");
            }
            parameter_count = export_->parameter_count;
        }
        result.push_back({symbol.library, symbol.symbol, route, function_id,
                          parameter_count});
    }
    return result;
}

const HleThunkDescriptor* DecodeAndroidBoundaryThunk(
    const std::uint64_t pc,
    const std::span<const HleThunkDescriptor> descriptors) noexcept {
    const auto code = pc & ~UINT64_C(1);
    if (code < kBionicHleThunkBegin || code >= kBionicHleThunkEnd) {
        return nullptr;
    }
    const auto offset = code - kBionicHleThunkBegin;
    if (offset % kThunkStride != 0U) return nullptr;
    const auto index = offset / kThunkStride;
    if (index >= descriptors.size()) return nullptr;
    return &descriptors[static_cast<std::size_t>(index)];
}

}  // namespace ogplay::runtime::detail
