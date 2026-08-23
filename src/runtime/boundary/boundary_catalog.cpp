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

void AddNamedModule(std::vector<BoundaryModuleDescriptor>& modules,
                    const std::string_view soname,
                    const std::span<const NamedExport> exports) {
    BoundaryModuleDescriptor module;
    module.soname = soname;
    module.exports.reserve(exports.size());
    for (std::size_t index = 0; index < exports.size(); ++index) {
        if (index > (std::numeric_limits<std::uint16_t>::max)()) {
            throw std::length_error("boundary module local id overflows");
        }
        module.exports.push_back(BoundaryExportDescriptor{
            .name = std::string(exports[index].first),
            .local_id = static_cast<std::uint16_t>(index),
            .parameter_count = exports[index].second,
            .address = memory::GuestAddress{}});
    }
    modules.push_back(std::move(module));
}

void AddGlesModule(std::vector<BoundaryModuleDescriptor>& modules,
                   const std::string_view soname,
                   const std::span<const gles::GlesApi> apis) {
    BoundaryModuleDescriptor module;
    module.soname = soname;
    for (const auto api : apis) {
        const auto count = gles::GlesFunctionCount(api);
        for (std::size_t index = 0; index < count; ++index) {
            const auto function = gles::DescribeGlesFunction(
                api, static_cast<gles::GlesThunkId>(index));
            if (module.exports.size() >
                (std::numeric_limits<std::uint16_t>::max)()) {
                throw std::length_error("GLES boundary local id overflows");
            }
            module.exports.push_back(BoundaryExportDescriptor{
                .name = std::string(function.name),
                .local_id = static_cast<std::uint16_t>(module.exports.size()),
                .parameter_count =
                    static_cast<std::uint8_t>(function.parameter_count),
                .address = memory::GuestAddress{}});
        }
    }
    modules.push_back(std::move(module));
}

}  // namespace

bool AndroidApiRange::Contains(const AndroidApi api) const noexcept {
    return static_cast<std::uint8_t>(api) >= static_cast<std::uint8_t>(minimum) &&
           static_cast<std::uint8_t>(api) <= static_cast<std::uint8_t>(maximum);
}

BoundaryCatalog::BoundaryCatalog(const AndroidApi api) : api_(api) {
    AddNamedModule(modules_, "libandroid.so", kAndroidExports);
    AddNamedModule(modules_, "libEGL.so", kEglExports);
    constexpr std::array gles1_apis{gles::GlesApi::gles1,
                                   gles::GlesApi::gles1_extensions};
    AddGlesModule(modules_, "libGLESv1_CM.so", gles1_apis);
    constexpr std::array gles2_apis{gles::GlesApi::gles2};
    AddGlesModule(modules_, "libGLESv2.so", gles2_apis);
    AddNamedModule(modules_, "liblog.so", kLogExports);

    std::set<std::string, std::less<>> sonames;
    for (auto& module : modules_) {
        if (!module.api.Contains(api_) || !sonames.insert(module.soname).second) {
            throw std::logic_error("invalid boundary module catalog");
        }
        module.first_slot = slot_count_;
        std::set<std::string, std::less<>> names;
        std::set<std::uint16_t> local_ids;
        std::uint16_t expected_local_id{};
        for (auto& export_ : module.exports) {
            if (!export_.api.Contains(api_) ||
                !names.insert(export_.name).second ||
                !local_ids.insert(export_.local_id).second ||
                export_.local_id != expected_local_id++) {
                throw std::logic_error("invalid boundary export catalog");
            }
            export_.address = memory::GuestAddress{
                kBionicHleThunkBegin + slot_count_ * kThunkStride + 1U};
            ++slot_count_;
        }
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
