#include "runtime/boundary/modules/egl/egl_module.h"

#include <mutex>
#include <stdexcept>
#include <thread>

namespace ogplay::runtime {
namespace {
constexpr std::uint32_t kFakeDisplay = 1;
constexpr std::uint32_t kFakeConfig = 2;
constexpr std::uint32_t kFakeSurface = 3;
constexpr std::uint32_t kFakeContext = 4;
constexpr std::uint32_t kEglWidth = 0x3057;
constexpr std::uint32_t kEglHeight = 0x3056;
}  // namespace

EglModule::EglModule(BoundaryCallServices& calls,
                     GraphicsBoundaryContext& graphics) noexcept
    : calls_(calls), graphics_(graphics) {}

BoundaryCallServices& EglModule::CallServices() noexcept { return calls_; }

template <std::uint16_t FunctionId>
std::uint32_t EglModule::ExecuteExport(const A32CallFrame& call) {
    const auto args = call.RegisterArguments();
    const auto tid = call.ThreadId();
    if constexpr (FunctionId == 0U) return kFakeDisplay;
    if constexpr (FunctionId == 1U) {
        graphics_.Write32(call.Pointer<std::uint32_t>(1).Address().Value(), 1, tid);
        graphics_.Write32(call.Pointer<std::uint32_t>(2).Address().Value(), 5, tid);
        return 1;
    }
    if constexpr (FunctionId == 2U) {
        graphics_.Write32(args[2], kFakeConfig, tid);
        graphics_.Write32(call.Argument(4), 1, tid);
        return 1;
    }
    if constexpr (FunctionId == 3U) {
        graphics_.Write32(args[3], 0, tid);
        return 1;
    }
    if constexpr (FunctionId == 4U) return kFakeSurface;
    if constexpr (FunctionId == 5U) return kFakeContext;
    if constexpr (FunctionId == 6U) {
        if (graphics_.managed_surface) {
            throw std::runtime_error(
                "guest EGL cannot replace a host-managed ANGLE surface");
        }
        if (args[3] != 0 && !graphics_.angle_frame.has_value()) {
            graphics_.angle_frame.emplace(gles::AngleFrame::CreatePbuffer(
                graphics_.backend, graphics_.layout.render_width,
                graphics_.layout.render_height));
            graphics_.gl_owner = std::this_thread::get_id();
            graphics_.InitializeGuestGlDefaults();
            graphics_.frames.SetRenderTargetReady(true);
        } else if (args[3] == 0 && graphics_.gl_owner.has_value()) {
            graphics_.ReleaseManagedSurfaceFromCallingThread();
        }
        return 1;
    }
    if constexpr (FunctionId == 7U) {
        graphics_.Write32(
            args[3], args[2] == kEglWidth
                         ? graphics_.layout.logical_width
                         : args[2] == kEglHeight
                               ? graphics_.layout.logical_height : 0,
            tid);
        return 1;
    }
    if constexpr (FunctionId == 8U) {
        if (!graphics_.angle_frame.has_value()) {
            throw std::runtime_error("eglSwapBuffers has no current ANGLE frame");
        }
        graphics_.PublishFrame();
        return 1;
    }
    if constexpr (FunctionId == 9U || FunctionId == 10U) return 1;
    if constexpr (FunctionId == 11U) {
        if (graphics_.managed_surface) {
            throw std::runtime_error(
                "guest EGL cannot terminate a host-managed ANGLE surface");
        }
        graphics_.gl_owner.reset();
        graphics_.angle_frame.reset();
        graphics_.ResetGuestGraphics();
        graphics_.frames.SetRenderTargetReady(false);
        return 1;
    }
    throw std::logic_error("unbound concrete libEGL export");
}

#define OGPLAY_DEFINE_EGL(name, id, count, method) \
    std::uint32_t EglModule::method(const A32CallFrame& call) { \
        return ExecuteExport<id>(call); \
    }
OGPLAY_EGL_BOUNDARY_EXPORTS(OGPLAY_DEFINE_EGL)
#undef OGPLAY_DEFINE_EGL

}  // namespace ogplay::runtime
