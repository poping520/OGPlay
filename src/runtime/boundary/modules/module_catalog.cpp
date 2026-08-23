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
#include "runtime/boundary/modules/opensles/opensles_abi.h"
#include "runtime/boundary/modules/opensles/opensles_exports.h"

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
#define OGPLAY_OPENSLES_METADATA(name, id, count, kind, method)                 \
    NamedExport{name, id, count},
constexpr std::array kOpenSlesCallableExports{
    OGPLAY_OPENSLES_BOUNDARY_EXPORTS(OGPLAY_OPENSLES_METADATA)};
#undef OGPLAY_OPENSLES_METADATA
#undef OGPLAY_NAMED_METADATA

void AddNamedModule(std::vector<BoundaryModuleDefinition>& modules,
                    std::vector<std::vector<BoundaryExportDefinition>>& storage,
                    const std::string_view soname,
                    const std::span<const NamedExport> exports) {
    auto& module_exports = storage.emplace_back();
    module_exports.reserve(exports.size());
    for (const auto& export_ : exports) {
        module_exports.push_back({export_.name, export_.local_id,
                                  export_.parameter_count, {},
                                  BoundaryExportKind::public_function,
                                  memory::GuestAddress{}, 4U});
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
                static_cast<std::uint8_t>(function.parameter_count), {},
                BoundaryExportKind::public_function,
                memory::GuestAddress{}, 4U});
        }
    }
    modules.push_back({soname, {}, module_exports});
}

void AddOpenSlesModule(
    std::vector<BoundaryModuleDefinition>& modules,
    std::vector<std::vector<BoundaryExportDefinition>>& storage) {
    auto& exports = storage.emplace_back();
    exports.reserve(kOpenSlesCallableExports.size() + OpenSlesDataExports().size());
#define OGPLAY_ADD_OPENSLES(name, id, count, kind, method)                     \
    exports.push_back({name, id, count, {}, BoundaryExportKind::kind,          \
                       memory::GuestAddress{}, 4U});
    OGPLAY_OPENSLES_BOUNDARY_EXPORTS(OGPLAY_ADD_OPENSLES)
#undef OGPLAY_ADD_OPENSLES
    exports.insert(exports.end(), OpenSlesDataExports().begin(),
                   OpenSlesDataExports().end());
    modules.push_back({"libOpenSLES.so", {}, exports});
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
    AddOpenSlesModule(modules, storage);
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
            if (export_.kind == BoundaryExportKind::private_callable) continue;
            result.push_back({
                module.soname, export_.name, export_.address,
                export_.kind == BoundaryExportKind::public_data
                    ? BoundarySymbolKind::data
                    : BoundarySymbolKind::function,
                export_.size});
        }
    }
    std::size_t override_index{};
    for (const auto& override_ : GuestSymbolOverrides()) {
        result.push_back({std::string(override_.library),
                          std::string(override_.symbol),
                          memory::GuestAddress{kBionicHleThunkBegin +
                              (catalog.SlotCount() +
                               static_cast<std::uint32_t>(override_index++)) *
                                  kThunkStride + 1U},
                          BoundarySymbolKind::function, 4U});
    }
    return result;
}

std::vector<HleThunkDescriptor> BuildAndroidBoundaryDescriptors(
    const std::span<const BionicHleSymbol> symbols) {
    static_cast<void>(symbols);
    std::vector<HleThunkDescriptor> result;
    const auto& catalog = AndroidBoundaryCatalog(AndroidApi::api19);
    result.reserve(catalog.SlotCount() + GuestSymbolOverrides().size());
    for (const auto& module : catalog.Modules()) {
        for (const auto& export_ : module.exports) {
            if (export_.kind == BoundaryExportKind::public_data) continue;
            result.push_back({module.soname, export_.name, export_.local_id,
                              export_.parameter_count});
        }
    }
    for (const auto& override_ : GuestSymbolOverrides()) {
        result.push_back({override_.library, override_.symbol,
                          override_.local_id, override_.parameter_count});
    }
    return result;
}

}  // namespace detail

}  // namespace ogplay::runtime
