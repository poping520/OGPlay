#include "audio.h"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace sweetshop {
namespace {

constexpr int kSynthRate = 22050;
constexpr int kOutputRate = 44100;
// A new sound waits behind every queued buffer: 3 x 512 frames keeps that under ~35 ms
// while leaving slack for a late callback.
constexpr int kFramesPerBuffer = 512;
constexpr double kTau = 6.283185307179586;
constexpr float kMusicVolume = 0.42F;
constexpr float kEffectVolume = 0.8F;

using Buffer = std::vector<float>;
enum class Instrument { marimba, bell, bass, tick };

double Frequency(const int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

// Adds one note; with `wrap` the tail continues at the start so the music loops seamlessly.
void Note(Buffer& out, const double start, const int midi, const double duration, const Instrument instrument,
          const float volume, const bool wrap = false) {
    const double frequency = Frequency(midi);
    const int first = static_cast<int>(start * kSynthRate);
    const int count = static_cast<int>(duration * kSynthRate);
    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / kSynthRate;
        const double attack = std::min(1.0, t / 0.004);
        double sample = 0;
        switch (instrument) {
        case Instrument::marimba:
            sample = attack * std::exp(-t * 7.0) *
                     (std::sin(kTau * frequency * t) + 0.2 * std::exp(-t * 25.0) * std::sin(kTau * 4.0 * frequency * t));
            break;
        case Instrument::bell:
            sample = attack * std::exp(-t * 4.0) *
                     (std::sin(kTau * frequency * t) + 0.35 * std::sin(kTau * 2.76 * frequency * t) +
                      0.12 * std::sin(kTau * 5.4 * frequency * t));
            break;
        case Instrument::bass:
            sample = std::min(1.0, t / 0.012) * std::exp(-t * 3.0) *
                     (std::sin(kTau * frequency * t) + 0.25 * std::sin(kTau * 2.0 * frequency * t));
            break;
        case Instrument::tick:
            sample = attack * std::exp(-t * 18.0) * std::sin(kTau * frequency * t);
            break;
        }
        int index = first + i;
        if (wrap) index %= static_cast<int>(out.size());
        else if (index >= static_cast<int>(out.size())) break;
        out[index] += static_cast<float>(sample * volume);
    }
}

// Pitch glide used for pops and thumps.
void Glide(Buffer& out, const double start, const double from, const double to, const double duration,
           const double decay, const float volume) {
    const int first = static_cast<int>(start * kSynthRate);
    const int count = static_cast<int>(duration * kSynthRate);
    double phase = 0;
    for (int i = 0; i < count && first + i < static_cast<int>(out.size()); ++i) {
        const double t = static_cast<double>(i) / kSynthRate;
        phase += kTau * (from + (to - from) * (t / duration)) / kSynthRate;
        out[first + i] += static_cast<float>(std::min(1.0, t / 0.003) * std::exp(-t * decay) * std::sin(phase) * volume);
    }
}

void Noise(Buffer& out, const double start, const double duration, const double decay, const double brightness,
           const float volume) {
    std::mt19937 random(7);
    std::uniform_real_distribution<float> white(-1.0F, 1.0F);
    const int first = static_cast<int>(start * kSynthRate);
    const int count = static_cast<int>(duration * kSynthRate);
    float low = 0;
    for (int i = 0; i < count && first + i < static_cast<int>(out.size()); ++i) {
        const double t = static_cast<double>(i) / kSynthRate;
        const float cutoff = static_cast<float>(brightness * (1.0 - t / duration) + 0.02);
        low += cutoff * (white(random) - low);
        out[first + i] += static_cast<float>(std::exp(-t * decay) * low * volume);
    }
}

std::vector<std::int16_t> Finish(const Buffer& buffer, const float peak_target) {
    float peak = 0.0001F;
    for (const float sample : buffer) peak = std::max(peak, std::abs(sample));
    std::vector<std::int16_t> result(buffer.size());
    const float gain = peak_target * 32767.0F / peak;
    for (std::size_t i = 0; i < buffer.size(); ++i) result[i] = static_cast<std::int16_t>(buffer[i] * gain);
    return result;
}

Buffer Seconds(const double duration) { return Buffer(static_cast<std::size_t>(duration * kSynthRate), 0.0F); }

}  // namespace

Audio::~Audio() { Shutdown(); }

bool Audio::Init() {
    if (engine_object_ != nullptr) return true;
    if (slCreateEngine(&engine_object_, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS ||
        (*engine_object_)->Realize(engine_object_, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
        (*engine_object_)->GetInterface(engine_object_, SL_IID_ENGINE, &engine_) != SL_RESULT_SUCCESS ||
        (*engine_)->CreateOutputMix(engine_, &mix_object_, 0, nullptr, nullptr) != SL_RESULT_SUCCESS ||
        (*mix_object_)->Realize(mix_object_, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_WARN, "sweetshop", "OpenSL ES engine unavailable; playing silently");
        Shutdown();
        return false;
    }
    SLDataLocator_AndroidSimpleBufferQueue locator{SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, kQueuedBuffers};
    SLDataFormat_PCM format{SL_DATAFORMAT_PCM,           1,
                            SL_SAMPLINGRATE_44_1,        SL_PCMSAMPLEFORMAT_FIXED_16,
                            SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER,
                            SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource source{&locator, &format};
    SLDataLocator_OutputMix output{SL_DATALOCATOR_OUTPUTMIX, mix_object_};
    SLDataSink sink{&output, nullptr};
    const SLInterfaceID interfaces[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean required[1] = {SL_BOOLEAN_TRUE};
    if ((*engine_)->CreateAudioPlayer(engine_, &player_object_, &source, &sink, 1, interfaces, required) !=
            SL_RESULT_SUCCESS ||
        (*player_object_)->Realize(player_object_, SL_BOOLEAN_FALSE) != SL_RESULT_SUCCESS ||
        (*player_object_)->GetInterface(player_object_, SL_IID_PLAY, &play_) != SL_RESULT_SUCCESS ||
        (*player_object_)->GetInterface(player_object_, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &queue_) !=
            SL_RESULT_SUCCESS ||
        (*queue_)->RegisterCallback(queue_, OnBuffer, this) != SL_RESULT_SUCCESS) {
        __android_log_print(ANDROID_LOG_WARN, "sweetshop", "OpenSL ES player unavailable; playing silently");
        Shutdown();
        return false;
    }
    for (auto& buffer : buffers_) {
        buffer.assign(kFramesPerBuffer, 0);
        (*queue_)->Enqueue(queue_, buffer.data(), static_cast<SLuint32>(buffer.size() * sizeof(std::int16_t)));
    }
    (*play_)->SetPlayState(play_, SL_PLAYSTATE_PLAYING);
    synth_thread_ = std::thread([this] { Synthesize(); });
    return true;
}

void Audio::Shutdown() {
    if (synth_thread_.joinable()) synth_thread_.join();
    if (player_object_ != nullptr) (*player_object_)->Destroy(player_object_);
    if (mix_object_ != nullptr) (*mix_object_)->Destroy(mix_object_);
    if (engine_object_ != nullptr) (*engine_object_)->Destroy(engine_object_);
    player_object_ = mix_object_ = engine_object_ = nullptr;
    engine_ = nullptr;
    play_ = nullptr;
    queue_ = nullptr;
}

void Audio::Pause() {
    if (play_ != nullptr) (*play_)->SetPlayState(play_, SL_PLAYSTATE_PAUSED);
}

void Audio::Resume() {
    if (play_ != nullptr) (*play_)->SetPlayState(play_, SL_PLAYSTATE_PLAYING);
}

void Audio::Play(const Sfx sfx, const int variant) {
    if (!ready_.load() || !enabled_.load()) return;
    static constexpr int kPentatonic[] = {0, 2, 4, 7, 9, 12, 14, 16, 19};
    const int step = std::clamp(variant, 0, static_cast<int>(std::size(kPentatonic)) - 1);
    Voice voice;
    voice.samples = &effects_[static_cast<int>(sfx)];
    voice.step = static_cast<double>(kSynthRate) / kOutputRate *
                 (sfx == Sfx::match ? std::pow(2.0, kPentatonic[step] / 12.0) : 1.0);
    voice.volume = kEffectVolume;
    std::lock_guard<std::mutex> lock(mutex_);
    if (voices_.size() >= 12) voices_.erase(voices_.begin());
    voices_.push_back(voice);
}

void Audio::OnBuffer(SLAndroidSimpleBufferQueueItf queue, void* context) {
    auto* self = static_cast<Audio*>(context);
    auto& buffer = self->buffers_[self->next_buffer_];
    self->next_buffer_ = (self->next_buffer_ + 1) % kQueuedBuffers;
    self->Mix(buffer.data(), static_cast<int>(buffer.size()));
    (*queue)->Enqueue(queue, buffer.data(), static_cast<SLuint32>(buffer.size() * sizeof(std::int16_t)));
}

void Audio::Mix(std::int16_t* out, const int frames) {
    std::vector<float> mix(static_cast<std::size_t>(frames), 0.0F);
    if (ready_.load() && enabled_.load() && !music_.empty()) {
        const double step = static_cast<double>(kSynthRate) / kOutputRate;
        const auto length = static_cast<double>(music_.size());
        for (int frame = 0; frame < frames; ++frame) {
            const auto index = static_cast<std::size_t>(music_position_);
            const double fraction = music_position_ - static_cast<double>(index);
            const float a = music_[index], b = music_[(index + 1) % music_.size()];
            mix[frame] += (a + (b - a) * static_cast<float>(fraction)) * kMusicVolume;
            music_position_ += step;
            if (music_position_ >= length) music_position_ -= length;
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& voice : voices_) {
            const auto& samples = *voice.samples;
            for (int frame = 0; frame < frames; ++frame) {
                const auto index = static_cast<std::size_t>(voice.position);
                if (index >= samples.size()) break;
                mix[frame] += samples[index] * voice.volume;
                voice.position += voice.step;
            }
        }
        voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                     [](const Voice& voice) {
                                         return static_cast<std::size_t>(voice.position) >= voice.samples->size();
                                     }),
                      voices_.end());
    }
    for (int frame = 0; frame < frames; ++frame)
        out[frame] = static_cast<std::int16_t>(std::clamp(mix[frame], -32767.0F, 32767.0F));
}

void Audio::Synthesize() {
    const auto effect = [this](Sfx sfx) -> std::vector<std::int16_t>& { return effects_[static_cast<int>(sfx)]; };
    {
        Buffer b = Seconds(0.2);
        Note(b, 0, 91, 0.2, Instrument::marimba, 1.0F);
        effect(Sfx::select) = Finish(b, 0.35F);
    }
    {
        Buffer b = Seconds(0.07);
        Glide(b, 0, 900, 1400, 0.07, 40, 1.0F);
        effect(Sfx::click) = Finish(b, 0.35F);
    }
    {
        Buffer b = Seconds(0.12);
        Glide(b, 0, 380, 780, 0.12, 22, 1.0F);
        effect(Sfx::swap) = Finish(b, 0.45F);
    }
    {
        Buffer b = Seconds(0.35);
        Note(b, 0, 52, 0.18, Instrument::bass, 1.0F);
        Note(b, 0.13, 48, 0.22, Instrument::bass, 1.0F);
        effect(Sfx::invalid) = Finish(b, 0.55F);
    }
    {
        Buffer b = Seconds(0.45);
        Note(b, 0, 72, 0.45, Instrument::marimba, 1.0F);
        Note(b, 0, 84, 0.35, Instrument::bell, 0.25F);
        effect(Sfx::match) = Finish(b, 0.5F);
    }
    {
        Buffer b = Seconds(0.6);
        const int notes[] = {84, 88, 91, 96};
        for (int i = 0; i < 4; ++i) Note(b, i * 0.06, notes[i], 0.4, Instrument::bell, 0.7F);
        effect(Sfx::create) = Finish(b, 0.5F);
    }
    {
        Buffer b = Seconds(0.45);
        Noise(b, 0, 0.45, 6, 0.6, 1.0F);
        Glide(b, 0, 600, 1800, 0.3, 8, 0.4F);
        effect(Sfx::line) = Finish(b, 0.55F);
    }
    {
        Buffer b = Seconds(0.6);
        Glide(b, 0, 170, 45, 0.5, 5, 1.0F);
        Noise(b, 0, 0.5, 7, 0.25, 0.8F);
        effect(Sfx::bomb) = Finish(b, 0.7F);
    }
    {
        Buffer b = Seconds(1.0);
        const int notes[] = {84, 88, 91, 96, 100, 103};
        for (int i = 0; i < 6; ++i) Note(b, i * 0.07, notes[i], 0.55, Instrument::bell, 0.6F);
        Noise(b, 0, 0.6, 5, 0.9, 0.15F);
        effect(Sfx::rainbow) = Finish(b, 0.6F);
    }
    {
        Buffer b = Seconds(0.7);
        Note(b, 0, 88, 0.7, Instrument::bell, 1.0F);
        effect(Sfx::star) = Finish(b, 0.5F);
    }
    {
        Buffer b = Seconds(1.6);
        const int notes[] = {72, 76, 79, 84};
        for (int i = 0; i < 4; ++i) Note(b, i * 0.11, notes[i], 0.5, Instrument::marimba, 1.0F);
        for (const int note : {84, 88, 91}) Note(b, 0.48, note, 1.1, Instrument::bell, 0.45F);
        effect(Sfx::win) = Finish(b, 0.6F);
    }
    {
        Buffer b = Seconds(1.2);
        const int notes[] = {67, 64, 60, 55};
        for (int i = 0; i < 4; ++i) Note(b, i * 0.2, notes[i], 0.5, Instrument::marimba, 1.0F);
        Note(b, 0.6, 43, 0.6, Instrument::bass, 0.6F);
        effect(Sfx::lose) = Finish(b, 0.55F);
    }

    // Eight bars at 104 BPM over C - Am - F - G; the melody resolves back into bar one.
    const double beat = 60.0 / 104.0;
    Buffer music = Seconds(beat * 32);
    const int roots[4] = {48, 45, 41, 43};
    const int chords[4][3] = {{60, 64, 67}, {57, 60, 64}, {53, 57, 60}, {55, 59, 62}};
    const int melody[8][8] = {{72, -1, 76, 79, 76, -1, 74, 72}, {74, 76, -1, 72, 67, -1, 69, 72},
                              {69, -1, 72, 76, 74, 72, -1, 69}, {72, -1, 69, 67, 64, -1, 67, -1},
                              {65, -1, 69, 72, 74, 72, -1, 69}, {72, 74, -1, 77, 76, -1, 74, 72},
                              {71, -1, 74, 79, 77, 74, -1, 71}, {74, -1, 72, 71, 67, -1, 71, 74}};
    for (int bar = 0; bar < 8; ++bar) {
        const int chord = bar / 2;
        const double bar_start = bar * 4 * beat;
        Note(music, bar_start, roots[chord], beat * 1.8, Instrument::bass, 0.55F, true);
        Note(music, bar_start + 2 * beat, roots[chord] + 7, beat * 1.8, Instrument::bass, 0.45F, true);
        for (const int offbeat : {1, 3})
            for (const int note : chords[chord])
                Note(music, bar_start + offbeat * beat, note, beat * 0.8, Instrument::tick, 0.07F, true);
        for (int step = 0; step < 8; ++step)
            if (melody[bar][step] >= 0)
                Note(music, bar_start + step * beat / 2, melody[bar][step], beat * 1.2, Instrument::marimba, 0.3F,
                     true);
    }
    music_ = Finish(music, 0.55F);
    ready_.store(true);
}

}  // namespace sweetshop
