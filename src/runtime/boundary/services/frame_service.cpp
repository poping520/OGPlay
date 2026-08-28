#include "runtime/boundary/services/frame_service.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace ogplay::runtime {

FrameService::FrameService(
    const gles::SupersampleLayout& layout,
    const std::span<const detail::HleThunkDescriptor> descriptors) noexcept
    : layout_(layout), descriptors_(descriptors) {}

std::optional<AndroidBoundaryFrame> FrameService::TakeLatestFrame() {
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
    AndroidBoundaryFrame frame{layout_.logical_width, layout_.logical_height,
                               0, std::move(rgba8)};
    std::scoped_lock lock(mutex_);
    frame.sequence = ++frame_sequence_;
    latest_frame_ = std::move(frame);
}

void FrameService::PublishAngleFrame(gles::AngleFrame& angle_frame) {
    std::vector<std::uint8_t> readback;
    if (layout_.factor == 1U) {
        std::scoped_lock lock(mutex_);
        readback = std::move(recycled_rgba8_);
    }
    angle_frame.ReadRgba8(readback);
    AndroidBoundaryFrame frame{
        layout_.logical_width, layout_.logical_height, 0,
        gles::ResolveSupersampledRgba8(std::move(readback), layout_)};
    std::scoped_lock lock(mutex_);
    frame.sequence = ++frame_sequence_;
    if (layout_.factor == 1U && latest_frame_.has_value() &&
        latest_frame_->rgba8.capacity() >= recycled_rgba8_.capacity()) {
        recycled_rgba8_ = std::move(latest_frame_->rgba8);
    }
    latest_frame_ = std::move(frame);
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
    const std::array<std::uint32_t, 4>& arguments, const bool gpu) {
    if (!gpu) return;
    if (descriptor_index >= descriptors_.size() ||
        descriptor_index > (std::numeric_limits<std::uint16_t>::max)()) {
        throw std::logic_error("GPU trace descriptor is outside its catalog");
    }
    std::scoped_lock lock(trace_mutex_);
    gpu_trace_[gpu_trace_write_] = {
        static_cast<std::uint16_t>(descriptor_index), arguments};
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
