#include "ogplay/video/ffmpeg_video_player.h"

#include <algorithm>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "ffmpeg_api.h"

namespace ogplay::video {
namespace {

using ffabi::Api;

constexpr std::size_t kMaxQueuedVideoFrames = 64U;
constexpr std::size_t kMaxBufferedPcmSamples = 8U * 1024U * 1024U;

[[nodiscard]] std::int64_t RescaleToMs(std::int64_t value,
                                       ffabi::Rational time_base) {
    if (time_base.den == 0) return 0;
    return value * time_base.num * 1000 / time_base.den;
}

class FfmpegVideoPlayer final : public VideoPlayer {
public:
    FfmpegVideoPlayer(const Api* api, const std::filesystem::path& host_path);
    ~FfmpegVideoPlayer() override;

    [[nodiscard]] const VideoMetadata& Metadata() const noexcept override {
        return metadata_;
    }
    [[nodiscard]] std::optional<VideoFrame> TakeFrame(
        std::int64_t position_ms) override;
    [[nodiscard]] std::size_t ReadPcm(
        std::span<std::int16_t> interleaved) override;
    void SeekTo(std::int64_t position_ms) override;

private:
    void OpenStreams(const std::filesystem::path& host_path);
    void* OpenCodec(const ffabi::CodecParameters* params, const void* decoder);
    void ConfigureResampler(const ffabi::CodecParameters* params);
    [[nodiscard]] std::int64_t FramePtsMs(const ffabi::Frame* frame,
                                          ffabi::Rational time_base,
                                          std::int64_t fallback) const;

    // Reads one demux packet and routes it to its codec; false at demux EOF
    // (drain packets are then sent exactly once).
    [[nodiscard]] bool ReadAndRoutePacket();
    void SendVideoPacket(const ffabi::Packet* packet);
    void SendAudioPacket(const ffabi::Packet* packet);
    void ReceiveQueuedVideoFrames();
    void DrainAudioFrames();
    void AppendPcm(const ffabi::Frame* frame);
    // Moves the next decoded video frame into pending_; false at video EOF.
    [[nodiscard]] bool EnsurePendingVideoFrame();
    [[nodiscard]] VideoFrame ConvertToRgba(ffabi::Frame* frame);
    void ReleaseFrame(ffabi::Frame*& frame);
    void ClearDecodeState();

    const Api* api_;
    VideoMetadata metadata_{};
    ffabi::FormatContext* format_ = nullptr;
    int video_stream_ = -1;
    int audio_stream_ = -1;
    ffabi::Rational video_time_base_{0, 1};
    ffabi::Rational audio_time_base_{0, 1};
    std::int64_t frame_duration_estimate_ms_ = 33;
    void* video_codec_ = nullptr;
    void* audio_codec_ = nullptr;
    void* sws_ = nullptr;
    int sws_src_width_ = 0;
    int sws_src_height_ = 0;
    int sws_src_format_ = -1;
    void* swr_ = nullptr;
    ffabi::Packet* packet_ = nullptr;
    std::deque<ffabi::Frame*> video_queue_;
    ffabi::Frame* pending_ = nullptr;
    std::int64_t pending_pts_ms_ = 0;
    std::int64_t last_video_pts_ms_ = 0;
    std::int64_t last_audio_pts_ms_ = 0;
    std::deque<std::int16_t> pcm_buffer_;
    std::int64_t audio_drop_before_ms_ = 0;
    bool demux_eof_ = false;
    bool video_eof_ = false;
    bool audio_eof_ = false;
    std::int64_t last_position_ms_ = -1;
};

FfmpegVideoPlayer::FfmpegVideoPlayer(const Api* api,
                                     const std::filesystem::path& host_path)
    : api_(api) {
    try {
        OpenStreams(host_path);
    } catch (...) {
        ClearDecodeState();
        throw;
    }
}

FfmpegVideoPlayer::~FfmpegVideoPlayer() { ClearDecodeState(); }

void FfmpegVideoPlayer::ClearDecodeState() {
    ReleaseFrame(pending_);
    while (!video_queue_.empty()) {
        ReleaseFrame(video_queue_.front());
        video_queue_.pop_front();
    }
    if (packet_ != nullptr) api_->av_packet_free(&packet_);
    if (sws_ != nullptr) {
        api_->sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (swr_ != nullptr) api_->swr_free(&swr_);
    if (video_codec_ != nullptr) api_->avcodec_free_context(&video_codec_);
    if (audio_codec_ != nullptr) api_->avcodec_free_context(&audio_codec_);
    if (format_ != nullptr) api_->avformat_close_input(&format_);
}

void FfmpegVideoPlayer::ReleaseFrame(ffabi::Frame*& frame) {
    if (frame != nullptr) api_->av_frame_free(&frame);
}

void* FfmpegVideoPlayer::OpenCodec(const ffabi::CodecParameters* params,
                                   const void* decoder) {
    void* codec = api_->avcodec_alloc_context3(decoder);
    if (codec == nullptr) {
        throw VideoPlayerError("ffmpeg codec context allocation failed");
    }
    if (api_->avcodec_parameters_to_context(codec, params) < 0 ||
        api_->avcodec_open2(codec, decoder, nullptr) < 0) {
        api_->avcodec_free_context(&codec);
        throw VideoPlayerError("ffmpeg decoder open failed");
    }
    return codec;
}

void FfmpegVideoPlayer::ConfigureResampler(
    const ffabi::CodecParameters* params) {
    swr_ = api_->swr_alloc();
    if (swr_ == nullptr) throw VideoPlayerError("ffmpeg swr_alloc failed");
    const std::string in_layout =
        std::to_string(params->ch_layout.nb_channels) + "C";
    const std::string out_layout =
        metadata_.audio_channels == 1U ? "mono" : "stereo";
    const char* in_fmt = api_->av_get_sample_fmt_name(params->format);
    if (in_fmt == nullptr) {
        throw VideoPlayerError("ffmpeg unknown audio sample format");
    }
    if (api_->av_opt_set(swr_, "in_chlayout", in_layout.c_str(), 0) < 0 ||
        api_->av_opt_set(swr_, "out_chlayout", out_layout.c_str(), 0) < 0 ||
        api_->av_opt_set_int(swr_, "in_sample_rate", params->sample_rate, 0) <
            0 ||
        api_->av_opt_set_int(swr_, "out_sample_rate", params->sample_rate, 0) <
            0 ||
        api_->av_opt_set(swr_, "in_sample_fmt", in_fmt, 0) < 0 ||
        api_->av_opt_set(swr_, "out_sample_fmt", "s16", 0) < 0 ||
        api_->swr_init(swr_) < 0) {
        throw VideoPlayerError("ffmpeg resampler configuration failed");
    }
}

void FfmpegVideoPlayer::OpenStreams(const std::filesystem::path& host_path) {
    const std::string path_utf8 = host_path.string();
    if (api_->avformat_open_input(&format_, path_utf8.c_str(), nullptr,
                                  nullptr) < 0) {
        throw VideoPlayerError("ffmpeg cannot open input: " + path_utf8);
    }
    if (api_->avformat_find_stream_info(format_, nullptr) < 0) {
        throw VideoPlayerError("ffmpeg cannot read stream info");
    }

    const void* video_decoder = nullptr;
    video_stream_ = api_->av_find_best_stream(format_, ffabi::kMediaTypeVideo,
                                              -1, -1, &video_decoder, 0);
    if (video_stream_ < 0 || video_decoder == nullptr) {
        throw VideoPlayerError("ffmpeg found no decodable video stream");
    }
    const ffabi::Stream* video = format_->streams[video_stream_];
    video_time_base_ = video->time_base;
    metadata_.width = static_cast<std::uint32_t>(
        std::max(video->codecpar->width, 0));
    metadata_.height = static_cast<std::uint32_t>(
        std::max(video->codecpar->height, 0));

    std::int64_t duration_ms = 0;
    if (video->duration != ffabi::kNoPtsValue && video->duration > 0) {
        duration_ms = RescaleToMs(video->duration, video_time_base_);
    } else if (format_->duration != ffabi::kNoPtsValue &&
               format_->duration > 0) {
        duration_ms = format_->duration * 1000 / ffabi::kAvTimeBase;
    }
    metadata_.duration_ms = duration_ms;

    const ffabi::Rational framerate = video->codecpar->framerate;
    if (framerate.num > 0 && framerate.den > 0) {
        frame_duration_estimate_ms_ = std::max<std::int64_t>(
            1000LL * framerate.den / framerate.num, 1);
    }

    const void* audio_decoder = nullptr;
    audio_stream_ = api_->av_find_best_stream(format_, ffabi::kMediaTypeAudio,
                                              -1, -1, &audio_decoder, 0);
    if (audio_stream_ >= 0 && audio_decoder != nullptr) {
        const ffabi::Stream* audio = format_->streams[audio_stream_];
        audio_time_base_ = audio->time_base;
        const int channels = audio->codecpar->ch_layout.nb_channels;
        if (channels > 0 && audio->codecpar->sample_rate > 0) {
            metadata_.audio_channels =
                static_cast<std::uint8_t>(std::min(channels, 2));
            metadata_.audio_sample_rate =
                static_cast<std::uint32_t>(audio->codecpar->sample_rate);
        } else {
            audio_stream_ = -1;
        }
    } else {
        audio_stream_ = -1;
    }

    ValidateVideoMetadata(metadata_);

    video_codec_ = OpenCodec(video->codecpar, video_decoder);
    if (audio_stream_ >= 0) {
        audio_codec_ = OpenCodec(format_->streams[audio_stream_]->codecpar,
                                 audio_decoder);
        ConfigureResampler(format_->streams[audio_stream_]->codecpar);
    } else {
        audio_eof_ = true;
    }
    packet_ = api_->av_packet_alloc();
    if (packet_ == nullptr) {
        throw VideoPlayerError("ffmpeg packet allocation failed");
    }
}

std::int64_t FfmpegVideoPlayer::FramePtsMs(const ffabi::Frame* frame,
                                           ffabi::Rational time_base,
                                           std::int64_t fallback) const {
    if (frame->pts != ffabi::kNoPtsValue) {
        return RescaleToMs(frame->pts, time_base);
    }
    if (frame->pkt_dts != ffabi::kNoPtsValue) {
        return RescaleToMs(frame->pkt_dts, time_base);
    }
    return fallback;
}

void FfmpegVideoPlayer::ReceiveQueuedVideoFrames() {
    while (true) {
        ffabi::Frame* frame = api_->av_frame_alloc();
        if (frame == nullptr) {
            throw VideoPlayerError("ffmpeg frame allocation failed");
        }
        const int rc = api_->avcodec_receive_frame(video_codec_, frame);
        if (rc != 0) {
            api_->av_frame_free(&frame);
            if (rc == ffabi::kErrorEagain) return;
            if (rc == ffabi::kErrorEof) {
                video_eof_ = true;
                return;
            }
            throw VideoPlayerError("ffmpeg video decode failed");
        }
        if (video_queue_.size() >= kMaxQueuedVideoFrames) {
            api_->av_frame_free(&frame);
            throw VideoPlayerError("ffmpeg video frame queue overflow");
        }
        video_queue_.push_back(frame);
    }
}

void FfmpegVideoPlayer::SendVideoPacket(const ffabi::Packet* packet) {
    int rc = api_->avcodec_send_packet(video_codec_, packet);
    if (rc == ffabi::kErrorEagain) {
        ReceiveQueuedVideoFrames();
        rc = api_->avcodec_send_packet(video_codec_, packet);
    }
    if (rc != 0 && rc != ffabi::kErrorEof) {
        throw VideoPlayerError("ffmpeg video packet rejected");
    }
}

void FfmpegVideoPlayer::AppendPcm(const ffabi::Frame* frame) {
    const auto channels = static_cast<std::size_t>(metadata_.audio_channels);
    const std::int64_t pts_ms =
        FramePtsMs(frame, audio_time_base_, last_audio_pts_ms_);
    last_audio_pts_ms_ =
        pts_ms + frame->nb_samples * 1000LL /
                     std::max<std::int64_t>(metadata_.audio_sample_rate, 1);

    std::vector<std::int16_t> converted(
        static_cast<std::size_t>(frame->nb_samples) * channels);
    auto* out = reinterpret_cast<std::uint8_t*>(converted.data());
    const int got = api_->swr_convert(swr_, &out, frame->nb_samples,
                                      frame->extended_data, frame->nb_samples);
    if (got < 0) throw VideoPlayerError("ffmpeg audio resample failed");

    std::size_t skip_frames = 0;
    if (pts_ms < audio_drop_before_ms_) {
        skip_frames = static_cast<std::size_t>(
            std::min<std::int64_t>((audio_drop_before_ms_ - pts_ms) *
                                       metadata_.audio_sample_rate / 1000,
                                   got));
    }
    const std::size_t begin = skip_frames * channels;
    const std::size_t end = static_cast<std::size_t>(got) * channels;
    if (pcm_buffer_.size() + (end - begin) > kMaxBufferedPcmSamples) {
        throw VideoPlayerError("ffmpeg pcm buffer overflow");
    }
    using PcmDifference = std::vector<std::int16_t>::difference_type;
    pcm_buffer_.insert(
        pcm_buffer_.end(),
        converted.cbegin() + static_cast<PcmDifference>(begin),
        converted.cbegin() + static_cast<PcmDifference>(end));
}

void FfmpegVideoPlayer::DrainAudioFrames() {
    if (audio_codec_ == nullptr || audio_eof_) return;
    while (true) {
        ffabi::Frame* frame = api_->av_frame_alloc();
        if (frame == nullptr) {
            throw VideoPlayerError("ffmpeg frame allocation failed");
        }
        const int rc = api_->avcodec_receive_frame(audio_codec_, frame);
        if (rc != 0) {
            api_->av_frame_free(&frame);
            if (rc == ffabi::kErrorEagain) return;
            if (rc == ffabi::kErrorEof) {
                audio_eof_ = true;
                return;
            }
            throw VideoPlayerError("ffmpeg audio decode failed");
        }
        AppendPcm(frame);
        api_->av_frame_free(&frame);
    }
}

void FfmpegVideoPlayer::SendAudioPacket(const ffabi::Packet* packet) {
    if (audio_codec_ == nullptr) return;
    int rc = api_->avcodec_send_packet(audio_codec_, packet);
    if (rc == ffabi::kErrorEagain) {
        DrainAudioFrames();
        rc = api_->avcodec_send_packet(audio_codec_, packet);
    }
    if (rc != 0 && rc != ffabi::kErrorEof) {
        throw VideoPlayerError("ffmpeg audio packet rejected");
    }
    DrainAudioFrames();
}

bool FfmpegVideoPlayer::ReadAndRoutePacket() {
    if (demux_eof_) return false;
    while (true) {
        const int rc = api_->av_read_frame(format_, packet_);
        if (rc < 0) {
            demux_eof_ = true;
            SendVideoPacket(nullptr);
            if (audio_codec_ != nullptr) SendAudioPacket(nullptr);
            return false;
        }
        const int index = packet_->stream_index;
        if (index == video_stream_) {
            SendVideoPacket(packet_);
            api_->av_packet_unref(packet_);
            return true;
        }
        if (index == audio_stream_) {
            SendAudioPacket(packet_);
            api_->av_packet_unref(packet_);
            return true;
        }
        api_->av_packet_unref(packet_);
    }
}

bool FfmpegVideoPlayer::EnsurePendingVideoFrame() {
    while (pending_ == nullptr) {
        if (!video_queue_.empty()) {
            pending_ = video_queue_.front();
            video_queue_.pop_front();
            pending_pts_ms_ = FramePtsMs(
                pending_, video_time_base_,
                last_video_pts_ms_ + frame_duration_estimate_ms_);
            last_video_pts_ms_ = pending_pts_ms_;
            return true;
        }
        if (video_eof_) return false;
        ReceiveQueuedVideoFrames();
        if (video_queue_.empty() && !video_eof_) {
            if (!ReadAndRoutePacket() && demux_eof_ && video_queue_.empty()) {
                // Drain packets were just sent; one more receive pass picks
                // up buffered frames or flips video_eof_.
                ReceiveQueuedVideoFrames();
                if (video_queue_.empty()) video_eof_ = true;
            }
        }
    }
    return true;
}

VideoFrame FfmpegVideoPlayer::ConvertToRgba(ffabi::Frame* frame) {
    if (sws_ != nullptr &&
        (sws_src_width_ != frame->width || sws_src_height_ != frame->height ||
         sws_src_format_ != frame->format)) {
        api_->sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (sws_ == nullptr) {
        sws_ = api_->sws_getContext(
            frame->width, frame->height, frame->format,
            static_cast<int>(metadata_.width),
            static_cast<int>(metadata_.height), ffabi::kPixFmtRgba,
            ffabi::kSwsBilinear, nullptr, nullptr, nullptr);
        if (sws_ == nullptr) {
            throw VideoPlayerError("ffmpeg pixel converter creation failed");
        }
        sws_src_width_ = frame->width;
        sws_src_height_ = frame->height;
        sws_src_format_ = frame->format;
    }
    VideoFrame result;
    result.width = metadata_.width;
    result.height = metadata_.height;
    result.rgba8.resize(static_cast<std::size_t>(metadata_.width) *
                        metadata_.height * 4U);
    std::uint8_t* dst[1] = {result.rgba8.data()};
    const int dst_linesize[1] = {static_cast<int>(metadata_.width) * 4};
    if (api_->sws_scale(sws_, frame->data, frame->linesize, 0, frame->height,
                        dst, dst_linesize) <= 0) {
        throw VideoPlayerError("ffmpeg pixel conversion failed");
    }
    return result;
}

std::optional<VideoFrame> FfmpegVideoPlayer::TakeFrame(
    std::int64_t position_ms) {
    if (position_ms < 0) {
        throw VideoPlayerError("ffmpeg player position must be >= 0");
    }
    if (position_ms < last_position_ms_) {
        throw VideoPlayerError(
            "ffmpeg player position went backwards without SeekTo");
    }
    last_position_ms_ = position_ms;

    ffabi::Frame* current = nullptr;
    std::int64_t current_pts = 0;
    while (EnsurePendingVideoFrame() && pending_pts_ms_ <= position_ms) {
        ReleaseFrame(current);
        current = std::exchange(pending_, nullptr);
        current_pts = pending_pts_ms_;
    }
    if (current == nullptr) return std::nullopt;
    VideoFrame frame = ConvertToRgba(current);
    frame.position_ms = current_pts;
    ReleaseFrame(current);
    return frame;
}

std::size_t FfmpegVideoPlayer::ReadPcm(std::span<std::int16_t> interleaved) {
    if (!metadata_.HasAudio()) return 0U;
    const auto channels = static_cast<std::size_t>(metadata_.audio_channels);
    if (interleaved.size() % channels != 0U) {
        throw VideoPlayerError(
            "ffmpeg player pcm span not a multiple of channel count");
    }
    while (pcm_buffer_.size() < interleaved.size() && !audio_eof_) {
        if (!ReadAndRoutePacket()) {
            DrainAudioFrames();
            if (demux_eof_) break;
        }
    }
    const std::size_t samples =
        std::min(interleaved.size(),
                 pcm_buffer_.size() - (pcm_buffer_.size() % channels));
    for (std::size_t i = 0; i < samples; ++i) {
        interleaved[i] = pcm_buffer_.front();
        pcm_buffer_.pop_front();
    }
    return samples / channels;
}

void FfmpegVideoPlayer::SeekTo(std::int64_t position_ms) {
    if (position_ms < 0 || position_ms > metadata_.duration_ms) {
        throw VideoPlayerError("ffmpeg player seek out of bounds");
    }
    if (api_->av_seek_frame(format_, -1, position_ms * 1000,
                            ffabi::kSeekFlagBackward) < 0) {
        throw VideoPlayerError("ffmpeg seek failed");
    }
    api_->avcodec_flush_buffers(video_codec_);
    if (audio_codec_ != nullptr) api_->avcodec_flush_buffers(audio_codec_);
    ReleaseFrame(pending_);
    while (!video_queue_.empty()) {
        ReleaseFrame(video_queue_.front());
        video_queue_.pop_front();
    }
    pcm_buffer_.clear();
    demux_eof_ = false;
    video_eof_ = false;
    audio_eof_ = audio_codec_ == nullptr;
    audio_drop_before_ms_ = position_ms;
    last_video_pts_ms_ = position_ms;
    last_audio_pts_ms_ = position_ms;
    last_position_ms_ = position_ms;
}

}  // namespace

bool FfmpegAvailable() {
    return ffabi::LoadFfmpegApi(nullptr) != nullptr;
}

std::string FfmpegUnavailableReason() {
    std::string reason;
    (void)ffabi::LoadFfmpegApi(&reason);
    return reason;
}

std::unique_ptr<VideoPlayer> OpenFfmpegVideo(
    const std::filesystem::path& host_path) {
    std::string reason;
    const Api* api = ffabi::LoadFfmpegApi(&reason);
    if (api == nullptr) {
        throw VideoPlayerError("ffmpeg unavailable: " + reason);
    }
    return std::make_unique<FfmpegVideoPlayer>(api, host_path);
}

VideoPlayerFactory MakeFfmpegVideoPlayerFactory() {
    return [](const std::filesystem::path& host_path) {
        return OpenFfmpegVideo(host_path);
    };
}

}  // namespace ogplay::video
