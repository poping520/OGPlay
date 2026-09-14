#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <span>
#include <string>

#include "ogplay/gles/angle_backend.h"

namespace ogplay::gles {

using EglHandle = std::uintptr_t;

enum class EglOperation {
    unavailable,
    get_platform_display,
    initialize,
    choose_config,
    bind_api,
    create_context,
    create_surface,
    make_current,
};

class EglLifecycleError final : public std::runtime_error {
public:
    EglLifecycleError(EglOperation operation, std::uint32_t native_error);

    [[nodiscard]] EglOperation Operation() const noexcept;
    [[nodiscard]] std::uint32_t NativeError() const noexcept;

private:
    EglOperation operation_;
    std::uint32_t native_error_;
};

class EglApi {
public:
    virtual ~EglApi() = default;

    [[nodiscard]] virtual EglHandle GetPlatformDisplay(
        AngleBackend backend) = 0;
    virtual bool Initialize(EglHandle display, int& major, int& minor) = 0;
    virtual bool ChoosePbufferConfig(EglHandle display,
                                     EglHandle& config) = 0;
    virtual bool BindOpenGlesApi() = 0;
    [[nodiscard]] virtual EglHandle CreateContext(EglHandle display,
                                                  EglHandle config,
                                                  int client_version,
                                                  EglHandle share_context) = 0;
    [[nodiscard]] virtual EglHandle CreatePbufferSurface(
        EglHandle display, EglHandle config, std::uint32_t width,
        std::uint32_t height) = 0;
    virtual bool MakeCurrent(EglHandle display, EglHandle draw_surface,
                             EglHandle read_surface,
                             EglHandle context) = 0;
    virtual bool DestroySurface(EglHandle display, EglHandle surface) = 0;
    virtual bool DestroyContext(EglHandle display, EglHandle context) = 0;
    virtual bool Terminate(EglHandle display) = 0;
    [[nodiscard]] virtual std::uint32_t GetError() = 0;
};

struct EglContextInfo final {
    AngleBackend backend{};
    int egl_major{};
    int egl_minor{};
    int client_version{};
    std::uint32_t width{};
    std::uint32_t height{};

    bool operator==(const EglContextInfo&) const = default;
};

// Registry-owned display and surfaces. Contexts may outlive the handle of
// their share source, and surfaces are not owned by any particular context.
class EglDisplayResources final {
public:
    static std::shared_ptr<EglDisplayResources> Create(AngleBackend backend);
    ~EglDisplayResources();
    EglApi& Api() const noexcept { return *api_; }
    EglHandle Display() const noexcept { return display_; }
    EglHandle Config() const noexcept { return config_; }
    const EglContextInfo& Info() const noexcept { return info_; }
    std::string Extensions() const;
    std::int32_t ConfigAttribute(std::uint32_t name) const;
    EglHandle CreateSync(std::uint32_t type, std::span<const std::int32_t> attributes);
    void DestroySync(EglHandle sync);
    std::uint32_t ClientWaitSync(EglHandle sync, std::uint32_t flags, std::uint64_t timeout);
    std::int32_t SyncAttribute(EglHandle sync, std::uint32_t name);
    void WaitSync(EglHandle sync, std::uint32_t flags);
    void SignalSync(EglHandle sync, std::uint32_t mode);
    EglHandle CreateImage(EglHandle context, std::uint32_t target, std::uint32_t buffer,
                          std::span<const std::int32_t> attributes);
    void DestroyImage(EglHandle image);
    void SwapInterval(std::int32_t interval);
private:
    EglDisplayResources() = default;
    std::unique_ptr<EglApi> api_;
    EglHandle display_{};
    EglHandle config_{};
    EglContextInfo info_{};
    bool initialized_{};
};

class EglSurfaceResources final {
public:
    static std::shared_ptr<EglSurfaceResources> Create(
        std::shared_ptr<EglDisplayResources> display,
        std::uint32_t width, std::uint32_t height,
        std::uint32_t texture_format = 0x305CU, bool mipmap = false);
    ~EglSurfaceResources();
    EglHandle Surface() const noexcept { return surface_; }
    EglHandle Display() const noexcept { return display_->Display(); }
    std::uint32_t Width() const noexcept { return width_; }
    std::uint32_t Height() const noexcept { return height_; }
    void BindTexture(bool bind);
    void SetAttribute(std::uint32_t name, std::int32_t value);
    void SwapBuffers();
private:
    EglSurfaceResources() = default;
    std::shared_ptr<EglDisplayResources> display_;
    EglHandle surface_{};
    std::uint32_t width_{}, height_{};
};

class EglLifecycle final {
public:
    static EglLifecycle CreateContext(std::shared_ptr<EglDisplayResources> display,
        int client_version, EglHandle share_context = 0);
    void BindSurfaces(std::shared_ptr<EglSurfaceResources> draw,
                      std::shared_ptr<EglSurfaceResources> read);
    static EglLifecycle CreatePbuffer(EglApi& api, AngleBackend backend,
                                      std::uint32_t width,
                                      std::uint32_t height,
                                      int client_version = 2,
                                      EglHandle share_context = 0);

    ~EglLifecycle();
    EglLifecycle(const EglLifecycle&) = delete;
    EglLifecycle& operator=(const EglLifecycle&) = delete;
    EglLifecycle(EglLifecycle&& other) noexcept;
    EglLifecycle& operator=(EglLifecycle&& other) noexcept;

    [[nodiscard]] const EglContextInfo& Info() const noexcept;
    [[nodiscard]] bool IsCurrent() const noexcept;
    [[nodiscard]] EglHandle NativeDisplay() const noexcept;
    [[nodiscard]] EglHandle NativeContext() const noexcept;
    [[nodiscard]] EglHandle NativeSurface() const noexcept;
    void BindCurrentOnCallingThread();
    void ReleaseCurrent();
    void MarkNotCurrent() noexcept { current_ = false; draw_.reset(); read_.reset(); }

private:
    EglLifecycle(EglApi& api, EglContextInfo info) noexcept;
    void Reset() noexcept;

    EglApi* api_{};
    EglContextInfo info_{};
    EglHandle display_{};
    EglHandle context_{};
    EglHandle surface_{};
    bool initialized_{};
    bool current_{};
    std::shared_ptr<EglDisplayResources> registry_display_;
    std::shared_ptr<EglSurfaceResources> draw_, read_;
};

[[nodiscard]] bool IsNativeAngleEglAvailable() noexcept;
[[nodiscard]] std::unique_ptr<EglApi> CreateNativeAngleEglApi();

}  // namespace ogplay::gles
