#include "ogplay/runtime/boundary/boundary_catalog.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

#include "ogplay/gles/gles_dispatch.h"

namespace ogplay::runtime {
namespace {

constexpr std::uint32_t kThunkStride = 4U;

using NamedExport = std::pair<std::string_view, std::uint8_t>;

constexpr std::array<NamedExport, 20> kAndroidExports{{
    {"AConfiguration_new", 0}, {"AConfiguration_delete", 1},
    {"AConfiguration_fromAssetManager", 2}, {"AConfiguration_getLanguage", 2},
    {"AConfiguration_getCountry", 2}, {"ALooper_prepare", 1},
    {"ALooper_addFd", 6}, {"ALooper_pollAll", 4},
    {"AInputQueue_attachLooper", 5}, {"AInputQueue_detachLooper", 1},
    {"AInputQueue_getEvent", 2}, {"AInputQueue_preDispatchEvent", 2},
    {"AInputQueue_finishEvent", 3}, {"AInputEvent_getType", 1},
    {"AKeyEvent_getAction", 1}, {"AKeyEvent_getKeyCode", 1},
    {"AMotionEvent_getAction", 1}, {"AMotionEvent_getX", 2},
    {"AMotionEvent_getY", 2}, {"ANativeWindow_setBuffersGeometry", 4},
}};

constexpr std::array<NamedExport, 12> kEglExports{{
    {"eglGetDisplay", 1}, {"eglInitialize", 3}, {"eglChooseConfig", 5},
    {"eglGetConfigAttrib", 4}, {"eglCreateWindowSurface", 4},
    {"eglCreateContext", 4}, {"eglMakeCurrent", 4}, {"eglQuerySurface", 4},
    {"eglSwapBuffers", 2}, {"eglDestroyContext", 2},
    {"eglDestroySurface", 2}, {"eglTerminate", 1},
}};

constexpr std::array<NamedExport, 2> kLogExports{{
    {"__android_log_print", 3}, {"__android_log_write", 3},
}};

void AddNamedModule(std::vector<BoundaryModuleDefinition>& modules,
                    std::vector<std::vector<BoundaryExportDefinition>>& storage,
                    const std::string_view soname,
                    const std::span<const NamedExport> exports) {
    auto& module_exports = storage.emplace_back();
    module_exports.reserve(exports.size());
    for (std::size_t index = 0; index < exports.size(); ++index) {
        if (index > (std::numeric_limits<std::uint16_t>::max)()) {
            throw std::length_error("boundary module local id overflows");
        }
        module_exports.push_back(BoundaryExportDefinition{
            .name = exports[index].first,
            .local_id = static_cast<std::uint16_t>(index),
            .parameter_count = exports[index].second});
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
            module_exports.push_back(BoundaryExportDefinition{
                .name = function.name,
                .local_id = static_cast<std::uint16_t>(module_exports.size()),
                .parameter_count =
                    static_cast<std::uint8_t>(function.parameter_count)});
        }
    }
    modules.push_back({soname, {}, module_exports});
}

std::vector<BoundaryModuleDefinition> BuiltinDefinitions(
    std::vector<std::vector<BoundaryExportDefinition>>& storage) {
    std::vector<BoundaryModuleDefinition> modules;
    storage.reserve(5);
    AddNamedModule(modules, storage, "libandroid.so", kAndroidExports);
    AddNamedModule(modules, storage, "libEGL.so", kEglExports);
    constexpr std::array gles1_apis{gles::GlesApi::gles1,
                                   gles::GlesApi::gles1_extensions};
    AddGlesModule(modules, storage, "libGLESv1_CM.so", gles1_apis);
    constexpr std::array gles2_apis{gles::GlesApi::gles2};
    AddGlesModule(modules, storage, "libGLESv2.so", gles2_apis);
    AddNamedModule(modules, storage, "liblog.so", kLogExports);
    return modules;
}

}  // namespace

bool AndroidApiRange::Contains(const AndroidApi api) const noexcept {
    return static_cast<std::uint8_t>(api) >= static_cast<std::uint8_t>(minimum) &&
           static_cast<std::uint8_t>(api) <= static_cast<std::uint8_t>(maximum);
}

BoundaryCatalog::BoundaryCatalog(const AndroidApi api) : api_(api) {
    std::vector<std::vector<BoundaryExportDefinition>> storage;
    const auto definitions = BuiltinDefinitions(storage);
    *this = BoundaryCatalog(api, definitions);
}

BoundaryCatalog::BoundaryCatalog(
    const AndroidApi api,
    const std::span<const BoundaryModuleDefinition> definitions)
    : api_(api) {
    std::set<std::string, std::less<>> sonames;
    for (const auto& definition : definitions) {
        if (!definition.api.Contains(api_)) continue;
        if (!sonames.insert(std::string(definition.soname)).second) {
            throw std::logic_error("invalid boundary module catalog");
        }
        BoundaryModuleDescriptor module;
        module.soname = definition.soname;
        module.api = definition.api;
        module.first_slot = slot_count_;
        std::set<std::string, std::less<>> names;
        std::set<std::uint16_t> local_ids;
        for (const auto& definition_export : definition.exports) {
            if (!definition_export.api.Contains(api_)) continue;
            if (!names.insert(std::string(definition_export.name)).second ||
                !local_ids.insert(definition_export.local_id).second) {
                throw std::logic_error("invalid boundary export catalog");
            }
            BoundaryExportDescriptor export_;
            export_.name = definition_export.name;
            export_.local_id = definition_export.local_id;
            export_.parameter_count = definition_export.parameter_count;
            export_.api = definition_export.api;
            export_.address = memory::GuestAddress{
                kBionicHleThunkBegin + slot_count_ * kThunkStride + 1U};
            module.exports.push_back(std::move(export_));
            ++slot_count_;
        }
        if (!module.exports.empty()) modules_.push_back(std::move(module));
    }
}

AndroidApi BoundaryCatalog::Api() const noexcept { return api_; }

std::span<const BoundaryModuleDescriptor> BoundaryCatalog::Modules() const noexcept {
    return modules_;
}

const BoundaryModuleDescriptor* BoundaryCatalog::FindModule(
    const std::string_view soname) const noexcept {
    const auto found = std::find_if(modules_.begin(), modules_.end(),
                                    [&](const auto& module) {
                                        return module.soname == soname;
                                    });
    return found == modules_.end() ? nullptr : &*found;
}

std::uint32_t BoundaryCatalog::SlotCount() const noexcept { return slot_count_; }

const BoundaryCatalog& AndroidBoundaryCatalog(const AndroidApi api) {
    static const BoundaryCatalog api19(AndroidApi::api19);
    static const BoundaryCatalog api22(AndroidApi::api22);
    static const BoundaryCatalog api23(AndroidApi::api23);
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

}  // namespace ogplay::runtime
