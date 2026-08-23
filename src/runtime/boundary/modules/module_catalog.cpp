#include "runtime/boundary/modules/module_catalog.h"

#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/runtime/bionic/guest_symbol_override.h"
#include "runtime/boundary/core/boundary_symbols.h"
#include "runtime/boundary/modules/android/android_exports.h"
#include "runtime/boundary/modules/egl/egl_exports.h"
#include "runtime/boundary/modules/log/log_exports.h"

namespace ogplay::runtime {
namespace {

struct NamedExport final {
    std::string_view name;
    std::uint16_t local_id;
    std::uint8_t parameter_count;
};

#define OGPLAY_NAMED_METADATA(name, id, count, method) NamedExport{name, id, count},
constexpr std::array kAndroidExports{
    OGPLAY_ANDROID_BOUNDARY_EXPORTS(OGPLAY_NAMED_METADATA)};
constexpr std::array kEglExports{
    OGPLAY_EGL_BOUNDARY_EXPORTS(OGPLAY_NAMED_METADATA)};
constexpr std::array kLogExports{
    OGPLAY_LOG_BOUNDARY_EXPORTS(OGPLAY_NAMED_METADATA)};
#undef OGPLAY_NAMED_METADATA

void AddNamedModule(std::vector<BoundaryModuleDefinition>& modules,
                    std::vector<std::vector<BoundaryExportDefinition>>& storage,
                    const std::string_view soname,
                    const std::span<const NamedExport> exports) {
    auto& module_exports = storage.emplace_back();
    module_exports.reserve(exports.size());
    for (const auto& export_ : exports) {
        module_exports.push_back({export_.name, export_.local_id,
                                  export_.parameter_count});
    }
    modules.push_back({soname, {}, module_exports});
}

void AddGlesModule(std::vector<BoundaryModuleDefinition>& modules,
                   std::vector<std::vector<BoundaryExportDefinition>>& storage,
                   const std::string_view soname,
                   const std::span<const gles::GlesApi> apis) {
    auto& module_exports = storage.emplace_back();
    for (const auto api : apis) {
        const auto count = gles::GlesFunctionCount(api);
        for (std::size_t index = 0; index < count; ++index) {
            const auto function = gles::DescribeGlesFunction(
                api, static_cast<gles::GlesThunkId>(index));
            if (module_exports.size() >
                (std::numeric_limits<std::uint16_t>::max)()) {
                throw std::length_error("GLES boundary local id overflows");
            }
            module_exports.push_back({
                function.name,
                static_cast<std::uint16_t>(module_exports.size()),
                static_cast<std::uint8_t>(function.parameter_count)});
        }
    }
    modules.push_back({soname, {}, module_exports});
}

BoundaryCatalog BuildCatalog(const AndroidApi api) {
    std::vector<std::vector<BoundaryExportDefinition>> storage;
    std::vector<BoundaryModuleDefinition> modules;
    storage.reserve(6);
    AddNamedModule(modules, storage, "libandroid.so", kAndroidExports);
    AddNamedModule(modules, storage, "libEGL.so", kEglExports);
    constexpr std::array gles1_apis{gles::GlesApi::gles1,
                                   gles::GlesApi::gles1_extensions};
    AddGlesModule(modules, storage, "libGLESv1_CM.so", gles1_apis);
    constexpr std::array gles2_apis{gles::GlesApi::gles2};
    AddGlesModule(modules, storage, "libGLESv2.so", gles2_apis);
    AddNamedModule(modules, storage, "liblog.so", kLogExports);
    AddNamedModule(modules, storage, "libOpenSLES.so",
                   std::span<const NamedExport>{});
    return BoundaryCatalog(api, modules);
}

}  // namespace

const BoundaryCatalog& AndroidBoundaryCatalog(const AndroidApi api) {
    static const BoundaryCatalog api19 = BuildCatalog(AndroidApi::api19);
    static const BoundaryCatalog api22 = BuildCatalog(AndroidApi::api22);
    static const BoundaryCatalog api23 = BuildCatalog(AndroidApi::api23);
    switch (api) {
        case AndroidApi::api19: return api19;
        case AndroidApi::api22: return api22;
        case AndroidApi::api23: return api23;
    }
    throw std::logic_error("unsupported Android boundary API");
}

bool IsAndroidBoundaryLibrary(const AndroidApi api,
                              const std::string_view soname) noexcept {
    return AndroidBoundaryCatalog(api).FindModule(soname) != nullptr;
}

namespace detail {

std::vector<BionicHleSymbol> BuildAndroidBoundarySymbols(const AndroidApi api) {
    constexpr std::uint32_t kThunkStride = 4U;
    std::vector<BionicHleSymbol> result;
    const auto& catalog = AndroidBoundaryCatalog(api);
    result.reserve(catalog.SlotCount() + GuestSymbolOverrides().size());
    for (const auto& module : catalog.Modules()) {
        for (const auto& export_ : module.exports) {
            result.push_back({module.soname, export_.name, export_.address});
        }
    }
    for (const auto& override_ : GuestSymbolOverrides()) {
        result.push_back({std::string(override_.library),
                          std::string(override_.symbol),
                          memory::GuestAddress{kBionicHleThunkBegin +
                              static_cast<std::uint32_t>(result.size()) *
                                  kThunkStride + 1U}});
    }
    return result;
}

std::vector<HleThunkDescriptor> BuildAndroidBoundaryDescriptors(
    const std::span<const BionicHleSymbol> symbols) {
    constexpr std::uint32_t kThunkStride = 4U;
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
        const auto overrides = GuestSymbolOverrides();
        const auto override_ = std::find_if(
            overrides.begin(), overrides.end(), [&](const auto& candidate) {
                return candidate.library == symbol.library &&
                       candidate.symbol == symbol.symbol;
            });
        if (override_ != overrides.end()) {
            result.push_back({symbol.library, symbol.symbol,
                              override_->local_id,
                              override_->parameter_count});
            continue;
        }
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
        result.push_back({symbol.library, symbol.symbol, export_->local_id,
                          export_->parameter_count});
    }
    return result;
}

}  // namespace detail

}  // namespace ogplay::runtime
