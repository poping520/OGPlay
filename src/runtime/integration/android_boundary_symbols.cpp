#include "android_boundary_symbols.h"

#include <array>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include "ogplay/gles/gles_dispatch.h"

namespace ogplay::runtime::detail {
namespace {

constexpr std::uint32_t kThunkStride = 4U;

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

}  // namespace ogplay::runtime::detail
