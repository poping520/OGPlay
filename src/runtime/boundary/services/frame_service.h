#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ogplay/core/gpu_state.h"
#include "ogplay/gles/angle_frame.h"
#include "ogplay/gles/angle_readback.h"
#include "ogplay/gles/supersample.h"
#include "ogplay/runtime/boundary/android_boundary_hle.h"
#include "runtime/boundary/core/boundary_symbols.h"

namespace ogplay::runtime {

class FrameService final {
public:
    FrameService(const gles::SupersampleLayout& layout,
                 std::span<const detail::HleThunkDescriptor> descriptors,
                 bool async_readback = false) noexcept;
    ~FrameService();

    [[nodiscard]] std::optional<AndroidBoundaryFrame> TakeLatestFrame();
    void PublishSoftwareFrame(std::vector<std::uint8_t> rgba8);
    void PublishAngleFrame(gles::AngleFrame& frame);
    void RecycleFrame(AndroidBoundaryFrame&& frame);
    void SetReadbackFilter(FrameReadbackFilter covered);
    void ResetReadback();

    void SetRenderTargetReady(bool ready);
    void RecordDraw();
    void RecordClear();
    void RecordShaderCompile();
    void RecordProgramLink();
    void RecordGpuCall(std::size_t descriptor_index,
                       const std::array<std::uint32_t, 4>& arguments,
                       bool gpu, std::uint32_t error = 0U);

    [[nodiscard]] std::optional<core::GpuStats> TryStats() const;
    [[nodiscard]] core::GpuStats Stats() const;
    [[nodiscard]] std::vector<core::GpuRenderTarget> RenderTargets() const;
    [[nodiscard]] std::vector<core::GpuTraceEntry> Trace(
        std::string_view filter, std::size_t limit) const;
    [[nodiscard]] std::optional<std::vector<core::GpuTraceEntry>> TryTrace(
        std::size_t limit) const;

private:
    void PublishReadback(std::vector<std::uint8_t> pixels, std::uint64_t epoch);
    struct RawGpuTraceEntry final {
        std::uint16_t descriptor_index{};
        std::array<std::uint32_t, 4> registers{};
        std::uint32_t error{};
    };

    const gles::SupersampleLayout& layout_;
    std::span<const detail::HleThunkDescriptor> descriptors_;
    mutable std::mutex mutex_;
    mutable std::mutex trace_mutex_;
    std::uint64_t frame_sequence_{};
    std::optional<AndroidBoundaryFrame> latest_frame_;
    std::vector<std::uint8_t> recycled_rgba8_;
    core::GpuStats gpu_stats_{0, 0, 0, 0, 0, {{0, 0, "color0"}}};
    std::array<RawGpuTraceEntry, 2048> gpu_trace_{};
    std::size_t gpu_trace_write_{};
    std::size_t gpu_trace_count_{};
    bool gpu_render_target_ready_{};
    FrameReadbackFilter readback_filter_;
    bool async_enabled_{};
    std::uint64_t readback_epoch_{};
    std::shared_ptr<gles::AsyncAngleReadback> async_readback_;
};

}  // namespace ogplay::runtime
