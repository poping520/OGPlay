#include "ogplay/audio/pcm_mix.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ogplay::audio {

void SaturateStereoPcm16(const std::span<const std::int64_t> accumulator,
                         const std::span<std::int16_t> output) {
    if (accumulator.size() != output.size() || output.size() % 2U != 0U) {
        throw std::invalid_argument(
            "PCM mix saturate requires matching stereo buffers");
    }
    constexpr auto kMin =
        static_cast<std::int64_t>((std::numeric_limits<std::int16_t>::min)());
    constexpr auto kMax =
        static_cast<std::int64_t>((std::numeric_limits<std::int16_t>::max)());
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::int16_t>(
            std::clamp(accumulator[index], kMin, kMax));
    }
}

void CopyPcm16IntoAccumulator(const std::span<const std::int16_t> pcm,
                              const std::span<std::int64_t> accumulator) {
    if (pcm.size() != accumulator.size()) {
        throw std::invalid_argument(
            "PCM mix copy requires matching buffer lengths");
    }
    for (std::size_t index = 0; index < pcm.size(); ++index) {
        accumulator[index] += pcm[index];
    }
}

}  // namespace ogplay::audio
