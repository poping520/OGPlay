#pragma once

// Internal runtime-loading surface for FFmpeg (ADR-0021). The struct mirrors
// reproduce the leading fields of the FFmpeg 7.x C ABI (avutil 59 /
// avcodec 61 / avformat 61 / swscale 8 / swresample 5) exactly as declared in
// the upstream headers; only mirrored fields may be accessed, trailing fields
// are reachable only by FFmpeg itself. The loader refuses any other major
// version, so a layout mismatch cannot occur silently.

#include <cerrno>
#include <cstdint>
#include <limits>
#include <string>

namespace ogplay::video::ffabi {

inline constexpr unsigned kAvutilMajor = 59U;
inline constexpr unsigned kAvcodecMajor = 61U;
inline constexpr unsigned kAvformatMajor = 61U;
inline constexpr unsigned kSwscaleMajor = 8U;
inline constexpr unsigned kSwresampleMajor = 5U;

struct Rational final {
    int num;
    int den;
};

struct ChannelLayout final {
    int order;
    int nb_channels;
    union {
        std::uint64_t mask;
        void* map;
    } u;
    void* opaque;
};

struct CodecParameters final {
    int codec_type;
    int codec_id;
    std::uint32_t codec_tag;
    std::uint8_t* extradata;
    int extradata_size;
    void* coded_side_data;
    int nb_coded_side_data;
    int format;
    std::int64_t bit_rate;
    int bits_per_coded_sample;
    int bits_per_raw_sample;
    int profile;
    int level;
    int width;
    int height;
    Rational sample_aspect_ratio;
    Rational framerate;
    int field_order;
    int color_range;
    int color_primaries;
    int color_trc;
    int color_space;
    int chroma_location;
    int video_delay;
    ChannelLayout ch_layout;
    int sample_rate;
    // trailing fields omitted
};

struct Stream final {
    const void* av_class;
    int index;
    int id;
    CodecParameters* codecpar;
    void* priv_data;
    Rational time_base;
    std::int64_t start_time;
    std::int64_t duration;
    // trailing fields omitted
};

struct FormatContext final {
    const void* av_class;
    const void* iformat;
    const void* oformat;
    void* priv_data;
    void* pb;
    int ctx_flags;
    unsigned nb_streams;
    Stream** streams;
    unsigned nb_stream_groups;
    void** stream_groups;
    unsigned nb_chapters;
    void** chapters;
    char* url;
    std::int64_t start_time;
    std::int64_t duration;
    // trailing fields omitted
};

struct Packet final {
    void* buf;
    std::int64_t pts;
    std::int64_t dts;
    std::uint8_t* data;
    int size;
    int stream_index;
    int flags;
    void* side_data;
    int side_data_elems;
    std::int64_t duration;
    std::int64_t pos;
    void* opaque;
    void* opaque_ref;
    Rational time_base;
};

struct Frame final {
    std::uint8_t* data[8];
    int linesize[8];
    std::uint8_t** extended_data;
    int width;
    int height;
    int nb_samples;
    int format;
    int key_frame;
    int pict_type;
    Rational sample_aspect_ratio;
    std::int64_t pts;
    std::int64_t pkt_dts;
    Rational time_base;
    // trailing fields omitted
};

inline constexpr int kMediaTypeVideo = 0;
inline constexpr int kMediaTypeAudio = 1;
inline constexpr int kPixFmtRgba = 26;
inline constexpr int kSampleFmtS16 = 1;
inline constexpr int kSwsBilinear = 2;
inline constexpr int kSeekFlagBackward = 1;
inline constexpr std::int64_t kAvTimeBase = 1000000;
inline constexpr std::int64_t kNoPtsValue =
    std::numeric_limits<std::int64_t>::min();
inline constexpr int kErrorEagain = -EAGAIN;
inline constexpr int kErrorEof =
    -static_cast<int>(static_cast<std::uint32_t>('E') |
                      (static_cast<std::uint32_t>('O') << 8U) |
                      (static_cast<std::uint32_t>('F') << 16U) |
                      (static_cast<std::uint32_t>(' ') << 24U));

struct Api final {
    unsigned (*avutil_version)();
    unsigned (*avcodec_version)();
    unsigned (*avformat_version)();
    unsigned (*swscale_version)();
    unsigned (*swresample_version)();

    Frame* (*av_frame_alloc)();
    void (*av_frame_free)(Frame**);
    void (*av_frame_unref)(Frame*);
    int (*av_opt_set)(void*, const char*, const char*, int);
    int (*av_opt_set_int)(void*, const char*, std::int64_t, int);
    const char* (*av_get_sample_fmt_name)(int);

    int (*avformat_open_input)(FormatContext**, const char*, void*, void*);
    void (*avformat_close_input)(FormatContext**);
    int (*avformat_find_stream_info)(FormatContext*, void*);
    int (*av_find_best_stream)(FormatContext*, int, int, int, const void**,
                               int);
    int (*av_read_frame)(FormatContext*, Packet*);
    int (*av_seek_frame)(FormatContext*, int, std::int64_t, int);

    Packet* (*av_packet_alloc)();
    void (*av_packet_free)(Packet**);
    void (*av_packet_unref)(Packet*);
    void* (*avcodec_alloc_context3)(const void*);
    void (*avcodec_free_context)(void**);
    int (*avcodec_parameters_to_context)(void*, const CodecParameters*);
    int (*avcodec_open2)(void*, const void*, void*);
    int (*avcodec_send_packet)(void*, const Packet*);
    int (*avcodec_receive_frame)(void*, Frame*);
    void (*avcodec_flush_buffers)(void*);

    void* (*sws_getContext)(int, int, int, int, int, int, int, void*, void*,
                            const double*);
    int (*sws_scale)(void*, const std::uint8_t* const*, const int*, int, int,
                     std::uint8_t* const*, const int*);
    void (*sws_freeContext)(void*);

    void* (*swr_alloc)();
    int (*swr_init)(void*);
    int (*swr_convert)(void*, std::uint8_t* const*, int,
                       const std::uint8_t* const*, int);
    void (*swr_free)(void**);
};

// Loads and pins the FFmpeg 7 shared libraries once per process. Returns the
// resolved API, or nullptr with a stable human-readable reason. Search order:
// OGPLAY_FFMPEG_DIR, the executable directory, then the system default paths.
[[nodiscard]] const Api* LoadFfmpegApi(std::string* unavailable_reason);

}  // namespace ogplay::video::ffabi
