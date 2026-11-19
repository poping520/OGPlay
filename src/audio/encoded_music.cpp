#include "ogplay/audio/encoded_music.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>

#include "ogplay/audio/encoded_audio.h"

namespace ogplay::audio {
namespace {

class MemoryEncodedSource final : public EncodedAudioDataSource {
public:
    explicit MemoryEncodedSource(std::vector<std::byte> bytes)
        : bytes_(std::make_shared<const std::vector<std::byte>>(std::move(bytes))) {}
    [[nodiscard]] std::uint64_t Size() const noexcept override { return bytes_->size(); }
    [[nodiscard]] std::size_t ReadAt(
        const std::uint64_t offset, const std::span<std::byte> destination,
        const std::stop_token stop) const override {
        if (stop.stop_requested() || offset >= bytes_->size()) return 0;
        const auto count = std::min<std::size_t>(
            destination.size(), bytes_->size() - static_cast<std::size_t>(offset));
        std::copy_n(bytes_->begin() + static_cast<std::ptrdiff_t>(offset), count,
                    destination.begin());
        return count;
    }
private:
    std::shared_ptr<const std::vector<std::byte>> bytes_;
};

[[nodiscard]] std::int32_t FramesToMs(const std::size_t frames,
                                      const std::uint32_t rate) {
    if (rate == 0U) return 0;
    return static_cast<std::int32_t>(frames * 1000U / rate);
}

}  // namespace

EncodedMusicMixer::EncodedMusicMixer(ExternalAudioDecoder decoder) : external_decoder_(std::move(decoder)) {}

std::uint32_t EncodedMusicMixer::Create() {
    std::scoped_lock lock(mutex_);
    const auto id = next_++;
    players_[id] = {};
    ++stats_.created_players;
    return id;
}

void EncodedMusicMixer::Destroy(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end()) return;
    if (found->second.prepare_task) found->second.prepare_task->Cancel();
    reserved_voices_ -= found->second.has_voice;
    players_.erase(found);
}

void EncodedMusicMixer::Reset(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found != players_.end()) {
        if (found->second.prepare_task) found->second.prepare_task->Cancel();
        const auto generation = found->second.generation + 1U;
        const auto stream = found->second.audio_stream;
        reserved_voices_ -= found->second.has_voice;
        found->second = {};
        found->second.audio_stream = stream;
        found->second.generation = generation;
    }
}

bool EncodedMusicMixer::SetEncoded(const std::uint32_t player,
                                   std::vector<std::byte> encoded) {
    if (encoded.empty() || encoded.size() > kMaximumEncodedAudioBytes) return false;
    return SetSource(player,
                     std::make_shared<MemoryEncodedSource>(std::move(encoded)));
}

bool EncodedMusicMixer::SetSource(
    const std::uint32_t player,
    std::shared_ptr<const EncodedAudioDataSource> source) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !source || source->Size() == 0 ||
        source->Size() > kMaximumEncodedAudioBytes) {
        return false;
    }
    std::uint64_t total = source->Size();
    for (const auto& [id, other] : players_) {
        if (id != player && other.source) total += other.source->Size();
    }
    if (total > kMaximumMusicBytes) return false;
    if (found->second.prepare_task) found->second.prepare_task->Cancel();
    const auto generation = found->second.generation + 1U;
    const auto stream = found->second.audio_stream;
    reserved_voices_ -= found->second.has_voice;
    found->second = {};
    found->second.audio_stream = stream;
    found->second.generation = generation;
    found->second.source = std::move(source);
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
    state.prepare_failure = std::move(task->failure);
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
    if (found == players_.end() || !found->second.source ||
        found->second.prepare_task) return false;
    found->second.decoder.reset();
    found->second.prepared = false;
    found->second.playing = false;
    found->second.decode_failed = false;
    found->second.prepare_failure.reset();
    auto task = std::make_shared<PrepareTask>();
    const auto source = found->second.source;
    // Raw task pointer is safe: the owner joins before destroying any task data.
    // No detached threads and no callback into the mixer from the worker.
    task->worker = std::jthread([work = task.get(), source, decoder = external_decoder_](std::stop_token stop) {
        std::unique_ptr<EncodedAudioStream> stream;
        std::optional<AudioDecodeFailure> failure;
        try {
            stream = std::make_unique<EncodedAudioStream>(source, stop, decoder);
        } catch (const AudioDecodeError& error) {
            failure = error.Failure();
        } catch (const std::exception& error) {
            failure = AudioDecodeFailure{error.what(), {}};
        }
        {
            std::scoped_lock lock(work->mutex);
            work->stream = std::move(stream);
            work->failure = std::move(failure);
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
        state.prepare_failure = std::move(task->failure);
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

std::optional<AudioDecodeFailure> EncodedMusicMixer::PrepareFailure(std::uint32_t player) const {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    return found == players_.end() ? std::nullopt : found->second.prepare_failure;
}

EncodedMusicMixer::StartStatus EncodedMusicMixer::Start(const std::uint32_t player) {
    std::scoped_lock lock(mutex_);
    const auto found = players_.find(player);
    if (found == players_.end() || !found->second.prepared) return StartStatus::not_prepared;
    if (!found->second.has_voice) {
        if (reserved_voices_ >= kMaximumVoices) return StartStatus::voice_limit;
        found->second.has_voice = true;
        ++reserved_voices_;
        stats_.peak_reserved_voices = std::max(stats_.peak_reserved_voices, reserved_voices_);
    }
    if (found->second.completed ||
        found->second.position >= static_cast<double>(found->second.total_frames)) {
        found->second.position = 0.0;
    }
    found->second.playing = true;
    found->second.completed = false;
    return StartStatus::started;
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
    return found != players_.end() && found->second.source != nullptr;
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
        if (player.source) bytes += static_cast<std::size_t>(player.source->Size());
        if (player.decoder) bytes += player.decoder->BufferedPcmBytes();
        if (player.prepare_task) {
            std::scoped_lock task_lock(player.prepare_task->mutex);
            if (player.prepare_task->stream)
                bytes += player.prepare_task->stream->BufferedPcmBytes();
        }
    }
    return bytes;
}

EncodedMusicMixer::PlaybackStats EncodedMusicMixer::Stats() const {
    std::scoped_lock lock(mutex_);
    return stats_;
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
        // MediaPlayer accepts any stream ID. Use the music gain for IDs without a mixer slot.
        const auto stream = static_cast<std::size_t>(player.audio_stream);
        const auto gain = stream_gains_[stream < stream_gains_.size() ? stream : 3U];
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
