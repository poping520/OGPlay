#include "ogplay/gles/egl_lifecycle.h"

#include <array>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "ogplay/hal/host_environment.h"
#include "ogplay/hal/rgb_surface_storage.h"
#include <vector>

#if OGPLAY_HAS_ANGLE

#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>
#include <cstring>
#endif

namespace ogplay::gles {
namespace {

#if OGPLAY_HAS_ANGLE
std::mutex native_display_mutex;
struct DisplayReference final { std::size_t count{}; int major{}, minor{}; };
std::map<EglHandle, DisplayReference> native_display_references;
#endif

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
        std::scoped_lock lock(native_display_mutex);
        auto& reference = native_display_references[display];
        if (reference.count != 0U) {
            ++reference.count;
            major = reference.major; minor = reference.minor;
            driver_environment_.reset();
            return true;
        }
        EGLint native_major{};
        EGLint native_minor{};
        const auto result = eglInitialize(ReinterpretHandle<EGLDisplay>(display),
                                          &native_major, &native_minor);
        driver_environment_.reset();
        major = native_major;
        minor = native_minor;
        if (result == EGL_TRUE) reference = {1U, major, minor};
        else native_display_references.erase(display);
        return result == EGL_TRUE;
    }

    bool ChoosePbufferConfig(const EglHandle display,
                             EglHandle& config) override {
        constexpr EGLint attributes[]{
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES2_BIT | 0x0040, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
            EGL_STENCIL_SIZE, 8, EGL_NONE};
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
                            const int client_version,
                            const EglHandle share_context) override {
        const EGLint attributes[]{EGL_CONTEXT_CLIENT_VERSION, client_version,
                                  EGL_NONE};
        return ReinterpretHandle<EglHandle>(eglCreateContext(
            ReinterpretHandle<EGLDisplay>(display),
            ReinterpretHandle<EGLConfig>(config),
            ReinterpretHandle<EGLContext>(share_context), attributes));
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
        std::scoped_lock lock(native_display_mutex);
        const auto found = native_display_references.find(display);
        if (found != native_display_references.end()) {
            if (--found->second.count != 0U) return true;
            native_display_references.erase(found);
        }
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

std::shared_ptr<EglDisplayResources> EglDisplayResources::Create(const AngleBackend backend) {
    auto result = std::shared_ptr<EglDisplayResources>(new EglDisplayResources);
    result->api_ = CreateNativeAngleEglApi();
    result->info_.backend = backend;
    result->display_ = result->api_->GetPlatformDisplay(backend);
    if (!result->display_) ThrowLastError(*result->api_, EglOperation::get_platform_display);
    if (!result->api_->Initialize(result->display_, result->info_.egl_major,
                                  result->info_.egl_minor))
        ThrowLastError(*result->api_, EglOperation::initialize);
    result->initialized_ = true;
    if (!result->api_->ChoosePbufferConfig(result->display_, result->config_))
        ThrowLastError(*result->api_, EglOperation::choose_config);
#if OGPLAY_HAS_ANGLE
    // Alpha size in eglChooseConfig is a minimum. Select an exact RGB format
    // from the returned candidates instead of relabelling an RGBA config.
    const EGLint attributes[]{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | 0x0040, EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    const auto native_display = ReinterpretHandle<EGLDisplay>(result->display_);
    EGLint count{};
    if (!eglChooseConfig(native_display, attributes, nullptr, 0, &count))
        ThrowLastError(*result->api_, EglOperation::choose_config);
    std::vector<EGLConfig> candidates(static_cast<std::size_t>(count));
    if (count && !eglChooseConfig(native_display, attributes, candidates.data(), count, &count))
        ThrowLastError(*result->api_, EglOperation::choose_config);
    for (const auto candidate : candidates) {
        const auto handle = ReinterpretHandle<EglHandle>(candidate);
        EGLint alpha{}, red{}, green{}, blue{}, samples{};
        if (!eglGetConfigAttrib(native_display, candidate, EGL_ALPHA_SIZE, &alpha) ||
            !eglGetConfigAttrib(native_display, candidate, EGL_RED_SIZE, &red) ||
            !eglGetConfigAttrib(native_display, candidate, EGL_GREEN_SIZE, &green) ||
            !eglGetConfigAttrib(native_display, candidate, EGL_BLUE_SIZE, &blue) ||
            !eglGetConfigAttrib(native_display, candidate, EGL_SAMPLES, &samples))
            ThrowLastError(*result->api_, EglOperation::choose_config);
        if (alpha == 0 && red == 8 && green == 8 && blue == 8 && samples == 0) {
            result->rgb_config_ = handle;
            break;
        }
    }
    const EGLint packed_attributes[]{EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,5,EGL_GREEN_SIZE,6,
        EGL_BLUE_SIZE,5,EGL_ALPHA_SIZE,0,EGL_NONE};
    EGLint packed_count{};
    EGLint packed_depth{-1}, packed_stencil{-1};
    if (eglChooseConfig(native_display,packed_attributes,nullptr,0,&packed_count)) {
        std::vector<EGLConfig> packed(static_cast<std::size_t>(packed_count));
        if (packed_count && eglChooseConfig(native_display,packed_attributes,packed.data(),packed_count,&packed_count)) {
            for (const auto candidate : packed) {
                EGLint r{},g{},b{},a{},samples{},depth{},stencil{};
                if (eglGetConfigAttrib(native_display,candidate,EGL_RED_SIZE,&r) &&
                    eglGetConfigAttrib(native_display,candidate,EGL_GREEN_SIZE,&g) &&
                    eglGetConfigAttrib(native_display,candidate,EGL_BLUE_SIZE,&b) &&
                    eglGetConfigAttrib(native_display,candidate,EGL_ALPHA_SIZE,&a) &&
                    eglGetConfigAttrib(native_display,candidate,EGL_SAMPLES,&samples) &&
                    eglGetConfigAttrib(native_display,candidate,EGL_DEPTH_SIZE,&depth) &&
                    eglGetConfigAttrib(native_display,candidate,EGL_STENCIL_SIZE,&stencil) &&
                    r==5 && g==6 && b==5 && a==0 && samples==0) {
                    // EGL sorts depth/stencil ascending. Retain the most capable
                    // native backing instead of discarding it for the first D0/S0.
                    if (stencil > packed_stencil ||
                        (stencil == packed_stencil && depth > packed_depth)) {
                        result->rgb565_config_=ReinterpretHandle<EglHandle>(candidate);
                        packed_depth=depth; packed_stencil=stencil;
                    }
                }
            }
        }
    }
    if (!result->rgb_config_ && backend.renderer == AngleRenderer::metal &&
        hal::HasRgbSurfaceStorage() &&
        (" " + result->Extensions() + " ").find(" EGL_ANGLE_iosurface_client_buffer ") != std::string::npos) {
        result->rgb_config_ = result->config_;
        result->rgb_client_buffer_ = true;
    }
#endif
    if (!result->rgb565_config_) result->InitializePackedRgbSupport();
    return result;
}

EglDisplayResources::~EglDisplayResources() {
    if (initialized_) static_cast<void>(api_->Terminate(display_));
}

std::shared_ptr<EglSurfaceResources> EglSurfaceResources::Create(
    std::shared_ptr<EglDisplayResources> display, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t texture_format, const bool mipmap,
    const bool rgb) {
    return Create(std::move(display),width,height,texture_format,mipmap,
                  rgb ? EglColorFormat::rgb888 : EglColorFormat::rgba8888);
}

std::shared_ptr<EglSurfaceResources> EglSurfaceResources::Create(
    std::shared_ptr<EglDisplayResources> display, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t texture_format,
    const bool mipmap, const EglColorFormat format) {
    const bool rgb = format == EglColorFormat::rgb888;
    if (format == EglColorFormat::rgb565 && !display->SupportsRgb565Surface())
        throw EglLifecycleError(EglOperation::create_surface,0x3005U);
    if (format == EglColorFormat::rgb565 && (texture_format != 0x305CU || mipmap))
        throw EglLifecycleError(EglOperation::create_surface,0x3009U);
    auto result = std::shared_ptr<EglSurfaceResources>(new EglSurfaceResources);
    result->display_ = std::move(display);
    result->width_ = width; result->height_ = height;
    auto& api = result->display_->Api();
    if (rgb && !result->display_->SupportsRgbSurface())
        throw EglLifecycleError(EglOperation::create_surface, 0x3005U);
#if OGPLAY_HAS_ANGLE
    if (rgb && result->display_->rgb_client_buffer_) {
        if (texture_format != EGL_NO_TEXTURE || mipmap)
            throw EglLifecycleError(EglOperation::create_surface, EGL_BAD_MATCH);
        result->rgb_storage_ = hal::CreateRgbSurfaceStorage(width, height);
        const EGLint attributes[]{EGL_WIDTH, static_cast<EGLint>(width),
            EGL_HEIGHT, static_cast<EGLint>(height), EGL_IOSURFACE_PLANE_ANGLE, 0,
            EGL_TEXTURE_TARGET, EGL_TEXTURE_2D, EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
            EGL_TEXTURE_INTERNAL_FORMAT_ANGLE, 0x1907 /* GL_RGB */,
            EGL_TEXTURE_TYPE_ANGLE, 0x1401 /* GL_UNSIGNED_BYTE */, EGL_NONE};
        result->surface_ = ReinterpretHandle<EglHandle>(eglCreatePbufferFromClientBuffer(
            ReinterpretHandle<EGLDisplay>(result->Display()), EGL_IOSURFACE_ANGLE,
            result->rgb_storage_->NativeBuffer(),
            ReinterpretHandle<EGLConfig>(result->display_->Config(true)), attributes));
    } else {
    const EGLint attributes[]{EGL_WIDTH, static_cast<EGLint>(width), EGL_HEIGHT,
        static_cast<EGLint>(height), EGL_TEXTURE_FORMAT, static_cast<EGLint>(texture_format),
        EGL_TEXTURE_TARGET, texture_format == EGL_NO_TEXTURE ? EGL_NO_TEXTURE : EGL_TEXTURE_2D,
        EGL_MIPMAP_TEXTURE, mipmap ? EGL_TRUE : EGL_FALSE, EGL_NONE};
    result->surface_ = ReinterpretHandle<EglHandle>(eglCreatePbufferSurface(
        ReinterpretHandle<EGLDisplay>(result->Display()),
        ReinterpretHandle<EGLConfig>(result->display_->Config(format)), attributes));
    }
#else
    static_cast<void>(texture_format); static_cast<void>(mipmap);
    result->surface_ = api.CreatePbufferSurface(result->Display(), result->display_->Config(rgb), width, height);
#endif
    if (!result->surface_) ThrowLastError(api, EglOperation::create_surface);
    if (format == EglColorFormat::rgb565 && result->display_->UsesPackedRgbFramebuffer()) {
        result->packed_rgb_image_ = result->display_->CreatePackedRgbImage(width,height,result->rgb_storage_);
        if (result->display_->HasPackedDepthStencil())
            result->packed_depth_image_ = result->display_->CreatePackedDepthStencilImage(width,height,result->depth_storage_);
    }
    return result;
}

EglSurfaceResources::~EglSurfaceResources() {
    if (packed_depth_image_) display_->DestroyImage(packed_depth_image_);
    if (packed_rgb_image_) display_->DestroyImage(packed_rgb_image_);
    if (surface_) static_cast<void>(display_->Api().DestroySurface(Display(), surface_));
}

#if OGPLAY_HAS_ANGLE
namespace {
template <typename Function> Function EglExtension(const char* name) {
    const auto function = reinterpret_cast<Function>(eglGetProcAddress(name));
    if (!function) throw EglLifecycleError(EglOperation::unavailable, EGL_BAD_MATCH);
    return function;
}
void CheckEgl(const EGLBoolean result, EglApi& api) {
    if (result != EGL_TRUE) ThrowLastError(api, EglOperation::unavailable);
}
}
#endif

EglHandle EglDisplayResources::Config(EglColorFormat format) const noexcept {
    if (format == EglColorFormat::rgb888) return rgb_config_;
    if (format == EglColorFormat::rgb565) return rgb565_config_ ? rgb565_config_ : config_;
    return config_;
}

EglHandle EglDisplayResources::CreatePackedRgbImage(std::uint32_t width,std::uint32_t height,
    std::unique_ptr<hal::RgbSurfaceStorage>& storage) {
    return CreatePackedImage(width,height,storage,false);
}
EglHandle EglDisplayResources::CreatePackedDepthStencilImage(std::uint32_t width,std::uint32_t height,
    std::unique_ptr<hal::RgbSurfaceStorage>& storage) {
    return CreatePackedImage(width,height,storage,true);
}
EglHandle EglDisplayResources::CreatePackedImage(std::uint32_t width,std::uint32_t height,
    std::unique_ptr<hal::RgbSurfaceStorage>& storage, bool depth_stencil) {
#if OGPLAY_HAS_ANGLE
    storage = depth_stencil ? hal::CreatePackedDepthStencilStorage(packed_rgb_device_,width,height)
                            : hal::CreatePackedRgbSurfaceStorage(packed_rgb_device_,width,height);
    const EGLint attributes[]{EGL_TEXTURE_INTERNAL_FORMAT_ANGLE,depth_stencil ? GL_DEPTH24_STENCIL8 : GL_RGB565,EGL_NONE};
    const auto image = EglExtension<PFNEGLCREATEIMAGEKHRPROC>("eglCreateImageKHR")(
        ReinterpretHandle<EGLDisplay>(display_),EGL_NO_CONTEXT,EGL_METAL_TEXTURE_ANGLE,
        storage->NativeBuffer(),attributes);
    if (!image) ThrowLastError(*api_,EglOperation::create_surface);
    return ReinterpretHandle<EglHandle>(image);
#else
    static_cast<void>(width);static_cast<void>(height);static_cast<void>(storage);static_cast<void>(depth_stencil);
    throw EglLifecycleError(EglOperation::unavailable,0);
#endif
}

void EglDisplayResources::InitializePackedRgbSupport() {
#if OGPLAY_HAS_ANGLE
    if (info_.backend.renderer != AngleRenderer::metal || !hal::HasPackedRgbSurfaceStorage() ||
        Extensions().find("EGL_ANGLE_metal_texture_client_buffer") == std::string::npos) return;
    const auto d=ReinterpretHandle<EGLDisplay>(display_);
    EGLAttrib device{},metal{};
    auto qd=EglExtension<PFNEGLQUERYDISPLAYATTRIBEXTPROC>("eglQueryDisplayAttribEXT");
    auto qa=EglExtension<PFNEGLQUERYDEVICEATTRIBEXTPROC>("eglQueryDeviceAttribEXT");
    if (!qd(d,EGL_DEVICE_EXT,&device) || !qa(reinterpret_cast<EGLDeviceEXT>(device),EGL_METAL_DEVICE_ANGLE,&metal)) return;
    packed_rgb_device_=reinterpret_cast<void*>(metal);
    const auto old_d=eglGetCurrentDisplay(); const auto old_c=eglGetCurrentContext();
    const auto old_draw=eglGetCurrentSurface(EGL_DRAW); const auto old_read=eglGetCurrentSurface(EGL_READ);
    EglHandle context{},surface{},image{},depth_image{};
    std::unique_ptr<hal::RgbSurfaceStorage> depth_storage;
    std::unique_ptr<hal::RgbSurfaceStorage> storage;
    bool supported{};
    try {
        context=api_->CreateContext(display_,config_,2,0);
        surface=api_->CreatePbufferSurface(display_,config_,2,2);
        if (!context || !surface || !api_->MakeCurrent(display_,surface,surface,context))
            throw EglLifecycleError(EglOperation::make_current,api_->GetError());
        const auto extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if (!extensions || !std::strstr(extensions,"GL_OES_EGL_image") || !std::strstr(extensions,"GL_ANGLE_framebuffer_blit"))
            throw EglLifecycleError(EglOperation::unavailable,EGL_BAD_MATCH);
        image=CreatePackedRgbImage(2,2,storage);
        GLuint fbo{},rb{};glGenFramebuffers(1,&fbo);glGenRenderbuffers(1,&rb);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glBindRenderbuffer(GL_RENDERBUFFER,rb);
        auto bind=reinterpret_cast<void(*)(GLenum,void*)>(eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES"));
        bind(GL_RENDERBUFFER,reinterpret_cast<void*>(image));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
        GLint r{},g{},b{},a{};
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_RED_SIZE,&r);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_GREEN_SIZE,&g);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_BLUE_SIZE,&b);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_ALPHA_SIZE,&a);
        const bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
        glDisable(GL_DITHER);glClearColor(.1f,.2f,.3f,.2f);glClear(GL_COLOR_BUFFER_BIT);
        std::array<GLubyte,4> px{};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px.data());
        supported=complete && r==5 && g==6 && b==5 && a==0 && px[0]>=24 && px[0]<=25 &&
            px[1]>=51 && px[1]<=53 && px[2]>=74 && px[2]<=75 && px[3]==255 && glGetError()==GL_NO_ERROR;
        if (supported) {
            GLuint depth_rb{};
            try {
                depth_image=CreatePackedDepthStencilImage(2,2,depth_storage);
                glGenRenderbuffers(1,&depth_rb);glBindRenderbuffer(GL_RENDERBUFFER,depth_rb);
                bind(GL_RENDERBUFFER,reinterpret_cast<void*>(depth_image));
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth_rb);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth_rb);
                GLint depth{},stencil{};
                glGetIntegerv(GL_DEPTH_BITS,&depth);glGetIntegerv(GL_STENCIL_BITS,&stencil);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE &&
                    glGetError()==GL_NO_ERROR && depth==24 && stencil==8) {
                    packed_depth_bits_=depth;packed_stencil_bits_=stencil;
                }
            } catch (const std::exception&) { for (unsigned drain=0;drain<8 && glGetError()!=GL_NO_ERROR;++drain) {} }
            if (depth_rb) glDeleteRenderbuffers(1,&depth_rb);
        }
        glDeleteFramebuffers(1,&fbo);glDeleteRenderbuffers(1,&rb);
    } catch (const std::exception&) { supported=false; }
    if (depth_image) DestroyImage(depth_image);
    depth_storage.reset();
    if (image) DestroyImage(image);
    storage.reset();
    if (old_c!=EGL_NO_CONTEXT) eglMakeCurrent(old_d,old_draw,old_read,old_c);
    else api_->MakeCurrent(display_,0,0,0);
    if (surface) api_->DestroySurface(display_,surface);
    if (context) api_->DestroyContext(display_,context);
    packed_rgb_framebuffer_=supported;
#endif
}

std::int32_t EglDisplayResources::ConfigAttribute(std::uint32_t name,EglColorFormat format) const {
#if OGPLAY_HAS_ANGLE
    if (format != EglColorFormat::rgb565) return ConfigAttribute(name,format==EglColorFormat::rgb888);
    if (!SupportsRgb565Surface()) throw EglLifecycleError(EglOperation::choose_config,EGL_BAD_CONFIG);
    if (packed_rgb_framebuffer_) {
        switch (name) {
        case EGL_RED_SIZE: case EGL_BLUE_SIZE:return 5;
        case EGL_GREEN_SIZE:return 6;
        case EGL_BUFFER_SIZE:return 16;
        case EGL_RENDERABLE_TYPE:case EGL_CONFORMANT:return EGL_OPENGL_ES2_BIT;
        case EGL_DEPTH_SIZE:return packed_depth_bits_;
        case EGL_STENCIL_SIZE:return packed_stencil_bits_;
        case EGL_ALPHA_SIZE:case EGL_SAMPLES:
        case EGL_SAMPLE_BUFFERS:case EGL_BIND_TO_TEXTURE_RGB:case EGL_BIND_TO_TEXTURE_RGBA:return 0;
        default:break;
        }
    }
    EGLint value{};CheckEgl(eglGetConfigAttrib(ReinterpretHandle<EGLDisplay>(display_),
        ReinterpretHandle<EGLConfig>(Config(format)),name,&value),*api_);return value;
#else
    static_cast<void>(name);static_cast<void>(format);throw EglLifecycleError(EglOperation::unavailable,0);
#endif
}

std::string EglDisplayResources::Extensions() const {
#if OGPLAY_HAS_ANGLE
    const auto* text = eglQueryString(ReinterpretHandle<EGLDisplay>(display_), EGL_EXTENSIONS);
    if (!text) ThrowLastError(*api_, EglOperation::unavailable);
    return text;
#else
    return {};
#endif
}

std::int32_t EglDisplayResources::ConfigAttribute(const std::uint32_t name, const bool rgb) const {
#if OGPLAY_HAS_ANGLE
    if (rgb && !SupportsRgbSurface()) throw EglLifecycleError(EglOperation::choose_config, EGL_BAD_CONFIG);
    if (rgb && rgb_client_buffer_) {
        switch (name) {
        case EGL_ALPHA_SIZE: case EGL_BIND_TO_TEXTURE_RGB: case EGL_BIND_TO_TEXTURE_RGBA: return 0;
        case EGL_BUFFER_SIZE: return 24;
        default: break;
        }
    }
    EGLint value{};
    CheckEgl(eglGetConfigAttrib(ReinterpretHandle<EGLDisplay>(display_),
        ReinterpretHandle<EGLConfig>(Config(rgb)), static_cast<EGLint>(name), &value), *api_);
    return value;
#else
    static_cast<void>(name); static_cast<void>(rgb); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

EglHandle EglDisplayResources::CreateSync(const std::uint32_t type,
    const std::span<const std::int32_t> attributes) {
#if OGPLAY_HAS_ANGLE
    const auto sync = EglExtension<PFNEGLCREATESYNCKHRPROC>("eglCreateSyncKHR")(
        ReinterpretHandle<EGLDisplay>(display_), type, attributes.data());
    if (!sync) ThrowLastError(*api_, EglOperation::unavailable);
    return ReinterpretHandle<EglHandle>(sync);
#else
    static_cast<void>(type); static_cast<void>(attributes);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
void EglDisplayResources::DestroySync(const EglHandle sync) {
#if OGPLAY_HAS_ANGLE
    CheckEgl(EglExtension<PFNEGLDESTROYSYNCKHRPROC>("eglDestroySyncKHR")(
        ReinterpretHandle<EGLDisplay>(display_), ReinterpretHandle<EGLSyncKHR>(sync)), *api_);
#else
    static_cast<void>(sync); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
std::uint32_t EglDisplayResources::ClientWaitSync(const EglHandle sync,
    const std::uint32_t flags, const std::uint64_t timeout) {
#if OGPLAY_HAS_ANGLE
    const auto value = EglExtension<PFNEGLCLIENTWAITSYNCKHRPROC>("eglClientWaitSyncKHR")(
        ReinterpretHandle<EGLDisplay>(display_), ReinterpretHandle<EGLSyncKHR>(sync), flags, timeout);
    if (!value) ThrowLastError(*api_, EglOperation::unavailable);
    return static_cast<std::uint32_t>(value);
#else
    static_cast<void>(sync); static_cast<void>(flags); static_cast<void>(timeout);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
std::int32_t EglDisplayResources::SyncAttribute(const EglHandle sync, const std::uint32_t name) {
#if OGPLAY_HAS_ANGLE
    EGLint value{};
    CheckEgl(EglExtension<PFNEGLGETSYNCATTRIBKHRPROC>("eglGetSyncAttribKHR")(
        ReinterpretHandle<EGLDisplay>(display_), ReinterpretHandle<EGLSyncKHR>(sync), name, &value), *api_);
    return value;
#else
    static_cast<void>(sync); static_cast<void>(name); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
void EglDisplayResources::WaitSync(const EglHandle sync, const std::uint32_t flags) {
#if OGPLAY_HAS_ANGLE
    CheckEgl(EglExtension<PFNEGLWAITSYNCKHRPROC>("eglWaitSyncKHR")(
        ReinterpretHandle<EGLDisplay>(display_), ReinterpretHandle<EGLSyncKHR>(sync), static_cast<EGLint>(flags)), *api_);
#else
    static_cast<void>(sync); static_cast<void>(flags); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
EglHandle EglDisplayResources::CreateImage(const EglHandle context,
    const std::uint32_t target, const std::uint32_t buffer,
    const std::span<const std::int32_t> attributes) {
#if OGPLAY_HAS_ANGLE
    const auto image = EglExtension<PFNEGLCREATEIMAGEKHRPROC>("eglCreateImageKHR")(
        ReinterpretHandle<EGLDisplay>(display_), ReinterpretHandle<EGLContext>(context), target,
        ReinterpretHandle<EGLClientBuffer>(static_cast<std::uintptr_t>(buffer)), attributes.data());
    if (!image) ThrowLastError(*api_, EglOperation::unavailable);
    return ReinterpretHandle<EglHandle>(image);
#else
    static_cast<void>(context); static_cast<void>(target); static_cast<void>(buffer); static_cast<void>(attributes);
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
void EglDisplayResources::SignalSync(const EglHandle sync, const std::uint32_t mode) {
#if OGPLAY_HAS_ANGLE
    CheckEgl(EglExtension<PFNEGLSIGNALSYNCKHRPROC>("eglSignalSyncKHR")(
        ReinterpretHandle<EGLDisplay>(display_), ReinterpretHandle<EGLSyncKHR>(sync), mode), *api_);
#else
    static_cast<void>(sync); static_cast<void>(mode); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
void EglDisplayResources::DestroyImage(const EglHandle image) {
#if OGPLAY_HAS_ANGLE
    CheckEgl(EglExtension<PFNEGLDESTROYIMAGEKHRPROC>("eglDestroyImageKHR")(
        ReinterpretHandle<EGLDisplay>(display_), ReinterpretHandle<EGLImageKHR>(image)), *api_);
#else
    static_cast<void>(image); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
void EglDisplayResources::SwapInterval(const std::int32_t interval) {
#if OGPLAY_HAS_ANGLE
    CheckEgl(eglSwapInterval(ReinterpretHandle<EGLDisplay>(display_), interval), *api_);
#else
    static_cast<void>(interval); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
void EglSurfaceResources::BindTexture(const bool bind) {
#if OGPLAY_HAS_ANGLE
    const auto display = ReinterpretHandle<EGLDisplay>(Display());
    const auto surface = ReinterpretHandle<EGLSurface>(surface_);
    CheckEgl(bind ? eglBindTexImage(display, surface, EGL_BACK_BUFFER) :
                   eglReleaseTexImage(display, surface, EGL_BACK_BUFFER), display_->Api());
#else
    static_cast<void>(bind); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
void EglSurfaceResources::SetAttribute(const std::uint32_t name, const std::int32_t value) {
#if OGPLAY_HAS_ANGLE
    CheckEgl(eglSurfaceAttrib(ReinterpretHandle<EGLDisplay>(Display()),
        ReinterpretHandle<EGLSurface>(surface_), static_cast<EGLint>(name), value), display_->Api());
#else
    static_cast<void>(name); static_cast<void>(value); throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}
void EglSurfaceResources::SwapBuffers() {
#if OGPLAY_HAS_ANGLE
    CheckEgl(eglSwapBuffers(ReinterpretHandle<EGLDisplay>(Display()),
        ReinterpretHandle<EGLSurface>(surface_)), display_->Api());
#else
    throw EglLifecycleError(EglOperation::unavailable, 0);
#endif
}

EglLifecycle EglLifecycle::CreateContext(std::shared_ptr<EglDisplayResources> display,
    const int client_version, const EglHandle share_context, const bool rgb) {
    return CreateContext(std::move(display),client_version,share_context,
                         rgb ? EglColorFormat::rgb888 : EglColorFormat::rgba8888);
}

EglLifecycle EglLifecycle::CreateContext(std::shared_ptr<EglDisplayResources> display,
    const int client_version,const EglHandle share_context,const EglColorFormat format) {
    if (format == EglColorFormat::rgb565 && client_version > 2 && display->UsesPackedRgbFramebuffer())
        throw EglLifecycleError(EglOperation::create_context,0x3009U);
    const bool rgb = format == EglColorFormat::rgb888;
    if (format == EglColorFormat::rgb565 && !display->SupportsRgb565Surface())
        throw EglLifecycleError(EglOperation::create_context,0x3005U);
    auto& api = display->Api();
    EglLifecycle result(api, display->Info());
    result.registry_display_ = std::move(display);
    result.display_ = result.registry_display_->Display();
    result.info_.client_version = client_version;
    if (!api.BindOpenGlesApi()) ThrowLastError(api, EglOperation::bind_api);
    if (rgb && !result.registry_display_->SupportsRgbSurface())
        throw EglLifecycleError(EglOperation::create_context, 0x3005U);
    result.context_ = api.CreateContext(result.display_, result.registry_display_->Config(format),
                                        client_version, share_context);
    if (!result.context_) ThrowLastError(api, EglOperation::create_context);
    return result;
}

void EglLifecycle::BindSurfaces(std::shared_ptr<EglSurfaceResources> draw,
                               std::shared_ptr<EglSurfaceResources> read) {
    if (!registry_display_ || !draw || !read || draw->Display() != display_ ||
        read->Display() != display_) throw std::logic_error("EGL surface/display mismatch");
    if (!api_->MakeCurrent(display_, draw->Surface(), read->Surface(), context_))
        ThrowLastError(*api_, EglOperation::make_current);
    draw_ = std::move(draw); read_ = std::move(read);
    info_.width = draw_->Width(); info_.height = draw_->Height();
    current_ = true;
}

EglLifecycle EglLifecycle::CreatePbuffer(EglApi& api,
                                         const AngleBackend backend,
                                         const std::uint32_t width,
                                         const std::uint32_t height,
                                         const int client_version,
                                         const EglHandle share_context) {
    static_cast<void>(AngleBackendName(backend));
    constexpr auto kMaxDimension =
        static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    if (width == 0 || height == 0 || width > kMaxDimension ||
        height > kMaxDimension) {
        throw std::invalid_argument(
            "EGL pbuffer dimensions must fit a positive EGLint");
    }
    if (client_version < 1 || client_version > 3) {
        throw std::invalid_argument("EGL client version must be 1, 2 or 3");
    }
    EglLifecycle lifecycle(api, {.backend = backend,
                                 .client_version = client_version,
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
        lifecycle.display_, config, lifecycle.info_.client_version,
        share_context);
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
      current_(std::exchange(other.current_, false)),
      registry_display_(std::move(other.registry_display_)),
      draw_(std::move(other.draw_)), read_(std::move(other.read_)) {}

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
    registry_display_ = std::move(other.registry_display_);
    draw_ = std::move(other.draw_); read_ = std::move(other.read_);
    return *this;
}

const EglContextInfo& EglLifecycle::Info() const noexcept {
    return info_;
}

bool EglLifecycle::IsCurrent() const noexcept {
    return current_;
}

EglHandle EglLifecycle::NativeDisplay() const noexcept { return display_; }
EglHandle EglLifecycle::NativeContext() const noexcept { return context_; }
EglHandle EglLifecycle::NativeSurface() const noexcept {
    return draw_ ? draw_->Surface() : surface_;
}

void EglLifecycle::BindCurrentOnCallingThread() {
    if (registry_display_) { BindSurfaces(draw_, read_); return; }
    if (api_ == nullptr || display_ == 0 || context_ == 0 || surface_ == 0) {
        throw std::logic_error("EGL lifecycle is not initialized");
    }
    if (current_) {
        throw std::logic_error("EGL lifecycle is already current");
    }
    if (!api_->MakeCurrent(display_, surface_, surface_, context_)) {
        ThrowLastError(*api_, EglOperation::make_current);
    }
    current_ = true;
}

void EglLifecycle::ReleaseCurrent() {
    if (api_ == nullptr || !current_) {
        throw std::logic_error("EGL lifecycle is not current");
    }
    if (!api_->MakeCurrent(display_, 0, 0, 0)) {
        ThrowLastError(*api_, EglOperation::make_current);
    }
    current_ = false;
    if (registry_display_) { draw_.reset(); read_.reset(); }
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
    draw_.reset(); read_.reset(); registry_display_.reset();
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
