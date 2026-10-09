#include "runtime/boundary/services/frame_service.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace ogplay::runtime {

FrameService::FrameService(
    const gles::SupersampleLayout& layout,
    const std::span<const detail::HleThunkDescriptor> descriptors,
    const bool async_readback) noexcept
    : layout_(layout), descriptors_(descriptors), async_enabled_(async_readback) {}

FrameService::~FrameService() {
    try { ResetReadback(); } catch (...) { /* Explicit lifecycle paths report failures. */ }
}

void FrameService::ResetReadback() {
    std::shared_ptr<gles::AsyncAngleReadback> retired;
    {
        std::scoped_lock lock(mutex_);
        ++readback_epoch_;
        retired = std::move(async_readback_);
    }
    // No frame-store lock across worker join or callback delivery.
    if (retired) {
        retired->Stop();
        retired->RethrowFailure();
    }
}

std::optional<AndroidBoundaryFrame> FrameService::TakeLatestFrame() {
    std::shared_ptr<gles::AsyncAngleReadback> readback;
    {
        std::scoped_lock lock(mutex_);
        readback = async_readback_;
    }
    if (readback) readback->RethrowFailure();
    std::scoped_lock lock(mutex_);
    auto result = std::move(latest_frame_);
    latest_frame_.reset();
    return result;
}

void FrameService::PublishSoftwareFrame(std::vector<std::uint8_t> rgba8) {
    const auto expected = static_cast<std::size_t>(layout_.logical_width) *
                          layout_.logical_height * 4U;
    if (rgba8.size() != expected) {
        throw std::invalid_argument(
            "software frame does not match the logical surface layout");
    }
    ResetReadback();
    AndroidBoundaryFrame frame{layout_.logical_width, layout_.logical_height,
                               0, std::move(rgba8)};
    std::scoped_lock lock(mutex_);
    frame.sequence = ++frame_sequence_;
    latest_frame_ = std::move(frame);
}

void FrameService::PublishAngleFrame(gles::AngleFrame& angle_frame) {
    FrameReadbackFilter covered;
    {
        std::scoped_lock lock(mutex_);
        covered = readback_filter_;
    }
    if (covered.covered && covered.covered(covered.owner)) { ResetReadback(); return; }
    std::shared_ptr<gles::AsyncAngleReadback> pipeline;
    {
        std::scoped_lock lock(mutex_);
        pipeline = async_readback_;
    }
    if (pipeline && !pipeline->Matches(angle_frame)) {
        ResetReadback();
        pipeline.reset();
    }
    if (pipeline) {
        static_cast<void>(pipeline->Enqueue());
        return;
    }
    std::vector<std::uint8_t> readback;
    if (layout_.factor == 1U) {
        std::scoped_lock lock(mutex_);
        readback = std::move(recycled_rgba8_);
    }
    std::uint64_t epoch{};
    {
        std::scoped_lock lock(mutex_);
        epoch = readback_epoch_;
    }
    angle_frame.ReadRgba8(readback);
    PublishReadback(std::move(readback), epoch);
    // First frame is current and synchronous. The next swap queues frame 2;
    // only its completion publishes it, so frame 1 is never counted twice.
    bool current_epoch{};
    { std::scoped_lock lock(mutex_); current_epoch = epoch == readback_epoch_; }
    if (async_enabled_ && current_epoch) {
        auto new_pipeline = gles::AsyncAngleReadback::Create(angle_frame,
            [this, epoch](std::vector<std::uint8_t> pixels) {
                PublishReadback(std::move(pixels), epoch);
            });
        std::scoped_lock lock(mutex_);
        if (epoch == readback_epoch_) async_readback_ = std::move(new_pipeline);
    }
}

void FrameService::PublishReadback(std::vector<std::uint8_t> readback,
                                 const std::uint64_t epoch) {
    AndroidBoundaryFrame frame{
        layout_.logical_width, layout_.logical_height, 0,
        gles::ResolveSupersampledRgba8(std::move(readback), layout_)};
    std::scoped_lock lock(mutex_);
    if (epoch != readback_epoch_) return;
    frame.sequence = ++frame_sequence_;
    if (layout_.factor == 1U && latest_frame_.has_value() &&
        latest_frame_->rgba8.capacity() >= recycled_rgba8_.capacity()) {
        recycled_rgba8_ = std::move(latest_frame_->rgba8);
    }
    latest_frame_ = std::move(frame);
}

void FrameService::SetReadbackFilter(FrameReadbackFilter covered) {
    std::scoped_lock lock(mutex_);
    readback_filter_ = std::move(covered);
}

void FrameService::RecycleFrame(AndroidBoundaryFrame&& frame) {
    const auto expected = static_cast<std::size_t>(layout_.logical_width) *
                          layout_.logical_height * 4U;
    if (frame.width != layout_.logical_width ||
        frame.height != layout_.logical_height || frame.rgba8.size() != expected) {
        throw std::invalid_argument(
            "recycled Android boundary frame layout does not match");
    }
    if (layout_.factor != 1U) return;
    std::scoped_lock lock(mutex_);
    if (frame.rgba8.capacity() >= recycled_rgba8_.capacity()) {
        recycled_rgba8_ = std::move(frame.rgba8);
    }
}

void FrameService::SetRenderTargetReady(const bool ready) {
    if (!ready) ResetReadback();
    std::scoped_lock lock(mutex_);
    gpu_render_target_ready_ = ready;
}
void FrameService::RecordDraw() {
    std::scoped_lock lock(mutex_);
    ++gpu_stats_.draws;
    ++gpu_stats_.draw_targets.front().draws;
}
void FrameService::RecordClear() {
    std::scoped_lock lock(mutex_);
    ++gpu_stats_.clears;
}
void FrameService::RecordShaderCompile() {
    std::scoped_lock lock(mutex_);
    ++gpu_stats_.shader_compiles;
}
void FrameService::RecordProgramLink() {
    std::scoped_lock lock(mutex_);
    ++gpu_stats_.program_links;
}

void FrameService::RecordGpuCall(
    const std::size_t descriptor_index,
    const std::array<std::uint32_t, 4>& arguments, const bool gpu,
    const std::uint32_t error) {
    if (!gpu) return;
    if (descriptor_index >= descriptors_.size() ||
        descriptor_index > (std::numeric_limits<std::uint16_t>::max)()) {
        throw std::logic_error("GPU trace descriptor is outside its catalog");
    }
    if (error != 0U) {
        std::scoped_lock lock(mutex_);
        ++gpu_stats_.gl_errors;
    }
    std::scoped_lock lock(trace_mutex_);
    gpu_trace_[gpu_trace_write_] = {
        static_cast<std::uint16_t>(descriptor_index), arguments, error};
    gpu_trace_write_ = (gpu_trace_write_ + 1U) % gpu_trace_.size();
    gpu_trace_count_ = std::min(gpu_trace_count_ + 1U, gpu_trace_.size());
}

core::GpuStats FrameService::Stats() const {
    std::scoped_lock lock(mutex_);
    return gpu_stats_;
}

std::vector<core::GpuRenderTarget> FrameService::RenderTargets() const {
    std::scoped_lock lock(mutex_);
    if (!gpu_render_target_ready_) return {};
    return {{0, layout_.render_width, layout_.render_height,
             "RGBA8", {"color0"}, false}};
}

std::vector<core::GpuTraceEntry> FrameService::Trace(
    const std::string_view filter, const std::size_t limit) const {
    std::scoped_lock lock(trace_mutex_);
    std::vector<core::GpuTraceEntry> result;
    const auto available = std::min(gpu_trace_count_, gpu_trace_.size());
    result.reserve(std::min(limit, available));
    for (std::size_t offset = 0; offset < available && result.size() < limit;
         ++offset) {
        const auto index =
            (gpu_trace_write_ + gpu_trace_.size() - 1U - offset) %
            gpu_trace_.size();
        const auto& raw = gpu_trace_[index];
        const auto name = descriptors_[raw.descriptor_index].name;
        if (!filter.empty() && name.find(filter) == std::string_view::npos) {
            continue;
        }
        core::GpuTraceEntry entry;
        if (raw.error != 0U) entry.error = raw.error;
        entry.call = name;
        for (std::size_t argument = 0; argument < raw.registers.size();
             ++argument) {
            entry.arguments.emplace("r" + std::to_string(argument),
                                    std::to_string(raw.registers[argument]));
        }
        result.push_back(std::move(entry));
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::optional<std::vector<core::GpuTraceEntry>> FrameService::TryTrace(
    const std::size_t limit) const {
    std::unique_lock lock(trace_mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return std::nullopt;
    std::vector<core::GpuTraceEntry> result;
    const auto available = std::min(gpu_trace_count_, gpu_trace_.size());
    result.reserve(std::min(limit, available));
    for (std::size_t offset = 0; offset < available && result.size() < limit;
         ++offset) {
        const auto index =
            (gpu_trace_write_ + gpu_trace_.size() - 1U - offset) %
            gpu_trace_.size();
        const auto& raw = gpu_trace_[index];
        core::GpuTraceEntry entry;
        if (raw.error != 0U) entry.error = raw.error;
        entry.call = descriptors_[raw.descriptor_index].name;
        for (std::size_t argument = 0; argument < raw.registers.size();
             ++argument) {
            entry.arguments.emplace("r" + std::to_string(argument),
                                    std::to_string(raw.registers[argument]));
        }
        result.push_back(std::move(entry));
    }
    std::reverse(result.begin(), result.end());
    return result;
}

}  // namespace ogplay::runtime

namespace ogplay::runtime {
std::optional<core::GpuStats> FrameService::TryStats() const {
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return std::nullopt;
    return core::GpuStats{gpu_stats_.draws, gpu_stats_.clears, gpu_stats_.shader_compiles, gpu_stats_.program_links, gpu_stats_.gl_errors, {}};
}
}
