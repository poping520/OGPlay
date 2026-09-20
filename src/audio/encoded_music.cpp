#include "ogplay/audio/encoded_music.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

#include "ogplay/audio/encoded_audio.h"

namespace ogplay::audio {
namespace {

[[nodiscard]] std::int32_t FramesToMs(const std::size_t frames,
                                      const std::uint32_t rate) {
    if (rate == 0U) return 0;
    return static_cast<std::int32_t>(frames * 1000U / rate);
}

}  // namespace

std::uint32_t EncodedMusicMixer::Create() {
    std::scoped_lock lock(mutex_);
    const auto id = next_++;
    players_[id] = {};
    return id;
}

void EncodedMusicMixer::Destroy(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    players_.erase(player);
}

void EncodedMusicMixer::Reset(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found != players_.end()) {
        const auto generation = found->second.generation + 1U;
        found->second = {};
        found->second.generation = generation;
    }
}

bool EncodedMusicMixer::SetEncoded(const std::uint32_t player,
                                   std::vector<std::byte> encoded) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || encoded.empty() ||
        encoded.size() > kMaximumEncodedAudioBytes) {
        return false;
    }
    const auto generation = found->second.generation + 1U;
    found->second = {};
    found->second.generation = generation;
    found->second.encoded = std::move(encoded);
    return true;
}

bool EncodedMusicMixer::Prepare(const std::uint32_t player) {
    std::vector<std::byte> encoded;
    std::uint64_t generation{};
    {
        std::scoped_lock lock(mutex_);
        const auto found = players_.find(player);
        if (found == players_.end() || found->second.encoded.empty()) return false;
        encoded = found->second.encoded;
        generation = found->second.generation;
    }
    try {
        auto pcm = DecodeEncodedAudio(encoded);
        return CommitPrepared(player, generation, std::move(pcm));
    } catch (const std::exception&) {
        return false;
    }
}

bool EncodedMusicMixer::CommitPrepared(const std::uint32_t player,
                                       const std::uint64_t generation,
                                       Pcm16Audio pcm) {
        std::scoped_lock lock(mutex_);
        const auto found = players_.find(player);
        if (found == players_.end() || found->second.generation != generation) {
            return false;
        }
        std::size_t total = pcm.interleaved_samples.size() * sizeof(std::int16_t);
        for (const auto& [id, other] : players_) {
            if (id != player && other.pcm.has_value()) {
                total += other.pcm->interleaved_samples.size() * sizeof(std::int16_t);
            }
        }
        if (total > kMaximumDecodedPcmBytes) return false;
        found->second.sample_rate = pcm.sample_rate;
        found->second.channels = pcm.channels;
        found->second.total_frames = pcm.Frames();
        found->second.pcm = std::move(pcm);
        found->second.prepared = found->second.total_frames > 0U;
        found->second.position = 0.0;
        found->second.completed = false;
        return found->second.prepared;
}

bool EncodedMusicMixer::BeginPrepare(const std::uint32_t player) {
    std::vector<std::byte> encoded;
    std::shared_ptr<PrepareTask> task;
    {
        std::scoped_lock lock(mutex_);
        const auto found = players_.find(player);
        if (found == players_.end() || found->second.encoded.empty() ||
            found->second.prepare_task) return false;
        encoded = found->second.encoded;
        task = std::make_shared<PrepareTask>();
        found->second.prepare_task = task;
    }
    std::thread([task, encoded = std::move(encoded)]() mutable {
        try {
            auto pcm = DecodeEncodedAudio(encoded);
            std::scoped_lock lock(task->mutex);
            task->pcm = std::move(pcm);
            task->done = true;
        } catch (const std::exception&) {
            std::scoped_lock lock(task->mutex);
            task->done = true;
        }
    }).detach();
    return true;
}

EncodedMusicMixer::PrepareStatus EncodedMusicMixer::PollPrepare(
    const std::uint32_t player) {
    std::shared_ptr<PrepareTask> task;
    std::uint64_t generation{};
    {
        std::scoped_lock lock(mutex_);
        const auto found = players_.find(player);
        if (found == players_.end() || !found->second.prepare_task) {
            return PrepareStatus::failed;
        }
        task = found->second.prepare_task;
        generation = found->second.generation;
    }
    std::optional<Pcm16Audio> pcm;
    {
        std::scoped_lock lock(task->mutex);
        if (!task->done) return PrepareStatus::pending;
        pcm = std::move(task->pcm);
    }
    {
        std::scoped_lock lock(mutex_);
        const auto found = players_.find(player);
        if (found != players_.end() && found->second.prepare_task == task) {
            found->second.prepare_task.reset();
        }
    }
    return pcm && CommitPrepared(player, generation, std::move(*pcm))
               ? PrepareStatus::ready
               : PrepareStatus::failed;
}

void EncodedMusicMixer::Start(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.prepared) return;
    if (found->second.completed ||
        found->second.position >= static_cast<double>(found->second.total_frames)) {
        found->second.position = 0.0;
    }
    found->second.playing = true;
    found->second.completed = false;
}

void EncodedMusicMixer::Pause(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found != players_.end()) found->second.playing = false;
}

void EncodedMusicMixer::Stop(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end()) return;
    found->second.playing = false;
    found->second.position = 0.0;
}

void EncodedMusicMixer::SeekMs(const std::uint32_t player,
                               const std::int32_t milliseconds) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.prepared ||
        found->second.sample_rate == 0U) {
        return;
    }
    auto frame = static_cast<std::size_t>(std::max(milliseconds, 0)) *
                 found->second.sample_rate / 1000U;
    frame = std::min(frame, found->second.total_frames);
    found->second.position = static_cast<double>(frame);
    found->second.completed = false;
}

void EncodedMusicMixer::SetLooping(const std::uint32_t player,
                                   const bool looping) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found != players_.end()) found->second.looping = looping;
}

void EncodedMusicMixer::SetVolume(const std::uint32_t player, const float left,
                                  const float right) {
    std::scoped_lock lock(mutex_);
    if (!std::isfinite(left) || !std::isfinite(right) || left < 0.0F ||
        left > 1.0F || right < 0.0F || right > 1.0F) {
        return;
    }
    const auto found = players_.find(player);
    if (found == players_.end()) return;
    found->second.left = left;
    found->second.right = right;
}

bool EncodedMusicMixer::IsPlaying(const std::uint32_t player) const {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    return found != players_.end() && found->second.playing;
}

bool EncodedMusicMixer::HasEncoded(const std::uint32_t player) const {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    return found != players_.end() && !found->second.encoded.empty();
}

bool EncodedMusicMixer::AnyPlaying() const {
    std::scoped_lock lock(mutex_);
    for (const auto& [_, player] : players_) {
        if (player.playing) return true;
    }
    return false;
}

bool EncodedMusicMixer::IsLooping(const std::uint32_t player) const {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    return found != players_.end() && found->second.looping;
}

bool EncodedMusicMixer::Completed(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.completed) return false;
    found->second.completed = false;
    return true;
}

std::int32_t EncodedMusicMixer::DurationMs(const std::uint32_t player) const {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.prepared) return 0;
    return FramesToMs(found->second.total_frames, found->second.sample_rate);
}

std::int32_t EncodedMusicMixer::PositionMs(const std::uint32_t player) const {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.prepared) return 0;
    return FramesToMs(static_cast<std::size_t>(found->second.position),
                      found->second.sample_rate);
}

std::size_t EncodedMusicMixer::CachedDecodedBytes() const {
    std::scoped_lock lock(mutex_);
    std::size_t bytes{};
    for (const auto& [_, player] : players_) {
        bytes += player.encoded.size();
        if (player.pcm.has_value()) {
            bytes += player.pcm->interleaved_samples.size() * sizeof(std::int16_t);
        }
    }
    return bytes;
}

void EncodedMusicMixer::MixIntoAccumulator(
    const std::span<std::int64_t> accumulator, const std::uint32_t output_rate) {
    if (output_rate == 0U || accumulator.size() % 2U != 0U) {
        throw std::invalid_argument(
            "encoded music output must be stereo PCM with a positive rate");
    }
    std::scoped_lock lock(mutex_);
    const auto output_frames = accumulator.size() / 2U;
    for (auto& [_, player] : players_) {
        if (!player.playing || !player.pcm.has_value()) continue;
        const auto& pcm = *player.pcm;
        const auto source_frames = pcm.Frames();
        if (source_frames == 0U || player.sample_rate == 0U) continue;
        const auto step = static_cast<double>(player.sample_rate) /
                          static_cast<double>(output_rate);
        for (std::size_t rendered = 0; rendered < output_frames; ++rendered) {
            if (player.position >= static_cast<double>(source_frames)) {
                if (player.looping) {
                    player.position = std::fmod(
                        player.position, static_cast<double>(source_frames));
                } else {
                    player.playing = false;
                    player.completed = true;
                    break;
                }
            }
            const auto first = static_cast<std::size_t>(player.position);
            const auto source_l = pcm.interleaved_samples[first * pcm.channels];
            const auto source_r =
                pcm.channels == 1U
                    ? source_l
                    : pcm.interleaved_samples[first * pcm.channels + 1U];
            accumulator[rendered * 2U] +=
                static_cast<std::int64_t>(source_l * player.left);
            accumulator[rendered * 2U + 1U] +=
                static_cast<std::int64_t>(source_r * player.right);
            player.position += step;
        }
    }
}

}  // namespace ogplay::audio
