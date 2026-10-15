#include "ogplay/video/ffmpeg_video_player.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "ogplay/video/decoded_video_player.h"

namespace ogplay::video {
namespace {

constexpr std::size_t kMaxQueuedVideoFrames = 64U;
constexpr std::size_t kMaxQueuedVideoBytes = 256U * 1024U * 1024U;
constexpr std::size_t kVideoBackpressureBytes = 192U * 1024U * 1024U;
constexpr std::size_t kVideoBackpressureThreshold = 48U;
constexpr std::size_t kMaxBufferedPcmSamples = 8U * 1024U * 1024U;
constexpr int kAvioBufferSize = 32 * 1024;

[[nodiscard]] std::string AvError(const int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> text{};
    av_strerror(code, text.data(), text.size());
    return text.data();
}

[[nodiscard]] std::int64_t RescaleToMs(std::int64_t value, AVRational time_base) {
    if (time_base.den == 0) return 0;
    return value * time_base.num * 1000 / time_base.den;
}

struct SelfCheck final {
    bool ok{};
    std::string reason;
};

// Guards against a runtime library from a different package than the headers
// and against a package that lost a component OGPlay relies on.
const SelfCheck& FfmpegSelfCheck() {
    static const SelfCheck result = []() -> SelfCheck {
        // FFmpeg's own logger writes to stderr, bypassing structured logs; it
        // also warns on every custom-AVIO open. Failures reach callers as
        // VideoPlayerError messages, so only FFmpeg errors stay visible.
        av_log_set_level(AV_LOG_ERROR);
        if (avutil_version() != LIBAVUTIL_VERSION_INT ||
            avcodec_version() != LIBAVCODEC_VERSION_INT ||
            avformat_version() != LIBAVFORMAT_VERSION_INT ||
            swscale_version() != LIBSWSCALE_VERSION_INT ||
            swresample_version() != LIBSWRESAMPLE_VERSION_INT) {
            return {false, "ogplay_ffmpeg runtime version differs from its headers"};
        }
        for (const AVCodecID id : {AV_CODEC_ID_H264, AV_CODEC_ID_MPEG4, AV_CODEC_ID_AAC}) {
            if (avcodec_find_decoder(id) == nullptr)
                return {false, std::string("ogplay_ffmpeg lacks the ") +
                                   avcodec_get_name(id) + " decoder"};
        }
        if (av_find_input_format("mov") == nullptr)
            return {false, "ogplay_ffmpeg lacks the mov demuxer"};
        return {true, {}};
    }();
    return result;
}

std::size_t PixelStorageBytes(const AVFrame* frame) {
    if (frame->width <= 0 || frame->height <= 0 || frame->width > 4096 || frame->height > 4096)
        throw VideoPlayerError("ffmpeg decoded picture has out-of-bounds dimensions");
    std::size_t bytes{};
    const auto planes = av_pix_fmt_count_planes(static_cast<AVPixelFormat>(frame->format));
    if (planes <= 0 || planes > 8)
        throw VideoPlayerError("ffmpeg decoded picture has unsupported plane layout");
    std::array<const void*, 8> buffers{};
    std::array<std::size_t, 8> sizes{};
    std::size_t count{};
    for (int plane = 0; plane < planes; ++plane) {
        const auto* ref = av_frame_get_plane_buffer(frame, plane);
        if (!ref || !ref->buffer || ref->size == 0U)
            throw VideoPlayerError("ffmpeg decoded picture lacks owned pixel backing");
        auto slot = std::find(buffers.begin(), buffers.begin() + static_cast<std::ptrdiff_t>(count), ref->buffer);
        if (slot == buffers.begin() + static_cast<std::ptrdiff_t>(count)) {
            buffers[count] = ref->buffer;
            sizes[count++] = ref->size;
        } else {
            const auto index = static_cast<std::size_t>(slot - buffers.begin());
            sizes[index] = std::max(sizes[index], ref->size);
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (sizes[i] > std::numeric_limits<std::size_t>::max() - bytes)
            throw VideoPlayerError("ffmpeg decoded picture storage overflow");
        bytes += sizes[i];
    }
    return bytes;
}

struct FrameDeleter final {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

class FrameConverter final {
public:
    explicit FrameConverter(VideoMetadata metadata) : metadata_(metadata) {}
    ~FrameConverter() { sws_freeContext(sws_); }
    FrameConverter(const FrameConverter&) = delete;
    FrameConverter& operator=(const FrameConverter&) = delete;
    VideoFrame Convert(const AVFrame* frame);
private:
    VideoMetadata metadata_;
    SwsContext* sws_{};
    int sws_src_width_{};
    int sws_src_height_{};
    int sws_src_format_{-1};
};

class FfmpegPicture final : public DecodedVideoPicture {
public:
    FfmpegPicture(FramePtr frame, std::int64_t position, std::shared_ptr<FrameConverter> converter)
        : frame_(std::move(frame)), position_(position), converter_(std::move(converter)) {
        bytes_ = PixelStorageBytes(frame_.get());
    }
    std::int64_t PositionMs() const noexcept override { return position_; }
    std::size_t StorageBytes() const noexcept override { return bytes_; }
    VideoFrame ToRgba() override {
        if (!frame_) throw VideoPlayerError("decoded picture was already converted");
        auto frame = std::move(frame_);
        auto result = converter_->Convert(frame.get());
        result.position_ms = position_;
        return result;
    }
private:
    FramePtr frame_;
    std::int64_t position_{};
    std::size_t bytes_{};
    std::shared_ptr<FrameConverter> converter_;
};

VideoFrame FrameConverter::Convert(const AVFrame* frame) {
    if (sws_ != nullptr &&
        (sws_src_width_ != frame->width || sws_src_height_ != frame->height ||
         sws_src_format_ != frame->format)) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (sws_ == nullptr) {
        sws_ = sws_getContext(
            frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
            static_cast<int>(metadata_.width), static_cast<int>(metadata_.height),
            AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
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
    if (sws_scale(sws_, frame->data, frame->linesize, 0, frame->height,
                  dst, dst_linesize) <= 0) {
        throw VideoPlayerError("ffmpeg pixel conversion failed");
    }
    return result;
}

// Serves a host file through the same ReadAt interface as guest VFS sources.
class HostFileSource final : public VideoDataSource {
public:
    explicit HostFileSource(const std::filesystem::path& path)
        : file_(path, std::ios::binary) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!file_ || error)
            throw VideoPlayerError("ffmpeg cannot open input: " + path.string());
        size_ = size;
    }
    [[nodiscard]] std::uint64_t Size() const noexcept override { return size_; }
    [[nodiscard]] std::size_t ReadAt(const std::uint64_t offset,
                                     const std::span<std::byte> destination) const override {
        if (offset >= size_ || destination.empty()) return 0U;
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(destination.size(), size_ - offset));
        std::scoped_lock lock(mutex_);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(reinterpret_cast<char*>(destination.data()),
                   static_cast<std::streamsize>(count));
        if (file_.gcount() != static_cast<std::streamsize>(count))
            throw VideoPlayerError("ffmpeg host input read failed");
        return count;
    }
private:
    mutable std::mutex mutex_;
    mutable std::ifstream file_;
    std::uint64_t size_{};
};

class FfmpegVideoPlayer final : public DecodedVideoPlayer {
public:
    explicit FfmpegVideoPlayer(std::shared_ptr<const VideoDataSource> source);
    ~FfmpegVideoPlayer() override;

    [[nodiscard]] const VideoMetadata& Metadata() const noexcept override {
        return metadata_;
    }
    [[nodiscard]] std::optional<VideoFrame> TakeFrame(
        std::int64_t position_ms) override;
    std::unique_ptr<DecodedVideoPicture> TakeDecodedPicture(std::int64_t position_ms) override;
    [[nodiscard]] std::size_t ReadPcm(
        std::span<std::int16_t> interleaved) override;
    void SeekTo(std::int64_t position_ms) override;
    [[nodiscard]] bool PcmEnded() const override {
        return audio_eof_ && pcm_buffer_.empty();
    }

private:
    void OpenStreams();
    static int ReadSource(void* opaque, std::uint8_t* destination, int size);
    static std::int64_t SeekSource(void* opaque, std::int64_t offset, int whence);
    [[nodiscard]] static const AVCodec* RequireDecoder(const AVStream* stream,
                                                       const char* kind);
    [[nodiscard]] static AVCodecContext* OpenCodec(const AVCodecParameters* params,
                                                   const AVCodec* decoder);
    void ConfigureResampler();
    [[nodiscard]] std::int64_t FramePtsMs(const AVFrame* frame,
                                          AVRational time_base,
                                          std::int64_t fallback) const;

    // Reads one demux packet and routes it to its codec; false at demux EOF
    // (drain packets are then sent exactly once).
    [[nodiscard]] bool ReadAndRoutePacket();
    void SendVideoPacket(const AVPacket* packet);
    void SendAudioPacket(const AVPacket* packet);
    void ReceiveQueuedVideoFrames();
    void DrainAudioFrames();
    void AppendPcm(const AVFrame* frame);
    // Moves the next decoded video frame into pending_; false at video EOF.
    [[nodiscard]] bool EnsurePendingVideoFrame();
    void ClearDecodeState();

    VideoMetadata metadata_{};
    AVFormatContext* format_ = nullptr;
    std::shared_ptr<const VideoDataSource> source_;
    AVIOContext* avio_ = nullptr;
    std::uint64_t source_offset_{};
    int video_stream_ = -1;
    int audio_stream_ = -1;
    AVRational video_time_base_{0, 1};
    AVRational audio_time_base_{0, 1};
    std::int64_t frame_duration_estimate_ms_ = 33;
    AVCodecContext* video_codec_ = nullptr;
    AVCodecContext* audio_codec_ = nullptr;
    std::shared_ptr<FrameConverter> converter_;
    SwrContext* swr_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::deque<FramePtr> video_queue_;
    // Includes pending_: moving a future picture out of the deque must not
    // make its pixel backing disappear from the demux budget.
    std::size_t queued_video_bytes_{};
    std::size_t max_video_frame_bytes_{};
    FramePtr pending_;
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

FfmpegVideoPlayer::FfmpegVideoPlayer(std::shared_ptr<const VideoDataSource> source)
    : source_(std::move(source)) {
    if (!source_) throw VideoPlayerError("video data source is null");
    try {
        OpenStreams();
        converter_ = std::make_shared<FrameConverter>(metadata_);
    } catch (...) {
        ClearDecodeState();
        throw;
    }
}

FfmpegVideoPlayer::~FfmpegVideoPlayer() { ClearDecodeState(); }

void FfmpegVideoPlayer::ClearDecodeState() {
    pending_.reset();
    video_queue_.clear();
    av_packet_free(&packet_);
    converter_.reset();
    swr_free(&swr_);
    avcodec_free_context(&video_codec_);
    avcodec_free_context(&audio_codec_);
    // Custom IO: closing the input leaves pb to us, so the AVIO context and its
    // (possibly reallocated) buffer are released afterwards, then the source.
    avformat_close_input(&format_);
    if (avio_ != nullptr) {
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
    }
}

const AVCodec* FfmpegVideoPlayer::RequireDecoder(const AVStream* stream,
                                                 const char* kind) {
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == nullptr)
        throw VideoCodecUnsupportedError(kind, avcodec_get_name(stream->codecpar->codec_id));
    return decoder;
}

AVCodecContext* FfmpegVideoPlayer::OpenCodec(const AVCodecParameters* params,
                                             const AVCodec* decoder) {
    AVCodecContext* codec = avcodec_alloc_context3(decoder);
    if (codec == nullptr) {
        throw VideoPlayerError("ffmpeg codec context allocation failed");
    }
    int rc = avcodec_parameters_to_context(codec, params);
    if (rc >= 0) rc = avcodec_open2(codec, decoder, nullptr);
    if (rc < 0) {
        avcodec_free_context(&codec);
        throw VideoPlayerError(std::string("ffmpeg ") + decoder->name +
                               " decoder open failed: " + AvError(rc));
    }
    return codec;
}

void FfmpegVideoPlayer::ConfigureResampler() {
    AVChannelLayout in_layout{};
    if (audio_codec_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&in_layout, audio_codec_->ch_layout.nb_channels);
    else if (av_channel_layout_copy(&in_layout, &audio_codec_->ch_layout) < 0)
        throw VideoPlayerError("ffmpeg audio channel layout copy failed");
    AVChannelLayout out_layout{};
    av_channel_layout_default(&out_layout, metadata_.audio_channels);
    const int rc = swr_alloc_set_opts2(
        &swr_, &out_layout, AV_SAMPLE_FMT_S16, audio_codec_->sample_rate,
        &in_layout, audio_codec_->sample_fmt, audio_codec_->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);
    if (rc < 0 || swr_init(swr_) < 0) {
        throw VideoPlayerError("ffmpeg resampler configuration failed");
    }
}

int FfmpegVideoPlayer::ReadSource(void* opaque, std::uint8_t* destination,
                                  const int size) {
    auto& self = *static_cast<FfmpegVideoPlayer*>(opaque);
    if (size <= 0) return 0;
    try {
        auto bytes = std::span(reinterpret_cast<std::byte*>(destination),
                               static_cast<std::size_t>(size));
        const auto count = self.source_->ReadAt(self.source_offset_, bytes);
        self.source_offset_ += count;
        return count == 0 ? AVERROR_EOF : static_cast<int>(count);
    } catch (...) {
        return AVERROR(EIO);
    }
}

std::int64_t FfmpegVideoPlayer::SeekSource(void* opaque,
                                           const std::int64_t offset,
                                           const int whence) {
    auto& self = *static_cast<FfmpegVideoPlayer*>(opaque);
    if ((whence & AVSEEK_SIZE) != 0)
        return static_cast<std::int64_t>(self.source_->Size());
    const int origin = whence & ~AVSEEK_FORCE;
    std::int64_t base{};
    if (origin == SEEK_CUR) base = static_cast<std::int64_t>(self.source_offset_);
    else if (origin == SEEK_END) base = static_cast<std::int64_t>(self.source_->Size());
    else if (origin != SEEK_SET) return AVERROR(EINVAL);
    if ((offset < 0 && -offset > base) ||
        (offset > 0 && static_cast<std::uint64_t>(offset) >
                           self.source_->Size() - static_cast<std::uint64_t>(base)))
        return AVERROR(EINVAL);
    const auto result = base + offset;
    self.source_offset_ = static_cast<std::uint64_t>(result);
    return result;
}

void FfmpegVideoPlayer::OpenStreams() {
    auto* buffer = static_cast<unsigned char*>(av_malloc(kAvioBufferSize));
    if (buffer == nullptr) throw VideoPlayerError("ffmpeg IO buffer allocation failed");
    avio_ = avio_alloc_context(buffer, kAvioBufferSize, 0, this, &ReadSource, nullptr,
                               &SeekSource);
    if (avio_ == nullptr) {
        av_free(buffer);
        throw VideoPlayerError("ffmpeg custom IO allocation failed");
    }
    format_ = avformat_alloc_context();
    if (format_ == nullptr) throw VideoPlayerError("ffmpeg format allocation failed");
    format_->pb = avio_;
    format_->flags |= AVFMT_FLAG_CUSTOM_IO;
    // avformat_open_input frees format_ and nulls it on failure.
    if (const int rc = avformat_open_input(&format_, nullptr, nullptr, nullptr); rc < 0)
        throw VideoPlayerError("ffmpeg cannot open input: " + AvError(rc));
    if (const int rc = avformat_find_stream_info(format_, nullptr); rc < 0)
        throw VideoPlayerError("ffmpeg cannot read stream info: " + AvError(rc));

    video_stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_ < 0) throw VideoPlayerError("ffmpeg found no video stream");
    const AVStream* video = format_->streams[video_stream_];
    const AVCodec* video_decoder = RequireDecoder(video, "video");
    video_time_base_ = video->time_base;
    metadata_.width = static_cast<std::uint32_t>(std::max(video->codecpar->width, 0));
    metadata_.height = static_cast<std::uint32_t>(std::max(video->codecpar->height, 0));
    metadata_.duration_ms = video->duration != AV_NOPTS_VALUE && video->duration > 0
        ? RescaleToMs(video->duration, video_time_base_)
        : (format_->duration != AV_NOPTS_VALUE && format_->duration > 0
               ? format_->duration * 1000 / AV_TIME_BASE : 0);
    const auto framerate = video->codecpar->framerate;
    if (framerate.num > 0 && framerate.den > 0)
        frame_duration_estimate_ms_ = std::max<std::int64_t>(
            1000LL * framerate.den / framerate.num, 1);

    // A present audio track must be decodable: silently dropping CG sound
    // would fake success for a codec this build does not carry.
    const AVCodec* audio_decoder = nullptr;
    audio_stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_ >= 0) {
        const AVStream* audio = format_->streams[audio_stream_];
        audio_decoder = RequireDecoder(audio, "audio");
        audio_time_base_ = audio->time_base;
        const int channels = audio->codecpar->ch_layout.nb_channels;
        if (channels > 0 && audio->codecpar->sample_rate > 0) {
            metadata_.audio_channels = static_cast<std::uint8_t>(std::min(channels, 2));
            metadata_.audio_sample_rate = static_cast<std::uint32_t>(audio->codecpar->sample_rate);
        } else {
            audio_stream_ = -1;
        }
    }
    ValidateVideoMetadata(metadata_);
    video_codec_ = OpenCodec(video->codecpar, video_decoder);
    if (audio_stream_ >= 0) {
        audio_codec_ = OpenCodec(format_->streams[audio_stream_]->codecpar, audio_decoder);
        ConfigureResampler();
    } else {
        audio_eof_ = true;
    }
    packet_ = av_packet_alloc();
    if (packet_ == nullptr) throw VideoPlayerError("ffmpeg packet allocation failed");
}

std::int64_t FfmpegVideoPlayer::FramePtsMs(const AVFrame* frame,
                                           AVRational time_base,
                                           std::int64_t fallback) const {
    if (frame->pts != AV_NOPTS_VALUE) {
        return RescaleToMs(frame->pts, time_base);
    }
    if (frame->pkt_dts != AV_NOPTS_VALUE) {
        return RescaleToMs(frame->pkt_dts, time_base);
    }
    return fallback;
}

void FfmpegVideoPlayer::ReceiveQueuedVideoFrames() {
    while (true) {
        // Audio and video share one demux cursor. Audio reads may run ahead
        // of the presentation clock, so leave decoded output in the codec
        // until TakeFrame drains the bounded host queue.
        if (video_queue_.size() >= kMaxQueuedVideoFrames ||
            (max_video_frame_bytes_ != 0U &&
             queued_video_bytes_ > kMaxQueuedVideoBytes - max_video_frame_bytes_)) return;
        FramePtr frame(av_frame_alloc());
        if (!frame) {
            throw VideoPlayerError("ffmpeg frame allocation failed");
        }
        const int rc = avcodec_receive_frame(video_codec_, frame.get());
        if (rc != 0) {
            if (rc == AVERROR(EAGAIN)) return;
            if (rc == AVERROR_EOF) {
                video_eof_ = true;
                return;
            }
            throw VideoPlayerError("ffmpeg video decode failed: " + AvError(rc));
        }
        const auto bytes = PixelStorageBytes(frame.get());
        if (bytes > kMaxQueuedVideoBytes - queued_video_bytes_)
            throw VideoPlayerError("ffmpeg decoded video exceeds pixel buffer budget");
        video_queue_.push_back(std::move(frame));
        queued_video_bytes_ += bytes;
        max_video_frame_bytes_ = std::max(max_video_frame_bytes_, bytes);
    }
}

void FfmpegVideoPlayer::SendVideoPacket(const AVPacket* packet) {
    int rc = avcodec_send_packet(video_codec_, packet);
    if (rc == AVERROR(EAGAIN)) {
        ReceiveQueuedVideoFrames();
        rc = avcodec_send_packet(video_codec_, packet);
    }
    if (rc != 0 && rc != AVERROR_EOF) {
        throw VideoPlayerError("ffmpeg video packet rejected: " + AvError(rc));
    }
}

void FfmpegVideoPlayer::AppendPcm(const AVFrame* frame) {
    const auto channels = static_cast<std::size_t>(metadata_.audio_channels);
    const std::int64_t pts_ms =
        FramePtsMs(frame, audio_time_base_, last_audio_pts_ms_);
    last_audio_pts_ms_ =
        pts_ms + frame->nb_samples * 1000LL /
                     std::max<std::int64_t>(metadata_.audio_sample_rate, 1);

    std::vector<std::int16_t> converted(
        static_cast<std::size_t>(frame->nb_samples) * channels);
    auto* out = reinterpret_cast<std::uint8_t*>(converted.data());
    const int got = swr_convert(swr_, &out, frame->nb_samples,
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
        FramePtr frame(av_frame_alloc());
        if (!frame) {
            throw VideoPlayerError("ffmpeg frame allocation failed");
        }
        const int rc = avcodec_receive_frame(audio_codec_, frame.get());
        if (rc != 0) {
            if (rc == AVERROR(EAGAIN)) return;
            if (rc == AVERROR_EOF) {
                audio_eof_ = true;
                return;
            }
            throw VideoPlayerError("ffmpeg audio decode failed: " + AvError(rc));
        }
        AppendPcm(frame.get());
    }
}

void FfmpegVideoPlayer::SendAudioPacket(const AVPacket* packet) {
    if (audio_codec_ == nullptr) return;
    int rc = avcodec_send_packet(audio_codec_, packet);
    if (rc == AVERROR(EAGAIN)) {
        DrainAudioFrames();
        rc = avcodec_send_packet(audio_codec_, packet);
    }
    if (rc != 0 && rc != AVERROR_EOF) {
        throw VideoPlayerError("ffmpeg audio packet rejected: " + AvError(rc));
    }
    DrainAudioFrames();
}

bool FfmpegVideoPlayer::ReadAndRoutePacket() {
    if (demux_eof_) return false;
    while (true) {
        const int rc = av_read_frame(format_, packet_);
        if (rc < 0) {
            demux_eof_ = true;
            SendVideoPacket(nullptr);
            if (audio_codec_ != nullptr) SendAudioPacket(nullptr);
            return false;
        }
        const int index = packet_->stream_index;
        if (index == video_stream_) {
            SendVideoPacket(packet_);
            av_packet_unref(packet_);
            return true;
        }
        if (index == audio_stream_) {
            SendAudioPacket(packet_);
            av_packet_unref(packet_);
            return true;
        }
        av_packet_unref(packet_);
    }
}

bool FfmpegVideoPlayer::EnsurePendingVideoFrame() {
    while (!pending_) {
        if (!video_queue_.empty()) {
            pending_ = std::move(video_queue_.front());
            video_queue_.pop_front();
            pending_pts_ms_ = FramePtsMs(
                pending_.get(), video_time_base_,
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

std::optional<VideoFrame> FfmpegVideoPlayer::TakeFrame(std::int64_t position_ms) {
    auto picture = TakeDecodedPicture(position_ms);
    if (!picture) return std::nullopt;
    return picture->ToRgba();
}

std::unique_ptr<DecodedVideoPicture> FfmpegVideoPlayer::TakeDecodedPicture(
    std::int64_t position_ms) {
    if (position_ms < 0) {
        throw VideoPlayerError("ffmpeg player position must be >= 0");
    }
    if (position_ms < last_position_ms_) {
        throw VideoPlayerError(
            "ffmpeg player position went backwards without SeekTo");
    }
    last_position_ms_ = position_ms;

    FramePtr current;
    std::int64_t current_pts = 0;
    while (EnsurePendingVideoFrame() && pending_pts_ms_ <= position_ms) {
        queued_video_bytes_ -= PixelStorageBytes(pending_.get());
        current = std::move(pending_);
        current_pts = pending_pts_ms_;
    }
    if (!current) return nullptr;
    return std::make_unique<FfmpegPicture>(std::move(current), current_pts, converter_);
}

std::size_t FfmpegVideoPlayer::ReadPcm(std::span<std::int16_t> interleaved) {
    if (!metadata_.HasAudio()) return 0U;
    const auto channels = static_cast<std::size_t>(metadata_.audio_channels);
    if (interleaved.size() % channels != 0U) {
        throw VideoPlayerError(
            "ffmpeg player pcm span not a multiple of channel count");
    }
    while (pcm_buffer_.size() < interleaved.size() && !audio_eof_) {
        if (video_queue_.size() >= kVideoBackpressureThreshold ||
            queued_video_bytes_ >= kVideoBackpressureBytes) break;
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
    if (const int rc = av_seek_frame(format_, -1, position_ms * 1000, AVSEEK_FLAG_BACKWARD);
        rc < 0) {
        throw VideoPlayerError("ffmpeg seek failed: " + AvError(rc));
    }
    avcodec_flush_buffers(video_codec_);
    if (audio_codec_ != nullptr) avcodec_flush_buffers(audio_codec_);
    pending_.reset();
    video_queue_.clear();
    queued_video_bytes_ = 0;
    max_video_frame_bytes_ = 0;
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

bool FfmpegAvailable() { return FfmpegSelfCheck().ok; }

std::string FfmpegUnavailableReason() { return FfmpegSelfCheck().reason; }

std::unique_ptr<VideoPlayer> OpenFfmpegVideo(std::shared_ptr<const VideoDataSource> source) {
    if (const auto& check = FfmpegSelfCheck(); !check.ok)
        throw VideoPlayerError("ffmpeg unavailable: " + check.reason);
    return std::make_unique<FfmpegVideoPlayer>(std::move(source));
}

std::unique_ptr<VideoPlayer> OpenFfmpegVideo(const std::filesystem::path& host_path) {
    return OpenFfmpegVideo(std::make_shared<HostFileSource>(host_path));
}

VideoPlayerFactory MakeFfmpegVideoPlayerFactory() {
    return [](const std::filesystem::path& host_path) {
        return OpenFfmpegVideo(host_path);
    };
}

VideoSourcePlayerFactory MakeFfmpegVideoSourcePlayerFactory() {
    return [](std::shared_ptr<const VideoDataSource> source) {
        return OpenFfmpegVideo(std::move(source));
    };
}

}  // namespace ogplay::video
