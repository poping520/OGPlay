#include "ogplay/audio/open_sles_pcm_mixer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ogplay/audio/pcm_mix.h"

namespace ogplay::audio {
namespace {

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
    {
        std::scoped_lock lock(mutex_);
        players_.erase(player);
    }
    queue_changed_.notify_all();
}

bool OpenSlesPcmMixer::HasPlayer(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    return players_.contains(player);
}

void OpenSlesPcmMixer::SetPlayerKind(const PlayerId player,
                                     const OpenSlesPlayerKind kind) {
    std::scoped_lock lock(mutex_);
    Require(player).kind = kind;
}

OpenSlesPlayerKind OpenSlesPcmMixer::PlayerKind(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    return Require(player).kind;
}

void OpenSlesPcmMixer::SetPlaybackRate(const PlayerId player, const float rate) {
    if (!std::isfinite(rate) || rate < 0.5F || rate > 2.0F) {
        throw std::invalid_argument("PCM playback rate must be within 0.5..2");
    }
    std::scoped_lock lock(mutex_);
    Require(player).playback_rate = rate;
}

void OpenSlesPcmMixer::SetLoop(const PlayerId player,
                               const std::uint32_t start_frame,
                               const std::uint32_t end_frame,
                               const std::int32_t loop_count) {
    if (loop_count < -1 || end_frame < start_frame) {
        throw std::invalid_argument("PCM loop points are invalid");
    }
    std::scoped_lock lock(mutex_);
    auto& target = Require(player);
    target.loop_start = start_frame;
    target.loop_end = end_frame;
    target.loop_count = loop_count;
    target.loops_remaining = loop_count;
}

void OpenSlesPcmMixer::ReplaceStatic(Player& player,
                                     const std::span<const std::byte> pcm) {
    player.queue.clear();
    player.queue.push_back(Buffer{player.next_sequence++, {pcm.begin(), pcm.end()}});
    player.frame_position = 0.0;
    player.has_carry = false;
    player.play_index = 0U;
    player.loops_remaining = player.loop_count;
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
    if (target.kind == OpenSlesPlayerKind::audio_track_static) {
        ReplaceStatic(target, pcm);
        return true;
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

OpenSlesEnqueueResult OpenSlesPcmMixer::EnqueueBlocking(
    const PlayerId player, const std::span<const std::byte> pcm,
    const std::size_t maximum_queued_bytes) {
    std::unique_lock lock(mutex_);
    auto found = players_.find(player);
    if (found == players_.end()) {
        return OpenSlesEnqueueResult::player_destroyed;
    }
    const auto bytes_per_frame = BytesPerFrame(found->second.format);
    if (pcm.empty() || pcm.size() % bytes_per_frame != 0U) {
        throw std::invalid_argument(
            "OpenSL PCM buffer is empty or frame-misaligned");
    }
    const auto byte_budget = std::min(maximum_queued_bytes, kMaximumQueuedBytes);
    if (pcm.size() > kMaximumBufferBytes) {
        throw std::length_error("OpenSL PCM buffer exceeds byte budget");
    }
    if (found->second.kind == OpenSlesPlayerKind::audio_track_static) {
        ReplaceStatic(found->second, pcm);
        return OpenSlesEnqueueResult::enqueued;
    }
    const auto ready = [this, player, size = pcm.size(), byte_budget] {
        const auto current = players_.find(player);
        return interrupted_ || current == players_.end() ||
               (current->second.queue.size() < current->second.capacity &&
                QueuedBytes(current->second) <= byte_budget - size);
    };
    if (!ready()) {
        ++blocking_writers_;
        queue_changed_.wait(lock, ready);
        --blocking_writers_;
    }
    if (interrupted_) return OpenSlesEnqueueResult::interrupted;
    found = players_.find(player);
    if (found == players_.end()) {
        return OpenSlesEnqueueResult::player_destroyed;
    }
    found->second.queue.push_back(
        Buffer{found->second.next_sequence++, {pcm.begin(), pcm.end()}});
    return OpenSlesEnqueueResult::enqueued;
}

std::size_t OpenSlesPcmMixer::QueuedBytes(const Player& player) {
    std::size_t queued_bytes{};
    for (const auto& queued : player.queue) queued_bytes += queued.pcm.size();
    if (!player.queue.empty() &&
        player.kind != OpenSlesPlayerKind::audio_track_static) {
        const auto bytes_per_frame = BytesPerFrame(player.format);
        const auto front_frames =
            player.queue.front().pcm.size() / bytes_per_frame;
        const auto consumed_frames = std::min(
            static_cast<std::size_t>(player.frame_position), front_frames);
        const auto consumed_bytes = std::min(
            player.queue.front().pcm.size(), consumed_frames * bytes_per_frame);
        queued_bytes -= consumed_bytes;
    }
    return queued_bytes;
}

std::size_t OpenSlesPcmMixer::QueuedBytes(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    return QueuedBytes(Require(player));
}

std::size_t OpenSlesPcmMixer::BlockingWriterCount() const noexcept {
    std::scoped_lock lock(mutex_);
    return blocking_writers_;
}

std::size_t OpenSlesPcmMixer::InterruptBlockingWaits() noexcept {
    std::size_t waiters{};
    {
        std::scoped_lock lock(mutex_);
        interrupted_ = true;
        waiters = blocking_writers_;
    }
    queue_changed_.notify_all();
    return waiters;
}

void OpenSlesPcmMixer::Clear(const PlayerId player) {
    {
        std::scoped_lock lock(mutex_);
        auto& target = Require(player);
        if (target.kind != OpenSlesPlayerKind::audio_track_static) {
            target.queue.clear();
        }
        target.frame_position = 0.0;
        target.played_source_frames = 0.0;
        target.play_index = 0U;
        target.has_carry = false;
    }
    queue_changed_.notify_all();
}

void OpenSlesPcmMixer::ClearQueueKeepHead(const PlayerId player) {
    {
        std::scoped_lock lock(mutex_);
        auto& target = Require(player);
        if (target.kind == OpenSlesPlayerKind::audio_track_static) return;
        target.queue.clear();
        target.frame_position = 0.0;
        target.has_carry = false;
    }
    queue_changed_.notify_all();
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
    if (state != OpenSlesPlayState::stopped) return;
    target.played_source_frames = 0.0;
    if (target.kind == OpenSlesPlayerKind::audio_track_stream) {
        return;
    }
    target.frame_position = 0.0;
    target.has_carry = false;
    target.loops_remaining = target.loop_count;
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
    const auto wrapped = static_cast<std::uint64_t>(milliseconds);
    return static_cast<std::uint32_t>(wrapped);
}

std::uint32_t OpenSlesPcmMixer::PositionFrames(const PlayerId player) const {
    std::scoped_lock lock(mutex_);
    const auto frames = static_cast<std::uint64_t>(
        Require(player).played_source_frames);
    return static_cast<std::uint32_t>(frames);
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

bool OpenSlesPcmMixer::MixPlayerFrame(
    Player& player, const PlayerId id, const std::size_t out_frame,
    const double left_gain, const double right_gain,
    const std::span<std::int64_t> accumulator,
    std::vector<OpenSlesConsumedBuffer>& consumed, const double step) {
    const auto consume_front = [&] {
        if (player.kind == OpenSlesPlayerKind::audio_track_static) return false;
        const auto frames =
            player.queue.front().pcm.size() / BytesPerFrame(player.format);
        if (player.frame_position < static_cast<double>(frames)) return false;
        player.carry_left = Sample(player, player.queue.front(), frames - 1U, 0U);
        player.carry_right = Sample(player, player.queue.front(), frames - 1U, 1U);
        player.has_carry = true;
        consumed.push_back({id, player.queue.front().sequence});
        player.queue.erase(player.queue.begin());
        ++player.play_index;
        player.frame_position -= static_cast<double>(frames);
        return true;
    };
    while (!player.queue.empty() && consume_front()) {
    }
    if (player.queue.empty()) return false;

    auto& buffer = player.queue.front();
    auto frames = buffer.pcm.size() / BytesPerFrame(player.format);
    if (player.kind == OpenSlesPlayerKind::audio_track_static &&
        player.loop_end > player.loop_start &&
        (player.loop_count != 0 || player.loops_remaining != 0)) {
        frames = std::min(frames, static_cast<std::size_t>(player.loop_end));
    }
    if (frames == 0U) return false;
    if (player.kind == OpenSlesPlayerKind::audio_track_static &&
        player.frame_position >= static_cast<double>(frames)) {
        if (player.loop_count == -1 || player.loops_remaining > 0) {
            player.frame_position = static_cast<double>(player.loop_start);
            player.has_carry = false;
            if (player.loops_remaining > 0) --player.loops_remaining;
        } else {
            return false;
        }
    }

    const auto first = static_cast<std::size_t>(player.frame_position);
    if (first >= frames) return false;
    const auto fraction = player.frame_position - static_cast<double>(first);
    std::int16_t left_a = Sample(player, buffer, first, 0U);
    std::int16_t right_a = Sample(player, buffer, first, 1U);
    std::int16_t left_b = left_a;
    std::int16_t right_b = right_a;
    if (first + 1U < frames) {
        left_b = Sample(player, buffer, first + 1U, 0U);
        right_b = Sample(player, buffer, first + 1U, 1U);
    } else if (player.kind != OpenSlesPlayerKind::audio_track_static &&
               player.queue.size() > 1U) {
        left_b = Sample(player, player.queue[1], 0U, 0U);
        right_b = Sample(player, player.queue[1], 0U, 1U);
    }
    const auto mix_channel = [&](const std::int16_t a, const std::int16_t b,
                                 const double gain, const std::size_t channel) {
        const auto sample = static_cast<double>(a) +
                            (static_cast<double>(b) - static_cast<double>(a)) *
                                fraction;
        accumulator[out_frame * 2U + channel] +=
            static_cast<std::int64_t>(sample * gain);
    };
    mix_channel(left_a, left_b, left_gain, 0U);
    mix_channel(right_a, right_b, right_gain, 1U);
    player.frame_position += step;
    player.played_source_frames += step;
    return true;
}

std::vector<OpenSlesConsumedBuffer> OpenSlesPcmMixer::MixIntoAccumulator(
    const std::span<std::int64_t> accumulator, const std::uint32_t output_rate) {
    if (output_rate == 0U || accumulator.size() % 2U != 0U) {
        throw std::invalid_argument("OpenSL output must be stereo with a positive rate");
    }
    std::unique_lock lock(mutex_);
    std::vector<OpenSlesConsumedBuffer> consumed;
    bool queue_progressed{};
    const auto output_frames = accumulator.size() / 2U;
    for (auto& [id, player] : players_) {
        if (player.state != OpenSlesPlayState::playing) continue;
        const auto step = static_cast<double>(player.format.sample_rate) /
                          static_cast<double>(output_rate) *
                          static_cast<double>(player.playback_rate);
        const auto gain = player.mute ? 0.0 : MillibelGain(player.millibel);
        const auto pan = static_cast<double>(player.stereo_position) / 1000.0;
        const auto left_gain = gain * player.left_volume *
                               (pan > 0.0 ? 1.0 - pan : 1.0);
        const auto right_gain = gain * player.right_volume *
                                (pan < 0.0 ? 1.0 + pan : 1.0);
        for (std::size_t out_frame = 0; out_frame < output_frames; ++out_frame) {
            if (!MixPlayerFrame(player, id, out_frame, left_gain, right_gain,
                                accumulator, consumed, step)) {
                break;
            }
            queue_progressed = true;
        }
        while (!player.queue.empty() &&
               player.kind != OpenSlesPlayerKind::audio_track_static) {
            const auto frames = player.queue.front().pcm.size() /
                                BytesPerFrame(player.format);
            if (player.frame_position < static_cast<double>(frames)) break;
            player.carry_left =
                Sample(player, player.queue.front(), frames - 1U, 0U);
            player.carry_right =
                Sample(player, player.queue.front(), frames - 1U, 1U);
            player.has_carry = true;
            consumed.push_back({id, player.queue.front().sequence});
            player.queue.erase(player.queue.begin());
            ++player.play_index;
            player.frame_position -= static_cast<double>(frames);
            queue_progressed = true;
        }
    }
    lock.unlock();
    if (queue_progressed) queue_changed_.notify_all();
    return consumed;
}

std::vector<OpenSlesConsumedBuffer>
OpenSlesPcmMixer::MixAdditiveStereoPcm16(
    const std::span<std::int16_t> output, const std::uint32_t output_rate) {
    std::vector<std::int64_t> accumulator(output.size());
    CopyPcm16IntoAccumulator(output, accumulator);
    auto consumed = MixIntoAccumulator(accumulator, output_rate);
    SaturateStereoPcm16(accumulator, output);
    return consumed;
}

}  // namespace ogplay::audio
