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
    if (players_.size() >= kMaximumPlayers) throw std::length_error("music player limit reached");
    const auto id = next_++;
    players_[id] = {};
    return id;
}

void EncodedMusicMixer::Destroy(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found != players_.end() && found->second.prepare_task)
        found->second.prepare_task->Cancel();
    players_.erase(player);
}

void EncodedMusicMixer::Reset(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found != players_.end()) {
        if (found->second.prepare_task) found->second.prepare_task->Cancel();
        const auto generation = found->second.generation + 1U;
        const auto stream = found->second.audio_stream;
        found->second = {};
        found->second.audio_stream = stream;
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
    std::size_t total = encoded.size();
    for (const auto& [id, other] : players_) {
        if (id != player && other.encoded) total += other.encoded->size();
    }
    if (total > kMaximumMusicBytes) return false;
    if (found->second.prepare_task) found->second.prepare_task->Cancel();
    const auto generation = found->second.generation + 1U;
    const auto stream = found->second.audio_stream;
    found->second = {};
    found->second.audio_stream = stream;
    found->second.generation = generation;
    found->second.encoded = std::make_shared<const std::vector<std::byte>>(std::move(encoded));
    return true;
}

bool EncodedMusicMixer::Prepare(const std::uint32_t player) {
    if (!BeginPrepare(player)) return false;
    std::shared_ptr<PrepareTask> task;
    {
        std::scoped_lock lock(mutex_);
        const auto found = players_.find(player);
        if (found == players_.end()) return false;
        task = found->second.prepare_task;
    }
    if (!task) return false;
    {
        std::unique_lock lock(task->mutex);
        task->ready.wait(lock, [&] { return task->done; });
    }
    // Poll only this task. Reset/source replacement must not publish another one.
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || found->second.prepare_task != task) return false;
    auto& state = found->second;
    state.decoder = std::move(task->stream);
    state.prepare_task.reset();
    if (!state.decoder) return false;
    state.sample_rate = state.decoder->Rate();
    state.channels = state.decoder->Channels();
    state.total_frames = state.decoder->Frames();
    state.prepared = true;
    state.position = 0;
    state.completed = false;
    return true;
}

bool EncodedMusicMixer::BeginPrepare(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.encoded ||
        found->second.prepare_task) return false;
    found->second.decoder.reset();
    found->second.prepared = false;
    found->second.playing = false;
    found->second.decode_failed = false;
    auto task = std::make_shared<PrepareTask>();
    const auto encoded = found->second.encoded;
    // Raw task pointer is safe: the owner joins before destroying any task data.
    // No detached threads and no callback into the mixer from the worker.
    task->worker = std::jthread([work = task.get(), encoded](std::stop_token stop) {
        std::unique_ptr<EncodedAudioStream> stream;
        try { stream = std::make_unique<EncodedAudioStream>(encoded, stop); }
        catch (const std::exception&) {}
        {
            std::scoped_lock lock(work->mutex);
            work->stream = std::move(stream);
            work->done = true;
        }
        work->ready.notify_all();
    });
    found->second.prepare_task = std::move(task);
    return true;
}

EncodedMusicMixer::PrepareStatus EncodedMusicMixer::PollPrepare(
    const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.prepare_task)
        return PrepareStatus::failed;
    auto& state = found->second;
    auto task = state.prepare_task;
    {
        std::scoped_lock task_lock(task->mutex);
        if (!task->done) return PrepareStatus::pending;
        state.decoder = std::move(task->stream);
    }
    state.prepare_task.reset();
    if (!state.decoder) return PrepareStatus::failed;
    state.sample_rate = state.decoder->Rate();
    state.channels = state.decoder->Channels();
    state.total_frames = state.decoder->Frames();
    state.prepared = true;
    state.position = 0;
    state.completed = false;
    return PrepareStatus::ready;
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

void EncodedMusicMixer::SetAudioStream(std::uint32_t player, std::int32_t stream) {
    if (stream < 0 || stream >= 10) throw std::invalid_argument("invalid audio stream");
    std::scoped_lock lock(mutex_);
    players_.at(player).audio_stream = stream;
}
void EncodedMusicMixer::SetStreamGain(std::int32_t stream, float gain) {
    if (stream < 0 || stream >= 10 || !std::isfinite(gain) || gain < 0 || gain > 1)
        throw std::invalid_argument("invalid stream gain");
    std::scoped_lock lock(mutex_);
    stream_gains_[static_cast<std::size_t>(stream)] = gain;
}

bool EncodedMusicMixer::IsPlaying(const std::uint32_t player) const {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    return found != players_.end() && found->second.playing;
}

bool EncodedMusicMixer::HasEncoded(const std::uint32_t player) const {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    return found != players_.end() && found->second.encoded != nullptr;
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

bool EncodedMusicMixer::TakeDecodeFailure(std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.decode_failed) return false;
    found->second.decode_failed = false;
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
        if (player.encoded) bytes += player.encoded->size();
        if (player.decoder) bytes += player.decoder->BufferedPcmBytes();
        if (player.prepare_task) {
            std::scoped_lock task_lock(player.prepare_task->mutex);
            if (player.prepare_task->stream)
                bytes += player.prepare_task->stream->BufferedPcmBytes();
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
        if (!player.playing || !player.decoder) continue;
        const auto source_frames = player.decoder->Frames();
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
            std::array<std::int16_t, 2> samples;
            try {
                samples = player.decoder->Sample(first);
            } catch (const std::exception&) {
                player.playing = false;
                player.decode_failed = true;
                break;
            }
            const auto gain = stream_gains_[static_cast<std::size_t>(player.audio_stream)];
            const auto source_l = samples[0] * gain;
            const auto source_r = samples[1] * gain;
            accumulator[rendered * 2U] +=
                static_cast<std::int64_t>(source_l * player.left);
            accumulator[rendered * 2U + 1U] +=
                static_cast<std::int64_t>(source_r * player.right);
            player.position += step;
        }
    }
}

}  // namespace ogplay::audio
