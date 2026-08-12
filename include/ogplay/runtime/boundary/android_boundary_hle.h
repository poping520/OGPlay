#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "ogplay/cpu/cpu.h"
#include "ogplay/core/gpu_state.h"
#include "ogplay/gles/angle_backend.h"
#include "ogplay/runtime/bionic/bionic_profile.h"

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
};

struct AndroidBoundaryFrame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t sequence{};
    std::vector<std::uint8_t> rgba8;
};

struct AndroidBoundaryOptions final {
    bool allow_gles1_material_single_face{};
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
    void PresentManagedSurface();
    void CloseManagedSurface();
    [[nodiscard]] const BionicHleSymbolProvider& Symbols() const noexcept;
    [[nodiscard]] bool Handle(cpu::Cpu& cpu, const cpu::RunResult& stopped);
    void NotifyFileWrite();
    void PushInput(const AndroidBoundaryInput& input);
    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame();
    // Publishes a host-composed RGBA frame (logical surface size) into the
    // same frame store and sequence as GL presents.
    void PublishSoftwareFrame(std::vector<std::uint8_t> rgba8);
    void RecycleFrame(AndroidBoundaryFrame&& frame);
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
