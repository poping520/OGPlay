#include "ogplay/video/buffered_video_player.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace ogplay::video {
namespace {

class BufferedVideoPlayer final : public VideoPlayer {
public:
    explicit BufferedVideoPlayer(std::unique_ptr<VideoPlayer> decoder)
        : decoder_(std::move(decoder)) {
        if (!decoder_) throw VideoPlayerError("buffered video decoder is null");
        ValidateVideoMetadata(decoder_->Metadata());
        Start(0);
    }
    ~BufferedVideoPlayer() override { Stop(); }

    const VideoMetadata& Metadata() const noexcept override {
        return decoder_->Metadata();
    }
    std::optional<VideoFrame> TakeFrame(const std::int64_t position) override {
        std::scoped_lock lock(mutex_);
        Rethrow();
        if (position < 0 || position < position_) {
            throw VideoPlayerError("buffered video position went backwards without SeekTo");
        }
        position_ = position;
        std::optional<VideoFrame> result;
        while (!frames_.empty() && frames_.front().position_ms <= position) {
            frame_bytes_ -= frames_.front().rgba8.size();
            result = std::move(frames_.front());
            frames_.pop_front();
        }
        wake_.notify_all();
        return result;
    }
    std::size_t ReadPcm(const std::span<std::int16_t> output) override {
        const auto channels = Metadata().audio_channels;
        if (channels == 0U) return 0U;
        if (output.size() % channels != 0U) {
            throw VideoPlayerError("buffered video PCM span has incomplete frames");
        }
        std::scoped_lock lock(mutex_);
        Rethrow();
        const auto samples = std::min(output.size(), pcm_.size());
        for (std::size_t i = 0; i < samples; ++i) {
            output[i] = pcm_.front();
            pcm_.pop_front();
        }
        wake_.notify_all();
        return samples / channels;
    }
    bool PcmEnded() const override {
        std::scoped_lock lock(mutex_);
        return audio_ended_ && pcm_.empty();
    }
    void SeekTo(const std::int64_t position) override {
        if (position < 0 || position > Metadata().duration_ms) {
            throw VideoPlayerError("buffered video seek out of bounds");
        }
        Stop();
        decoder_->SeekTo(position);
        Start(position);
    }

private:
    void Rethrow() const {
        if (failure_) std::rethrow_exception(failure_);
    }
    void Stop() {
        {
            std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    void Start(const std::int64_t position) {
        frames_.clear();
        pcm_.clear();
        frame_bytes_ = 0;
        position_ = position;
        stopping_ = false;
        primed_ = false;
        failure_ = {};
        audio_ended_ = !Metadata().HasAudio();
        worker_ = std::thread([this, position] { Decode(position); });
        // Open/seek may wait for a short pre-roll. Playback/audio pulls never
        // acquire a lock held by the decoder or wait on the worker.
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return primed_ || failure_; });
        if (failure_) {
            lock.unlock();
            Stop();
            Rethrow();
        }
    }
    void Decode(const std::int64_t start) noexcept {
        try {
            const auto& metadata = Metadata();
            const auto channels = metadata.audio_channels;
            const auto pcm_limit = static_cast<std::size_t>(
                metadata.audio_sample_rate) * channels / 2U;
            bool audio_done = !metadata.HasAudio();
            bool audio_waiting{};
            std::uint64_t remainder{};
            for (auto position = start; ; position += 10) {
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait(lock, [&] {
                        return stopping_ || (audio_done
                            ? position <= position_ + 500
                            : pcm_.size() < pcm_limit &&
                              (!audio_waiting || position <= position_ + 500));
                    });
                    if (stopping_) return;
                }
                // Decoder, demux, conversion and source IO are worker-only.
                auto frame = decoder_->TakeFrame(position);
                std::vector<std::int16_t> audio;
                if (!audio_done) {
                    remainder += static_cast<std::uint64_t>(metadata.audio_sample_rate) * 10U;
                    audio.resize(static_cast<std::size_t>(remainder / 1000U) * channels);
                    remainder %= 1000U;
                    const auto got = decoder_->ReadPcm(audio);
                    audio.resize(got * channels);
                    audio_done = decoder_->PcmEnded();
                    audio_waiting = got == 0U && !audio_done;
                }
                std::scoped_lock lock(mutex_);
                if (stopping_) return;
                if (frame) {
                    frame_bytes_ += frame->rgba8.size();
                    frames_.push_back(std::move(*frame));
                    // Audio must continue even if presentation falls behind.
                    // Drop the oldest decoded pictures, never PCM.
                    while (frames_.size() > 64U || frame_bytes_ > 64U * 1024U * 1024U) {
                        frame_bytes_ -= frames_.front().rgba8.size();
                        frames_.pop_front();
                    }
                }
                pcm_.insert(pcm_.end(), audio.begin(), audio.end());
                audio_ended_ = audio_done;
                const bool done = position >= metadata.duration_ms && audio_done;
                if (position >= start + 250 || done) primed_ = true;
                wake_.notify_all();
                if (done) return;
            }
        } catch (...) {
            std::scoped_lock lock(mutex_);
            failure_ = std::current_exception();
            wake_.notify_all();
        }
    }

    std::unique_ptr<VideoPlayer> decoder_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::deque<VideoFrame> frames_;
    std::deque<std::int16_t> pcm_;
    std::size_t frame_bytes_{};
    std::int64_t position_{};
    bool stopping_{};
    bool primed_{};
    bool audio_ended_{};
    std::exception_ptr failure_;
};
}  // namespace

std::unique_ptr<VideoPlayer> MakeBufferedVideoPlayer(std::unique_ptr<VideoPlayer> decoder) {
    return std::make_unique<BufferedVideoPlayer>(std::move(decoder));
}
}  // namespace ogplay::video
