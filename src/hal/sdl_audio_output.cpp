#include "ogplay/hal/audio.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if OGPLAY_HAS_SDL3
#include <SDL3/SDL.h>
#endif

namespace ogplay::hal {

#if OGPLAY_HAS_SDL3
namespace {

[[nodiscard]] std::string RequestedDriverName(const AudioBackend backend) {
    switch (backend) {
    case AudioBackend::automatic: return {};
    case AudioBackend::dummy: return "dummy";
    }
    throw std::invalid_argument("unknown audio backend");
}

[[nodiscard]] SDL_AudioFormat SdlFormat(const AudioSampleFormat format) {
    switch (format) {
    case AudioSampleFormat::signed_16_le: return SDL_AUDIO_S16LE;
    case AudioSampleFormat::float_32_le: return SDL_AUDIO_F32LE;
    }
    throw std::invalid_argument("unknown audio sample format");
}

[[nodiscard]] std::size_t BytesPerSample(const AudioSampleFormat format) {
    switch (format) {
    case AudioSampleFormat::signed_16_le: return sizeof(std::int16_t);
    case AudioSampleFormat::float_32_le: return sizeof(float);
    }
    throw std::invalid_argument("unknown audio sample format");
}

[[nodiscard]] std::runtime_error SdlError(
    const std::string_view operation) {
    return std::runtime_error(
        std::string(operation) + " failed: " + SDL_GetError());
}

class SdlAudioOutput final : public AudioOutput {
public:
    SdlAudioOutput(AudioStreamConfig config, const AudioBackend backend)
        : config_(config),
          frame_bytes_(BytesPerSample(config.format) * config.channels) {
        if (config.sample_rate == 0U || config.channels == 0U ||
            config.channels > 8U ||
            config.sample_rate >
                static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument("audio stream config is out of range");
        }
        const auto requested = RequestedDriverName(backend);
        const auto already_initialized =
            (SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) != 0U;
        if (already_initialized && !requested.empty()) {
            const auto* current = SDL_GetCurrentAudioDriver();
            if (current == nullptr || requested != current) {
                throw std::logic_error(
                    "SDL audio subsystem already uses another backend");
            }
        }
        if (!already_initialized && !requested.empty() &&
            !SDL_SetHint(SDL_HINT_AUDIO_DRIVER, requested.c_str())) {
            throw SdlError("SDL_SetHint(SDL_HINT_AUDIO_DRIVER)");
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            throw SdlError("SDL_InitSubSystem(SDL_INIT_AUDIO)");
        }
        initialized_ = true;
        const SDL_AudioSpec spec{
            SdlFormat(config.format), static_cast<int>(config.channels),
            static_cast<int>(config.sample_rate)};
        stream_ = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream_ == nullptr) {
            const auto error = SdlError("SDL_OpenAudioDeviceStream");
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            initialized_ = false;
            throw error;
        }
    }

    ~SdlAudioOutput() override {
        if (stream_ != nullptr) {
            if (started_) {
                static_cast<void>(SDL_PauseAudioStreamDevice(stream_));
            }
            SDL_DestroyAudioStream(stream_);
        }
        if (initialized_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    [[nodiscard]] AudioStreamConfig Config() const override {
        return config_;
    }
    [[nodiscard]] bool IsStarted() const override { return started_; }
    [[nodiscard]] std::uint64_t QueuedFrames() const override {
        const auto bytes = SDL_GetAudioStreamQueued(stream_);
        if (bytes < 0) throw SdlError("SDL_GetAudioStreamQueued");
        return static_cast<std::uint64_t>(bytes) / frame_bytes_;
    }
    void Start() override {
        if (started_) return;
        if (!SDL_ResumeAudioStreamDevice(stream_)) {
            throw SdlError("SDL_ResumeAudioStreamDevice");
        }
        started_ = true;
    }
    void Stop() override {
        if (!started_) return;
        if (!SDL_PauseAudioStreamDevice(stream_)) {
            throw SdlError("SDL_PauseAudioStreamDevice");
        }
        started_ = false;
    }
    void Submit(const std::span<const std::byte> interleaved_samples) override {
        if (interleaved_samples.size() % frame_bytes_ != 0U ||
            interleaved_samples.size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::invalid_argument(
                "audio submission is not a complete bounded frame set");
        }
        if (interleaved_samples.empty()) return;
        if (!SDL_PutAudioStreamData(
                stream_, interleaved_samples.data(),
                static_cast<int>(interleaved_samples.size()))) {
            throw SdlError("SDL_PutAudioStreamData");
        }
    }

private:
    AudioStreamConfig config_;
    std::size_t frame_bytes_{};
    SDL_AudioStream* stream_{};
    bool initialized_{};
    bool started_{};
};

}  // namespace
#endif

std::unique_ptr<AudioOutput> CreateSdlAudioOutput(
    const AudioStreamConfig config, const AudioBackend backend) {
#if OGPLAY_HAS_SDL3
    return std::make_unique<SdlAudioOutput>(config, backend);
#else
    static_cast<void>(config);
    static_cast<void>(backend);
    throw std::runtime_error("SDL3 support is disabled in this build");
#endif
}

}  // namespace ogplay::hal
