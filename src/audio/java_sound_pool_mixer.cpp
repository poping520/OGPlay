#include "ogplay/audio/java_sound_pool_mixer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ogplay::audio {

JavaSoundPoolMixer::JavaSoundPoolMixer(EncodedResourceLoader loader)
    : loader_(std::move(loader)) {}

bool JavaSoundPoolMixer::Enabled() const noexcept {
    return static_cast<bool>(loader_);
}

bool JavaSoundPoolMixer::Load(const std::int32_t resource) {
    std::scoped_lock lock(mutex_);
    if (!loader_ || resource < 0) return false;
    if (resources_.contains(resource)) return true;
    try {
        const auto encoded = loader_(resource);
        if (encoded.empty()) {
            failures_[resource] = "SoundPool encoded resource is unavailable";
            return false;
        }
        auto decoded = DecodeOggVorbis(encoded);
        resources_.emplace(resource, std::move(decoded));
        failures_.erase(resource);
        return true;
    } catch (const std::exception& error) {
        failures_[resource] = error.what();
        return false;
    }
}

void JavaSoundPoolMixer::Unload(const std::int32_t resource) {
    std::scoped_lock lock(mutex_);
    resources_.erase(resource);
    failures_.erase(resource);
    std::erase_if(voices_, [resource](const Voice& voice) {
        return voice.resource == resource;
    });
}

std::vector<JavaSoundPoolMixer::Voice>::iterator JavaSoundPoolMixer::FindVoice(
    const JavaSoundPoolKind kind, const std::int32_t resource,
    const std::int32_t instance) {
    return std::ranges::find_if(
        voices_, [kind, resource, instance](const Voice& voice) {
            return voice.kind == kind && voice.resource == resource &&
                   voice.instance == instance;
        });
}

bool JavaSoundPoolMixer::Play(const JavaSoundPoolKind kind,
                              const std::int32_t resource,
                              const std::int32_t instance,
                              const float volume) {
    std::scoped_lock lock(mutex_);
    if (!resources_.contains(resource) || !std::isfinite(volume) ||
        volume < 0.0F || volume > 1.0F) {
        return false;
    }
    const auto found = FindVoice(kind, resource, instance);
    if (found == voices_.end()) {
        voices_.push_back({kind, resource, instance, 0.0, volume, 1.0F,
                           false});
    } else {
        found->position = 0.0;
        found->volume = volume;
        found->pitch = 1.0F;
        found->paused = false;
    }
    return true;
}

void JavaSoundPoolMixer::Pause(const JavaSoundPoolKind kind,
                               const std::int32_t resource,
                               const std::int32_t instance) {
    std::scoped_lock lock(mutex_);
    if (const auto found = FindVoice(kind, resource, instance);
        found != voices_.end()) {
        found->paused = true;
    }
}

void JavaSoundPoolMixer::Resume(const JavaSoundPoolKind kind,
                                const std::int32_t resource,
                                const std::int32_t instance) {
    std::scoped_lock lock(mutex_);
    if (const auto found = FindVoice(kind, resource, instance);
        found != voices_.end()) {
        found->paused = false;
    }
}

void JavaSoundPoolMixer::Stop(const JavaSoundPoolKind kind,
                              const std::int32_t resource,
                              const std::int32_t instance) {
    std::scoped_lock lock(mutex_);
    if (const auto found = FindVoice(kind, resource, instance);
        found != voices_.end()) {
        voices_.erase(found);
    }
}

void JavaSoundPoolMixer::SetVolume(const JavaSoundPoolKind kind,
                                   const std::int32_t resource,
                                   const std::int32_t instance,
                                   const float volume) {
    std::scoped_lock lock(mutex_);
    if (!std::isfinite(volume) || volume < 0.0F || volume > 1.0F) return;
    if (const auto found = FindVoice(kind, resource, instance);
        found != voices_.end()) {
        found->volume = volume;
    }
}

void JavaSoundPoolMixer::SetPitch(const JavaSoundPoolKind kind,
                                  const std::int32_t resource,
                                  const std::int32_t instance,
                                  const float pitch) {
    std::scoped_lock lock(mutex_);
    if (!std::isfinite(pitch) || pitch < 0.5F || pitch > 2.0F) return;
    if (const auto found = FindVoice(kind, resource, instance);
        found != voices_.end()) {
        found->pitch = pitch;
    }
}

void JavaSoundPoolMixer::Reset(const JavaSoundPoolKind kind,
                               const std::int32_t resource,
                               const std::int32_t instance) {
    std::scoped_lock lock(mutex_);
    if (const auto found = FindVoice(kind, resource, instance);
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
                voice.resource != *except_resource);
    });
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
}

std::size_t JavaSoundPoolMixer::RenderStereoPcm16(
    const std::span<std::int16_t> output, const std::uint32_t output_rate) {
    if (output_rate == 0U || output.size() % 2U != 0U) {
        throw std::invalid_argument(
            "SoundPool output must be stereo PCM with a positive rate");
    }
    std::scoped_lock lock(mutex_);
    std::ranges::fill(output, std::int16_t{});
    const auto output_frames = output.size() / 2U;
    mix_scratch_.assign(output.size(), 0);
    for (auto voice = voices_.begin(); voice != voices_.end();) {
        const auto sound = resources_.find(voice->resource);
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
        const auto step = static_cast<double>(pcm.sample_rate) /
                          static_cast<double>(output_rate) * voice->pitch;
        std::size_t rendered{};
        while (rendered < output_frames &&
               voice->position < static_cast<double>(source_frames)) {
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
                mix_scratch_[rendered * 2U + channel] +=
                    static_cast<std::int64_t>(sample * voice->volume);
            }
            voice->position += step;
            ++rendered;
        }
        if (voice->position >= static_cast<double>(source_frames)) {
            voice = voices_.erase(voice);
        } else {
            ++voice;
        }
    }
    for (std::size_t index = 0; index < output.size(); ++index) {
        output[index] = static_cast<std::int16_t>(std::clamp(
            mix_scratch_[index],
            static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::min()),
            static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max())));
    }
    return output_frames;
}

std::optional<std::string> JavaSoundPoolMixer::LoadFailure(
    const std::int32_t resource) const {
    std::scoped_lock lock(mutex_);
    const auto found = failures_.find(resource);
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

}  // namespace ogplay::audio
