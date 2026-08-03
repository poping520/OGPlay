#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace ogplay::hal {

enum class AudioSampleFormat : std::uint8_t { signed_16_le, float_32_le };

struct AudioStreamConfig final {
    std::uint32_t sample_rate{};
    std::uint8_t channels{};
    AudioSampleFormat format{AudioSampleFormat::signed_16_le};

    bool operator==(const AudioStreamConfig&) const = default;
};

class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    [[nodiscard]] virtual AudioStreamConfig Config() const = 0;
    [[nodiscard]] virtual bool IsStarted() const = 0;
    [[nodiscard]] virtual std::uint64_t QueuedFrames() const = 0;
    virtual void Start() = 0;
    virtual void Stop() = 0;
    virtual void Submit(std::span<const std::byte> interleaved_samples) = 0;
};

}  // namespace ogplay::hal
