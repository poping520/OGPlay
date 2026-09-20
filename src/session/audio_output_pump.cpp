#include <stdexcept>

#include "ogplay/session/audio_output_pump.h"

#include <chrono>
#include <span>

namespace ogplay::session {
namespace {

constexpr std::uint64_t kTargetQueuedFrames = 4096U;
constexpr std::size_t kChunkFrames = 1024U;

}  // namespace

AudioOutputPump::AudioOutputPump(Mix mix, hal::AudioOutput* output,
                                 const std::uint32_t sample_rate,
                                 const std::uint8_t channels,
                                 FailureInterrupt failure_interrupt)
    : mix_(std::move(mix)),
      failure_interrupt_(std::move(failure_interrupt)),
      output_(output),
      sample_rate_(sample_rate),
      channels_(channels),
      chunk_(kChunkFrames * channels) {
    if (!mix_ || sample_rate_ == 0U || channels_ != 2U) {
        throw std::invalid_argument("audio output pump needs stereo mix");
    }
}

AudioOutputPump::~AudioOutputPump() { Stop(); }

void AudioOutputPump::StartRealtime() {
    Stop();
    device_failed_.store(false, std::memory_order_relaxed);
    {
        std::scoped_lock lock(failure_mutex_);
        worker_failure_ = {};
    }
    worker_ = std::jthread([this](const std::stop_token stop) {
        while (!stop.stop_requested()) {
            try {
                if (device_failed_.load(std::memory_order_relaxed) ||
                    suspended_.load(std::memory_order_relaxed) ||
                    output_ == nullptr || !output_->IsStarted()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                PumpRealtimeOnce();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } catch (...) {
                const auto failure = std::current_exception();
                // Never leave a producer waiting after the only consumer exits.
                // Interrupt before publishing so observers see the complete failure.
                try {
                    if (failure_interrupt_) failure_interrupt_();
                } catch (...) {
                    // Preserve the original mixer/device failure.
                }
                std::scoped_lock lock(failure_mutex_);
                worker_failure_ = failure;
                return;
            }
        }
    });
}

void AudioOutputPump::Stop() noexcept {
    if (worker_.joinable()) worker_.request_stop();
    worker_ = {};
}

void AudioOutputPump::SetSuspended(const bool suspended) noexcept {
    suspended_.store(suspended, std::memory_order_relaxed);
}

bool AudioOutputPump::DeviceFailed() const noexcept {
    return device_failed_.load(std::memory_order_relaxed);
}

void AudioOutputPump::RethrowWorkerFailure() {
    std::exception_ptr failure;
    {
        std::scoped_lock lock(failure_mutex_);
        failure = worker_failure_;
    }
    if (failure) std::rethrow_exception(failure);
}

std::size_t AudioOutputPump::MixOffline(const std::span<std::int16_t> output) {
    std::scoped_lock lock(mutex_);
    return mix_(output, sample_rate_);
}

std::uint64_t AudioOutputPump::FramesForTicks(
    const std::uint64_t tick_delta, const std::uint64_t ticks_per_second) {
    if (ticks_per_second == 0U) return 0U;
    const auto scaled =
        tick_delta * static_cast<std::uint64_t>(sample_rate_) + frame_remainder_;
    const auto frames = scaled / ticks_per_second;
    frame_remainder_ = scaled % ticks_per_second;
    return frames;
}

void AudioOutputPump::PumpRealtimeOnce() {
    if (output_ == nullptr || device_failed_.load(std::memory_order_relaxed)) {
        return;
    }
    for (std::size_t chunk = 0; chunk < 4U; ++chunk) {
        try {
            if (output_->QueuedFrames() >= kTargetQueuedFrames) break;
        } catch (...) {
            device_failed_.store(true, std::memory_order_relaxed);
            throw;
        }
        std::size_t frames{};
        {
            std::scoped_lock lock(mutex_);
            frames = mix_(chunk_, sample_rate_);
        }
        if (frames == 0U) break;
        const auto samples = frames * channels_;
        try {
            output_->Submit(std::as_bytes(std::span{chunk_}.first(samples)));
        } catch (...) {
            device_failed_.store(true, std::memory_order_relaxed);
            throw;
        }
    }
}

}  // namespace ogplay::session
