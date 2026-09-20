#include "ogplay/audio/java_sound_pool_mixer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ogplay/audio/encoded_audio.h"
#include "ogplay/audio/pcm_mix.h"

namespace ogplay::audio {

JavaSoundPoolMixer::JavaSoundPoolMixer(EncodedResourceLoader loader)
    : loader_(std::move(loader)) {}

bool JavaSoundPoolMixer::Enabled() const noexcept {
    return static_cast<bool>(loader_);
}

std::uint32_t JavaSoundPoolMixer::CreatePool(const std::int32_t max_streams, const std::int32_t stream) {
    if (stream < 0 || stream >= 10) throw std::invalid_argument("invalid audio stream");
    std::scoped_lock lock(mutex_);
    const auto id = next_pool_++;
    pools_[id] = Pool{std::max(max_streams, 1), 1, {}, stream};
    return id;
}

void JavaSoundPoolMixer::DestroyPool(const std::uint32_t pool) {
    std::scoped_lock lock(mutex_);
    pools_.erase(pool);
    std::erase_if(voices_, [pool](const Voice& voice) {
        return voice.pool == pool;
    });
    CollectUnusedResourcesLocked();
}

bool JavaSoundPoolMixer::Load(const std::int32_t resource) {
    return Load(EncodedAudioSource::Resource(resource));
}

std::int32_t JavaSoundPoolMixer::LoadSample(
    const std::uint32_t pool, const EncodedAudioSource& source) {
    {
        std::scoped_lock lock(mutex_);
        if (!pools_.contains(pool)) return 0;
    }
    if (!Load(source)) return 0;
    std::scoped_lock lock(mutex_);
    const auto found = pools_.find(pool);
    if (found == pools_.end()) return 0;
    const auto sound = found->second.next_sound++;
    found->second.samples[sound] = source;
    return sound;
}

bool JavaSoundPoolMixer::UnloadSample(const std::uint32_t pool,
                                      const std::int32_t sound) {
    std::scoped_lock lock(mutex_);
    const auto found = pools_.find(pool);
    if (found == pools_.end()) return false;
    const auto sample = found->second.samples.find(sound);
    if (sample == found->second.samples.end()) return false;
    found->second.samples.erase(sample);
    std::erase_if(voices_, [pool, sound](const Voice& voice) {
        return voice.pool == pool && voice.sound == sound;
    });
    CollectUnusedResourcesLocked();
    return true;
}

std::optional<EncodedAudioSource> JavaSoundPoolMixer::SampleSource(
    const std::uint32_t pool, const std::int32_t sound) const {
    std::scoped_lock lock(mutex_);
    const auto found = pools_.find(pool);
    if (found == pools_.end()) return std::nullopt;
    const auto sample = found->second.samples.find(sound);
    return sample == found->second.samples.end()
               ? std::nullopt
               : std::optional<EncodedAudioSource>{sample->second};
}

std::vector<EncodedAudioSource> JavaSoundPoolMixer::PoolSources(
    const std::uint32_t pool) const {
    std::scoped_lock lock(mutex_);
    std::vector<EncodedAudioSource> result;
    const auto found = pools_.find(pool);
    if (found == pools_.end()) return result;
    result.reserve(found->second.samples.size());
    for (const auto& [_, source] : found->second.samples) result.push_back(source);
    return result;
}

void JavaSoundPoolMixer::CollectUnusedResourcesLocked() {
    for (auto resource = resources_.begin(); resource != resources_.end();) {
        const bool referenced = std::ranges::any_of(
            pools_, [&resource](const auto& item) {
                return std::ranges::any_of(
                    item.second.samples, [&resource](const auto& sample) {
                        return sample.second == resource->first;
                    });
            });
        if (referenced) {
            ++resource;
        } else {
            failures_.erase(resource->first);
            resource = resources_.erase(resource);
        }
    }
}

bool JavaSoundPoolMixer::Load(const EncodedAudioSource& source) {
    if (!Enabled() ||
        (source.kind == EncodedAudioSource::Kind::resource &&
         source.resource < 0)) {
        return false;
    }
    {
        std::scoped_lock lock(mutex_);
        if (resources_.contains(source)) return true;
    }
    EncodedResource encoded;
    try {
        encoded = loader_(source);
        if (encoded.bytes.empty() ||
            encoded.bytes.size() > kMaximumEncodedAudioBytes) {
            std::scoped_lock lock(mutex_);
            failures_[source] = "encoded audio source is unavailable";
            return false;
        }
        auto decoded = DecodeEncodedAudio(encoded.bytes);
        std::scoped_lock lock(mutex_);
        if (resources_.contains(source)) return true;
        std::size_t cached = decoded.interleaved_samples.size() * sizeof(std::int16_t);
        for (const auto& [_, pcm] : resources_) {
            cached += pcm.interleaved_samples.size() * sizeof(std::int16_t);
        }
        if (cached > kMaximumDecodedPcmBytes) {
            failures_[source] = "decoded audio cache exceeds the process budget";
            return false;
        }
        resources_.emplace(source, std::move(decoded));
        failures_.erase(source);
        return true;
    } catch (const std::exception& error) {
        std::scoped_lock lock(mutex_);
        failures_[source] = error.what();
        return false;
    }
}

void JavaSoundPoolMixer::Unload(const std::int32_t resource) {
    Unload(EncodedAudioSource::Resource(resource));
}

void JavaSoundPoolMixer::Unload(const EncodedAudioSource& source) {
    std::scoped_lock lock(mutex_);
    resources_.erase(source);
    failures_.erase(source);
    std::erase_if(voices_, [&source](const Voice& voice) {
        return voice.source == source;
    });
}

std::vector<JavaSoundPoolMixer::Voice>::iterator JavaSoundPoolMixer::FindVoice(
    const JavaSoundPoolKind kind, const EncodedAudioSource& source,
    const std::int32_t instance) {
    return std::ranges::find_if(
        voices_, [kind, &source, instance](const Voice& voice) {
            return voice.kind == kind && voice.source == source &&
                   voice.instance == instance;
        });
}

bool JavaSoundPoolMixer::Play(const JavaSoundPoolKind kind,
                              const std::int32_t resource,
                              const std::int32_t instance,
                              const float volume, const bool looping) {
    return Play(kind, EncodedAudioSource::Resource(resource), instance,
                volume, looping);
}

bool JavaSoundPoolMixer::Play(const JavaSoundPoolKind kind,
                              const EncodedAudioSource& source,
                              const std::int32_t instance,
                              const float volume, const bool looping) {
    std::scoped_lock lock(mutex_);
    if (!resources_.contains(source) || !std::isfinite(volume) ||
        volume < 0.0F || volume > 1.0F) {
        return false;
    }
    const auto found = FindVoice(kind, source, instance);
    if (found == voices_.end()) {
        voices_.push_back({kind,
                           source,
                           0U,
                           instance,
                           instance,
                           0,
                           0,
                           looping ? -1 : 0,
                           next_age_++,
                           0.0,
                           volume,
                           volume,
                           volume,
                           1.0F,
                           false,
                           false,
                           looping});
    } else {
        found->position = 0.0;
        found->volume = volume;
        found->left = volume;
        found->right = volume;
        found->pitch = 1.0F;
        found->paused = false;
        found->looping = looping;
        found->loops_remaining = looping ? -1 : 0;
    }
    return true;
}

std::int32_t JavaSoundPoolMixer::PlaySample(
    const std::uint32_t pool, const std::int32_t sound, const float left,
    const float right, const std::int32_t priority, const std::int32_t loop,
    const float rate) {
    std::scoped_lock lock(mutex_);
    const auto found = pools_.find(pool);
    if (found == pools_.end()) return 0;
    const auto sample = found->second.samples.find(sound);
    if (sample == found->second.samples.end() ||
        !resources_.contains(sample->second) || !std::isfinite(left) ||
        !std::isfinite(right) || !std::isfinite(rate) || left < 0.0F ||
        left > 1.0F || right < 0.0F || right > 1.0F || rate < 0.5F ||
        rate > 2.0F) {
        return 0;
    }
    std::size_t active{};
    std::size_t victim = voices_.size();
    for (std::size_t index = 0; index < voices_.size(); ++index) {
        if (voices_[index].pool != pool || voices_[index].paused) continue;
        ++active;
        if (victim == voices_.size() ||
            voices_[index].priority < voices_[victim].priority ||
            (voices_[index].priority == voices_[victim].priority &&
             voices_[index].age < voices_[victim].age)) {
            victim = index;
        }
    }
    if (active >= static_cast<std::size_t>(found->second.max_streams)) {
        if (victim == voices_.size() || voices_[victim].priority > priority) {
            return 0;
        }
        voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(victim));
    }
    const auto stream = next_stream_++;
    voices_.push_back({JavaSoundPoolKind::pool,
                       sample->second,
                       pool,
                       stream,
                       stream,
                       sound,
                       priority,
                       loop,
                       next_age_++,
                       0.0,
                       left,
                       right,
                       std::max(left, right),
                       rate,
                       false,
                       false,
                       loop != 0});
    return stream;
}

void JavaSoundPoolMixer::Pause(const JavaSoundPoolKind kind,
                               const std::int32_t resource,
                               const std::int32_t instance) {
    Pause(kind, EncodedAudioSource::Resource(resource), instance);
}

void JavaSoundPoolMixer::Pause(const JavaSoundPoolKind kind,
                               const EncodedAudioSource& source,
                               const std::int32_t instance) {
    std::scoped_lock lock(mutex_);
    if (const auto found = FindVoice(kind, source, instance);
        found != voices_.end()) {
        found->paused = true;
    }
}

void JavaSoundPoolMixer::Resume(const JavaSoundPoolKind kind,
                                const std::int32_t resource,
                                const std::int32_t instance) {
    Resume(kind, EncodedAudioSource::Resource(resource), instance);
}

void JavaSoundPoolMixer::Resume(const JavaSoundPoolKind kind,
                                const EncodedAudioSource& source,
                                const std::int32_t instance) {
    std::scoped_lock lock(mutex_);
    if (const auto found = FindVoice(kind, source, instance);
        found != voices_.end()) {
        found->paused = false;
    }
}

void JavaSoundPoolMixer::Stop(const JavaSoundPoolKind kind,
                              const std::int32_t resource,
                              const std::int32_t instance) {
    Stop(kind, EncodedAudioSource::Resource(resource), instance);
}

void JavaSoundPoolMixer::Stop(const JavaSoundPoolKind kind,
                              const EncodedAudioSource& source,
                              const std::int32_t instance) {
    std::scoped_lock lock(mutex_);
    if (const auto found = FindVoice(kind, source, instance);
        found != voices_.end()) {
        voices_.erase(found);
    }
}

void JavaSoundPoolMixer::SetVolume(const JavaSoundPoolKind kind,
                                   const std::int32_t resource,
                                   const std::int32_t instance,
                                   const float volume) {
    SetVolume(kind, EncodedAudioSource::Resource(resource), instance, volume);
}

void JavaSoundPoolMixer::SetVolume(const JavaSoundPoolKind kind,
                                   const EncodedAudioSource& source,
                                   const std::int32_t instance,
                                   const float volume) {
    std::scoped_lock lock(mutex_);
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) return;
    if (const auto found = FindVoice(kind, source, instance);
        found != voices_.end()) {
        found->volume = volume;
        found->left = volume;
        found->right = volume;
    }
}

void JavaSoundPoolMixer::SetStreamVolume(const std::int32_t stream,
                                         const float left, const float right) {
    std::scoped_lock lock(mutex_);
    if (!std::isfinite(left) || !std::isfinite(right) || left < 0.0F ||
        left > 1.0F || right < 0.0F || right > 1.0F) {
        return;
    }
    for (auto& voice : voices_) {
        if (voice.stream == stream) {
            voice.left = left;
            voice.right = right;
            voice.volume = std::max(left, right);
        }
    }
}

void JavaSoundPoolMixer::SetStreamLoop(const std::int32_t stream,
                                       const std::int32_t loop) {
    std::scoped_lock lock(mutex_);
    for (auto& voice : voices_) {
        if (voice.stream == stream) {
            voice.loops_remaining = loop;
            voice.looping = loop != 0;
        }
    }
}

void JavaSoundPoolMixer::SetStreamPriority(const std::int32_t stream,
                                           const std::int32_t priority) {
    std::scoped_lock lock(mutex_);
    for (auto& voice : voices_) {
        if (voice.stream == stream) voice.priority = priority;
    }
}

void JavaSoundPoolMixer::PauseStream(const std::int32_t stream) {
    std::scoped_lock lock(mutex_);
    for (auto& voice : voices_) {
        if (voice.stream == stream) voice.paused = true;
    }
}

void JavaSoundPoolMixer::ResumeStream(const std::int32_t stream) {
    std::scoped_lock lock(mutex_);
    for (auto& voice : voices_) {
        if (voice.stream == stream) voice.paused = false;
    }
}

void JavaSoundPoolMixer::StopStream(const std::int32_t stream) {
    std::scoped_lock lock(mutex_);
    std::erase_if(voices_, [stream](const Voice& voice) {
        return voice.stream == stream;
    });
}

void JavaSoundPoolMixer::SetStreamRate(const std::int32_t stream,
                                       const float rate) {
    std::scoped_lock lock(mutex_);
    if (!std::isfinite(rate) || rate < 0.5F || rate > 2.0F) return;
    for (auto& voice : voices_) {
        if (voice.stream == stream) voice.pitch = rate;
    }
}

void JavaSoundPoolMixer::SetPitch(const JavaSoundPoolKind kind,
                                  const std::int32_t resource,
                                  const std::int32_t instance,
                                  const float pitch) {
    std::scoped_lock lock(mutex_);
    if (!std::isfinite(pitch) || pitch < 0.5F || pitch > 2.0F) return;
    if (const auto found = FindVoice(
            kind, EncodedAudioSource::Resource(resource), instance);
        found != voices_.end()) {
        found->pitch = pitch;
    }
}

void JavaSoundPoolMixer::Reset(const JavaSoundPoolKind kind,
                               const std::int32_t resource,
                               const std::int32_t instance) {
    Reset(kind, EncodedAudioSource::Resource(resource), instance);
}

void JavaSoundPoolMixer::Reset(const JavaSoundPoolKind kind,
                               const EncodedAudioSource& source,
                               const std::int32_t instance) {
    std::scoped_lock lock(mutex_);
    if (const auto found = FindVoice(kind, source, instance);
        found != voices_.end()) {
        found->position = 0.0;
    }
}

void JavaSoundPoolMixer::StopAll(
    const JavaSoundPoolKind kind,
    const std::optional<std::int32_t> except_resource) {
    std::scoped_lock lock(mutex_);
    std::erase_if(voices_, [kind, except_resource](const Voice& voice) {
        return voice.kind == kind &&
               (!except_resource.has_value() ||
                voice.source !=
                    EncodedAudioSource::Resource(*except_resource));
    });
}

void JavaSoundPoolMixer::PauseAll(const JavaSoundPoolKind kind) {
    std::scoped_lock lock(mutex_);
    for (auto& voice : voices_) {
        if (voice.kind == kind) voice.paused = true;
    }
}

void JavaSoundPoolMixer::ResumeAll(const JavaSoundPoolKind kind) {
    std::scoped_lock lock(mutex_);
    for (auto& voice : voices_) {
        if (voice.kind == kind) voice.paused = false;
    }
}

void JavaSoundPoolMixer::AutoPausePool(const std::uint32_t pool) {
    std::scoped_lock lock(mutex_);
    for (auto& voice : voices_) {
        if (voice.pool == pool && !voice.paused) {
            voice.paused = true;
            voice.auto_paused = true;
        }
    }
}

void JavaSoundPoolMixer::AutoResumePool(const std::uint32_t pool) {
    std::scoped_lock lock(mutex_);
    for (auto& voice : voices_) {
        if (voice.pool == pool && voice.auto_paused) {
            voice.paused = false;
            voice.auto_paused = false;
        }
    }
}

void JavaSoundPoolMixer::StopAllSounds() {
    std::scoped_lock lock(mutex_);
    voices_.clear();
}

void JavaSoundPoolMixer::Destroy() {
    std::scoped_lock lock(mutex_);
    voices_.clear();
    resources_.clear();
    failures_.clear();
    pools_.clear();
}

void JavaSoundPoolMixer::MixIntoAccumulator(
    const std::span<std::int64_t> accumulator, const std::uint32_t output_rate) {
    if (output_rate == 0U || accumulator.size() % 2U != 0U) {
        throw std::invalid_argument(
            "SoundPool output must be stereo PCM with a positive rate");
    }
    std::scoped_lock lock(mutex_);
    const auto output_frames = accumulator.size() / 2U;
    for (auto voice = voices_.begin(); voice != voices_.end();) {
        const auto sound = resources_.find(voice->source);
        if (sound == resources_.end()) {
            voice = voices_.erase(voice);
            continue;
        }
        if (voice->paused) {
            ++voice;
            continue;
        }
        const auto& pcm = sound->second;
        const auto source_frames = pcm.Frames();
        if (source_frames == 0U) {
            voice = voices_.erase(voice);
            continue;
        }
        const auto step = static_cast<double>(pcm.sample_rate) /
                          static_cast<double>(output_rate) * voice->pitch;
        std::size_t rendered{};
        while (rendered < output_frames) {
            if (voice->position >= static_cast<double>(source_frames)) {
                if (voice->loops_remaining < 0) {
                    voice->position = std::fmod(
                        voice->position, static_cast<double>(source_frames));
                } else if (voice->loops_remaining > 0) {
                    --voice->loops_remaining;
                    voice->position = std::fmod(
                        voice->position, static_cast<double>(source_frames));
                } else {
                    break;
                }
            }
            const auto first = static_cast<std::size_t>(voice->position);
            const auto second = std::min(first + 1U, source_frames - 1U);
            const auto fraction = voice->position - static_cast<double>(first);
            for (std::size_t channel = 0; channel < 2U; ++channel) {
                const auto source_channel = pcm.channels == 1U ? 0U : channel;
                const auto a = pcm.interleaved_samples[
                    first * pcm.channels + source_channel];
                const auto b = pcm.interleaved_samples[
                    second * pcm.channels + source_channel];
                const auto sample = static_cast<double>(a) +
                    (static_cast<double>(b) - static_cast<double>(a)) *
                        fraction;
                accumulator[rendered * 2U + channel] +=
                    static_cast<std::int64_t>(
                        sample * (channel == 0U ? voice->left : voice->right) *
                        stream_gains_[static_cast<std::size_t>(
                            voice->pool == 0 ? 3 : pools_.at(voice->pool).audio_stream)]);
            }
            voice->position += step;
            ++rendered;
        }
        if (voice->position >= static_cast<double>(source_frames) &&
            voice->loops_remaining == 0) {
            voice = voices_.erase(voice);
        } else {
            ++voice;
        }
    }
}

std::size_t JavaSoundPoolMixer::RenderStereoPcm16(
    const std::span<std::int16_t> output, const std::uint32_t output_rate) {
    std::vector<std::int64_t> accumulator(output.size());
    MixIntoAccumulator(accumulator, output_rate);
    SaturateStereoPcm16(accumulator, output);
    return output.size() / 2U;
}

std::optional<std::string> JavaSoundPoolMixer::LoadFailure(
    const std::int32_t resource) const {
    return LoadFailure(EncodedAudioSource::Resource(resource));
}

std::optional<std::string> JavaSoundPoolMixer::LoadFailure(
    const EncodedAudioSource& source) const {
    std::scoped_lock lock(mutex_);
    const auto found = failures_.find(source);
    return found == failures_.end() ? std::nullopt
                                    : std::optional{found->second};
}

std::size_t JavaSoundPoolMixer::LoadedResourceCount() const {
    std::scoped_lock lock(mutex_);
    return resources_.size();
}

std::size_t JavaSoundPoolMixer::ActiveVoiceCount() const {
    std::scoped_lock lock(mutex_);
    return voices_.size();
}

void JavaSoundPoolMixer::SetStreamGain(std::int32_t stream, float gain) {
    if (stream < 0 || stream >= 10 || !std::isfinite(gain) || gain < 0 || gain > 1)
        throw std::invalid_argument("invalid stream gain");
    std::scoped_lock lock(mutex_);
    stream_gains_[static_cast<std::size_t>(stream)] = gain;
}
}  // namespace ogplay::audio
