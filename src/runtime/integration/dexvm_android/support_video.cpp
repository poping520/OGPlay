// Guest video pump (ADR-0021): publishes decoded frames, mixes video PCM
// into the session stereo buffer, and fires onCompletion at the frame
// boundary. The VideoView intrinsic handlers live in the per-class
// declaration file (android_widget_VideoView.cpp); the playback-position
// math and listener invocation they share with this pump are in shared.h.

#include <algorithm>
#include <stdexcept>

#include "ogplay/video/rgba_canvas.h"

#include "shared.h"

namespace ogplay::runtime {

namespace {

void SaturatingAdd(std::int16_t& destination, const std::int16_t value) {
    const auto sum = static_cast<std::int32_t>(destination) + value;
    destination = static_cast<std::int16_t>(
        std::clamp<std::int32_t>(sum, INT16_MIN, INT16_MAX));
}

// Mixes one view's audio into the stereo buffer. Nearest-neighbour
// resampling with an integer phase accumulator: every output frame maps to
// the current source frame; the phase advances by the source rate and
// consumes source frames whenever it crosses the output rate, so batches of
// any size stay drift-free.
bool MixOneVideoView(DexVmAndroidContext::VideoViewState& state,
                     const std::span<std::int16_t> output,
                     const std::uint32_t output_rate) {
    const auto& metadata = state.player->Metadata();
    if (!metadata.HasAudio()) return false;
    const auto channels =
        static_cast<std::size_t>(metadata.audio_channels);
    const auto source_rate = metadata.audio_sample_rate;
    const auto output_frames = output.size() / 2U;

    // Source frames touched by this batch (index of the last used frame,
    // relative to the carry-inclusive stream position).
    const auto last_used = static_cast<std::size_t>(
        (state.pcm_phase +
         static_cast<std::uint64_t>(output_frames - 1U) * source_rate) /
        output_rate);
    const bool has_carry = !state.pcm_carry.empty();
    const auto fetch_frames = last_used + 1U - (has_carry ? 1U : 0U);
    std::vector<std::int16_t> source((has_carry ? 1U : 0U) * channels);
    if (has_carry) {
        std::copy(state.pcm_carry.begin(), state.pcm_carry.end(),
                  source.begin());
    }
    if (fetch_frames > 0U) {
        std::vector<std::int16_t> fetched(fetch_frames * channels);
        const auto got = state.player->ReadPcm(fetched);
        fetched.resize(got * channels);
        source.insert(source.end(), fetched.begin(), fetched.end());
    }
    const auto available = source.size() / channels;
    if (available == 0U) return false;

    auto phase = state.pcm_phase;
    std::size_t index = 0;
    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        if (index >= available) break;  // end of audio: silence remains
        const auto* samples = source.data() + index * channels;
        const auto left = samples[0];
        const auto right = channels >= 2U ? samples[1] : samples[0];
        SaturatingAdd(output[frame * 2U], left);
        SaturatingAdd(output[frame * 2U + 1U], right);
        phase += source_rate;
        while (phase >= output_rate) {
            phase -= output_rate;
            ++index;
        }
    }
    state.pcm_phase = phase;
    // The frame the next batch starts on is still buffered locally; carry
    // it so the player cursor never rewinds.
    if (index < available) {
        state.pcm_carry.assign(source.begin() + static_cast<std::ptrdiff_t>(
                                                    index * channels),
                               source.begin() + static_cast<std::ptrdiff_t>(
                                                    (index + 1U) * channels));
    } else {
        state.pcm_carry.clear();
    }
    return true;
}

}  // namespace

bool AnyVideoPlaying(const DexVmAndroidContext& context) {
    for (const auto& [handle, state] : context.video_views) {
        if (state.player != nullptr && state.playing) return true;
    }
    return false;
}

std::size_t MixVideoPcmIntoStereo(
    DexVmAndroidContext& context,
    const std::span<std::int16_t> interleaved_stereo,
    const std::uint32_t output_rate) {
    if (output_rate == 0U || interleaved_stereo.empty() ||
        interleaved_stereo.size() % 2U != 0U) {
        throw std::invalid_argument(
            "video PCM mix needs a non-empty stereo buffer and a positive "
            "rate");
    }
    std::size_t contributed = 0;
    for (auto& [handle, state] : context.video_views) {
        if (state.player == nullptr || !state.playing) continue;
        if (MixOneVideoView(state, interleaved_stereo, output_rate)) {
            ++contributed;
        }
    }
    return contributed;
}

std::optional<std::string> PumpVideoViews(
    dexvm::Interpreter& vm, DexVmAndroidContext& context,
    const std::function<void(std::vector<std::uint8_t> rgba8)>& publish) {
    // Missing/unopenable streams complete asynchronously just like a real
    // MediaPlayer event. Clear the snapshot before invoking guest callbacks:
    // a callback may stop or restart the same view.
    std::vector<std::uint64_t> pending(
        context.pending_video_completion.begin(),
        context.pending_video_completion.end());
    context.pending_video_completion.clear();
    for (const auto handle : pending) {
        const auto error = android_intrinsics::InvokeVideoCompletionListener(
            vm, context, handle);
        if (error.has_value()) return error;
    }

    // Handle list first: onCompletion may mutate video_views (stopPlayback,
    // replay), which must not invalidate the iteration.
    std::vector<std::uint64_t> handles;
    handles.reserve(context.video_views.size());
    for (const auto& [handle, state] : context.video_views) {
        if (state.player != nullptr && state.playing) {
            handles.push_back(handle);
        }
    }
    const auto uptime = context.uptime_millis.load();
    for (const auto handle : handles) {
        const auto found = context.video_views.find(handle);
        if (found == context.video_views.end()) continue;
        auto& state = found->second;
        if (state.player == nullptr || !state.playing) continue;
        const auto position =
            android_intrinsics::VideoPositionOf(state, uptime);
        try {
            auto frame = state.player->TakeFrame(position);
            if (frame.has_value() && publish) {
                publish(video::ComposeRgbaOnCanvas(
                    *frame, context.surface_width, context.surface_height));
            }
        } catch (const video::VideoPlayerError& error) {
            return "video decode failed for " + state.guest_path + ": " +
                   error.what();
        }
        if (position >= state.duration_ms && !state.completed) {
            state.playing = false;
            state.base_position_ms = state.duration_ms;
            state.completed = true;
            const auto error =
                android_intrinsics::InvokeVideoCompletionListener(
                    vm, context, handle);
            if (error.has_value()) return error;
        }
    }
    return std::nullopt;
}

}  // namespace ogplay::runtime
