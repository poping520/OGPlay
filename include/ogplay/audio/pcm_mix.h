#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ogplay::audio {

void SaturateStereoPcm16(std::span<const std::int64_t> accumulator,
                         std::span<std::int16_t> output);

void CopyPcm16IntoAccumulator(std::span<const std::int16_t> pcm,
                              std::span<std::int64_t> accumulator);

}  // namespace ogplay::audio
