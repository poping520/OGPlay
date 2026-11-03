#pragma once

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace sweetshop {

constexpr int kQueuedBuffers = 3;

enum class Sfx { select, swap, invalid, match, create, line, bomb, rainbow, win, lose, star, click, count };

// One OpenSL ES buffer-queue player fed by a software mixer. Music and effects are
// synthesized at startup on a worker thread, so the APK carries no audio files.
class Audio {
public:
    ~Audio();
    bool Init();
    void Shutdown();
    void Play(Sfx sfx, int variant = 0);
    void SetEnabled(bool enabled) { enabled_.store(enabled); }
    [[nodiscard]] bool Enabled() const { return enabled_.load(); }
    void Pause();
    void Resume();

private:
    struct Voice {
        const std::vector<std::int16_t>* samples = nullptr;
        double position = 0;
        double step = 1;
        float volume = 1;
    };

    static void OnBuffer(SLAndroidSimpleBufferQueueItf queue, void* context);
    void Mix(std::int16_t* out, int frames);
    void Synthesize();

    SLObjectItf engine_object_ = nullptr, mix_object_ = nullptr, player_object_ = nullptr;
    SLEngineItf engine_ = nullptr;
    SLPlayItf play_ = nullptr;
    SLAndroidSimpleBufferQueueItf queue_ = nullptr;
    std::vector<std::int16_t> buffers_[kQueuedBuffers];
    int next_buffer_ = 0;

    std::thread synth_thread_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> enabled_{true};
    std::vector<std::int16_t> music_;
    std::vector<std::int16_t> effects_[static_cast<int>(Sfx::count)];
    double music_position_ = 0;
    std::mutex mutex_;
    std::vector<Voice> voices_;
};

}  // namespace sweetshop
