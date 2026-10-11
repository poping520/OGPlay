#include "ogplay/video/buffered_video_player.h"
#include "ogplay/video/decoded_video_player.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace ogplay::video {
namespace {

constexpr std::size_t kMaxFrameBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaxFrames = 64U;
constexpr std::int64_t kDecodeStepMs = 10;
constexpr std::int64_t kMaxReadAheadMs = 500;

class RgbaPicture final : public DecodedVideoPicture {
public:
    explicit RgbaPicture(VideoFrame frame) : frame_(std::move(frame)) {}
    std::int64_t PositionMs() const noexcept override { return frame_.position_ms; }
    std::size_t StorageBytes() const noexcept override { return frame_.rgba8.capacity(); }
    VideoFrame ToRgba() override {
        if (converted_) throw VideoPlayerError("decoded picture was already converted");
        converted_ = true;
        return std::move(frame_);
    }
private:
    VideoFrame frame_;
    bool converted_{};
};

class BufferedVideoPlayer final : public VideoPlayer {
public:
    explicit BufferedVideoPlayer(std::unique_ptr<VideoPlayer> decoder)
        : decoder_(std::move(decoder)) {
        if (!decoder_) throw VideoPlayerError("buffered video decoder is null");
        ValidateVideoMetadata(decoder_->Metadata());
        const auto& metadata = Metadata();
        const auto frame_bytes = static_cast<std::size_t>(metadata.width) * metadata.height * 4U;
        // Bootstrap conservatively; the first owned picture supplies actual
        // storage including native plane padding, rather than an RGBA guess.
        frame_capacity_ = std::max<std::size_t>(2, std::min(kMaxFrames, kMaxFrameBytes / frame_bytes));
        staged_ = dynamic_cast<DecodedVideoPlayer*>(decoder_.get());
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
        if (ready_ && ready_->position_ms <= position) {
            result = std::move(ready_);
            ready_.reset();
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
        audio_read_frames_ += samples / channels;
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
    // Reserve room for the current picture and one in-flight picture. Until
    // actual PTS spacing is known, use the worker's conservative polling step.
    std::int64_t ReadAheadMs() const {
        return std::clamp(static_cast<std::int64_t>(frame_capacity_ - 2U) *
                              frame_interval_ms_,
                          kDecodeStepMs, kMaxReadAheadMs);
    }
    std::size_t PcmLimitSamples() const {
        const auto& metadata = Metadata();
        const auto read_position = audio_start_ms_ + static_cast<std::int64_t>(
            audio_read_frames_ * 1000U / metadata.audio_sample_rate);
        // ReadPcm can feed a host device queue ahead of the caller's picture
        // clock. That lead uses the same picture capacity as wrapper pre-roll.
        const auto lead = std::max<std::int64_t>(0, read_position - position_);
        const auto ahead = std::max(kDecodeStepMs, ReadAheadMs() - lead);
        return static_cast<std::size_t>(ahead) * metadata.audio_sample_rate *
               metadata.audio_channels / 1000U;
    }
    bool HasDuePicture() const {
        return !frames_.empty() && frames_.front()->PositionMs() <= position_;
    }
    bool CanDecode(const std::int64_t position, const bool audio_done,
                   const bool audio_waiting) const {
        return !decode_done_ && (audio_done
            ? position <= position_ + ReadAheadMs()
            : pcm_.size() < PcmLimitSamples() &&
              (!audio_waiting || position <= position_ + ReadAheadMs()));
    }
    void QueueFrame(std::unique_ptr<DecodedVideoPicture> frame) {
        const auto bytes = frame->StorageBytes();
        if (bytes == 0U || bytes > kMaxFrameBytes / 2U)
            throw VideoPlayerError("buffered video budget cannot retain current and next pictures");
        max_picture_bytes_ = std::max(max_picture_bytes_, bytes);
        frame_capacity_ = std::min(kMaxFrames, kMaxFrameBytes / max_picture_bytes_);
        const auto pts = frame->PositionMs();
        if (last_frame_pts_ >= 0 && pts > last_frame_pts_) {
            const auto spacing = std::clamp(pts - last_frame_pts_,
                                            std::int64_t{1}, kMaxReadAheadMs);
            frame_interval_ms_ = interval_known_
                ? std::min(frame_interval_ms_, spacing) : spacing;
            interval_known_ = true;
        }
        last_frame_pts_ = pts;
        frame_bytes_ += bytes;
        frames_.push_back(std::move(frame));
        // Keep the newest due picture and the nearest future picture even
        // when audio advances without any presentation calls.
        while (frames_.size() > 1U && frames_[1]->PositionMs() <= position_) {
            frame_bytes_ -= frames_.front()->StorageBytes();
            frames_.pop_front();
        }
        while (frames_.size() > frame_capacity_ || frame_bytes_ > kMaxFrameBytes) {
            const auto protected_count = frames_.front()->PositionMs() <= position_ ? 2U : 1U;
            const auto discarded = frames_.begin() +
                static_cast<std::deque<std::unique_ptr<DecodedVideoPicture>>::difference_type>(protected_count);
            frame_bytes_ -= (*discarded)->StorageBytes();
            frames_.erase(discarded);
        }
    }
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
        ready_.reset();
        pcm_.clear();
        frame_bytes_ = 0;
        max_picture_bytes_ = 0;
        decode_done_ = false;
        converting_ = false;
        position_ = position;
        audio_start_ms_ = position;
        audio_read_frames_ = 0;
        last_frame_pts_ = -1;
        frame_interval_ms_ = kDecodeStepMs;
        interval_known_ = false;
        stopping_ = false;
        primed_ = false;
        failure_ = {};
        audio_ended_ = !Metadata().HasAudio();
        worker_ = std::thread([this, position] { Decode(position); });
        // Open/seek may wait for a short pre-roll. Playback/audio pulls never
        // acquire a lock held by the decoder or wait on the worker.
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] {
            return (primed_ && !HasDuePicture() && !converting_) || failure_;
        });
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
            bool audio_done = !metadata.HasAudio();
            bool audio_waiting{};
            std::uint64_t remainder{};
            auto position = start;
            for (;;) {
                std::unique_ptr<DecodedVideoPicture> due;
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait(lock, [&] {
                        return stopping_ || HasDuePicture() || CanDecode(position, audio_done, audio_waiting);
                    });
                    if (stopping_) return;
                    while (HasDuePicture()) {
                        frame_bytes_ -= frames_.front()->StorageBytes();
                        due = std::move(frames_.front());
                        frames_.pop_front();
                    }
                    converting_ = due != nullptr;
                }
                if (due) {
                    // Conversion belongs to the worker too. Foreground/audio
                    // pulls only move ready RGBA or copy PCM; future pictures
                    // are never converted just to fill the pre-roll queue.
                    auto rgba = due->ToRgba();
                    if (rgba.position_ms != due->PositionMs() ||
                        rgba.width != metadata.width || rgba.height != metadata.height ||
                        rgba.rgba8.size() != static_cast<std::size_t>(metadata.width) * metadata.height * 4U)
                        throw VideoPlayerError("decoded picture produced invalid RGBA output");
                    std::scoped_lock lock(mutex_);
                    if (stopping_) return;
                    ready_ = std::move(rgba);
                    converting_ = false;
                    wake_.notify_all();
                    continue;
                }
                // Decoder, demux, conversion and source IO are worker-only.
                std::unique_ptr<DecodedVideoPicture> frame;
                if (staged_) frame = staged_->TakeDecodedPicture(position);
                else if (auto rgba = decoder_->TakeFrame(position))
                    frame = std::make_unique<RgbaPicture>(std::move(*rgba));
                std::vector<std::int16_t> audio;
                if (!audio_done) {
                    remainder += static_cast<std::uint64_t>(metadata.audio_sample_rate) *
                                 static_cast<std::uint64_t>(kDecodeStepMs);
                    audio.resize(static_cast<std::size_t>(remainder / 1000U) * channels);
                    remainder %= 1000U;
                    const auto got = decoder_->ReadPcm(audio);
                    audio.resize(got * channels);
                    audio_done = decoder_->PcmEnded();
                    audio_waiting = got == 0U && !audio_done;
                }
                std::scoped_lock lock(mutex_);
                if (stopping_) return;
                if (frame) QueueFrame(std::move(frame));
                pcm_.insert(pcm_.end(), audio.begin(), audio.end());
                audio_ended_ = audio_done;
                decode_done_ = position >= metadata.duration_ms && audio_done;
                if (position + kDecodeStepMs >= start +
                        std::min<std::int64_t>(250, ReadAheadMs() / 2) || decode_done_)
                    primed_ = true;
                position += kDecodeStepMs;
                // Stay available after demux EOF: queued pictures still need
                // conversion as the caller advances its presentation clock.
                wake_.notify_all();
            }
        } catch (...) {
            std::scoped_lock lock(mutex_);
            failure_ = std::current_exception();
            wake_.notify_all();
        }
    }

    std::unique_ptr<VideoPlayer> decoder_;
    DecodedVideoPlayer* staged_{};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::deque<std::unique_ptr<DecodedVideoPicture>> frames_;
    std::optional<VideoFrame> ready_;
    std::deque<std::int16_t> pcm_;
    std::size_t frame_bytes_{};
    std::size_t frame_capacity_{};
    std::size_t max_picture_bytes_{};
    bool decode_done_{};
    bool converting_{};
    std::int64_t position_{};
    std::int64_t audio_start_ms_{};
    std::uint64_t audio_read_frames_{};
    std::int64_t last_frame_pts_{-1};
    std::int64_t frame_interval_ms_{kDecodeStepMs};
    bool interval_known_{};
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
