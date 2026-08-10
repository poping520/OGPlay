#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/loader/module_loader.h"
#include "ogplay/runtime/execution/guest_thread_runner.h"
#include "ogplay/runtime/framework/framework_asset.h"
#include "ogplay/runtime/integration/android_boundary_hle.h"
#include "ogplay/runtime/jni/jni_class_registry.h"
#include "ogplay/runtime/jni/jni_environment.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime {

class JniInvocationEngine;
class FrameworkScreenPolicyState;

class AndroidGuestProcessState final {
public:
    void RequestExit() noexcept;
    [[nodiscard]] bool ExitRequested() const noexcept;
    [[nodiscard]] std::uint64_t ExitRequestCount() const noexcept;

private:
    std::atomic<bool> exit_requested_{};
    std::atomic<std::uint64_t> exit_request_count_{};
};

void BindAndroidGuestJavaAudioHandlers(
    JniInvocationEngine& invocations,
    audio::JavaSoundPoolState& sound_pool,
    audio::JavaSoundPoolMixer* mixer = nullptr);

void BindAndroidGuestJavaDisplayHandlers(
    JniInvocationEngine& invocations,
    FrameworkScreenPolicyState& screen_policy);

void BindAndroidGuestJavaProcessHandlers(
    JniInvocationEngine& invocations,
    AndroidGuestProcessState& process_state);

struct AndroidGuestCallSessionRequest final {
    std::uint32_t api{19};
    std::string root_module;
    std::span<const loader::Elf32ModuleInput> modules;
    gles::AngleBackend backend;
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t maximum_ticks_per_call{UINT64_C(200000000)};
    std::uint32_t supersample_factor{1};
    VirtualFileSystem* filesystem{};
    std::function<void(std::string_view)> progress;
    std::optional<FrameworkDirectAssetImplementations> direct_assets{};
    AndroidBoundaryOptions boundary_options{};
    audio::JavaSoundPoolMixer::EncodedResourceLoader sound_resource_loader{};
};

class AndroidGuestCallSessionError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class AndroidGuestCallSession final : public core::GpuStateProvider {
public:
    [[nodiscard]] static std::unique_ptr<AndroidGuestCallSession> Start(
        const AndroidGuestCallSessionRequest& request);
    ~AndroidGuestCallSession();
    AndroidGuestCallSession(const AndroidGuestCallSession&) = delete;
    AndroidGuestCallSession& operator=(const AndroidGuestCallSession&) = delete;

    [[nodiscard]] A32GuestCallResult Invoke(const A32GuestCallFrame& frame);
    [[nodiscard]] memory::GuestAddress GuestEnvironment() const noexcept;
    [[nodiscard]] memory::GuestAddress GuestJavaVm() const noexcept;
    [[nodiscard]] JniEnvironment& Environment() noexcept;
    [[nodiscard]] JniClassRegistry& Classes() noexcept;
    void OpenManagedSurface();
    void PresentManagedSurface();
    void CloseManagedSurface();
    void PushInput(const AndroidBoundaryInput& input);
    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame();
    void RecycleFrame(AndroidBoundaryFrame&& frame);
    [[nodiscard]] std::size_t RenderStereoAudio(
        std::span<std::int16_t> output, std::uint32_t sample_rate);
    void Stop();
    [[nodiscard]] bool Running() const noexcept;
    [[nodiscard]] bool ExitRequested() const noexcept;

    [[nodiscard]] core::GpuStats Stats() const override;
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const override;
    [[nodiscard]] core::GpuCapabilities Capabilities() const override;
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        std::string_view filter, std::size_t limit) const override;

private:
    class Impl;
    explicit AndroidGuestCallSession(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
