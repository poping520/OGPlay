#include "ogplay/audio/open_sles_pcm_mixer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ogplay::audio {
namespace {

constexpr std::size_t kMaximumBufferBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumQueuedBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumPlayers = 256U;

[[nodiscard]] double MillibelGain(const std::int16_t millibel) {
    return std::pow(10.0, static_cast<double>(millibel) / 2000.0);
}

}  // namespace

std::size_t OpenSlesPcmMixer::BytesPerFrame(const OpenSlesPcmFormat format) {
    if ((format.channels != 1U && format.channels != 2U) ||
        (format.bits_per_sample != 8U && format.bits_per_sample != 16U) ||
        format.sample_rate == 0U) {
        throw std::invalid_argument("OpenSL mixer requires mono/stereo PCM8/16");
    }
    return static_cast<std::size_t>(format.channels) *
           (format.bits_per_sample / 8U);
}

OpenSlesPcmMixer::PlayerId OpenSlesPcmMixer::CreatePlayer(
    const OpenSlesPcmFormat format, const std::uint8_t queue_capacity) {
    static_cast<void>(BytesPerFrame(format));
    if (queue_capacity == 0U) {
        throw std::invalid_argument("OpenSL mixer queue capacity must be positive");
    }
    std::scoped_lock lock(mutex_);
    if (players_.size() >= kMaximumPlayers || next_player_ == 0U) {
        throw std::length_error("OpenSL mixer player limit exceeded");
    }
    const auto id = next_player_++;
    Player player;
    player.format = format;
    player.capacity = queue_capacity;
    players_.emplace(id, std::move(player));
    return id;
}

void OpenSlesPcmMixer::DestroyPlayer(const PlayerId player) noexcept {
    std::scoped_lock lock(mutex_);
    players_.erase(player);
}

bool OpenSlesPcmMixer::HasPlayer(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    return players_.contains(player);
}

bool OpenSlesPcmMixer::Enqueue(const PlayerId player,
                              const std::span<const std::byte> pcm) {
    std::scoped_lock lock(mutex_);
    auto& target = Require(player);
    const auto bytes_per_frame = BytesPerFrame(target.format);
    if (pcm.empty() || pcm.size() % bytes_per_frame != 0U) {
        throw std::invalid_argument("OpenSL PCM buffer is empty or frame-misaligned");
    }
    if (pcm.size() > kMaximumBufferBytes) {
        throw std::length_error("OpenSL PCM buffer exceeds byte limit");
    }
    if (target.queue.size() >= target.capacity) return false;
    std::size_t queued_bytes{};
    for (const auto& queued : target.queue) queued_bytes += queued.pcm.size();
    if (queued_bytes > kMaximumQueuedBytes - pcm.size()) {
        throw std::length_error("OpenSL PCM queue exceeds byte limit");
    }
    target.queue.push_back(
        Buffer{target.next_sequence++, {pcm.begin(), pcm.end()}});
    return true;
}

void OpenSlesPcmMixer::Clear(const PlayerId player) {
    std::scoped_lock lock(mutex_);
    auto& target = Require(player);
    target.queue.clear();
    target.frame_position = 0.0;
}

OpenSlesQueueState OpenSlesPcmMixer::QueueState(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    const auto& target = Require(player);
    return {static_cast<std::uint32_t>(target.queue.size()), target.play_index};
}

void OpenSlesPcmMixer::SetPlayState(const PlayerId player,
                                    const OpenSlesPlayState state) {
    std::scoped_lock lock(mutex_);
    auto& target = Require(player);
    target.state = state;
    if (state == OpenSlesPlayState::stopped) {
        target.frame_position = 0.0;
        target.played_source_frames = 0.0;
    }
}

OpenSlesPlayState OpenSlesPcmMixer::PlayState(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    return Require(player).state;
}

std::uint32_t OpenSlesPcmMixer::PositionMillis(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    const auto& target = Require(player);
    const auto milliseconds = target.played_source_frames * 1000.0 /
                              static_cast<double>(target.format.sample_rate);
    return static_cast<std::uint32_t>(std::min(
        milliseconds,
        static_cast<double>((std::numeric_limits<std::uint32_t>::max)())));
}

std::uint32_t OpenSlesPcmMixer::PositionFrames(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    const auto frames = Require(player).played_source_frames;
    return static_cast<std::uint32_t>(std::min(
        frames, static_cast<double>((std::numeric_limits<std::uint32_t>::max)())));
}

void OpenSlesPcmMixer::SetVolume(const PlayerId player,
                                 const std::int16_t millibel) {
    if (millibel < -9600 || millibel > 0) {
        throw std::invalid_argument("OpenSL volume must be within -9600..0 mB");
    }
    std::scoped_lock lock(mutex_);
    Require(player).millibel = millibel;
}

void OpenSlesPcmMixer::SetStereoVolume(const PlayerId player, const float left,
                                       const float right) {
    if (!std::isfinite(left) || !std::isfinite(right) || left < 0.0F ||
        left > 1.0F || right < 0.0F || right > 1.0F) {
        throw std::invalid_argument("PCM stereo volume must be within 0..1");
    }
    std::scoped_lock lock(mutex_);
    auto& target = Require(player);
    target.left_volume = left;
    target.right_volume = right;
}

void OpenSlesPcmMixer::SetMute(const PlayerId player, const bool mute) {
    std::scoped_lock lock(mutex_);
    Require(player).mute = mute;
}

void OpenSlesPcmMixer::SetStereoPosition(const PlayerId player,
                                         const std::int16_t permille) {
    if (permille < -1000 || permille > 1000) {
        throw std::invalid_argument("OpenSL stereo position must be within -1000..1000");
    }
    std::scoped_lock lock(mutex_);
    Require(player).stereo_position = permille;
}

std::int16_t OpenSlesPcmMixer::Sample(const Player& player,
                                     const Buffer& buffer,
                                     const std::size_t frame,
                                     const std::size_t channel) {
    const auto source_channel = player.format.channels == 1U ? 0U : channel;
    const auto sample_index = frame * player.format.channels + source_channel;
    if (player.format.bits_per_sample == 8U) {
        const auto value = std::to_integer<std::uint8_t>(buffer.pcm[sample_index]);
        return static_cast<std::int16_t>(
            (static_cast<std::int32_t>(value) - 128) * 256);
    }
    const auto offset = sample_index * 2U;
    const auto value = static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(buffer.pcm[offset])) |
        static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(buffer.pcm[offset + 1U]) << 8U);
    return static_cast<std::int16_t>(value);
}

OpenSlesPcmMixer::Player& OpenSlesPcmMixer::Require(const PlayerId player) {
    const auto found = players_.find(player);
    if (found == players_.end()) throw std::invalid_argument("stale OpenSL mixer player");
    return found->second;
}

const OpenSlesPcmMixer::Player& OpenSlesPcmMixer::Require(
    const PlayerId player) const {
    const auto found = players_.find(player);
    if (found == players_.end()) throw std::invalid_argument("stale OpenSL mixer player");
    return found->second;
}

std::vector<OpenSlesConsumedBuffer>
OpenSlesPcmMixer::MixAdditiveStereoPcm16(
    const std::span<std::int16_t> output, const std::uint32_t output_rate) {
    if (output_rate == 0U || output.size() % 2U != 0U) {
        throw std::invalid_argument("OpenSL output must be stereo with a positive rate");
    }
    std::scoped_lock lock(mutex_);
    scratch_.resize(output.size());
    for (std::size_t index = 0; index < output.size(); ++index) {
        scratch_[index] = output[index];
    }
    std::vector<OpenSlesConsumedBuffer> consumed;
    const auto output_frames = output.size() / 2U;
    for (auto& [id, player] : players_) {
        if (player.state != OpenSlesPlayState::playing) continue;
        const auto step = static_cast<double>(player.format.sample_rate) /
                          static_cast<double>(output_rate);
        const auto gain = player.mute ? 0.0 : MillibelGain(player.millibel);
        const auto pan = static_cast<double>(player.stereo_position) / 1000.0;
        const auto left_gain = gain * player.left_volume *
                               (pan > 0.0 ? 1.0 - pan : 1.0);
        const auto right_gain = gain * player.right_volume *
                                (pan < 0.0 ? 1.0 + pan : 1.0);
        for (std::size_t out_frame = 0; out_frame < output_frames; ++out_frame) {
            while (!player.queue.empty()) {
                const auto frames = player.queue.front().pcm.size() /
                                    BytesPerFrame(player.format);
                if (player.frame_position < static_cast<double>(frames)) break;
                consumed.push_back({id, player.queue.front().sequence});
                player.queue.erase(player.queue.begin());
                ++player.play_index;
                player.frame_position = 0.0;
            }
            if (player.queue.empty()) break;
            const auto& buffer = player.queue.front();
            const auto frames = buffer.pcm.size() / BytesPerFrame(player.format);
            const auto first = static_cast<std::size_t>(player.frame_position);
            const auto second = std::min(first + 1U, frames - 1U);
            const auto fraction = player.frame_position - static_cast<double>(first);
            for (std::size_t channel = 0; channel < 2U; ++channel) {
                const auto a = static_cast<double>(Sample(player, buffer, first, channel));
                const auto b = static_cast<double>(Sample(player, buffer, second, channel));
                const auto sample = a + (b - a) * fraction;
                const auto channel_gain = channel == 0U ? left_gain : right_gain;
                scratch_[out_frame * 2U + channel] +=
                    static_cast<std::int64_t>(sample * channel_gain);
            }
            player.frame_position += step;
            player.played_source_frames += step;
        }
        while (!player.queue.empty()) {
            const auto frames = player.queue.front().pcm.size() /
                                BytesPerFrame(player.format);
            if (player.frame_position < static_cast<double>(frames)) break;
            consumed.push_back({id, player.queue.front().sequence});
            player.queue.erase(player.queue.begin());
            ++player.play_index;
            player.frame_position = 0.0;
        }
    }
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::int16_t>(std::clamp(
            scratch_[index],
            static_cast<std::int64_t>((std::numeric_limits<std::int16_t>::min)()),
            static_cast<std::int64_t>((std::numeric_limits<std::int16_t>::max)())));
    }
    return consumed;
}

}  // namespace ogplay::audio
