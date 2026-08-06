#include "ogplay/gles/egl_lifecycle.h"

#include <array>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "ogplay/hal/host_environment.h"

#if OGPLAY_HAS_ANGLE
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#endif

namespace ogplay::gles {
namespace {

[[nodiscard]] std::string_view OperationName(const EglOperation operation) {
    switch (operation) {
    case EglOperation::unavailable:
        return "ANGLE EGL unavailable";
    case EglOperation::get_platform_display:
        return "eglGetPlatformDisplay";
    case EglOperation::initialize:
        return "eglInitialize";
    case EglOperation::choose_config:
        return "eglChooseConfig";
    case EglOperation::bind_api:
        return "eglBindAPI";
    case EglOperation::create_context:
        return "eglCreateContext";
    case EglOperation::create_surface:
        return "eglCreatePbufferSurface";
    case EglOperation::make_current:
        return "eglMakeCurrent";
    }
    return "unknown EGL operation";
}

[[nodiscard]] std::string ErrorMessage(const EglOperation operation,
                                       const std::uint32_t native_error) {
    std::ostringstream stream;
    stream << OperationName(operation);
    if (native_error != 0) {
        stream << " failed (EGL error 0x" << std::hex << std::uppercase
               << native_error << ')';
    }
    return stream.str();
}

[[noreturn]] void ThrowLastError(EglApi& api,
                                 const EglOperation operation) {
    throw EglLifecycleError(operation, api.GetError());
}

#if OGPLAY_HAS_ANGLE

template <typename Target, typename Source>
[[nodiscard]] Target ReinterpretHandle(const Source source) noexcept {
    return reinterpret_cast<Target>(source);
}

[[nodiscard]] EGLint RendererAttribute(const AngleRenderer renderer) {
    switch (renderer) {
    case AngleRenderer::d3d11:
        return EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE;
    case AngleRenderer::vulkan:
        return EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE;
    case AngleRenderer::metal:
        return EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE;
    }
    throw std::invalid_argument("unknown ANGLE renderer");
}

[[nodiscard]] EGLint DeviceAttribute(const AngleDevice device) {
    switch (device) {
    case AngleDevice::hardware:
        return EGL_PLATFORM_ANGLE_DEVICE_TYPE_HARDWARE_ANGLE;
    case AngleDevice::swiftshader:
        return EGL_PLATFORM_ANGLE_DEVICE_TYPE_SWIFTSHADER_ANGLE;
    }
    throw std::invalid_argument("unknown ANGLE device");
}

class NativeAngleEglApi final : public EglApi {
public:
    EglHandle GetPlatformDisplay(const AngleBackend backend) override {
        if (driver_environment_.has_value()) {
            throw std::logic_error(
                "previous ANGLE display initialization is incomplete");
        }
#if OGPLAY_ANGLE_HAS_SWIFTSHADER
        if (backend.device == AngleDevice::swiftshader) {
            const auto icd = hal::HostExecutableDirectory() /
                             "vk_swiftshader_icd.json";
            if (!std::filesystem::is_regular_file(icd)) {
                throw std::runtime_error(
                    "ANGLE SwiftShader ICD is missing beside the executable");
            }
            const auto path = std::filesystem::absolute(icd).string();
            const std::array overrides{
                hal::HostEnvironmentOverride{
                    "VK_DRIVER_FILES", std::optional<std::string>(path)},
                hal::HostEnvironmentOverride{
                    "VK_ICD_FILENAMES", std::optional<std::string>(path)},
            };
            driver_environment_.emplace(overrides);
        }
#endif
        auto device = backend.device;
#if OGPLAY_ANGLE_HAS_SWIFTSHADER
        if (backend.device == AngleDevice::swiftshader) {
            device = AngleDevice::hardware;
        }
#endif
        // Vulkan pbuffers never need a window system surface. Request ANGLE's
        // headless Vulkan display mode so initialization does not depend on
        // xcb/wayland WSI being selected for the host.
        if (backend.renderer == AngleRenderer::vulkan) {
            const EGLint attributes[]{
                EGL_PLATFORM_ANGLE_TYPE_ANGLE,
                RendererAttribute(backend.renderer),
                EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
                DeviceAttribute(device),
                EGL_PLATFORM_ANGLE_NATIVE_PLATFORM_TYPE_ANGLE,
                EGL_PLATFORM_VULKAN_DISPLAY_MODE_HEADLESS_ANGLE, EGL_NONE};
            const auto display = ReinterpretHandle<EglHandle>(
                eglGetPlatformDisplayEXT(
                    EGL_PLATFORM_ANGLE_ANGLE, nullptr, attributes));
            if (display == 0) {
                driver_environment_.reset();
            }
            return display;
        }
        const EGLint attributes[]{
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, RendererAttribute(backend.renderer),
            EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE,
            DeviceAttribute(device),
            EGL_NONE};
        const auto display = ReinterpretHandle<EglHandle>(
            eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, nullptr,
                                     attributes));
        if (display == 0) {
            driver_environment_.reset();
        }
        return display;
    }

    bool Initialize(const EglHandle display, int& major, int& minor) override {
        EGLint native_major{};
        EGLint native_minor{};
        const auto result = eglInitialize(ReinterpretHandle<EGLDisplay>(display),
                                          &native_major, &native_minor);
        driver_environment_.reset();
        major = native_major;
        minor = native_minor;
        return result == EGL_TRUE;
    }

    bool ChoosePbufferConfig(const EglHandle display,
                             EglHandle& config) override {
        constexpr EGLint attributes[]{
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig native_config{};
        EGLint count{};
        const auto result = eglChooseConfig(ReinterpretHandle<EGLDisplay>(display),
                                            attributes, &native_config, 1, &count);
        config = count > 0 ? ReinterpretHandle<EglHandle>(native_config) : 0;
        return result == EGL_TRUE && count > 0;
    }

    bool BindOpenGlesApi() override {
        return eglBindAPI(EGL_OPENGL_ES_API) == EGL_TRUE;
    }

    EglHandle CreateContext(const EglHandle display, const EglHandle config,
                            const int client_version) override {
        const EGLint attributes[]{EGL_CONTEXT_CLIENT_VERSION, client_version,
                                  EGL_NONE};
        return ReinterpretHandle<EglHandle>(eglCreateContext(
            ReinterpretHandle<EGLDisplay>(display),
            ReinterpretHandle<EGLConfig>(config), EGL_NO_CONTEXT, attributes));
    }

    EglHandle CreatePbufferSurface(const EglHandle display,
                                   const EglHandle config,
                                   const std::uint32_t width,
                                   const std::uint32_t height) override {
        const EGLint attributes[]{EGL_WIDTH, static_cast<EGLint>(width),
                                  EGL_HEIGHT, static_cast<EGLint>(height),
                                  EGL_NONE};
        return ReinterpretHandle<EglHandle>(eglCreatePbufferSurface(
            ReinterpretHandle<EGLDisplay>(display),
            ReinterpretHandle<EGLConfig>(config), attributes));
    }

    bool MakeCurrent(const EglHandle display, const EglHandle draw_surface,
                     const EglHandle read_surface,
                     const EglHandle context) override {
        return eglMakeCurrent(ReinterpretHandle<EGLDisplay>(display),
                              ReinterpretHandle<EGLSurface>(draw_surface),
                              ReinterpretHandle<EGLSurface>(read_surface),
                              ReinterpretHandle<EGLContext>(context)) == EGL_TRUE;
    }

    bool DestroySurface(const EglHandle display,
                        const EglHandle surface) override {
        return eglDestroySurface(ReinterpretHandle<EGLDisplay>(display),
                                 ReinterpretHandle<EGLSurface>(surface)) == EGL_TRUE;
    }

    bool DestroyContext(const EglHandle display,
                        const EglHandle context) override {
        return eglDestroyContext(ReinterpretHandle<EGLDisplay>(display),
                                 ReinterpretHandle<EGLContext>(context)) == EGL_TRUE;
    }

    bool Terminate(const EglHandle display) override {
        return eglTerminate(ReinterpretHandle<EGLDisplay>(display)) == EGL_TRUE;
    }

    std::uint32_t GetError() override {
        return static_cast<std::uint32_t>(eglGetError());
    }

private:
    std::optional<hal::ScopedHostEnvironment> driver_environment_;
};

#endif

}  // namespace

EglLifecycleError::EglLifecycleError(const EglOperation operation,
                                     const std::uint32_t native_error)
    : std::runtime_error(ErrorMessage(operation, native_error)),
      operation_(operation), native_error_(native_error) {}

EglOperation EglLifecycleError::Operation() const noexcept {
    return operation_;
}

std::uint32_t EglLifecycleError::NativeError() const noexcept {
    return native_error_;
}

EglLifecycle::EglLifecycle(EglApi& api, EglContextInfo info) noexcept
    : api_(&api), info_(std::move(info)) {}

EglLifecycle EglLifecycle::CreatePbuffer(EglApi& api,
                                         const AngleBackend backend,
                                         const std::uint32_t width,
                                         const std::uint32_t height) {
    static_cast<void>(AngleBackendName(backend));
    constexpr auto kMaxDimension =
        static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    if (width == 0 || height == 0 || width > kMaxDimension ||
        height > kMaxDimension) {
        throw std::invalid_argument(
            "EGL pbuffer dimensions must fit a positive EGLint");
    }
    EglLifecycle lifecycle(api, {.backend = backend,
                                 .client_version = 2,
                                 .width = width,
                                 .height = height});
    lifecycle.display_ = api.GetPlatformDisplay(backend);
    if (lifecycle.display_ == 0) {
        ThrowLastError(api, EglOperation::get_platform_display);
    }
    if (!api.Initialize(lifecycle.display_, lifecycle.info_.egl_major,
                        lifecycle.info_.egl_minor)) {
        ThrowLastError(api, EglOperation::initialize);
    }
    lifecycle.initialized_ = true;

    EglHandle config{};
    if (!api.ChoosePbufferConfig(lifecycle.display_, config) || config == 0) {
        ThrowLastError(api, EglOperation::choose_config);
    }
    if (!api.BindOpenGlesApi()) {
        ThrowLastError(api, EglOperation::bind_api);
    }
    lifecycle.context_ = api.CreateContext(
        lifecycle.display_, config, lifecycle.info_.client_version);
    if (lifecycle.context_ == 0) {
        ThrowLastError(api, EglOperation::create_context);
    }
    lifecycle.surface_ = api.CreatePbufferSurface(
        lifecycle.display_, config, width, height);
    if (lifecycle.surface_ == 0) {
        ThrowLastError(api, EglOperation::create_surface);
    }
    if (!api.MakeCurrent(lifecycle.display_, lifecycle.surface_,
                         lifecycle.surface_, lifecycle.context_)) {
        ThrowLastError(api, EglOperation::make_current);
    }
    lifecycle.current_ = true;
    return lifecycle;
}

EglLifecycle::~EglLifecycle() {
    Reset();
}

EglLifecycle::EglLifecycle(EglLifecycle&& other) noexcept
    : api_(std::exchange(other.api_, nullptr)), info_(other.info_),
      display_(std::exchange(other.display_, 0)),
      context_(std::exchange(other.context_, 0)),
      surface_(std::exchange(other.surface_, 0)),
      initialized_(std::exchange(other.initialized_, false)),
      current_(std::exchange(other.current_, false)) {}

EglLifecycle& EglLifecycle::operator=(EglLifecycle&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Reset();
    api_ = std::exchange(other.api_, nullptr);
    info_ = other.info_;
    display_ = std::exchange(other.display_, 0);
    context_ = std::exchange(other.context_, 0);
    surface_ = std::exchange(other.surface_, 0);
    initialized_ = std::exchange(other.initialized_, false);
    current_ = std::exchange(other.current_, false);
    return *this;
}

const EglContextInfo& EglLifecycle::Info() const noexcept {
    return info_;
}

bool EglLifecycle::IsCurrent() const noexcept {
    return current_;
}

void EglLifecycle::Reset() noexcept {
    if (api_ == nullptr) {
        return;
    }
    if (current_) {
        static_cast<void>(api_->MakeCurrent(display_, 0, 0, 0));
    }
    if (surface_ != 0) {
        static_cast<void>(api_->DestroySurface(display_, surface_));
    }
    if (context_ != 0) {
        static_cast<void>(api_->DestroyContext(display_, context_));
    }
    if (initialized_) {
        static_cast<void>(api_->Terminate(display_));
    }
    api_ = nullptr;
    display_ = 0;
    context_ = 0;
    surface_ = 0;
    initialized_ = false;
    current_ = false;
}

bool IsNativeAngleEglAvailable() noexcept {
#if OGPLAY_HAS_ANGLE
    return true;
#else
    return false;
#endif
}

std::unique_ptr<EglApi> CreateNativeAngleEglApi() {
#if OGPLAY_HAS_ANGLE
    return std::make_unique<NativeAngleEglApi>();
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

}  // namespace ogplay::gles
