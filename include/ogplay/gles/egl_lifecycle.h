#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>

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
                                                  int client_version) = 0;
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

class EglLifecycle final {
public:
    static EglLifecycle CreatePbuffer(EglApi& api, AngleBackend backend,
                                      std::uint32_t width,
                                      std::uint32_t height);

    ~EglLifecycle();
    EglLifecycle(const EglLifecycle&) = delete;
    EglLifecycle& operator=(const EglLifecycle&) = delete;
    EglLifecycle(EglLifecycle&& other) noexcept;
    EglLifecycle& operator=(EglLifecycle&& other) noexcept;

    [[nodiscard]] const EglContextInfo& Info() const noexcept;
    [[nodiscard]] bool IsCurrent() const noexcept;

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
};

[[nodiscard]] bool IsNativeAngleEglAvailable() noexcept;
[[nodiscard]] std::unique_ptr<EglApi> CreateNativeAngleEglApi();

}  // namespace ogplay::gles
