#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "ogplay/hal/audio.h"

namespace ogplay::session {

class AudioOutputPump final {
public:
    using Mix = std::function<std::size_t(std::span<std::int16_t>,
                                          std::uint32_t)>;

    AudioOutputPump(Mix mix, hal::AudioOutput* output, std::uint32_t sample_rate,
                    std::uint8_t channels);
    ~AudioOutputPump();
    AudioOutputPump(const AudioOutputPump&) = delete;
    AudioOutputPump& operator=(const AudioOutputPump&) = delete;

    void StartRealtime();
    void Stop() noexcept;
    void SetSuspended(bool suspended) noexcept;
    [[nodiscard]] bool DeviceFailed() const noexcept;
    [[nodiscard]] std::size_t MixOffline(std::span<std::int16_t> output);
    [[nodiscard]] std::uint64_t FramesForTicks(std::uint64_t tick_delta,
                                               std::uint64_t ticks_per_second);

private:
    void PumpRealtimeOnce();

    Mix mix_;
    hal::AudioOutput* output_{};
    std::uint32_t sample_rate_{};
    std::uint8_t channels_{};
    std::vector<std::int16_t> chunk_;
    std::mutex mutex_;
    std::jthread worker_;
    std::atomic<bool> suspended_{};
    std::atomic<bool> device_failed_{};
    std::uint64_t frame_remainder_{};
};

}  // namespace ogplay::session
