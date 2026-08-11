#include "android_boundary_symbols.h"

#include <array>
#include <cstdint>
#include <limits>
#include <ranges>
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
                                          const std::string_view name,
                                          const std::size_t fallback) {
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
    if (fallback > (std::numeric_limits<std::uint16_t>::max)()) {
        throw std::length_error("Android boundary function id overflows");
    }
    return static_cast<std::uint16_t>(fallback);
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
    static constexpr std::array<std::uint8_t, 42> counts{
        3, 3, 3, 3, 1,
        0, 1, 2, 2, 2, 1, 6, 4, 5, 1, 2, 2, 3, 1, 1, 1, 1, 2, 2, 4,
        1, 3, 5, 4, 4, 4, 4, 4, 2, 2, 2, 1,
        4, 4, 1, 3, 3,
    };
    if (function_id >= counts.size()) {
        throw std::logic_error("non-GLES function id is outside its catalog");
    }
    return counts[function_id];
}

}  // namespace

std::vector<BionicHleSymbol> BuildAndroidBoundarySymbols() {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 42> names{{
        {"libc.so", "memcpy"}, {"libc.so", "memmove"}, {"libc.so", "memset"},
        {"libc.so", "memcmp"}, {"libc.so", "strlen"},
        {"libandroid.so", "AConfiguration_new"},
        {"libandroid.so", "AConfiguration_delete"},
        {"libandroid.so", "AConfiguration_fromAssetManager"},
        {"libandroid.so", "AConfiguration_getLanguage"},
        {"libandroid.so", "AConfiguration_getCountry"},
        {"libandroid.so", "ALooper_prepare"},
        {"libandroid.so", "ALooper_addFd"},
        {"libandroid.so", "ALooper_pollAll"},
        {"libandroid.so", "AInputQueue_attachLooper"},
        {"libandroid.so", "AInputQueue_detachLooper"},
        {"libandroid.so", "AInputQueue_getEvent"},
        {"libandroid.so", "AInputQueue_preDispatchEvent"},
        {"libandroid.so", "AInputQueue_finishEvent"},
        {"libandroid.so", "AInputEvent_getType"},
        {"libandroid.so", "AKeyEvent_getAction"},
        {"libandroid.so", "AKeyEvent_getKeyCode"},
        {"libandroid.so", "AMotionEvent_getAction"},
        {"libandroid.so", "AMotionEvent_getX"},
        {"libandroid.so", "AMotionEvent_getY"},
        {"libandroid.so", "ANativeWindow_setBuffersGeometry"},
        {"libEGL.so", "eglGetDisplay"}, {"libEGL.so", "eglInitialize"},
        {"libEGL.so", "eglChooseConfig"}, {"libEGL.so", "eglGetConfigAttrib"},
        {"libEGL.so", "eglCreateWindowSurface"}, {"libEGL.so", "eglCreateContext"},
        {"libEGL.so", "eglMakeCurrent"}, {"libEGL.so", "eglQuerySurface"},
        {"libEGL.so", "eglSwapBuffers"}, {"libEGL.so", "eglDestroyContext"},
        {"libEGL.so", "eglDestroySurface"}, {"libEGL.so", "eglTerminate"},
        {"libGLESv2.so", "glViewport"}, {"libGLESv2.so", "glClearColor"},
        {"libGLESv2.so", "glClear"}, {"liblog.so", "__android_log_print"},
        {"liblog.so", "__android_log_write"},
    }};
    std::vector<BionicHleSymbol> result;
    result.reserve(names.size() + gles::GlesDispatchTable::FunctionCount() +
                   gles::GlesFunctionCount(gles::GlesApi::gles1) +
                   gles::GlesFunctionCount(gles::GlesApi::gles1_extensions));
    for (std::size_t index = 0; index < names.size(); ++index) {
        result.push_back({std::string(names[index].first), std::string(names[index].second),
                          memory::GuestAddress{kBionicHleThunkBegin +
                                               static_cast<std::uint32_t>(index) *
                                                   kThunkStride + 1U}});
    }
    for (std::size_t index = 0; index < gles::GlesDispatchTable::FunctionCount(); ++index) {
        const auto function = gles::GlesDispatchTable::Describe(
            static_cast<gles::GlesThunkId>(index));
        const auto already_registered = std::ranges::any_of(
            result, [&function](const BionicHleSymbol& candidate) {
                return candidate.library == "libGLESv2.so" &&
                       candidate.symbol == function.name;
            });
        if (already_registered) continue;
        result.push_back({"libGLESv2.so", std::string(function.name),
                          memory::GuestAddress{kBionicHleThunkBegin +
                                               static_cast<std::uint32_t>(result.size()) *
                                                   kThunkStride + 1U}});
    }
    for (std::size_t index = 0;
         index < gles::GlesFunctionCount(gles::GlesApi::gles1); ++index) {
        const auto function = gles::DescribeGlesFunction(
            gles::GlesApi::gles1, static_cast<gles::GlesThunkId>(index));
        result.push_back({"libGLESv1_CM.so", std::string(function.name),
                          memory::GuestAddress{kBionicHleThunkBegin +
                                               static_cast<std::uint32_t>(result.size()) *
                                                   kThunkStride + 1U}});
    }
    for (std::size_t index = 0;
         index < gles::GlesFunctionCount(gles::GlesApi::gles1_extensions); ++index) {
        const auto function = gles::DescribeGlesFunction(
            gles::GlesApi::gles1_extensions, static_cast<gles::GlesThunkId>(index));
        result.push_back({"libGLESv1_CM.so", std::string(function.name),
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
        const auto function_id = FunctionIdFor(route, symbol.symbol, index);
        result.push_back({symbol.library, symbol.symbol, route, function_id,
                          ParameterCountFor(route, function_id)});
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
