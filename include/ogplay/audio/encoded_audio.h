#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ogplay/audio/ogg_vorbis.h"

namespace ogplay::audio {

constexpr std::size_t kMaximumEncodedAudioBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumDecodedPcmBytes = 128U * 1024U * 1024U;

[[nodiscard]] std::vector<std::byte> SliceSourceWindow(
    std::span<const std::byte> bytes, std::uint64_t offset,
    std::uint64_t length);

[[nodiscard]] Pcm16Audio DecodeEncodedAudio(std::span<const std::byte> encoded);

}  // namespace ogplay::audio
