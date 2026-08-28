#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/audio/open_sles_pcm_mixer.h"
#include "ogplay/cpu/cpu.h"
#include "ogplay/core/gpu_state.h"
#include "ogplay/gles/angle_backend.h"
#include "ogplay/gles/gles_dispatch.h"
#include "ogplay/runtime/bionic/bionic_profile.h"
#include "ogplay/runtime/boundary/opensles_callback.h"
#include "ogplay/runtime/supervisor_call_progress.h"

namespace ogplay::core {
class Logger;
}

namespace ogplay::runtime {

enum class AndroidBoundaryInputType : std::uint8_t {
    key,
    pointer_motion,
    pointer_button,
};

struct AndroidBoundaryInput final {
    AndroidBoundaryInputType type{};
    std::int32_t code{};
    float x{};
    float y{};
    bool pressed{};
    std::int32_t device_id{};
    std::int32_t scan_code{};
    std::int32_t unicode_char{};
    std::int32_t meta_state{};
    std::int32_t repeat_count{};
    std::int64_t event_time_ms{};
};

struct AndroidBoundaryFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
    std::vector<std::uint8_t> rgba8;
};

struct BionicDynamicLinkHooks final {
    void* owner{};
    std::uint32_t (*open)(void*, std::string_view, std::uint32_t,
                          std::uint64_t){};
    std::uint32_t (*symbol)(void*, std::uint32_t, std::string_view,
                            std::uint64_t){};
    std::int32_t (*close)(void*, std::uint32_t, std::uint64_t){};
};

struct AndroidBoundaryOptions final {
    bool allow_gles1_material_single_face{};
    bool allow_gles1_single_stage_texcoord_fallback{true};
    core::Logger* logger{};
    void* guest_file_owner{};
    bool (*read_guest_file)(void*, std::string_view,
                            std::vector<std::byte>&){};
    BionicDynamicLinkHooks dynamic_link{};
    OpenSlesCallbackSink open_sles_callbacks{};
};

class AndroidBoundaryHle final : public core::GpuStateProvider {
public:
    AndroidBoundaryHle(memory::AddressSpace& address_space,
                       gles::AngleBackend backend,
                       std::uint32_t width, std::uint32_t height,
                       std::uint32_t supersample_factor = 1,
                       AndroidBoundaryOptions options = {});
    ~AndroidBoundaryHle();
    AndroidBoundaryHle(const AndroidBoundaryHle&) = delete;
    AndroidBoundaryHle& operator=(const AndroidBoundaryHle&) = delete;

    void MapThunks();
    void OpenManagedSurface();
    void BindManagedSurfaceOnCallingThread();
    void ReleaseManagedSurfaceFromCallingThread();
    [[nodiscard]] bool ManagedSurfaceIsOpen() const noexcept;
    [[nodiscard]] std::string ManagedGlString(std::uint32_t parameter);
    [[nodiscard]] std::uint32_t InvokeManagedGles(
        gles::GlesApi api, std::string_view name,
        std::span<const std::uint32_t> arguments,
        std::uint64_t thread_id = 0);
    void PresentManagedSurface();
    void CloseManagedSurface();
    [[nodiscard]] const BionicHleSymbolProvider& Symbols() const noexcept;
    [[nodiscard]] cpu::HostCallHook FastHostCallHook() noexcept;
    [[nodiscard]] SupervisorCallProgress HandleWithProgress(
        cpu::Cpu& cpu, const cpu::RunResult& stopped);
    // Compatibility wrapper for boundary-only consumers.
    [[nodiscard]] bool Handle(cpu::Cpu& cpu, const cpu::RunResult& stopped);
    void NotifyFileWrite();
    void PushInput(const AndroidBoundaryInput& input);
    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame();
    // Publishes a host-composed RGBA frame (logical surface size) into the
    // same frame store and sequence as GL presents.
    void PublishSoftwareFrame(std::vector<std::uint8_t> rgba8);
    void RecycleFrame(AndroidBoundaryFrame&& frame);
    [[nodiscard]] std::vector<audio::OpenSlesConsumedBuffer>
    MixOpenSlesPcm16(std::span<std::int16_t> output,
                     std::uint32_t output_rate);
    [[nodiscard]] audio::OpenSlesPcmMixer& PcmPlayback() noexcept;
    [[nodiscard]] core::GpuStats Stats() const override;
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const override;
    [[nodiscard]] core::GpuCapabilities Capabilities() const override;
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        std::string_view filter, std::size_t limit) const override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ogplay::runtime
